// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

namespace diagon {
namespace index {

class IndexReader;

/**
 * @deprecated shared_ptr manages IndexReader lifecycle now.
 * Keep a shared_ptr<IndexReader> instead of using RefCountGuard.
 *
 * Previously: RAII guard for IndexReader reference counting.
 * Now: No-op wrapper (incRef/decRef are deprecated no-ops).
 */
class [[deprecated("Use shared_ptr<IndexReader> instead of RefCountGuard")]] RefCountGuard {
public:
    explicit RefCountGuard(IndexReader* /*reader*/) {}

    ~RefCountGuard() = default;

    RefCountGuard(RefCountGuard&& other) noexcept = default;

    RefCountGuard(const RefCountGuard&) = delete;
    RefCountGuard& operator=(const RefCountGuard&) = delete;
    RefCountGuard& operator=(RefCountGuard&&) = delete;
};

}  // namespace index
}  // namespace diagon
