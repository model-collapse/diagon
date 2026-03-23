// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/PostingsFormat.h"
#include "diagon/codecs/lucene90/LuceneFST.h"
#include "diagon/codecs/lucene90/Lucene90PostingsReader.h"
#include "diagon/index/Fields.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/store/IndexInput.h"
#include "diagon/util/BytesRef.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace codecs {
namespace lucene90 {

// ==================== Constants ====================

// Codec names and versions (from Lucene90BlockTreeTermsReader.java)
static constexpr const char* TERMS_CODEC_NAME = "BlockTreeTermsDict";
static constexpr const char* TERMS_INDEX_CODEC_NAME = "BlockTreeTermsIndex";
static constexpr const char* TERMS_META_CODEC_NAME = "BlockTreeTermsMeta";

static constexpr int32_t BLOCKTREE_VERSION_START = 0;
static constexpr int32_t BLOCKTREE_VERSION_MSB_VLONG = 1;
static constexpr int32_t BLOCKTREE_VERSION_CONTINUOUS_ARCS = 2;
static constexpr int32_t BLOCKTREE_VERSION_CURRENT = BLOCKTREE_VERSION_CONTINUOUS_ARCS;

// Output flags (2 low bits of FST output)
static constexpr int OUTPUT_FLAGS_NUM_BITS = 2;
static constexpr int OUTPUT_FLAGS_MASK = 0x3;
static constexpr int OUTPUT_FLAG_IS_FLOOR = 0x1;
static constexpr int OUTPUT_FLAG_HAS_TERMS = 0x2;

/**
 * Per-field metadata read from .tmd.
 */
struct FieldReaderMeta {
    int fieldNumber = 0;
    std::string fieldName;
    int64_t numTerms = 0;
    std::vector<uint8_t> rootCode;    // Root block code (MSB VLong encoded)
    int64_t sumTotalTermFreq = 0;
    int64_t sumDocFreq = 0;
    int docCount = 0;
    std::vector<uint8_t> minTerm;
    std::vector<uint8_t> maxTerm;
    int64_t indexStartFP = 0;         // Start of this field's FST in .tip
    index::IndexOptions indexOptions = index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
};

// Forward declarations
class Lucene90FieldReader;
class Lucene90SegmentTermsEnum;

/**
 * FieldsProducer for reading Lucene 9.x BlockTree format (.tmd/.tim/.tip).
 *
 * Reads per-field metadata from .tmd, FST index from .tip, and term blocks from .tim.
 * Combined with Lucene90PostingsReader for full term → postings read pipeline.
 *
 * Based on: org.apache.lucene.backward_codecs.lucene90.blocktree.Lucene90BlockTreeTermsReader
 */
class Lucene90BlockTreeTermsReader : public FieldsProducer {
public:
    Lucene90BlockTreeTermsReader(index::SegmentReadState& state,
                                  std::unique_ptr<Lucene90PostingsReader> postingsReader);
    ~Lucene90BlockTreeTermsReader();

    std::unique_ptr<index::Terms> terms(const std::string& field) override;
    void checkIntegrity() override;
    void close() override;

    // Accessed by FieldReader/TermsEnum
    Lucene90PostingsReader* getPostingsReader() const { return postingsReader_.get(); }
    store::IndexInput* getTermsInput() const { return termsIn_.get(); }
    int version() const { return version_; }

private:
    std::unique_ptr<Lucene90PostingsReader> postingsReader_;
    std::unique_ptr<store::IndexInput> termsIn_;   // .tim
    std::unique_ptr<store::IndexInput> indexIn_;    // .tip
    int version_ = 0;

    // Per-field metadata and readers
    std::map<std::string, FieldReaderMeta> fieldMeta_;
    std::map<std::string, std::shared_ptr<Lucene90FieldReader>> fieldReaders_;
};

/**
 * Terms implementation for a single field in a Lucene90 BlockTree.
 */
class Lucene90FieldReader : public index::Terms {
public:
    Lucene90FieldReader(Lucene90BlockTreeTermsReader* parent,
                         const FieldReaderMeta& meta,
                         std::unique_ptr<LuceneFST> fst,
                         store::IndexInput* timIn);

    std::unique_ptr<index::TermsEnum> iterator() const override;
    int64_t size() const override { return meta_.numTerms; }
    int getDocCount() const override { return meta_.docCount; }
    int64_t getSumTotalTermFreq() const override { return meta_.sumTotalTermFreq; }
    int64_t getSumDocFreq() const override { return meta_.sumDocFreq; }

    // Accessed by SegmentTermsEnum
    const FieldReaderMeta& meta() const { return meta_; }
    const LuceneFST* fst() const { return fst_.get(); }
    Lucene90BlockTreeTermsReader* parent() const { return parent_; }
    store::IndexInput* timInput() const { return timIn_; }

    int64_t rootBlockFP() const { return rootBlockFP_; }

private:
    Lucene90BlockTreeTermsReader* parent_;
    FieldReaderMeta meta_;
    std::unique_ptr<LuceneFST> fst_;
    store::IndexInput* timIn_;       // Borrowed pointer to .tim
    int64_t rootBlockFP_ = 0;
};

/**
 * Frame representing one loaded block from .tim.
 */
struct Lucene90TermsFrame {
    int64_t fp = 0;                    // Block file pointer in .tim
    int64_t fpOrig = 0;               // Original FP (for floor block chaining)
    int64_t fpEnd = 0;                // End of block

    int entCount = 0;                  // Number of entries
    int nextEnt = 0;                   // Next entry index

    bool isLastInFloor = false;
    bool isLeafBlock = false;
    bool hasTerms = false;
    bool isFloor = false;

    // Prefix shared by all entries in this block
    int prefixLength = 0;

    // Suffix data (decompressed)
    std::vector<uint8_t> suffixBytes;
    int suffixBytesPos = 0;

    // Suffix length data
    std::vector<uint8_t> suffixLengthBytes;
    int suffixLengthPos = 0;
    bool allSuffixesEqual = false;
    int equalSuffixLength = 0;

    // Stats data
    std::vector<uint8_t> statBytes;
    int statPos = 0;
    int statsSingletonRunLength = 0;

    // Metadata data (for decodeTerm)
    std::vector<uint8_t> metaBytes;
    int metaPos = 0;

    // Floor data
    std::vector<uint8_t> floorData;
    int floorDataPos = 0;
    int numFollowFloorBlocks = 0;
    int nextFloorLabel = 0;

    // Per-entry state
    Lucene90TermState termState;

    void reset() {
        fp = fpOrig = fpEnd = 0;
        entCount = nextEnt = 0;
        isLastInFloor = isLeafBlock = hasTerms = isFloor = false;
        prefixLength = 0;
        suffixBytes.clear();
        suffixBytesPos = 0;
        suffixLengthBytes.clear();
        suffixLengthPos = 0;
        allSuffixesEqual = false;
        equalSuffixLength = 0;
        statBytes.clear();
        statPos = 0;
        statsSingletonRunLength = 0;
        metaBytes.clear();
        metaPos = 0;
        floorData.clear();
        floorDataPos = 0;
        numFollowFloorBlocks = 0;
        nextFloorLabel = 0;
        termState = Lucene90TermState{};
    }
};

/**
 * TermsEnum for seeking and iterating terms in Lucene90 BlockTree format.
 *
 * MVP: Supports seekExact() only (not seekCeil/next).
 *
 * Based on: org.apache.lucene.backward_codecs.lucene90.blocktree.SegmentTermsEnum
 */
class Lucene90SegmentTermsEnum : public index::TermsEnum {
public:
    explicit Lucene90SegmentTermsEnum(const Lucene90FieldReader* fieldReader);

    // TermsEnum interface
    bool seekExact(const util::BytesRef& text) override;
    SeekStatus seekCeil(const util::BytesRef& /*text*/) override {
        return SeekStatus::END;
    }
    bool next() override { return false; }
    util::BytesRef term() const override;
    int docFreq() const override;
    int64_t totalTermFreq() const override;
    std::unique_ptr<index::PostingsEnum> postings() override;

    // Extended: postings with positions
    std::unique_ptr<index::PostingsEnum> postingsWithPositions();

private:
    const Lucene90FieldReader* fieldReader_;
    std::unique_ptr<store::IndexInput> timIn_;  // Cloned .tim input

    // Current term state
    std::vector<uint8_t> currentTerm_;
    bool termFound_ = false;

    // Frame for the current block
    Lucene90TermsFrame frame_;

    // FST output accumulator
    std::vector<uint8_t> fstOutput_;

    // Internal helpers
    bool seekExactInternal(const std::vector<uint8_t>& target);
    std::unique_ptr<index::PostingsEnum> postingsInternal(bool needPositions);

    // Decode the block at the given FP
    void loadBlock(int64_t blockFP);

    // Scan within the loaded block for the target term
    bool scanToTerm(const std::vector<uint8_t>& target);

    // Decode stats and metadata for the current entry
    void decodeMetaData();

    // Navigate floor blocks
    void scanToFloorFrame(const std::vector<uint8_t>& target);

    // Decode FST output → blockFP + flags
    int64_t decodeBlockFP(const std::vector<uint8_t>& output, bool& isFloor, bool& hasTerms) const;

    // Read VInt from a byte array
    static int32_t readVInt(const uint8_t* data, int& pos);
    static int64_t readVLong(const uint8_t* data, int& pos);
};

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
