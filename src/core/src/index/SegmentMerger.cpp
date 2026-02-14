// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/SegmentMerger.h"

#include "diagon/codecs/LiveDocsFormat.h"
#include "diagon/index/SegmentReader.h"

#include <stdexcept>

namespace diagon {
namespace index {

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

    // Merge data from source segments
    // Note: For Phase 4, we implement the infrastructure but stub out the actual data copying
    // Full implementation would copy postings, doc values, and stored fields

    // Create merged SegmentInfo
    auto mergedInfo = std::make_shared<SegmentInfo>(segmentName_, docMapping_.newMaxDoc,
                                                    sourceSegments_[0]->codecName());

    // Set FieldInfos
    mergedInfo->setFieldInfos(std::move(mergedFieldInfos));

    // Set diagnostics
    mergedInfo->setDiagnostic("source", "merge");
    mergedInfo->setDiagnostic("mergeMaxNumSegments", std::to_string(sourceSegments_.size()));

    // Calculate size (sum of source segments, adjusted for deletions)
    int64_t totalSize = 0;
    for (const auto& seg : sourceSegments_) {
        if (seg->maxDoc() == 0)
            continue;
        // Adjust size for deletion ratio
        double liveRatio = (double)(seg->maxDoc() - seg->delCount()) / seg->maxDoc();
        totalSize += (int64_t)(seg->sizeInBytes() * liveRatio);
    }
    mergedInfo->setSizeInBytes(totalSize);

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
    // Stub: Full implementation would:
    // 1. Open SegmentReader for each source segment
    // 2. For each field with postings:
    //    - Iterate terms in sorted order
    //    - For each term, merge postings from all segments
    //    - Remap doc IDs using mapDocID()
    //    - Skip deleted documents
    // 3. Write merged postings to new segment

    return docMapping_.newMaxDoc;
}

void SegmentMerger::mergeDocValues(const FieldInfos& mergedFieldInfos, int docBase) {
    // Stub: Full implementation would:
    // 1. Open SegmentReader for each source segment
    // 2. For each field with doc values:
    //    - Read doc values from source segments
    //    - Remap doc IDs using mapDocID()
    //    - Skip deleted documents
    //    - Write to merged segment
}

void SegmentMerger::mergeStoredFields(int docBase) {
    // Stub: Full implementation would:
    // 1. Open SegmentReader for each source segment
    // 2. For each live document:
    //    - Read stored fields from source
    //    - Write to merged segment with new doc ID
    //    - Skip deleted documents
}

bool SegmentMerger::isDeleted(int sourceSegmentIdx, int docID) {
    if (sourceSegmentIdx < 0 || sourceSegmentIdx >= static_cast<int>(sourceSegments_.size())) {
        return true;
    }

    const auto& segment = sourceSegments_[sourceSegmentIdx];

    if (!segment->hasDeletions()) {
        return false;  // No deletions
    }

    // Open segment reader to check live docs
    try {
        auto reader = SegmentReader::open(directory_, segment);
        const util::Bits* liveDocs = reader->getLiveDocs();

        if (!liveDocs) {
            reader->decRef();
            return false;  // No live docs bitmap
        }

        bool deleted = !liveDocs->get(docID);
        reader->decRef();
        return deleted;
    } catch (const std::exception&) {
        return true;  // Assume deleted if can't read
    }
}

int SegmentMerger::mapDocID(int sourceSegmentIdx, int oldDocID) {
    if (sourceSegmentIdx < 0 || sourceSegmentIdx >= static_cast<int>(sourceSegments_.size())) {
        return -1;
    }

    // Check if deleted
    if (isDeleted(sourceSegmentIdx, oldDocID)) {
        return -1;
    }

    // Calculate new doc ID
    int newDocID = docMapping_.segmentBases[sourceSegmentIdx];

    // Count live docs before this one in the same segment
    const auto& segment = sourceSegments_[sourceSegmentIdx];
    if (segment->hasDeletions()) {
        try {
            auto reader = SegmentReader::open(directory_, segment);
            const util::Bits* liveDocs = reader->getLiveDocs();

            if (liveDocs) {
                for (int i = 0; i < oldDocID; i++) {
                    if (liveDocs->get(i)) {
                        newDocID++;
                    }
                }
            } else {
                newDocID += oldDocID;
            }

            reader->decRef();
        } catch (const std::exception&) {
            return -1;
        }
    } else {
        // No deletions - simple offset
        newDocID += oldDocID;
    }

    return newDocID;
}

}  // namespace index
}  // namespace diagon
