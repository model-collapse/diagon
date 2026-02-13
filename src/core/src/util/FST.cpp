// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/FST.h"
#include "diagon/util/PackedFST.h"

namespace diagon {
namespace util {

// ==================== FST (Adapter over PackedFST) ====================

FST::FST()
    : packed_(std::make_unique<PackedFST>()) {}

FST::FST(std::unique_ptr<PackedFST> packed)
    : packed_(std::move(packed)) {}

FST::Output FST::get(const BytesRef& input) const {
    return packed_->get(input);
}

FST::Output FST::getLongestPrefixMatch(const BytesRef& input, int& prefixLen) const {
    return packed_->getLongestPrefixMatch(input, prefixLen);
}

std::vector<uint8_t> FST::serialize() const {
    return packed_->serialize();
}

std::unique_ptr<FST> FST::deserialize(const std::vector<uint8_t>& data) {
    auto packed = PackedFST::deserialize(data);
    return std::make_unique<FST>(std::move(packed));
}

const std::vector<std::pair<std::vector<uint8_t>, FST::Output>>& FST::getAllEntries() const {
    return packed_->getAllEntries();
}

// ==================== FST::Builder (Adapter over PackedFST::Builder) ====================

FST::Builder::Builder()
    : packedBuilder_(std::make_unique<PackedFST::Builder>()) {}

FST::Builder::~Builder() = default;

void FST::Builder::add(const BytesRef& input, Output output) {
    packedBuilder_->add(input, output);
}

std::unique_ptr<FST> FST::Builder::finish() {
    auto packed = packedBuilder_->finish();
    return std::make_unique<FST>(std::move(packed));
}

const std::vector<FST::Builder::Entry>& FST::Builder::getEntries() const {
    // Need to convert PackedFST entries to FST entries
    // Both have the same structure, so we can safely cast
    return reinterpret_cast<const std::vector<FST::Builder::Entry>&>(
        packedBuilder_->getEntries());
}

}  // namespace util
}  // namespace diagon
