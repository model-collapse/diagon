// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/Codec.h"

#include <stdexcept>

namespace diagon {
namespace codecs {

std::unordered_map<std::string, std::function<std::unique_ptr<Codec>()>>& Codec::getRegistry() {
    static std::unordered_map<std::string, std::function<std::unique_ptr<Codec>()>> registry;
    return registry;
}

std::string& Codec::getDefaultCodecName() {
    static std::string defaultCodecName = "Lucene104";
    return defaultCodecName;
}

void Codec::registerCodec(const std::string& name,
                          std::function<std::unique_ptr<Codec>()> factory) {
    getRegistry()[name] = factory;
}

Codec& Codec::getDefault() {
    return forName(getDefaultCodecName());
}

Codec& Codec::forName(const std::string& name) {
    auto& registry = getRegistry();
    auto it = registry.find(name);
    if (it == registry.end()) {
        throw std::runtime_error("Unknown codec: " + name);
    }

    // Create and cache a singleton instance per codec name
    // This is safe because codecs are stateless
    static std::unordered_map<std::string, std::unique_ptr<Codec>> instances;
    auto instIt = instances.find(name);
    if (instIt == instances.end()) {
        instances[name] = it->second();
        instIt = instances.find(name);
    }

    return *instIt->second;
}

std::vector<std::string> Codec::availableCodecs() {
    std::vector<std::string> names;
    auto& registry = getRegistry();
    names.reserve(registry.size());
    for (const auto& pair : registry) {
        names.push_back(pair.first);
    }
    return names;
}

}  // namespace codecs
}  // namespace diagon
