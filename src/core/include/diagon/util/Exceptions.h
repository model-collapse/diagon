// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <stdexcept>
#include <string>

namespace diagon {

/**
 * @brief Base exception class for all Diagon exceptions.
 */
class DiagonException : public std::runtime_error {
public:
    explicit DiagonException(const std::string& message)
        : std::runtime_error(message) {}
};

/**
 * @brief Thrown when an I/O operation fails.
 */
class IOException : public DiagonException {
public:
    explicit IOException(const std::string& message)
        : DiagonException(message) {}
};

/**
 * @brief Thrown when a file is not found.
 */
class FileNotFoundException : public IOException {
public:
    explicit FileNotFoundException(const std::string& message)
        : IOException(message) {}
};

/**
 * @brief Thrown when attempting to create a file that already exists.
 */
class FileAlreadyExistsException : public IOException {
public:
    explicit FileAlreadyExistsException(const std::string& message)
        : IOException(message) {}
};

/**
 * @brief Thrown when an operation is attempted on a closed resource.
 */
class AlreadyClosedException : public DiagonException {
public:
    explicit AlreadyClosedException(const std::string& message)
        : DiagonException(message) {}
};

/**
 * @brief Thrown when an unsupported operation is attempted.
 */
class UnsupportedOperationException : public DiagonException {
public:
    explicit UnsupportedOperationException(const std::string& message)
        : DiagonException(message) {}
};

/**
 * @brief Thrown when a lock cannot be obtained.
 */
class LockObtainFailedException : public IOException {
public:
    explicit LockObtainFailedException(const std::string& message)
        : IOException(message) {}
};

/**
 * @brief Thrown when attempting to read beyond EOF.
 */
class EOFException : public IOException {
public:
    explicit EOFException(const std::string& message)
        : IOException(message) {}
};

}  // namespace diagon
