// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>

namespace diagon::store {

/**
 * @brief IOContext provides hints for I/O operations to optimize buffering and caching.
 *
 * Based on: org.apache.lucene.store.IOContext
 *
 * Used to provide hints to Directory implementations about:
 * - Access patterns (sequential vs random)
 * - Operation type (merge, flush, read)
 * - Expected data size
 *
 * This allows implementations to optimize:
 * - Buffer sizes
 * - Read-ahead strategies
 * - Memory mapping decisions
 * - OS cache hints (POSIX_FADV_SEQUENTIAL, etc.)
 */
struct IOContext {
    /**
     * @brief Type of I/O operation being performed.
     */
    enum class Type {
        /** Default I/O (no specific hints) */
        DEFAULT,

        /** Merging segments (large sequential read/write) */
        MERGE,

        /** Random read access (multiple passes expected) */
        READ,

        /** Sequential read (single pass, won't re-read) */
        READONCE,

        /** Flushing to index (sequential write) */
        FLUSH
    };

    /**
     * @brief Read advice for memory-mapped files.
     *
     * Provides hints to the OS about expected access patterns.
     * Maps to posix_madvise() on Linux/macOS, file flags on Windows.
     */
    enum class ReadAdvice {
        /** Normal caching (default OS behavior) */
        NORMAL,

        /** Sequential access with read-ahead */
        SEQUENTIAL,

        /** Random access, disable read-ahead */
        RANDOM
    };

    /** The type of I/O operation */
    Type type;

    /** If true, data will be read sequentially once and not re-read */
    bool readOnce;

    /** For MERGE context: size of merge operation in bytes */
    int64_t mergeSize;

    /** For FLUSH context: estimated flush size in bytes */
    int64_t flushSize;

    /**
     * @brief Default constructor - creates DEFAULT context.
     */
    IOContext()
        : type(Type::DEFAULT)
        , readOnce(false)
        , mergeSize(0)
        , flushSize(0) {}

    /**
     * @brief Construct from type.
     * @param t I/O context type
     */
    explicit IOContext(Type t)
        : type(t)
        , readOnce(t == Type::READONCE)
        , mergeSize(0)
        , flushSize(0) {}

    /**
     * @brief Construct MERGE context with size hint.
     * @param size Size of merge operation in bytes
     */
    static IOContext forMerge(int64_t size) {
        IOContext ctx(Type::MERGE);
        ctx.mergeSize = size;
        return ctx;
    }

    /**
     * @brief Construct FLUSH context with size hint.
     * @param size Estimated flush size in bytes
     */
    static IOContext forFlush(int64_t size) {
        IOContext ctx(Type::FLUSH);
        ctx.flushSize = size;
        return ctx;
    }

    /**
     * @brief Converts IOContext type to appropriate ReadAdvice.
     *
     * Mapping:
     * - MERGE/FLUSH → SEQUENTIAL (large sequential reads/writes)
     * - READONCE → SEQUENTIAL (single-pass sequential read)
     * - READ → RANDOM (multiple random accesses expected)
     * - DEFAULT → NORMAL (let OS decide)
     *
     * @return ReadAdvice appropriate for this context
     */
    ReadAdvice getReadAdvice() const {
        switch (type) {
            case Type::MERGE:
            case Type::FLUSH:
            case Type::READONCE:
                return ReadAdvice::SEQUENTIAL;
            case Type::READ:
                return ReadAdvice::RANDOM;
            case Type::DEFAULT:
            default:
                return ReadAdvice::NORMAL;
        }
    }

    // Common pre-defined contexts
    static const IOContext DEFAULT;
    static const IOContext READONCE;
    static const IOContext READ;
    static const IOContext MERGE;
    static const IOContext FLUSH;
};

}  // namespace diagon::store
