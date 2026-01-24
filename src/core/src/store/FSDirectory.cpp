// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/FSDirectory.h"

#include "diagon/util/Exceptions.h"

#include <sys/file.h>

#include <algorithm>
#include <atomic>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

namespace diagon::store {

// ==================== FSDirectory ====================

std::unique_ptr<FSDirectory> FSDirectory::open(const std::filesystem::path& path) {
    // Create directory if it doesn't exist
    std::filesystem::create_directories(path);
    return std::make_unique<FSDirectory>(path);
}

FSDirectory::FSDirectory(const std::filesystem::path& path)
    : directory_(std::filesystem::absolute(path)) {
    if (!std::filesystem::exists(directory_)) {
        throw IOException("Directory does not exist: " + directory_.string());
    }
    if (!std::filesystem::is_directory(directory_)) {
        throw IOException("Path is not a directory: " + directory_.string());
    }
}

std::vector<std::string> FSDirectory::listAll() const {
    ensureOpen();

    std::vector<std::string> files;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path().filename().string());
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        throw IOException("Failed to list directory: " + std::string(e.what()));
    }

    // Sort for consistent ordering (Lucene returns sorted array)
    std::sort(files.begin(), files.end());
    return files;
}

void FSDirectory::deleteFile(const std::string& name) {
    ensureOpen();

    auto path = directory_ / name;
    try {
        if (!std::filesystem::remove(path)) {
            throw FileNotFoundException("File not found: " + name);
        }
    } catch (const std::filesystem::filesystem_error& e) {
        throw IOException("Failed to delete file: " + std::string(e.what()));
    }
}

int64_t FSDirectory::fileLength(const std::string& name) const {
    ensureOpen();

    auto path = directory_ / name;
    try {
        return static_cast<int64_t>(std::filesystem::file_size(path));
    } catch (const std::filesystem::filesystem_error& e) {
        throw FileNotFoundException("File not found: " + name);
    }
}

std::unique_ptr<IndexOutput> FSDirectory::createOutput(const std::string& name,
                                                       const IOContext& context) {
    ensureOpen();

    auto path = directory_ / name;

    // Check if file already exists
    if (std::filesystem::exists(path)) {
        throw FileAlreadyExistsException("File already exists: " + name);
    }

    return std::make_unique<FSIndexOutput>(path);
}

std::unique_ptr<IndexOutput> FSDirectory::createTempOutput(const std::string& prefix,
                                                           const std::string& suffix,
                                                           const IOContext& context) {
    ensureOpen();

    // Generate unique temp filename
    std::string tempName = prefix + "_" + generateTempId() + suffix + ".tmp";
    return createOutput(tempName, context);
}

std::unique_ptr<IndexInput> FSDirectory::openInput(const std::string& name,
                                                   const IOContext& context) const {
    ensureOpen();

    auto path = directory_ / name;
    if (!std::filesystem::exists(path)) {
        throw FileNotFoundException("File not found: " + name);
    }

    return std::make_unique<FSIndexInput>(path);
}

void FSDirectory::rename(const std::string& source, const std::string& dest) {
    ensureOpen();

    auto srcPath = directory_ / source;
    auto destPath = directory_ / dest;

    try {
        std::filesystem::rename(srcPath, destPath);
    } catch (const std::filesystem::filesystem_error& e) {
        throw IOException("Failed to rename file: " + std::string(e.what()));
    }
}

void FSDirectory::sync(const std::vector<std::string>& names) {
    ensureOpen();

    for (const auto& name : names) {
        auto path = directory_ / name;
        fsyncFile(path);
    }
}

void FSDirectory::syncMetaData() {
    ensureOpen();
    fsyncDirectory(directory_);
}

std::unique_ptr<Lock> FSDirectory::obtainLock(const std::string& name) {
    ensureOpen();

    auto lockPath = directory_ / name;
    return FSLock::obtain(lockPath);
}

void FSDirectory::close() {
    closed_.store(true, std::memory_order_relaxed);
}

std::string FSDirectory::toString() const {
    return "FSDirectory@" + directory_.string();
}

std::string FSDirectory::generateTempId() const {
    static std::atomic<uint64_t> counter{0};
    return std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
}

void FSDirectory::fsyncFile(const std::filesystem::path& path) {
#ifdef __linux__
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd != -1) {
        ::fsync(fd);
        ::close(fd);
    }
#elif defined(__APPLE__)
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd != -1) {
        ::fcntl(fd, F_FULLFSYNC);
        ::close(fd);
    }
#endif
    // On other platforms, best effort via stream flush
}

void FSDirectory::fsyncDirectory(const std::filesystem::path& path) {
#ifdef __linux__
    int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd != -1) {
        ::fsync(fd);
        ::close(fd);
    }
#elif defined(__APPLE__)
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd != -1) {
        ::fcntl(fd, F_FULLFSYNC);
        ::close(fd);
    }
#endif
}

// ==================== FSIndexInput ====================

FSIndexInput::FSIndexInput(const std::filesystem::path& path, size_t bufferSize)
    : file_path_(path)
    , file_(path, std::ios::binary)
    , buffer_(bufferSize)
    , buffer_position_(0)
    , buffer_length_(0)
    , slice_offset_(0)
    , is_slice_(false) {
    if (!file_.is_open()) {
        throw IOException("Failed to open file: " + path.string());
    }

    // Get file length
    file_.seekg(0, std::ios::end);
    file_length_ = file_.tellg();
    file_.seekg(0, std::ios::beg);
    file_position_ = 0;

    slice_length_ = file_length_;
}

FSIndexInput::FSIndexInput(const std::filesystem::path& path, int64_t offset, int64_t length,
                           size_t bufferSize)
    : file_path_(path)
    , file_(path, std::ios::binary)
    , buffer_(bufferSize)
    , buffer_position_(0)
    , buffer_length_(0)
    , slice_offset_(offset)
    , slice_length_(length)
    , is_slice_(true) {
    if (!file_.is_open()) {
        throw IOException("Failed to open file: " + path.string());
    }

    // Get file length
    file_.seekg(0, std::ios::end);
    file_length_ = file_.tellg();

    // Seek to slice start
    file_.seekg(slice_offset_, std::ios::beg);
    file_position_ = 0;
}

uint8_t FSIndexInput::readByte() {
    if (buffer_position_ >= buffer_length_) {
        refillBuffer();
    }
    return buffer_[buffer_position_++];
}

void FSIndexInput::readBytes(uint8_t* buffer, size_t length) {
    size_t remaining = length;
    size_t offset = 0;

    while (remaining > 0) {
        if (buffer_position_ >= buffer_length_) {
            refillBuffer();
        }

        size_t available = buffer_length_ - buffer_position_;
        size_t toCopy = std::min(remaining, available);

        memcpy(buffer + offset, buffer_.data() + buffer_position_, toCopy);
        buffer_position_ += toCopy;
        offset += toCopy;
        remaining -= toCopy;
    }
}

int64_t FSIndexInput::getFilePointer() const {
    return file_position_ - (buffer_length_ - buffer_position_);
}

void FSIndexInput::seek(int64_t pos) {
    if (pos < 0 || pos > length()) {
        throw IOException("Invalid seek position: " + std::to_string(pos));
    }

    // Calculate absolute file position
    int64_t absolutePos = is_slice_ ? (slice_offset_ + pos) : pos;

    // Check if position is in current buffer
    int64_t bufferStart = file_position_ - buffer_length_;
    if (absolutePos >= bufferStart && absolutePos < file_position_) {
        // Position is in buffer
        buffer_position_ = absolutePos - bufferStart;
    } else {
        // Need to seek in file
        file_.seekg(absolutePos, std::ios::beg);
        if (!file_) {
            throw IOException("Seek failed");
        }
        file_position_ = absolutePos;
        buffer_position_ = 0;
        buffer_length_ = 0;
    }
}

int64_t FSIndexInput::length() const {
    return slice_length_;
}

std::unique_ptr<IndexInput> FSIndexInput::clone() const {
    std::unique_ptr<FSIndexInput> cloned;
    if (is_slice_) {
        cloned = std::make_unique<FSIndexInput>(file_path_, slice_offset_, slice_length_,
                                                buffer_.size());
    } else {
        cloned = std::make_unique<FSIndexInput>(file_path_, buffer_.size());
    }
    // Preserve current position
    cloned->seek(getFilePointer());
    return cloned;
}

std::unique_ptr<IndexInput> FSIndexInput::slice(const std::string& sliceDescription, int64_t offset,
                                                int64_t length) const {
    if (offset < 0 || length < 0 || offset + length > this->length()) {
        throw IOException("Invalid slice parameters");
    }

    int64_t absoluteOffset = is_slice_ ? (slice_offset_ + offset) : offset;
    return std::make_unique<FSIndexInput>(file_path_, absoluteOffset, length, buffer_.size());
}

std::string FSIndexInput::toString() const {
    return "FSIndexInput(" + file_path_.string() + ")";
}

void FSIndexInput::refillBuffer() {
    int64_t available = length() - getFilePointer();
    if (available <= 0) {
        throw EOFException("Read past EOF");
    }

    size_t toRead = std::min(static_cast<size_t>(available), buffer_.size());

    file_.read(reinterpret_cast<char*>(buffer_.data()), toRead);
    buffer_length_ = file_.gcount();
    buffer_position_ = 0;
    file_position_ += buffer_length_;

    if (buffer_length_ == 0) {
        throw EOFException("Read past EOF");
    }
}

// ==================== FSIndexOutput ====================

FSIndexOutput::FSIndexOutput(const std::filesystem::path& path, size_t bufferSize)
    : file_path_(path)
    , file_(path, std::ios::binary | std::ios::trunc)
    , file_position_(0)
    , buffer_(bufferSize)
    , buffer_position_(0) {
    if (!file_.is_open()) {
        throw IOException("Failed to create file: " + path.string());
    }
}

FSIndexOutput::~FSIndexOutput() {
    if (file_.is_open()) {
        try {
            close();
        } catch (...) {
            // Ignore exceptions in destructor
        }
    }
}

void FSIndexOutput::writeByte(uint8_t b) {
    if (buffer_position_ >= buffer_.size()) {
        flushBuffer();
    }
    buffer_[buffer_position_++] = b;
}

void FSIndexOutput::writeBytes(const uint8_t* buffer, size_t length) {
    size_t remaining = length;
    size_t offset = 0;

    while (remaining > 0) {
        if (buffer_position_ >= buffer_.size()) {
            flushBuffer();
        }

        size_t available = buffer_.size() - buffer_position_;
        size_t toCopy = std::min(remaining, available);

        memcpy(buffer_.data() + buffer_position_, buffer + offset, toCopy);
        buffer_position_ += toCopy;
        offset += toCopy;
        remaining -= toCopy;
    }
}

int64_t FSIndexOutput::getFilePointer() const {
    return file_position_ + buffer_position_;
}

void FSIndexOutput::close() {
    if (file_.is_open()) {
        flushBuffer();
        file_.close();
    }
}

void FSIndexOutput::flushBuffer() {
    if (buffer_position_ > 0) {
        file_.write(reinterpret_cast<const char*>(buffer_.data()), buffer_position_);
        if (!file_) {
            throw IOException("Write failed");
        }
        file_position_ += buffer_position_;
        buffer_position_ = 0;
    }
}

// ==================== FSLock ====================

std::unique_ptr<Lock> FSLock::obtain(const std::filesystem::path& lockPath) {
    return std::make_unique<FSLock>(lockPath);
}

FSLock::FSLock(const std::filesystem::path& lockPath)
    : lock_path_(lockPath)
    , fd_(-1)
    , closed_(false) {
    // Create lock file
    fd_ = ::open(lockPath.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd_ == -1) {
        throw LockObtainFailedException("Failed to create lock file: " + lockPath.string());
    }

    // Try to lock (non-blocking)
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd_);
        throw LockObtainFailedException("Failed to obtain lock: " + lockPath.string());
    }
}

FSLock::~FSLock() {
    if (!closed_) {
        try {
            close();
        } catch (...) {
            // Ignore exceptions in destructor
        }
    }
}

void FSLock::close() {
    if (!closed_) {
        if (fd_ != -1) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
            fd_ = -1;
        }
        // Remove lock file
        std::filesystem::remove(lock_path_);
        closed_ = true;
    }
}

void FSLock::ensureValid() {
    if (closed_) {
        throw LockObtainFailedException("Lock has been closed");
    }
    if (fd_ == -1) {
        throw LockObtainFailedException("Lock file descriptor is invalid");
    }
}

}  // namespace diagon::store
