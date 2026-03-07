// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexReader.h"

namespace diagon {
namespace index {

// ==================== LeafReaderContextWrapper ====================

LeafReaderContextWrapper::LeafReaderContextWrapper(std::shared_ptr<LeafReader> reader)
    : ctx_(std::move(reader), 0, 0) {}

IndexReader* LeafReaderContextWrapper::reader() const {
    return ctx_.reader.get();
}

std::vector<LeafReaderContext> LeafReaderContextWrapper::leaves() const {
    return {ctx_};
}

// ==================== CompositeReaderContext ====================

IndexReader* CompositeReaderContext::reader() const {
    return reader_;
}

}  // namespace index
}  // namespace diagon
