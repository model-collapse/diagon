// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/SegmentMerger.h"

#include "diagon/codecs/Codec.h"
#include "diagon/codecs/LiveDocsFormat.h"
#include "diagon/codecs/NormsFormat.h"
#include "diagon/codecs/NumericDocValuesReader.h"
#include "diagon/codecs/NumericDocValuesWriter.h"
#include "diagon/codecs/PostingsFormat.h"
#include "diagon/codecs/StoredFieldsReader.h"
#include "diagon/codecs/StoredFieldsWriter.h"
#include "diagon/index/Fields.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentReader.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/index/Terms.h"
#include "diagon/index/TermsEnum.h"
#include "diagon/store/IOContext.h"
#include "diagon/util/BytesRef.h"

#include <algorithm>
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

    // Create writer for the merged segment
    codecs::StoredFieldsWriter writer(segmentName_);

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

    // Finish and flush to output files
    writer.finish(docMapping_.newMaxDoc);

    auto dataOut = directory_.createOutput(segmentName_ + ".fdt", store::IOContext::DEFAULT);
    auto indexOut = directory_.createOutput(segmentName_ + ".fdx", store::IOContext::DEFAULT);
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

}  // namespace index
}  // namespace diagon
