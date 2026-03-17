// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/blocktree/BlockTreeTermsWriter.h"

#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <stdexcept>

#ifdef HAVE_LZ4
#    include <lz4.h>
#endif

namespace diagon {
namespace codecs {
namespace blocktree {

namespace {

// Buffer encoding helpers for column-stride section building.
// These mirror IndexOutput::writeVInt/writeVLong but append to a byte vector.

void bufEncodeVInt(std::vector<uint8_t>& buf, int32_t val) {
    auto v = static_cast<uint32_t>(val);
    while (v > 0x7F) {
        buf.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(v));
}

void bufEncodeVLong(std::vector<uint8_t>& buf, int64_t val) {
    auto v = static_cast<uint64_t>(val);
    while (v > 0x7F) {
        buf.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(v));
}

// Write N bytes of v LSB-first to buffer (matches Lucene's writeLongNBytes).
void bufWriteNBytes(std::vector<uint8_t>& buf, int64_t v, int n) {
    for (int i = 0; i < n; i++) {
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
        v >>= 8;
    }
}

}  // anonymous namespace

BlockTreeTermsWriter::BlockTreeTermsWriter(store::IndexOutput* timOut, store::IndexOutput* tipOut,
                                           const index::FieldInfo& fieldInfo, const Config& config)
    : timOut_(timOut)
    , tipOut_(tipOut)
    , fieldInfo_(fieldInfo)
    , config_(config)
    , numTerms_(0)
    , termsStartFP_(timOut->getFilePointer())  // Capture starting FP for this field
    , finished_(false)
    , sumTotalTermFreq_(0)
    , sumDocFreq_(0)
    , docCount_(0) {
    if (!timOut_ || !tipOut_) {
        throw std::invalid_argument("Output streams cannot be null");
    }

    if (config_.minItemsInBlock < 2) {
        throw std::invalid_argument("minItemsInBlock must be >= 2");
    }

    if (config_.maxItemsInBlock < config_.minItemsInBlock) {
        throw std::invalid_argument("maxItemsInBlock must be >= minItemsInBlock");
    }
}

void BlockTreeTermsWriter::addTerm(const util::BytesRef& term, const TermStats& stats) {
    if (finished_) {
        throw std::runtime_error("Writer already finished");
    }

    // Verify sorted order
    if (lastTerm_.length() > 0 && term <= lastTerm_) {
        throw std::invalid_argument("Terms must be added in sorted order");
    }

    // Add to pending terms
    pendingTerms_.emplace_back(term, stats);

    // Save last term
    lastTermData_.assign(term.data(), term.data() + term.length());
    lastTerm_ = util::BytesRef(lastTermData_.data(), lastTermData_.size());
    numTerms_++;

    // Accumulate field-level statistics
    sumTotalTermFreq_ += stats.totalTermFreq;
    sumDocFreq_ += stats.docFreq;

    // Flush block if we hit max size
    if (static_cast<int>(pendingTerms_.size()) >= config_.maxItemsInBlock) {
        writeBlock();
    }
}

void BlockTreeTermsWriter::finish() {
    if (finished_) {
        return;
    }

    // Write remaining pending terms
    if (!pendingTerms_.empty()) {
        writeBlock();
    }

    // Write compact block index to .tip file
    writeBlockIndex();

    finished_ = true;
}

void BlockTreeTermsWriter::writeBlock() {
    if (pendingTerms_.empty()) {
        return;
    }

    int64_t blockFP = timOut_->getFilePointer();

    // Compute common prefix for all terms in block
    util::BytesRef prefix = pendingTerms_[0].term;
    int prefixLen = static_cast<int>(prefix.length());

    for (size_t i = 1; i < pendingTerms_.size(); i++) {
        prefixLen = sharedPrefixLength(prefix, pendingTerms_[i].term);
        if (prefixLen == 0) {
            break;
        }
    }

    // Write block header: VInt(code) where code = (termCount << 1) | isLastInFloor
    // Prefix is NOT stored here — derived from .tip block index first term at read time
    int termCount = static_cast<int>(pendingTerms_.size());
    timOut_->writeVInt((termCount << 1) | 0);  // isLastInFloor = 0 (no floor blocks)

    // === Section 1: Suffix data (with optional LZ4 compression) ===
    // Format: [suffix lengths section] + [concatenated suffix bytes]
    // Suffix lengths section uses all-equal optimization when possible.

    // Collect suffix lengths and check for all-equal
    std::vector<uint8_t> suffixLengths;
    suffixLengths.reserve(termCount);
    bool allSuffixEqual = true;
    int firstSuffixLen = -1;

    for (const auto& pending : pendingTerms_) {
        int suffixLen = static_cast<int>(pending.term.length()) - prefixLen;
        suffixLengths.push_back(static_cast<uint8_t>(suffixLen));
        if (firstSuffixLen < 0) {
            firstSuffixLen = suffixLen;
        } else if (suffixLen != firstSuffixLen) {
            allSuffixEqual = false;
        }
    }

    std::vector<uint8_t> suffixBuf;
    suffixBuf.reserve(termCount * 10);

    // Encode suffix lengths section
    if (allSuffixEqual && !suffixLengths.empty()) {
        // All suffix lengths equal: VInt((1 << 1) | 1) + byte(commonLength)
        bufEncodeVInt(suffixBuf, (1 << 1) | 1);
        suffixBuf.push_back(static_cast<uint8_t>(firstSuffixLen));
    } else {
        // Individual lengths: VInt((numLengths << 1) | 0) + [numLengths bytes]
        bufEncodeVInt(suffixBuf, (static_cast<int>(suffixLengths.size()) << 1) | 0);
        suffixBuf.insert(suffixBuf.end(), suffixLengths.begin(), suffixLengths.end());
    }

    // Concatenated suffix bytes (no per-term VInt overhead)
    for (size_t i = 0; i < pendingTerms_.size(); i++) {
        int suffixLen = suffixLengths[i];
        if (suffixLen > 0) {
            const uint8_t* suffixStart = pendingTerms_[i].term.data() + prefixLen;
            suffixBuf.insert(suffixBuf.end(), suffixStart, suffixStart + suffixLen);
        }
    }

    // Write suffix section: VLong((uncompressedSize << 3) | flags)
    // Bit 0: LZ4 compressed
    bool compressed = false;
#ifdef HAVE_LZ4
    if (suffixBuf.size() >= 32) {
        int srcSize = static_cast<int>(suffixBuf.size());
        int maxDstSize = LZ4_compressBound(srcSize);
        std::vector<uint8_t> compBuf(maxDstSize);
        int compSize = LZ4_compress_default(reinterpret_cast<const char*>(suffixBuf.data()),
                                            reinterpret_cast<char*>(compBuf.data()), srcSize,
                                            maxDstSize);
        if (compSize > 0 && static_cast<size_t>(compSize) < suffixBuf.size() * 3 / 4) {
            // Compressed saves >25%
            timOut_->writeVLong(static_cast<int64_t>((suffixBuf.size() << 3) | 0x01));
            timOut_->writeVInt(compSize);
            timOut_->writeBytes(compBuf.data(), compSize);
            compressed = true;
        }
    }
#endif
    if (!compressed) {
        timOut_->writeVLong(static_cast<int64_t>((suffixBuf.size() << 3) | 0x00));
        timOut_->writeBytes(suffixBuf.data(), suffixBuf.size());
    }

    // === Section 2: Stats (column-stride with singleton RLE) ===
    // Singleton RLE: consecutive terms with docFreq=1 AND totalTermFreq=1
    // are encoded as VInt(((runCount-1) << 1) | 1).
    // Non-singleton: VInt((docFreq << 1) | 0) + VLong(totalTermFreq - docFreq).
    std::vector<uint8_t> statsBuf;
    statsBuf.reserve(pendingTerms_.size() * 3);
    {
        size_t i = 0;
        size_t count = pendingTerms_.size();
        while (i < count) {
            if (pendingTerms_[i].stats.docFreq == 1 && pendingTerms_[i].stats.totalTermFreq == 1) {
                // Count consecutive singletons
                size_t runStart = i;
                while (i < count && pendingTerms_[i].stats.docFreq == 1 &&
                       pendingTerms_[i].stats.totalTermFreq == 1) {
                    i++;
                }
                int runCount = static_cast<int>(i - runStart);
                bufEncodeVInt(statsBuf, ((runCount - 1) << 1) | 1);
            } else {
                bufEncodeVInt(statsBuf, (pendingTerms_[i].stats.docFreq << 1) | 0);
                bufEncodeVLong(statsBuf, pendingTerms_[i].stats.totalTermFreq -
                                             pendingTerms_[i].stats.docFreq);
                i++;
            }
        }
    }
    timOut_->writeVInt(static_cast<int>(statsBuf.size()));
    timOut_->writeBytes(statsBuf.data(), statsBuf.size());

    // === Section 3: Metadata (conditional column-stride file pointer deltas) ===
    // Format: flags byte + [postingsFP column] + [posStartFP column?] + [skipStartFP section?]
    // flags bit0: always 1 (postingsFP present)
    // flags bit1: posStartFP column present (only if any term has posStartFP >= 0)
    // flags bit2: skipStartFP section present (only if any term has skipStartFP >= 0)
    std::vector<uint8_t> metaBuf;
    metaBuf.reserve(pendingTerms_.size() * 9);
    {
        bool blockHasSkip = false, blockHasPos = false;
        for (const auto& pending : pendingTerms_) {
            if (pending.stats.skipStartFP >= 0)
                blockHasSkip = true;
            if (pending.stats.posStartFP >= 0)
                blockHasPos = true;
        }

        // Write flags byte
        uint8_t flags = 0x01;  // bit0: postingsFP always present
        if (blockHasPos)
            flags |= 0x02;
        if (blockHasSkip)
            flags |= 0x04;
        metaBuf.push_back(flags);

        // Column 1: postingsFP (always present)
        int64_t lastFP = 0;
        for (const auto& pending : pendingTerms_) {
            bufEncodeVLong(metaBuf, pending.stats.postingsFP - lastFP);
            lastFP = pending.stats.postingsFP;
        }

        // Column 2: posStartFP (only if any term has position data)
        if (blockHasPos) {
            lastFP = 0;
            for (const auto& pending : pendingTerms_) {
                bufEncodeVLong(metaBuf, pending.stats.posStartFP - lastFP);
                lastFP = pending.stats.posStartFP;
            }
        }

        // Column 3: skipStartFP (sparse — bitmap + values for terms with skip data)
        if (blockHasSkip) {
            // Write bitmap: 1 bit per term, ceil(termCount/8) bytes
            int bitmapBytes = (termCount + 7) / 8;
            size_t bitmapStart = metaBuf.size();
            metaBuf.resize(metaBuf.size() + bitmapBytes, 0);
            for (int i = 0; i < termCount; i++) {
                if (pendingTerms_[i].stats.skipStartFP >= 0) {
                    metaBuf[bitmapStart + (i / 8)] |= (1 << (i % 8));
                }
            }
            // Delta-encode only the valid skipStartFP values
            lastFP = 0;
            for (int i = 0; i < termCount; i++) {
                if (pendingTerms_[i].stats.skipStartFP >= 0) {
                    bufEncodeVLong(metaBuf, pendingTerms_[i].stats.skipStartFP - lastFP);
                    lastFP = pendingTerms_[i].stats.skipStartFP;
                }
            }
        }
    }
    timOut_->writeVInt(static_cast<int>(metaBuf.size()));
    timOut_->writeBytes(metaBuf.data(), metaBuf.size());

    // Record block entry for compact .tip index
    const auto& firstTerm = pendingTerms_[0].term;
    blockEntries_.emplace_back(firstTerm.data(), firstTerm.length(), blockFP);

    // Clear pending terms
    pendingTerms_.clear();
}

// === TIP6 static helpers ===

int BlockTreeTermsWriter::bytesRequiredVLong(int64_t v) {
    // Matches Lucene's Long.BYTES - (Long.numberOfLeadingZeros(v|1) >>> 3)
    return 8 - (__builtin_clzll(static_cast<uint64_t>(v) | 1) >> 3);
}

BlockTreeTermsWriter::ChildSaveStrategy BlockTreeTermsWriter::chooseStrategy(
    int minLabel, int maxLabel, int labelCnt) {
    // Priority: BITS > ARRAY > REVERSE_ARRAY (BITS wins ties)
    ChildSaveStrategy best = ChildSaveStrategy::BITS;
    int bestCost = getStrategyBytes(ChildSaveStrategy::BITS, minLabel, maxLabel, labelCnt);

    int arrayCost = getStrategyBytes(ChildSaveStrategy::ARRAY, minLabel, maxLabel, labelCnt);
    if (arrayCost < bestCost) {
        best = ChildSaveStrategy::ARRAY;
        bestCost = arrayCost;
    }

    int revCost = getStrategyBytes(ChildSaveStrategy::REVERSE_ARRAY, minLabel, maxLabel, labelCnt);
    if (revCost < bestCost) {
        best = ChildSaveStrategy::REVERSE_ARRAY;
    }

    return best;
}

int BlockTreeTermsWriter::getStrategyBytes(ChildSaveStrategy s, int minLabel, int maxLabel,
                                           int labelCnt) {
    int range = maxLabel - minLabel + 1;
    switch (s) {
        case ChildSaveStrategy::BITS: return (range + 7) / 8;
        case ChildSaveStrategy::ARRAY: return labelCnt - 1;
        case ChildSaveStrategy::REVERSE_ARRAY: return range - labelCnt + 1;
    }
    return range;  // unreachable
}

// === TIP6 trie builder ===

std::unique_ptr<BlockTreeTermsWriter::Tip6Node> BlockTreeTermsWriter::buildTip6Trie() {
    auto root = std::make_unique<Tip6Node>();
    root->label = 0;
    root->blockFP = -1;
    root->fp = -1;

    for (const auto& entry : blockEntries_) {
        Tip6Node* current = root.get();
        for (uint8_t byte : entry.termData) {
            // Find or create child with this label (children are sorted by label)
            Tip6Node* child = nullptr;
            auto it = std::lower_bound(
                current->children.begin(), current->children.end(), byte,
                [](const std::unique_ptr<Tip6Node>& n, uint8_t b) { return n->label < b; });
            if (it != current->children.end() && (*it)->label == byte) {
                child = it->get();
            } else {
                auto newChild = std::make_unique<Tip6Node>();
                newChild->label = byte;
                newChild->blockFP = -1;
                newChild->fp = -1;
                child = newChild.get();
                current->children.insert(it, std::move(newChild));
            }
            current = child;
        }
        current->blockFP = entry.blockFP;
    }

    return root;
}

// === TIP6 postorder serializer ===

void BlockTreeTermsWriter::saveTip6TrieToBuffer(Tip6Node* root, std::vector<uint8_t>& buf) {
    // Iterative postorder DFS: children are written before parents,
    // enabling small delta FPs (parent.fp - child.fp).
    struct StackEntry {
        Tip6Node* node;
        size_t childIdx;
    };

    std::vector<StackEntry> stack;
    stack.push_back({root, 0});

    while (!stack.empty()) {
        auto& top = stack.back();
        if (top.childIdx < top.node->children.size()) {
            Tip6Node* child = top.node->children[top.childIdx].get();
            top.childIdx++;
            stack.push_back({child, 0});
        } else {
            Tip6Node* node = top.node;
            stack.pop_back();

            node->fp = static_cast<int64_t>(buf.size());

            int numChildren = static_cast<int>(node->children.size());
            bool hasOutput = (node->blockFP >= 0);

            if (numChildren == 0) {
                // === Leaf node (SIGN_NO_CHILDREN = 0x00) ===
                // header: [x][0=hasFloor][1=hasTerms][3bit fpBytes-1][2bit 0x00]
                int64_t outputFP = node->blockFP;
                int outputFpBytes = bytesRequiredVLong(outputFP);
                uint8_t header = static_cast<uint8_t>(
                    0x00 | ((outputFpBytes - 1) << 2) | 0x20);  // hasTerms=1
                buf.push_back(header);
                bufWriteNBytes(buf, outputFP, outputFpBytes);

            } else if (numChildren == 1) {
                // === Single-child node ===
                Tip6Node* child = node->children[0].get();
                int64_t childDeltaFP = node->fp - child->fp;
                int childFpBytes = bytesRequiredVLong(childDeltaFP);

                if (hasOutput) {
                    // SIGN_SINGLE_CHILD_WITH_OUTPUT = 0x01
                    int64_t encodedFP = (node->blockFP << 2) | 0x02;  // hasTerms=1
                    int encodedOutputFpBytes = bytesRequiredVLong(encodedFP);
                    uint8_t header = static_cast<uint8_t>(
                        0x01 | ((childFpBytes - 1) << 2) |
                        ((encodedOutputFpBytes - 1) << 5));
                    buf.push_back(header);
                    buf.push_back(child->label);
                    bufWriteNBytes(buf, childDeltaFP, childFpBytes);
                    bufWriteNBytes(buf, encodedFP, encodedOutputFpBytes);
                } else {
                    // SIGN_SINGLE_CHILD_WITHOUT_OUTPUT = 0x02
                    uint8_t header = static_cast<uint8_t>(
                        0x02 | ((childFpBytes - 1) << 2));
                    buf.push_back(header);
                    buf.push_back(child->label);
                    bufWriteNBytes(buf, childDeltaFP, childFpBytes);
                }

            } else {
                // === Multi-child node (SIGN_MULTI_CHILDREN = 0x03) ===
                int minLabel = node->children.front()->label;
                int maxLabel = node->children.back()->label;

                // Max delta is for the first child (furthest back in buffer)
                int64_t maxChildDeltaFP = node->fp - node->children.front()->fp;
                int childrenFpBytes = bytesRequiredVLong(maxChildDeltaFP);

                ChildSaveStrategy strategy = chooseStrategy(minLabel, maxLabel, numChildren);
                int strategyBytes = getStrategyBytes(strategy, minLabel, maxLabel, numChildren);

                int encodedOutputFpBytes = 1;  // default for no-output case
                if (hasOutput) {
                    int64_t encodedFP = (node->blockFP << 2) | 0x02;
                    encodedOutputFpBytes = bytesRequiredVLong(encodedFP);
                }

                // 3-byte header (LSB-first, 24-bit):
                //   [1:0]=sign [4:2]=fpBytes-1 [5]=hasOutput [8:6]=outFpBytes-1
                //   [10:9]=strategyCode [15:11]=stratBytes-1 [23:16]=minLabel
                int32_t hdr =
                    0x03
                    | ((childrenFpBytes - 1) << 2)
                    | ((hasOutput ? 1 : 0) << 5)
                    | ((encodedOutputFpBytes - 1) << 6)
                    | (static_cast<int>(strategy) << 9)
                    | ((strategyBytes - 1) << 11)
                    | (minLabel << 16);
                bufWriteNBytes(buf, hdr, 3);

                // Encoded output FP (if has output)
                if (hasOutput) {
                    int64_t encodedFP = (node->blockFP << 2) | 0x02;
                    bufWriteNBytes(buf, encodedFP, encodedOutputFpBytes);
                }

                // Strategy data (child labels)
                switch (strategy) {
                    case ChildSaveStrategy::BITS: {
                        int bytes = (maxLabel - minLabel + 1 + 7) / 8;
                        size_t start = buf.size();
                        buf.resize(start + bytes, 0);
                        for (const auto& c : node->children) {
                            int idx = c->label - minLabel;
                            buf[start + idx / 8] |= static_cast<uint8_t>(1 << (idx % 8));
                        }
                        break;
                    }
                    case ChildSaveStrategy::ARRAY: {
                        // All labels except minLabel (implicit in header)
                        for (size_t i = 1; i < node->children.size(); i++) {
                            buf.push_back(node->children[i]->label);
                        }
                        break;
                    }
                    case ChildSaveStrategy::REVERSE_ARRAY: {
                        // maxLabel byte + absent labels
                        buf.push_back(static_cast<uint8_t>(maxLabel));
                        size_t ci = 0;
                        for (int label = minLabel; label <= maxLabel; label++) {
                            if (ci < node->children.size() &&
                                node->children[ci]->label == label) {
                                ci++;
                            } else {
                                buf.push_back(static_cast<uint8_t>(label));
                            }
                        }
                        break;
                    }
                }

                // Child delta FPs (one per child, fixed-width)
                for (const auto& child : node->children) {
                    int64_t childDeltaFP = node->fp - child->fp;
                    bufWriteNBytes(buf, childDeltaFP, childrenFpBytes);
                }
            }
        }
    }
}

void BlockTreeTermsWriter::writeBlockIndex() {
    // Write TIP6 Lucene 103-aligned per-byte trie block index to .tip file.
    // Postorder serialization with bit-packed headers and BITS/ARRAY/REVERSE_ARRAY
    // child strategies — structural compactness without compression.

    tipOut_->writeInt(0x54495036);          // "TIP6" magic
    tipOut_->writeString(fieldInfo_.name);
    tipOut_->writeVLong(termsStartFP_);
    tipOut_->writeVLong(numTerms_);

    int numBlocks = static_cast<int>(blockEntries_.size());
    tipOut_->writeVInt(numBlocks);

    if (numBlocks == 0) {
        tipOut_->writeVLong(0);  // rootFP
        tipOut_->writeVInt(0);   // trieDataSize
        return;
    }

    // Build per-byte trie from sorted block entries
    auto root = buildTip6Trie();

    // Postorder-serialize to buffer
    std::vector<uint8_t> trieBuf;
    trieBuf.reserve(numBlocks * 8);
    saveTip6TrieToBuffer(root.get(), trieBuf);

    // Append 8 zero bytes for over-read safety (matching Lucene)
    for (int i = 0; i < 8; i++) {
        trieBuf.push_back(0);
    }

    int64_t rootFP = root->fp;
    int trieDataSize = static_cast<int>(trieBuf.size());

    tipOut_->writeVLong(rootFP);
    tipOut_->writeVInt(trieDataSize);
    tipOut_->writeBytes(trieBuf.data(), trieDataSize);
}

int BlockTreeTermsWriter::sharedPrefixLength(const util::BytesRef& a,
                                             const util::BytesRef& b) const {
    size_t minLen = std::min(a.length(), b.length());
    int shared = 0;

    for (size_t i = 0; i < minLen; i++) {
        if (a[i] != b[i]) {
            break;
        }
        shared++;
    }

    return shared;
}

}  // namespace blocktree
}  // namespace codecs
}  // namespace diagon
