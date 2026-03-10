// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/SegmentMerger.h"

#include "diagon/codecs/BKDWriter.h"
#include "diagon/codecs/Codec.h"
#include "diagon/codecs/LiveDocsFormat.h"
#include "diagon/codecs/NormsFormat.h"
#include "diagon/codecs/NumericDocValuesReader.h"
#include "diagon/codecs/NumericDocValuesWriter.h"
#include "diagon/codecs/PointValuesWriter.h"
#include "diagon/codecs/PostingsFormat.h"
#include "diagon/codecs/StoredFieldsReader.h"
#include "diagon/codecs/StoredFieldsWriter.h"
#include "diagon/index/Fields.h"
#include "diagon/index/PointValues.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentReader.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/store/IOContext.h"
#include "diagon/util/BytesRef.h"

#include <algorithm>
#include <cstring>
#include <queue>
#include <set>
#include <stdexcept>
#include <variant>
#include <vector>

namespace diagon {
namespace index {

// ============================================================================
// Anonymous namespace: merge helper classes
//
// Following Lucene's MappedMultiFields/MultiFields pattern:
//   MergedFields → MergedTerms → MergedTermsEnum → MergedPostingsEnum
//
// These present a unified, doc-ID-remapped view of postings across multiple
// source segments, which is then written through the codec's normal
// FieldsConsumer::write(Fields&, NormsProducer*) path.
// ============================================================================

namespace {

// Pre-computed doc ID mapping for a single source segment.
// Maps old (segment-local) doc IDs to new (merged) doc IDs.
struct SegmentDocMap {
    std::vector<int> oldToNew;  // oldToNew[oldDocID] = newDocID or -1 (deleted)
};

// ---------------------------------------------------------------------------
// MergedPostingsEnum
//
// Concatenates remapped postings from multiple segments for a single term.
// Since segment bases are monotonically increasing, the merged doc IDs are
// automatically in sorted order.
// ---------------------------------------------------------------------------
class MergedPostingsEnum : public PostingsEnum {
public:
    struct SegmentPostings {
        std::unique_ptr<PostingsEnum> postings;
        const SegmentDocMap* docMap;
    };

    explicit MergedPostingsEnum(std::vector<SegmentPostings>&& segments)
        : segments_(std::move(segments))
        , currentSeg_(0)
        , doc_(-1)
        , freq_(0) {}

    int nextDoc() override {
        while (currentSeg_ < static_cast<int>(segments_.size())) {
            auto& seg = segments_[currentSeg_];
            int oldDoc = seg.postings->nextDoc();

            if (oldDoc == NO_MORE_DOCS) {
                currentSeg_++;
                continue;
            }

            // Remap doc ID
            int newDoc = seg.docMap->oldToNew[oldDoc];
            if (newDoc == -1) {
                continue;  // Deleted doc, skip
            }

            doc_ = newDoc;
            freq_ = seg.postings->freq();
            return doc_;
        }

        doc_ = NO_MORE_DOCS;
        return NO_MORE_DOCS;
    }

    int advance(int target) override {
        // Linear advance through concatenated segments.
        // Correct because merged doc IDs are monotonically increasing.
        while (true) {
            int doc = nextDoc();
            if (doc >= target || doc == NO_MORE_DOCS) {
                return doc;
            }
        }
    }

    int docID() const override { return doc_; }

    int64_t cost() const override {
        int64_t total = 0;
        for (const auto& seg : segments_) {
            total += seg.postings->cost();
        }
        return total;
    }

    int freq() const override { return freq_; }

    int nextPosition() override {
        if (currentSeg_ < static_cast<int>(segments_.size())) {
            return segments_[currentSeg_].postings->nextPosition();
        }
        return -1;
    }

private:
    std::vector<SegmentPostings> segments_;
    int currentSeg_;
    int doc_;
    int freq_;
};

// ---------------------------------------------------------------------------
// MergedTermsEnum
//
// Multi-way merge sort of TermsEnum iterators from multiple segments.
// Uses linear scan to find the minimum term (efficient for 2-10 segments).
// For each unique term, collects all segments that contain it and produces
// a MergedPostingsEnum for their combined postings.
// ---------------------------------------------------------------------------
class MergedTermsEnum : public TermsEnum {
public:
    struct SegmentEntry {
        std::unique_ptr<TermsEnum> termsEnum;
        const SegmentDocMap* docMap;
        bool hasMore;
    };

    MergedTermsEnum(std::vector<SegmentEntry>&& entries)
        : entries_(std::move(entries))
        , initialized_(false) {}

    bool next() override {
        if (!initialized_) {
            // First call: advance all term enums to first term
            for (auto& entry : entries_) {
                entry.hasMore = entry.termsEnum->next();
            }
            initialized_ = true;
        } else {
            // Advance entries that matched the previous term
            for (int segIdx : currentSegments_) {
                entries_[segIdx].hasMore = entries_[segIdx].termsEnum->next();
            }
        }
        currentSegments_.clear();

        // Find minimum term across all segments (linear scan)
        util::BytesRef minTerm;
        bool found = false;

        for (size_t i = 0; i < entries_.size(); i++) {
            if (!entries_[i].hasMore)
                continue;

            util::BytesRef t = entries_[i].termsEnum->term();
            if (!found || t < minTerm) {
                minTerm = t;
                found = true;
            }
        }

        if (!found)
            return false;

        // Deep copy the minimum term (BytesRef may borrow from TermsEnum internals)
        currentTerm_ = minTerm.deepCopy();

        // Collect all segments that have this term
        for (size_t i = 0; i < entries_.size(); i++) {
            if (!entries_[i].hasMore)
                continue;
            util::BytesRef t = entries_[i].termsEnum->term();
            if (t == currentTerm_) {
                currentSegments_.push_back(static_cast<int>(i));
            }
        }

        return true;
    }

    bool seekExact(const util::BytesRef& /*text*/) override {
        // Not needed for merge (sequential iteration only)
        return false;
    }

    SeekStatus seekCeil(const util::BytesRef& /*text*/) override {
        // Not needed for merge
        return SeekStatus::END;
    }

    util::BytesRef term() const override { return currentTerm_; }

    int docFreq() const override {
        int total = 0;
        for (int segIdx : currentSegments_) {
            total += entries_[segIdx].termsEnum->docFreq();
        }
        return total;
    }

    int64_t totalTermFreq() const override {
        int64_t total = 0;
        for (int segIdx : currentSegments_) {
            total += entries_[segIdx].termsEnum->totalTermFreq();
        }
        return total;
    }

    std::unique_ptr<PostingsEnum> postings() override {
        std::vector<MergedPostingsEnum::SegmentPostings> segPostings;

        for (int segIdx : currentSegments_) {
            auto pe = entries_[segIdx].termsEnum->postings(FEATURE_POSITIONS);
            if (pe) {
                MergedPostingsEnum::SegmentPostings sp;
                sp.postings = std::move(pe);
                sp.docMap = entries_[segIdx].docMap;
                segPostings.push_back(std::move(sp));
            }
        }

        return std::make_unique<MergedPostingsEnum>(std::move(segPostings));
    }

private:
    std::vector<SegmentEntry> entries_;
    bool initialized_;

    util::BytesRef currentTerm_;
    std::vector<int> currentSegments_;  // Indices into entries_ that have currentTerm_
};

// ---------------------------------------------------------------------------
// MergedTerms
//
// Wraps Terms objects from multiple segments for a single field.
// Creates MergedTermsEnum that merge-sorts across all segments.
// ---------------------------------------------------------------------------
class MergedTerms : public Terms {
public:
    struct SegmentTermsRef {
        Terms* terms;  // Non-owning (owned by SegmentReader)
        const SegmentDocMap* docMap;
    };

    explicit MergedTerms(std::vector<SegmentTermsRef>&& refs)
        : refs_(std::move(refs)) {}

    std::unique_ptr<TermsEnum> iterator() const override {
        std::vector<MergedTermsEnum::SegmentEntry> entries;
        entries.reserve(refs_.size());

        for (const auto& ref : refs_) {
            MergedTermsEnum::SegmentEntry entry;
            entry.termsEnum = ref.terms->iterator();
            entry.docMap = ref.docMap;
            entry.hasMore = false;  // Initialized in MergedTermsEnum::next()
            entries.push_back(std::move(entry));
        }

        return std::make_unique<MergedTermsEnum>(std::move(entries));
    }

    int64_t size() const override { return -1; }  // Unknown for merged terms

    int getDocCount() const override { return -1; }  // Unknown for merged terms

private:
    std::vector<SegmentTermsRef> refs_;
};

// ---------------------------------------------------------------------------
// MergedFieldsIterator
//
// Iterates unique field names across all source segments (sorted order).
// ---------------------------------------------------------------------------
class MergedFieldsIterator : public Fields::Iterator {
public:
    explicit MergedFieldsIterator(std::vector<std::string>&& fieldNames)
        : fieldNames_(std::move(fieldNames))
        , pos_(0) {}

    bool hasNext() const override { return pos_ < fieldNames_.size(); }

    std::string next() override { return fieldNames_[pos_++]; }

private:
    std::vector<std::string> fieldNames_;
    size_t pos_;
};

// ---------------------------------------------------------------------------
// MergedFields
//
// Presents a unified Fields view of all source segments with doc ID remapping.
// Follows Lucene's MappedMultiFields pattern: the codec's FieldsConsumer
// iterates over this and writes the merged postings using its normal write path.
// ---------------------------------------------------------------------------
class MergedFields : public Fields {
public:
    MergedFields(const std::vector<std::shared_ptr<SegmentReader>>& readers,
                 const std::vector<SegmentDocMap>& docMaps, const FieldInfos& mergedFieldInfos)
        : readers_(readers)
        , docMaps_(docMaps)
        , mergedFieldInfos_(mergedFieldInfos) {
        // Collect unique field names that have postings
        std::set<std::string> fieldSet;
        for (const auto& fi : mergedFieldInfos_) {
            if (fi.hasPostings()) {
                fieldSet.insert(fi.name);
            }
        }
        fieldNames_.assign(fieldSet.begin(), fieldSet.end());
    }

    std::unique_ptr<Terms> terms(const std::string& field) override {
        // Collect Terms from all segments that have this field
        std::vector<MergedTerms::SegmentTermsRef> refs;

        for (size_t i = 0; i < readers_.size(); i++) {
            Terms* t = readers_[i]->terms(field);
            if (t) {
                MergedTerms::SegmentTermsRef ref;
                ref.terms = t;
                ref.docMap = &docMaps_[i];
                refs.push_back(ref);
            }
        }

        if (refs.empty())
            return nullptr;

        return std::make_unique<MergedTerms>(std::move(refs));
    }

    int size() const override { return static_cast<int>(fieldNames_.size()); }

    std::unique_ptr<Iterator> iterator() override {
        std::vector<std::string> names(fieldNames_);
        return std::make_unique<MergedFieldsIterator>(std::move(names));
    }

private:
    const std::vector<std::shared_ptr<SegmentReader>>& readers_;
    const std::vector<SegmentDocMap>& docMaps_;
    const FieldInfos& mergedFieldInfos_;
    std::vector<std::string> fieldNames_;
};

// ---------------------------------------------------------------------------
// InMemoryNormsProducer
//
// Simple NormsProducer backed by in-memory int64 arrays.
// Used during merge to feed merged norms data to the NormsConsumer codec.
// ---------------------------------------------------------------------------
class InMemoryNormsProducer : public codecs::NormsProducer {
public:
    void setNorms(int fieldNumber, std::vector<int64_t> norms) {
        normsMap_[fieldNumber] = std::move(norms);
    }

    std::unique_ptr<index::NumericDocValues> getNorms(const index::FieldInfo& field) override {
        auto it = normsMap_.find(field.number);
        if (it == normsMap_.end()) {
            return nullptr;
        }
        return std::make_unique<codecs::MemoryNumericDocValues>(it->second);
    }

    void checkIntegrity() override {}
    void close() override {}

private:
    std::unordered_map<int, std::vector<int64_t>> normsMap_;
};

}  // anonymous namespace

// ==================== Constructor ====================

SegmentMerger::SegmentMerger(store::Directory& directory, const std::string& segmentName,
                             const std::vector<std::shared_ptr<SegmentInfo>>& sourceSegments)
    : directory_(directory)
    , segmentName_(segmentName)
    , sourceSegments_(sourceSegments) {
    if (sourceSegments_.empty()) {
        throw std::invalid_argument("SegmentMerger: no source segments provided");
    }
}

// ==================== Main Merge Method ====================

std::shared_ptr<SegmentInfo> SegmentMerger::merge() {
    // Build merged FieldInfos (union of all fields)
    FieldInfos mergedFieldInfos = buildMergedFieldInfos();

    // Calculate doc ID mapping (accounting for deletions)
    docMapping_.newMaxDoc = 0;
    docMapping_.segmentBases.clear();
    docMapping_.segmentBases.reserve(sourceSegments_.size());

    for (size_t i = 0; i < sourceSegments_.size(); i++) {
        docMapping_.segmentBases.push_back(docMapping_.newMaxDoc);

        // Count live docs in this segment
        int liveDocs = sourceSegments_[i]->maxDoc() - sourceSegments_[i]->delCount();
        docMapping_.newMaxDoc += liveDocs;
    }

    // Merge postings from source segments
    if (mergedFieldInfos.hasPostings() && docMapping_.newMaxDoc > 0) {
        mergePostings(mergedFieldInfos);
    }

    // Merge stored fields
    if (docMapping_.newMaxDoc > 0) {
        mergeStoredFields(mergedFieldInfos);
    }

    // Merge doc values
    if (docMapping_.newMaxDoc > 0 && mergedFieldInfos.hasDocValues()) {
        mergeDocValues(mergedFieldInfos);
    }

    // Merge norms
    if (docMapping_.newMaxDoc > 0 && mergedFieldInfos.hasNorms()) {
        mergeNorms(mergedFieldInfos);
    }

    // Merge point values (BKD trees)
    if (docMapping_.newMaxDoc > 0) {
        mergePoints(mergedFieldInfos);
    }

    // Create merged SegmentInfo
    auto mergedInfo = std::make_shared<SegmentInfo>(segmentName_, docMapping_.newMaxDoc,
                                                    sourceSegments_[0]->codecName());

    // Discover output files by listing directory and matching segment prefix.
    // This mirrors Lucene's approach and handles codec-specific files correctly
    // (e.g., .pos only exists when fields have positions, .skp only with skip data).
    std::string prefix = segmentName_ + ".";
    int64_t totalSize = 0;
    for (const auto& file : directory_.listAll()) {
        if (file.size() > prefix.size() && file.compare(0, prefix.size(), prefix) == 0) {
            mergedInfo->addFile(file);
            totalSize += directory_.fileLength(file);
        }
    }
    mergedInfo->setSizeInBytes(totalSize);

    // Set FieldInfos
    mergedInfo->setFieldInfos(std::move(mergedFieldInfos));

    // Set diagnostics
    mergedInfo->setDiagnostic("source", "merge");
    mergedInfo->setDiagnostic("mergeMaxNumSegments", std::to_string(sourceSegments_.size()));

    // No deletions in merged segment (all deleted docs were skipped)
    mergedInfo->setDelCount(0);

    return mergedInfo;
}

// ==================== Helper Methods ====================

FieldInfos SegmentMerger::buildMergedFieldInfos() {
    // Collect all fields from source segments
    std::map<std::string, FieldInfo> fieldMap;
    int nextFieldNumber = 0;

    for (const auto& segment : sourceSegments_) {
        const auto& segFieldInfos = segment->fieldInfos();

        for (const auto& fieldInfo : segFieldInfos) {
            auto it = fieldMap.find(fieldInfo.name);

            if (it == fieldMap.end()) {
                // New field
                FieldInfo merged = fieldInfo;
                merged.number = nextFieldNumber++;
                fieldMap[fieldInfo.name] = merged;
            } else {
                // Field exists - merge attributes
                FieldInfo& existing = it->second;

                // Take most permissive indexOptions
                if (static_cast<int>(fieldInfo.indexOptions) >
                    static_cast<int>(existing.indexOptions)) {
                    existing.indexOptions = fieldInfo.indexOptions;
                }

                // Take most permissive docValuesType
                if (fieldInfo.docValuesType != DocValuesType::NONE &&
                    existing.docValuesType == DocValuesType::NONE) {
                    existing.docValuesType = fieldInfo.docValuesType;
                }

                // Merge point dimensions
                if (fieldInfo.pointDimensionCount > 0 && existing.pointDimensionCount == 0) {
                    existing.pointDimensionCount = fieldInfo.pointDimensionCount;
                    existing.pointIndexDimensionCount = fieldInfo.pointIndexDimensionCount;
                    existing.pointNumBytes = fieldInfo.pointNumBytes;
                }

                // Merge boolean flags (if any segment has it, enable it)
                existing.omitNorms = existing.omitNorms && fieldInfo.omitNorms;
                existing.storeTermVector = existing.storeTermVector || fieldInfo.storeTermVector;
                existing.storePayloads = existing.storePayloads || fieldInfo.storePayloads;
            }
        }
    }

    // Convert map to vector
    std::vector<FieldInfo> mergedFields;
    mergedFields.reserve(fieldMap.size());

    for (auto& pair : fieldMap) {
        mergedFields.push_back(pair.second);
    }

    return FieldInfos(std::move(mergedFields));
}

int SegmentMerger::mergePostings(const FieldInfos& mergedFieldInfos) {
    // Step 1: Open SegmentReaders for all source segments
    std::vector<std::shared_ptr<SegmentReader>> readers;
    readers.reserve(sourceSegments_.size());

    for (const auto& segInfo : sourceSegments_) {
        readers.push_back(SegmentReader::open(directory_, segInfo));
    }

    // Step 2: Pre-compute per-segment doc ID mapping arrays
    // This is O(totalDocs) memory but gives O(1) doc ID lookups during merge.
    std::vector<SegmentDocMap> docMaps(sourceSegments_.size());
    int nextNewDocID = 0;

    for (size_t segIdx = 0; segIdx < sourceSegments_.size(); segIdx++) {
        int maxDoc = sourceSegments_[segIdx]->maxDoc();
        docMaps[segIdx].oldToNew.resize(maxDoc);

        const util::Bits* liveDocs = readers[segIdx]->getLiveDocs();

        for (int doc = 0; doc < maxDoc; doc++) {
            if (liveDocs == nullptr || liveDocs->get(doc)) {
                docMaps[segIdx].oldToNew[doc] = nextNewDocID++;
            } else {
                docMaps[segIdx].oldToNew[doc] = -1;  // Deleted
            }
        }
    }

    // Step 3: Create MergedFields wrapper
    // This presents a unified view of all source segments with doc ID remapping.
    MergedFields mergedFields(readers, docMaps, mergedFieldInfos);

    // Step 4: Get codec and create FieldsConsumer for the merged segment
    std::string codecName = sourceSegments_[0]->codecName();
    auto& codec = codecs::Codec::forName(codecName);
    auto& postingsFormat = codec.postingsFormat();

    // Create write state for the output segment
    SegmentWriteState writeState(&directory_, segmentName_, docMapping_.newMaxDoc,
                                 mergedFieldInfos);

    // Step 5: Write merged postings using the codec's normal write path
    auto consumer = postingsFormat.fieldsConsumer(writeState);
    consumer->write(mergedFields, nullptr);  // No norms during postings merge
    consumer->close();

    return docMapping_.newMaxDoc;
}

void SegmentMerger::mergeDocValues(const FieldInfos& mergedFieldInfos) {
    if (docMapping_.newMaxDoc == 0) {
        return;
    }

    // Check if any field has numeric doc values
    bool hasNumericDV = false;
    for (const auto& fi : mergedFieldInfos) {
        if (fi.docValuesType == DocValuesType::NUMERIC) {
            hasNumericDV = true;
            break;
        }
    }
    if (!hasNumericDV) {
        return;
    }

    // Open readers for all source segments
    std::vector<std::shared_ptr<SegmentReader>> readers;
    readers.reserve(sourceSegments_.size());
    for (const auto& segInfo : sourceSegments_) {
        readers.push_back(SegmentReader::open(directory_, segInfo));
    }

    // Create writer for the merged segment
    codecs::NumericDocValuesWriter dvWriter(segmentName_, docMapping_.newMaxDoc);

    // For each field with numeric doc values
    for (const auto& fi : mergedFieldInfos) {
        if (fi.docValuesType != DocValuesType::NUMERIC) {
            continue;
        }

        int newDocID = 0;
        for (size_t segIdx = 0; segIdx < readers.size(); segIdx++) {
            auto* ndv = readers[segIdx]->getNumericDocValues(fi.name);
            const util::Bits* liveDocs = readers[segIdx]->getLiveDocs();
            int maxDoc = sourceSegments_[segIdx]->maxDoc();

            for (int doc = 0; doc < maxDoc; doc++) {
                if (liveDocs != nullptr && !liveDocs->get(doc)) {
                    continue;
                }

                if (ndv && ndv->advanceExact(doc)) {
                    dvWriter.addValue(fi, newDocID, ndv->longValue());
                }
                newDocID++;
            }
        }

        dvWriter.finishField(fi);
    }

    // Flush to output files
    auto dataOut = directory_.createOutput(segmentName_ + ".dvd", store::IOContext::DEFAULT);
    auto metaOut = directory_.createOutput(segmentName_ + ".dvm", store::IOContext::DEFAULT);
    dvWriter.flush(*dataOut, *metaOut);
    dataOut->close();
    metaOut->close();
}

void SegmentMerger::mergeStoredFields(const FieldInfos& mergedFieldInfos) {
    if (docMapping_.newMaxDoc == 0) {
        return;
    }

    // Open readers for all source segments
    std::vector<std::shared_ptr<SegmentReader>> readers;
    readers.reserve(sourceSegments_.size());
    bool hasStoredFields = false;

    for (const auto& segInfo : sourceSegments_) {
        auto reader = SegmentReader::open(directory_, segInfo);
        if (reader->storedFieldsReader() != nullptr) {
            hasStoredFields = true;
        }
        readers.push_back(std::move(reader));
    }

    if (!hasStoredFields) {
        return;  // No source segments have stored fields
    }

    // Open output files first — streaming mode writes blocks incrementally
    auto dataOut = directory_.createOutput(segmentName_ + ".fdt", store::IOContext::DEFAULT);
    auto indexOut = directory_.createOutput(segmentName_ + ".fdx", store::IOContext::DEFAULT);

    // Create writer in streaming mode: O(BLOCK_SIZE) RAM instead of O(N)
    codecs::StoredFieldsWriter writer(segmentName_, *dataOut, *indexOut);

    // Iterate source segments in order, copying live documents
    for (size_t segIdx = 0; segIdx < sourceSegments_.size(); segIdx++) {
        auto* sfReader = readers[segIdx]->storedFieldsReader();
        const util::Bits* liveDocs = readers[segIdx]->getLiveDocs();
        int maxDoc = sourceSegments_[segIdx]->maxDoc();

        for (int doc = 0; doc < maxDoc; doc++) {
            // Skip deleted docs
            if (liveDocs != nullptr && !liveDocs->get(doc)) {
                continue;
            }

            writer.startDocument();

            if (sfReader) {
                auto fields = sfReader->document(doc);
                for (const auto& [name, value] : fields) {
                    const FieldInfo* fi = mergedFieldInfos.fieldInfo(name);
                    if (!fi) {
                        continue;
                    }

                    std::visit(
                        [&writer, fi](const auto& val) {
                            using T = std::decay_t<decltype(val)>;
                            if constexpr (std::is_same_v<T, std::string>) {
                                writer.writeField(*fi, val);
                            } else if constexpr (std::is_same_v<T, int32_t>) {
                                writer.writeField(*fi, val);
                            } else if constexpr (std::is_same_v<T, int64_t>) {
                                writer.writeField(*fi, val);
                            }
                        },
                        value);
                }
            }

            writer.finishDocument();
        }
    }

    // Finish writes remaining partial block + index to disk
    writer.finish(docMapping_.newMaxDoc);

    // flush() is a no-op in streaming mode (data already on disk)
    writer.flush(*dataOut, *indexOut);
    dataOut->close();
    indexOut->close();
    writer.close();
}

void SegmentMerger::mergeNorms(const FieldInfos& mergedFieldInfos) {
    if (docMapping_.newMaxDoc == 0) {
        return;
    }

    // Check if any field has norms
    bool hasNorms = false;
    for (const auto& fi : mergedFieldInfos) {
        if (fi.hasNorms()) {
            hasNorms = true;
            break;
        }
    }
    if (!hasNorms) {
        return;
    }

    // Open readers for all source segments
    std::vector<std::shared_ptr<SegmentReader>> readers;
    readers.reserve(sourceSegments_.size());
    for (const auto& segInfo : sourceSegments_) {
        readers.push_back(SegmentReader::open(directory_, segInfo));
    }

    // Create write state and norms consumer via codec
    SegmentWriteState writeState(&directory_, segmentName_, docMapping_.newMaxDoc,
                                 mergedFieldInfos);

    std::string codecName = sourceSegments_[0]->codecName();
    auto& codec = codecs::Codec::forName(codecName);
    auto& nf = codec.normsFormat();
    auto normsConsumer = nf.normsConsumer(writeState);

    // For each field with norms
    for (const auto& fi : mergedFieldInfos) {
        if (!fi.hasNorms()) {
            continue;
        }

        // Build merged norms array across all source segments
        std::vector<int64_t> mergedNorms;
        mergedNorms.reserve(docMapping_.newMaxDoc);

        for (size_t segIdx = 0; segIdx < readers.size(); segIdx++) {
            NumericDocValues* normValues = readers[segIdx]->getNormValues(fi.name);
            const util::Bits* liveDocs = readers[segIdx]->getLiveDocs();
            int maxDoc = sourceSegments_[segIdx]->maxDoc();

            for (int doc = 0; doc < maxDoc; doc++) {
                if (liveDocs != nullptr && !liveDocs->get(doc)) {
                    continue;
                }

                int64_t normValue = 0;
                if (normValues && normValues->advanceExact(doc)) {
                    normValue = normValues->longValue();
                }
                mergedNorms.push_back(normValue);
            }
        }

        // Write merged norms through the codec's consumer
        InMemoryNormsProducer producer;
        producer.setNorms(fi.number, std::move(mergedNorms));
        normsConsumer->addNormsField(fi, producer);
    }

    normsConsumer->close();
}

void SegmentMerger::mergePoints(const FieldInfos& mergedFieldInfos) {
    // Check if any field has point values
    bool hasPoints = false;
    for (const auto& fi : mergedFieldInfos) {
        if (fi.pointDimensionCount > 0) {
            hasPoints = true;
            break;
        }
    }
    if (!hasPoints) {
        return;
    }

    // Open readers for all source segments
    std::vector<std::shared_ptr<SegmentReader>> readers;
    readers.reserve(sourceSegments_.size());
    for (const auto& segInfo : sourceSegments_) {
        readers.push_back(SegmentReader::open(directory_, segInfo));
    }

    // Build doc ID mappings for all segments (once, reused across fields)
    std::vector<std::vector<int>> allOldToNew(readers.size());
    for (size_t segIdx = 0; segIdx < readers.size(); segIdx++) {
        const util::Bits* liveDocs = readers[segIdx]->getLiveDocs();
        int maxDoc = sourceSegments_[segIdx]->maxDoc();
        int segBase = docMapping_.segmentBases[segIdx];
        allOldToNew[segIdx].resize(maxDoc);
        int mappedID = segBase;
        for (int doc = 0; doc < maxDoc; doc++) {
            if (liveDocs == nullptr || liveDocs->get(doc)) {
                allOldToNew[segIdx][doc] = mappedID++;
            } else {
                allOldToNew[segIdx][doc] = -1;
            }
        }
    }

    // Per-segment sorted run for a single field
    struct SortedRun {
        std::vector<int32_t> docIDs;
        std::vector<uint8_t> packedValues;
    };

    // Collect per-field, per-segment sorted runs, then K-way merge.
    // BKD intersect traverses left→right (ascending by packed value),
    // so each segment's output is already sorted. K-way merge is O(N log K)
    // instead of the previous O(N log N) full re-sort.

    // Pre-count fields with actual point data to write the header field count.
    // We process each field independently to cap peak memory at O(single_field * N)
    // instead of O(all_fields * N). This prevents OOM during forceMerge of large indices.
    int32_t pointFieldCount = 0;
    for (const auto& fi : mergedFieldInfos) {
        if (fi.pointDimensionCount > 0) {
            // Check if any source segment has point data for this field
            for (size_t segIdx = 0; segIdx < readers.size(); segIdx++) {
                if (readers[segIdx]->getPointValues(fi.name) != nullptr) {
                    pointFieldCount++;
                    break;
                }
            }
        }
    }

    if (pointFieldCount == 0) {
        return;
    }

    // Open output files once, stream-write each field independently
    auto kdmOut = directory_.createOutput(segmentName_ + ".kdm", store::IOContext::DEFAULT);
    auto kdiOut = directory_.createOutput(segmentName_ + ".kdi", store::IOContext::DEFAULT);
    auto kddOut = directory_.createOutput(segmentName_ + ".kdd", store::IOContext::DEFAULT);

    kdmOut->writeVInt(pointFieldCount);

    for (const auto& fi : mergedFieldInfos) {
        if (fi.pointDimensionCount == 0) {
            continue;
        }

        int packedLen = fi.pointDimensionCount * fi.pointNumBytes;
        index::BKDConfig config(fi.pointDimensionCount, fi.pointIndexDimensionCount,
                                fi.pointNumBytes);

        // Collect sorted runs from each segment for this field
        std::vector<SortedRun> runs;

        for (size_t segIdx = 0; segIdx < readers.size(); segIdx++) {
            auto* pointValues = readers[segIdx]->getPointValues(fi.name);
            if (!pointValues) {
                continue;
            }
            const util::Bits* liveDocs = readers[segIdx]->getLiveDocs();
            const std::vector<int>& oldToNew = allOldToNew[segIdx];

            // Pre-count points for this segment+field to avoid repeated vector reallocation.
            // intersect() visits every point, so we count first, then collect.
            struct CountVisitor : public PointValues::IntersectVisitor {
                int count = 0;
                const util::Bits* liveDocs;
                const std::vector<int>& oldToNew;

                CountVisitor(const util::Bits* ld, const std::vector<int>& mapping)
                    : liveDocs(ld)
                    , oldToNew(mapping) {}

                void visit(int /*docID*/) override {}
                void visit(int docID, const uint8_t* /*packedValue*/) override {
                    if (liveDocs != nullptr && !liveDocs->get(docID))
                        return;
                    if (oldToNew[docID] >= 0)
                        count++;
                }
                PointValues::Relation compare(const uint8_t*, const uint8_t*) override {
                    return PointValues::Relation::CELL_CROSSES_QUERY;
                }
            };

            CountVisitor counter(liveDocs, oldToNew);
            pointValues->intersect(counter);

            if (counter.count == 0) {
                continue;
            }

            SortedRun run;
            run.docIDs.reserve(counter.count);
            run.packedValues.reserve(static_cast<size_t>(counter.count) * packedLen);

            // Visitor that collects points in BKD sorted order
            struct SortedCollectVisitor : public PointValues::IntersectVisitor {
                SortedRun& run;
                int packedLen;
                const util::Bits* liveDocs;
                const std::vector<int>& oldToNew;

                SortedCollectVisitor(SortedRun& r, int pl, const util::Bits* ld,
                                     const std::vector<int>& mapping)
                    : run(r)
                    , packedLen(pl)
                    , liveDocs(ld)
                    , oldToNew(mapping) {}

                void visit(int /*docID*/) override {}

                void visit(int docID, const uint8_t* packedValue) override {
                    if (liveDocs != nullptr && !liveDocs->get(docID)) {
                        return;
                    }
                    int newDoc = oldToNew[docID];
                    if (newDoc >= 0) {
                        run.docIDs.push_back(newDoc);
                        size_t oldSize = run.packedValues.size();
                        run.packedValues.resize(oldSize + packedLen);
                        std::memcpy(run.packedValues.data() + oldSize, packedValue, packedLen);
                    }
                }

                PointValues::Relation compare(const uint8_t* /*minPacked*/,
                                              const uint8_t* /*maxPacked*/) override {
                    return PointValues::Relation::CELL_CROSSES_QUERY;
                }
            };

            SortedCollectVisitor visitor(run, packedLen, liveDocs, oldToNew);
            pointValues->intersect(visitor);

            if (!run.docIDs.empty()) {
                runs.push_back(std::move(run));
            }
        }

        if (runs.empty()) {
            continue;
        }

        // Merge runs and write BKD for this field immediately, then free memory.
        std::vector<int32_t> mergedDocIDs;
        std::vector<uint8_t> mergedPackedValues;

        if (runs.size() == 1) {
            // Single segment — no merge needed, already sorted
            mergedDocIDs = std::move(runs[0].docIDs);
            mergedPackedValues = std::move(runs[0].packedValues);
            runs.clear();  // Free source memory
        } else {
            // K-way merge of sorted runs using min-heap
            size_t totalPoints = 0;
            for (const auto& r : runs) {
                totalPoints += r.docIDs.size();
            }
            mergedDocIDs.reserve(totalPoints);
            mergedPackedValues.reserve(totalPoints * packedLen);

            struct HeapEntry {
                int segIdx;
                int pos;
            };

            // Min-heap comparator: smallest packed value has highest priority
            auto heapCmp = [&](const HeapEntry& a, const HeapEntry& b) {
                const uint8_t* va = runs[a.segIdx].packedValues.data() + (size_t)a.pos * packedLen;
                const uint8_t* vb = runs[b.segIdx].packedValues.data() + (size_t)b.pos * packedLen;
                int c = std::memcmp(va, vb, packedLen);
                if (c != 0)
                    return c > 0;  // min-heap: greater = lower priority
                return runs[a.segIdx].docIDs[a.pos] > runs[b.segIdx].docIDs[b.pos];
            };

            std::priority_queue<HeapEntry, std::vector<HeapEntry>, decltype(heapCmp)> heap(heapCmp);

            // Seed heap with first element from each run
            for (int i = 0; i < static_cast<int>(runs.size()); i++) {
                heap.push({i, 0});
            }

            while (!heap.empty()) {
                auto entry = heap.top();
                heap.pop();

                mergedDocIDs.push_back(runs[entry.segIdx].docIDs[entry.pos]);
                const uint8_t* val = runs[entry.segIdx].packedValues.data() +
                                     (size_t)entry.pos * packedLen;
                size_t oldSize = mergedPackedValues.size();
                mergedPackedValues.resize(oldSize + packedLen);
                std::memcpy(mergedPackedValues.data() + oldSize, val, packedLen);

                if (entry.pos + 1 < static_cast<int>(runs[entry.segIdx].docIDs.size())) {
                    heap.push({entry.segIdx, entry.pos + 1});
                }
            }

            // Free source runs before building BKD tree (halves peak memory)
            runs.clear();
            runs.shrink_to_fit();
        }

        // Write BKD for this field immediately
        codecs::BKDWriter writer(config);
        writer.writeField(fi.name, fi.number, mergedDocIDs, mergedPackedValues, *kdmOut, *kdiOut,
                          *kddOut, /*preSorted=*/true);

        // Memory for mergedDocIDs/mergedPackedValues is freed at end of this loop iteration
    }

    kdmOut->close();
    kdiOut->close();
    kddOut->close();
}

}  // namespace index
}  // namespace diagon
