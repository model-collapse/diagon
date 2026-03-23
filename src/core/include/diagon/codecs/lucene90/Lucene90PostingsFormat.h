// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/PostingsFormat.h"

namespace diagon {
namespace codecs {
namespace lucene90 {

/**
 * PostingsFormat for reading Lucene 9.x (OpenSearch 2.11) postings.
 *
 * Read-only: fieldsConsumer() throws, fieldsProducer() returns a
 * Lucene90BlockTreeTermsReader backed by Lucene90PostingsReader.
 *
 * Wire format:
 *   .tmd — terms dict metadata (per-field stats + FST metadata + postings sub-header)
 *   .tim — term blocks (prefix-compressed entries with stats + metadata)
 *   .tip — FST index (byte array per field)
 *   .doc — doc IDs and frequencies (128-block PFOR)
 *   .pos — positions (128-block PFOR)
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90PostingsFormat
 */
class Lucene90PostingsFormat : public PostingsFormat {
public:
    std::string getName() const override { return "Lucene90"; }

    std::unique_ptr<FieldsConsumer> fieldsConsumer(index::SegmentWriteState& state) override;
    std::unique_ptr<FieldsProducer> fieldsProducer(index::SegmentReadState& state) override;
};

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
