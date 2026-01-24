// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/Directory.h"

#include "diagon/util/Exceptions.h"

namespace diagon::store {

void Directory::ensureOpen() const {
    if (closed_.load(std::memory_order_relaxed)) {
        throw AlreadyClosedException("Directory has been closed");
    }
}

} // namespace diagon::store
