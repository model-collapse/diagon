// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/SearchProfiler.h"

namespace diagon {
namespace util {

#ifdef DIAGON_PROFILE_SEARCH

SearchProfiler& SearchProfiler::instance() {
    thread_local SearchProfiler profiler;
    return profiler;
}

#endif

}  // namespace util
}  // namespace diagon
