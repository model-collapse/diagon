// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexReader.h"

namespace diagon {
namespace index {

// ==================== LeafReaderContextWrapper ====================

LeafReaderContextWrapper::LeafReaderContextWrapper(LeafReader* reader)
    : ctx_(reader) {}

IndexReader* LeafReaderContextWrapper::reader() const {
    return ctx_.reader();
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
