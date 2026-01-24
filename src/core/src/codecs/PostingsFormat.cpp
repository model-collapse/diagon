// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/PostingsFormat.h"

#include <stdexcept>
#include <unordered_map>

namespace diagon {
namespace codecs {

std::unordered_map<std::string, std::function<std::unique_ptr<PostingsFormat>()>>&
PostingsFormat::getRegistry() {
    static std::unordered_map<std::string, std::function<std::unique_ptr<PostingsFormat>()>> registry;
    return registry;
}

void PostingsFormat::registerFormat(const std::string& name,
                                   std::function<std::unique_ptr<PostingsFormat>()> factory) {
    getRegistry()[name] = factory;
}

PostingsFormat& PostingsFormat::forName(const std::string& name) {
    auto& registry = getRegistry();
    auto it = registry.find(name);
    if (it == registry.end()) {
        throw std::runtime_error("Unknown PostingsFormat: " + name);
    }

    // Create and cache singleton instances
    static std::unordered_map<std::string, std::unique_ptr<PostingsFormat>> instances;
    auto instIt = instances.find(name);
    if (instIt == instances.end()) {
        instances[name] = it->second();
        instIt = instances.find(name);
    }

    return *instIt->second;
}

}  // namespace codecs
}  // namespace diagon
