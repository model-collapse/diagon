// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene90/Lucene90OSStoredFieldsWriter.h"

#include "diagon/codecs/lucene90/StoredFieldsInts.h"
#include "diagon/util/packed/DirectMonotonicWriter.h"

#include "diagon/store/IOContext.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#ifdef HAVE_LZ4
#    include <lz4.h>
#endif

namespace diagon {
namespace codecs {
namespace lucene90 {

// ==================== Constructor / Destructor ====================

Lucene90OSStoredFieldsWriter::Lucene90OSStoredFieldsWriter(store::Directory& dir,
                                                             const std::string& segmentName,
                                                             const uint8_t* segmentID,
                                                             const std::string& formatName)
    : segmentName_(segmentName)
    , formatName_(formatName)
    , dir_(dir)
    , docBase_(0)
    , numBufferedDocs_(0)
    , numStoredFieldsInDoc_(0)
    , numChunks_(0)
    , numDirtyChunks_(0)
    , numDirtyDocs_(0) {
    std::memcpy(segmentID_, segmentID, 16);

    // Open .fdt output and write header
    std::string fdtName = segmentName + "." + FIELDS_EXTENSION;
    fieldsStream_ = dir_.createOutput(fdtName, store::IOContext::DEFAULT);
    CodecUtil::writeIndexHeader(*fieldsStream_, formatName_, VERSION_CURRENT, segmentID_, "");
    // Write chunk size after header (Lucene format)
    fieldsStream_->writeVInt(CHUNK_SIZE);

    // Open .fdm output and write header
    std::string fdmName = segmentName + "." + META_EXTENSION;
    metaStream_ = dir_.createOutput(fdmName, store::IOContext::DEFAULT);
    CodecUtil::writeIndexHeader(*metaStream_, META_CODEC_NAME, META_VERSION_START, segmentID_, "");

    // Reserve initial buffer space
    bufferedDocs_.reserve(CHUNK_SIZE);
    numStoredFields_.reserve(MAX_DOCS_PER_CHUNK);
    endOffsets_.reserve(MAX_DOCS_PER_CHUNK);
}

Lucene90OSStoredFieldsWriter::~Lucene90OSStoredFieldsWriter() = default;

// ==================== Document Lifecycle ====================

void Lucene90OSStoredFieldsWriter::startDocument() {
    numStoredFieldsInDoc_ = 0;
}

void Lucene90OSStoredFieldsWriter::finishDocument() {
    numStoredFields_.push_back(numStoredFieldsInDoc_);
    endOffsets_.push_back(static_cast<int>(bufferedDocs_.size()));
    numBufferedDocs_++;

    if (triggerFlush()) {
        flushChunk(false);
    }
}

// ==================== Field Writers ====================

void Lucene90OSStoredFieldsWriter::writeField(const index::FieldInfo& info,
                                                const std::string& value) {
    // Write field header: VLong(fieldNumber << TYPE_BITS | STRING)
    int64_t infoAndBits = (static_cast<int64_t>(info.number) << TYPE_BITS) | STRING;
    bufWriteVLong(infoAndBits);
    // Write string: VInt(len) + UTF-8 bytes
    bufWriteString(value);
    numStoredFieldsInDoc_++;
}

void Lucene90OSStoredFieldsWriter::writeField(const index::FieldInfo& info, int32_t value) {
    // Write field header: VLong(fieldNumber << TYPE_BITS | NUMERIC_INT)
    int64_t infoAndBits = (static_cast<int64_t>(info.number) << TYPE_BITS) | NUMERIC_INT;
    bufWriteVLong(infoAndBits);
    // Write ZInt (zigzag-encoded VInt)
    bufWriteZInt(value);
    numStoredFieldsInDoc_++;
}

void Lucene90OSStoredFieldsWriter::writeField(const index::FieldInfo& info, int64_t value) {
    // Write field header: VLong(fieldNumber << TYPE_BITS | NUMERIC_LONG)
    int64_t infoAndBits = (static_cast<int64_t>(info.number) << TYPE_BITS) | NUMERIC_LONG;
    bufWriteVLong(infoAndBits);
    // Write TLong (timestamp-aware variable-length long)
    bufWriteTLong(value);
    numStoredFieldsInDoc_++;
}

// ==================== Chunk Flushing ====================

bool Lucene90OSStoredFieldsWriter::triggerFlush() const {
    return numBufferedDocs_ >= MAX_DOCS_PER_CHUNK ||
           static_cast<int>(bufferedDocs_.size()) >= CHUNK_SIZE;
}

void Lucene90OSStoredFieldsWriter::flushChunk(bool force) {
    if (numBufferedDocs_ == 0) return;

    // Track chunk for index
    chunkStartFPs_.push_back(fieldsStream_->getFilePointer());
    chunkDocCounts_.push_back(numBufferedDocs_);

    // Compute per-doc lengths from cumulative endOffsets
    std::vector<int> lengths(numBufferedDocs_);
    lengths[0] = endOffsets_[0];
    for (int i = 1; i < numBufferedDocs_; i++) {
        lengths[i] = endOffsets_[i] - endOffsets_[i - 1];
    }

    // Write chunk header
    writeHeader(docBase_, numBufferedDocs_, numStoredFields_.data(), lengths.data(),
                /*sliced=*/false, /*dirtyChunk=*/force);

    // Compress and write field data (raw LZ4 bytes, no framing — Lucene format)
    int totalBytes = static_cast<int>(bufferedDocs_.size());
    if (totalBytes > 0) {
#ifdef HAVE_LZ4
        int maxCompressedSize = LZ4_compressBound(totalBytes);
        std::vector<uint8_t> compressedBuf(maxCompressedSize);
        int compressedSize =
            LZ4_compress_default(reinterpret_cast<const char*>(bufferedDocs_.data()),
                                 reinterpret_cast<char*>(compressedBuf.data()), totalBytes,
                                 maxCompressedSize);
        if (compressedSize <= 0) {
            throw std::runtime_error("LZ4 compression failed");
        }
        // Write raw LZ4 compressed bytes (no length prefix — Lucene LZ4 format)
        fieldsStream_->writeBytes(compressedBuf.data(), compressedSize);
#else
        // No LZ4 available — write raw uncompressed bytes
        // (reader must handle based on decompressed == compressed size)
        fieldsStream_->writeBytes(bufferedDocs_.data(), totalBytes);
#endif
    }

    // Update statistics
    numChunks_++;
    if (force) {
        numDirtyChunks_++;
        numDirtyDocs_ += numBufferedDocs_;
    }

    // Advance docBase and reset buffers
    docBase_ += numBufferedDocs_;
    numBufferedDocs_ = 0;
    bufferedDocs_.clear();
    numStoredFields_.clear();
    endOffsets_.clear();
}

void Lucene90OSStoredFieldsWriter::writeHeader(int docBase, int numBufferedDocs,
                                                 const int* numStoredFields, const int* lengths,
                                                 bool sliced, bool dirtyChunk) {
    // docBase
    fieldsStream_->writeVInt(docBase);

    // token: numBufferedDocs<<2 | dirtyBit<<1 | slicedBit
    int token = (numBufferedDocs << 2) | (dirtyChunk ? 2 : 0) | (sliced ? 1 : 0);
    fieldsStream_->writeVInt(token);

    if (numBufferedDocs == 1) {
        // Single-doc chunk: use simple VInt encoding
        fieldsStream_->writeVInt(numStoredFields[0]);
        fieldsStream_->writeVInt(lengths[0]);
    } else {
        // Multi-doc chunk: use StoredFieldsInts block encoding
        StoredFieldsInts::writeInts(numStoredFields, 0, numBufferedDocs, *fieldsStream_);
        StoredFieldsInts::writeInts(lengths, 0, numBufferedDocs, *fieldsStream_);
    }
}

// ==================== Finish / Close ====================

void Lucene90OSStoredFieldsWriter::finish(int numDocs) {
    // Flush remaining buffered docs as dirty chunk
    if (numBufferedDocs_ > 0) {
        flushChunk(/*force=*/true);
    }

    // Validate doc count
    if (docBase_ != numDocs) {
        throw std::runtime_error("Expected " + std::to_string(numDocs) + " docs, got " +
                                 std::to_string(docBase_));
    }

    // Write fields index (.fdx data + .fdm metadata)
    writeFieldsIndex(numDocs);

    // Write stored fields statistics to .fdm
    metaStream_->writeVLong(numChunks_);
    metaStream_->writeVLong(numDirtyChunks_);
    metaStream_->writeVLong(numDirtyDocs_);

    // Write footers
    CodecUtil::writeFooter(*metaStream_);
    CodecUtil::writeFooter(*fieldsStream_);
}

void Lucene90OSStoredFieldsWriter::writeFieldsIndex(int numDocs) {
    // Open .fdx output
    std::string fdxName = segmentName_ + "." + INDEX_EXTENSION;
    auto indexStream = dir_.createOutput(fdxName, store::IOContext::DEFAULT);
    CodecUtil::writeIndexHeader(*indexStream, INDEX_CODEC_NAME, META_VERSION_START, segmentID_, "");

    int64_t numChunksPlus1 = static_cast<int64_t>(chunkDocCounts_.size()) + 1;

    // Base data pointer for DM arrays in .fdx (after header)
    int64_t baseDataFP = indexStream->getFilePointer();

    // Write metadata header to .fdm
    metaStream_->writeInt(numDocs);
    metaStream_->writeInt(BLOCK_SHIFT);
    metaStream_->writeVInt(static_cast<int32_t>(numChunksPlus1));

    // === DirectMonotonic for cumulative doc counts ===
    // Metadata → .fdm, packed data → .fdx
    {
        util::packed::DirectMonotonicWriter dmWriter(metaStream_.get(), indexStream.get(),
                                                     numChunksPlus1, BLOCK_SHIFT);
        int64_t cumDocs = 0;
        for (size_t i = 0; i < chunkDocCounts_.size(); i++) {
            dmWriter.add(cumDocs);
            cumDocs += chunkDocCounts_[i];
        }
        dmWriter.add(numDocs);  // sentinel
        dmWriter.finish();
    }

    // Record relative offset where FP data starts in .fdx
    int64_t startPointersDataOffset = indexStream->getFilePointer() - baseDataFP;

    // Write relative offset of FP data between the two DM metadata blocks
    metaStream_->writeLong(startPointersDataOffset);

    // === DirectMonotonic for chunk start file pointers ===
    // Metadata → .fdm, packed data → .fdx
    int64_t maxPointer = fieldsStream_->getFilePointer();
    {
        util::packed::DirectMonotonicWriter dmWriter(metaStream_.get(), indexStream.get(),
                                                     numChunksPlus1, BLOCK_SHIFT);
        for (size_t i = 0; i < chunkStartFPs_.size(); i++) {
            dmWriter.add(chunkStartFPs_[i]);
        }
        dmWriter.add(maxPointer);  // sentinel
        dmWriter.finish();
    }

    // Write trailing metadata to .fdm
    int64_t indexDataLength = indexStream->getFilePointer() - baseDataFP;
    metaStream_->writeLong(indexDataLength);
    metaStream_->writeLong(maxPointer);

    // Write .fdx footer
    CodecUtil::writeFooter(*indexStream);
    indexStream->close();
}

void Lucene90OSStoredFieldsWriter::close() {
    if (metaStream_) {
        metaStream_->close();
        metaStream_.reset();
    }
    if (fieldsStream_) {
        fieldsStream_->close();
        fieldsStream_.reset();
    }
}

std::vector<std::string> Lucene90OSStoredFieldsWriter::getFiles() const {
    return {segmentName_ + "." + FIELDS_EXTENSION, segmentName_ + "." + INDEX_EXTENSION,
            segmentName_ + "." + META_EXTENSION};
}

int64_t Lucene90OSStoredFieldsWriter::ramBytesUsed() const {
    return static_cast<int64_t>(bufferedDocs_.capacity() +
                                numStoredFields_.capacity() * sizeof(int) +
                                endOffsets_.capacity() * sizeof(int) +
                                chunkDocCounts_.capacity() * sizeof(int) +
                                chunkStartFPs_.capacity() * sizeof(int64_t));
}

// ==================== Buffer Encoding Helpers ====================

void Lucene90OSStoredFieldsWriter::bufWriteByte(uint8_t b) {
    bufferedDocs_.push_back(b);
}

void Lucene90OSStoredFieldsWriter::bufWriteVInt(int32_t i) {
    uint32_t v = static_cast<uint32_t>(i);
    while (v > 0x7F) {
        bufWriteByte(static_cast<uint8_t>(v | 0x80));
        v >>= 7;
    }
    bufWriteByte(static_cast<uint8_t>(v));
}

void Lucene90OSStoredFieldsWriter::bufWriteVLong(int64_t l) {
    uint64_t v = static_cast<uint64_t>(l);
    while (v > 0x7F) {
        bufWriteByte(static_cast<uint8_t>(v | 0x80));
        v >>= 7;
    }
    bufWriteByte(static_cast<uint8_t>(v));
}

void Lucene90OSStoredFieldsWriter::bufWriteString(const std::string& s) {
    bufWriteVInt(static_cast<int32_t>(s.size()));
    for (char c : s) {
        bufWriteByte(static_cast<uint8_t>(c));
    }
}

void Lucene90OSStoredFieldsWriter::bufWriteZInt(int32_t i) {
    // ZigZag encode, then write as VInt
    int32_t zigzag = zigZagEncode32(i);
    bufWriteVInt(zigzag);
}

void Lucene90OSStoredFieldsWriter::bufWriteTLong(int64_t l) {
    // TLong: timestamp-aware variable-length long encoding
    // Based on: org.apache.lucene.codecs.lucene90.compressing.Lucene90CompressingStoredFieldsWriter
    //
    // Header byte format:
    //   bits 7-6: time unit (00=none, 01=seconds, 10=hours, 11=days)
    //   bits 5-0: zigzag-encoded value (if fits in 6 bits)
    //   If value doesn't fit in 6 bits, 5-0 = 0x20 marker and remaining written as VLong

    int header;
    int64_t value;

    if (l % DAY == 0) {
        header = DAY_ENCODING;
        value = l / DAY;
    } else if (l % HOUR == 0) {
        header = HOUR_ENCODING;
        value = l / HOUR;
    } else if (l % SECOND == 0) {
        header = SECOND_ENCODING;
        value = l / SECOND;
    } else {
        header = 0;
        value = l;
    }

    // ZigZag encode the value
    int64_t zigzag = zigZagEncode64(value);

    if (zigzag >= 0 && zigzag <= 0x1F) {
        // Fits in 5 bits of header byte
        bufWriteByte(static_cast<uint8_t>(header | static_cast<int>(zigzag)));
    } else {
        // Doesn't fit: write marker + VLong
        bufWriteByte(static_cast<uint8_t>(header | 0x20));
        bufWriteVLong(zigzag);
    }
}

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
