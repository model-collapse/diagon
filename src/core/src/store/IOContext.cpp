// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/IOContext.h"

namespace diagon::store {

// Static constant definitions
const IOContext IOContext::DEFAULT = IOContext(IOContext::Type::DEFAULT);
const IOContext IOContext::READONCE = IOContext(IOContext::Type::READONCE);
const IOContext IOContext::READ = IOContext(IOContext::Type::READ);
const IOContext IOContext::MERGE = IOContext(IOContext::Type::MERGE);
const IOContext IOContext::FLUSH = IOContext(IOContext::Type::FLUSH);

} // namespace diagon::store
