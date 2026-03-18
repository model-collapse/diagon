// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/PostingsEnum.h"

#include <memory>

namespace diagon {
namespace codecs {

/**
 * Abstract base class for postings readers.
 *
 * Provides a common interface for reading postings from different formats
 * (native Diagon format and Lucene-compatible OS format). Used by
 * BlockTreeTermsReader/SegmentTermsEnum to decouple from concrete reader types.
 */
class PostingsReaderBase {
public:
    virtual ~PostingsReaderBase() = default;

    /**
     * Get postings for a term.
     *
     * @param fieldInfo Field metadata
     * @param termState Term state (file pointers from term dictionary)
     * @param useBatch If true, use batch-optimized PostingsEnum (native only)
     * @return PostingsEnum for iterating documents
     */
    virtual std::unique_ptr<index::PostingsEnum> postings(
        const index::FieldInfo& fieldInfo, const lucene104::TermState& termState,
        bool useBatch) = 0;

    /**
     * Get postings with position support.
     *
     * @param fieldInfo Field metadata
     * @param termState Term state
     * @return PostingsEnum with nextPosition() support
     */
    virtual std::unique_ptr<index::PostingsEnum> postingsWithPositions(
        const index::FieldInfo& fieldInfo, const lucene104::TermState& termState) = 0;

    /**
     * Get impacts-aware postings for Block-Max WAND.
     *
     * @param fieldInfo Field metadata
     * @param termState Term state
     * @return PostingsEnum with impact/skip support
     */
    virtual std::unique_ptr<index::PostingsEnum> impactsPostings(
        const index::FieldInfo& fieldInfo, const lucene104::TermState& termState) = 0;

    /**
     * Close all input files.
     */
    virtual void close() = 0;
};

}  // namespace codecs
}  // namespace diagon
