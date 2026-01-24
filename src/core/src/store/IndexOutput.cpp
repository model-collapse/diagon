// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/IndexOutput.h"

#include "diagon/util/Exceptions.h"

namespace diagon::store {

void IndexOutput::writeVInt(int32_t i) {
    uint32_t ui = static_cast<uint32_t>(i);
    while ((ui & ~0x7FU) != 0) {
        writeByte(static_cast<uint8_t>((ui & 0x7F) | 0x80));
        ui >>= 7;
    }
    writeByte(static_cast<uint8_t>(ui));
}

void IndexOutput::writeVLong(int64_t l) {
    uint64_t ul = static_cast<uint64_t>(l);
    while ((ul & ~0x7FUL) != 0) {
        writeByte(static_cast<uint8_t>((ul & 0x7F) | 0x80));
        ul >>= 7;
    }
    writeByte(static_cast<uint8_t>(ul));
}

void IndexOutput::writeString(const std::string& s) {
    writeVInt(static_cast<int32_t>(s.length()));
    writeBytes(reinterpret_cast<const uint8_t*>(s.data()), s.length());
}

}  // namespace diagon::store
