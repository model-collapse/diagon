// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/PostingsFormat.h"

namespace diagon {
namespace codecs {
namespace lucene104 {

/**
 * OS-compatible postings format using Lucene 104 wire format.
 *
 * Uses ForUtil/PForUtil for 256-int blocks, two-level skip data,
 * and produces .doc/.pos files byte-level compatible with OpenSearch.
 */
class Lucene104OSPostingsFormat : public PostingsFormat {
public:
    std::string getName() const override { return "Lucene104OS"; }

    std::unique_ptr<FieldsConsumer> fieldsConsumer(index::SegmentWriteState& state) override;
    std::unique_ptr<FieldsProducer> fieldsProducer(index::SegmentReadState& state) override;
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
