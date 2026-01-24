// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/DocValuesFormat.h"

#include <stdexcept>
#include <unordered_map>

namespace diagon {
namespace codecs {

std::unordered_map<std::string, std::function<std::unique_ptr<DocValuesFormat>()>>&
DocValuesFormat::getRegistry() {
    static std::unordered_map<std::string, std::function<std::unique_ptr<DocValuesFormat>()>> registry;
    return registry;
}

void DocValuesFormat::registerFormat(const std::string& name,
                                    std::function<std::unique_ptr<DocValuesFormat>()> factory) {
    getRegistry()[name] = factory;
}

DocValuesFormat& DocValuesFormat::forName(const std::string& name) {
    auto& registry = getRegistry();
    auto it = registry.find(name);
    if (it == registry.end()) {
        throw std::runtime_error("Unknown DocValuesFormat: " + name);
    }

    // Create and cache singleton instances
    static std::unordered_map<std::string, std::unique_ptr<DocValuesFormat>> instances;
    auto instIt = instances.find(name);
    if (instIt == instances.end()) {
        instances[name] = it->second();
        instIt = instances.find(name);
    }

    return *instIt->second;
}

}  // namespace codecs
}  // namespace diagon
