// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

namespace diagon {
namespace index {

class IndexReader;

/**
 * RAII guard for IndexReader reference counting.
 *
 * Increments refCount on construction, decrements on destruction.
 * Prevents ref-count leaks in exception paths.
 *
 * Usage:
 *   auto guard = RefCountGuard(reader.get());
 *   // reader guaranteed alive until guard goes out of scope
 */
class RefCountGuard {
public:
    explicit RefCountGuard(IndexReader* reader) : reader_(reader) {
        if (reader_) reader_->incRef();
    }

    ~RefCountGuard() {
        if (reader_) reader_->decRef();
    }

    RefCountGuard(RefCountGuard&& other) noexcept : reader_(other.reader_) {
        other.reader_ = nullptr;
    }

    RefCountGuard(const RefCountGuard&) = delete;
    RefCountGuard& operator=(const RefCountGuard&) = delete;
    RefCountGuard& operator=(RefCountGuard&&) = delete;

private:
    IndexReader* reader_;
};

}  // namespace index
}  // namespace diagon
