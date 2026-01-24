// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/IndexInput.h"

#include "diagon/util/Exceptions.h"

namespace diagon::store {

int32_t IndexInput::readVInt() {
    uint8_t b = readByte();
    if ((b & 0x80) == 0) {
        return static_cast<int32_t>(b);
    }

    int32_t i = b & 0x7F;

    b = readByte();
    i |= (b & 0x7F) << 7;
    if ((b & 0x80) == 0) {
        return i;
    }

    b = readByte();
    i |= (b & 0x7F) << 14;
    if ((b & 0x80) == 0) {
        return i;
    }

    b = readByte();
    i |= (b & 0x7F) << 21;
    if ((b & 0x80) == 0) {
        return i;
    }

    b = readByte();
    // Last byte: only 4 bits used (total 32 bits)
    i |= (b & 0x0F) << 28;

    if ((b & 0xF0) != 0) {
        throw IOException("Invalid VInt encoding: too many bytes");
    }

    return i;
}

int64_t IndexInput::readVLong() {
    uint8_t b = readByte();
    if ((b & 0x80) == 0) {
        return static_cast<int64_t>(b);
    }

    int64_t i = b & 0x7FL;

    for (int shift = 7; shift < 64; shift += 7) {
        b = readByte();
        i |= (b & 0x7FL) << shift;
        if ((b & 0x80) == 0) {
            return i;
        }
    }

    throw IOException("Invalid VLong encoding: too many bytes");
}

std::string IndexInput::readString() {
    int32_t length = readVInt();
    if (length < 0) {
        throw IOException("Invalid string length: " + std::to_string(length));
    }

    std::string s;
    s.resize(length);
    readBytes(reinterpret_cast<uint8_t*>(s.data()), length);
    return s;
}

} // namespace diagon::store
