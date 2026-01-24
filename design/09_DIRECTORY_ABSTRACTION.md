# Directory Abstraction Design
## Based on Lucene Store Layer

Source references:
- `org.apache.lucene.store.Directory`
- `org.apache.lucene.store.FSDirectory`
- `org.apache.lucene.store.MMapDirectory`
- `org.apache.lucene.store.ByteBuffersDirectory`
- `org.apache.lucene.store.IndexInput`
- `org.apache.lucene.store.IndexOutput`
- `org.apache.lucene.store.Lock`

## Overview

Directory abstraction provides:
- **Filesystem independence**: Read/write index files without knowing storage details
- **Multiple implementations**: FS, mmap, in-memory, custom (S3, HDFS)
- **Lock management**: Prevents multiple writers
- **Checksum support**: Data integrity verification

## Directory (Abstract Base)

```cpp
/**
 * Directory is an abstract filesystem-like storage for index files.
 *
 * Implementations: FSDirectory, MMapDirectory, ByteBuffersDirectory
 *
 * Thread-safe for concurrent reads, synchronized writes.
 *
 * Based on: org.apache.lucene.store.Directory
 */
class Directory {
public:
    virtual ~Directory() = default;

    // ==================== File Listing ====================

    /**
     * List all files in directory
     */
    virtual std::vector<std::string> listAll() const = 0;

    // ==================== File Operations ====================

    /**
     * Delete file
     * @throws FileNotFoundException if file doesn't exist
     */
    virtual void deleteFile(const std::string& name) = 0;

    /**
     * File size in bytes
     */
    virtual int64_t fileLength(const std::string& name) const = 0;

    /**
     * Create output stream for writing
     * @param name File name
     * @param context I/O context (hints for buffering, etc.)
     */
    virtual std::unique_ptr<IndexOutput> createOutput(
        const std::string& name,
        const IOContext& context) = 0;

    /**
     * Create temporary output (deleted if not finalized)
     * @param prefix Filename prefix
     * @param suffix Filename suffix
     * @param context I/O context
     */
    virtual std::unique_ptr<IndexOutput> createTempOutput(
        const std::string& prefix,
        const std::string& suffix,
        const IOContext& context) = 0;

    /**
     * Open input stream for reading
     */
    virtual std::unique_ptr<IndexInput> openInput(
        const std::string& name,
        const IOContext& context) const = 0;

    /**
     * Open input with checksum verification
     */
    std::unique_ptr<ChecksumIndexInput> openChecksumInput(
        const std::string& name) const {
        auto input = openInput(name, IOContext::READONCE);
        return std::make_unique<BufferedChecksumIndexInput>(std::move(input));
    }

    // ==================== Atomic Operations ====================

    /**
     * Rename file atomically
     */
    virtual void rename(const std::string& source,
                       const std::string& dest) = 0;

    /**
     * Sync file to disk
     * @param names Files to sync
     */
    virtual void sync(const std::vector<std::string>& names) = 0;

    /**
     * Sync directory metadata
     */
    virtual void syncMetaData() = 0;

    // ==================== Locking ====================

    /**
     * Obtain lock
     * @param name Lock name (e.g., "write.lock")
     * @return Lock or nullptr if cannot acquire
     */
    virtual std::unique_ptr<Lock> obtainLock(const std::string& name) = 0;

    // ==================== Utilities ====================

    /**
     * Close directory
     */
    virtual void close() = 0;

    /**
     * Get file path (if filesystem-based)
     */
    virtual std::optional<std::filesystem::path> getPath() const {
        return std::nullopt;
    }

protected:
    /**
     * Ensure directory is open
     */
    void ensureOpen() const {
        if (closed_) {
            throw AlreadyClosedException("Directory is closed");
        }
    }

    std::atomic<bool> closed_{false};
};
```

## IOContext

```cpp
/**
 * IOContext provides hints for I/O operations.
 *
 * Based on: org.apache.lucene.store.IOContext
 */
struct IOContext {
    enum class Context {
        DEFAULT,
        MERGE,
        READ,
        READONCE,  // Sequential read, won't re-read
        FLUSH
    };

    Context context;
    bool readOnce;      // Sequential read only
    int64_t mergeSize;  // For MERGE context

    IOContext() : context(Context::DEFAULT), readOnce(false), mergeSize(0) {}

    explicit IOContext(Context ctx) : context(ctx), readOnce(false), mergeSize(0) {
        if (ctx == Context::READONCE) {
            readOnce = true;
        }
    }

    // Common contexts
    static const IOContext DEFAULT;
    static const IOContext READONCE;
    static const IOContext MERGE;
    static const IOContext FLUSH;
};
```

## IndexInput (Abstract)

```cpp
/**
 * IndexInput reads from an index file.
 *
 * Random access with seeking.
 * Immutable after creation.
 *
 * Based on: org.apache.lucene.store.IndexInput
 */
class IndexInput {
public:
    virtual ~IndexInput() = default;

    // ==================== Reading ====================

    /**
     * Read single byte
     */
    virtual uint8_t readByte() = 0;

    /**
     * Read bytes into buffer
     * @param buffer Destination
     * @param length Number of bytes
     */
    virtual void readBytes(uint8_t* buffer, size_t length) = 0;

    /**
     * Read short (16-bit)
     */
    virtual int16_t readShort() {
        return (static_cast<int16_t>(readByte()) << 8) | readByte();
    }

    /**
     * Read int (32-bit)
     */
    virtual int32_t readInt() {
        return (static_cast<int32_t>(readShort()) << 16) | (readShort() & 0xFFFF);
    }

    /**
     * Read long (64-bit)
     */
    virtual int64_t readLong() {
        return (static_cast<int64_t>(readInt()) << 32) | (readInt() & 0xFFFFFFFFLL);
    }

    /**
     * Read VInt (variable-length int)
     */
    virtual int32_t readVInt() {
        uint8_t b = readByte();
        if ((b & 0x80) == 0) return b;

        int32_t i = b & 0x7F;
        b = readByte();
        i |= (b & 0x7F) << 7;
        if ((b & 0x80) == 0) return i;

        b = readByte();
        i |= (b & 0x7F) << 14;
        if ((b & 0x80) == 0) return i;

        b = readByte();
        i |= (b & 0x7F) << 21;
        if ((b & 0x80) == 0) return i;

        b = readByte();
        i |= (b & 0x0F) << 28;
        return i;
    }

    /**
     * Read VLong (variable-length long)
     */
    virtual int64_t readVLong() {
        uint8_t b = readByte();
        if ((b & 0x80) == 0) return b;

        int64_t i = b & 0x7FL;
        for (int shift = 7; shift < 64; shift += 7) {
            b = readByte();
            i |= (b & 0x7FL) << shift;
            if ((b & 0x80) == 0) return i;
        }
        throw IOException("Invalid VLong encoding");
    }

    /**
     * Read string
     */
    virtual std::string readString() {
        int32_t length = readVInt();
        std::string s(length, '\0');
        readBytes(reinterpret_cast<uint8_t*>(s.data()), length);
        return s;
    }

    // ==================== Positioning ====================

    /**
     * Current file pointer position
     */
    virtual int64_t getFilePointer() const = 0;

    /**
     * Seek to position
     */
    virtual void seek(int64_t pos) = 0;

    /**
     * File length
     */
    virtual int64_t length() const = 0;

    // ==================== Cloning ====================

    /**
     * Clone this input (shares file handle, independent position)
     */
    virtual std::unique_ptr<IndexInput> clone() const = 0;

    /**
     * Create slice (view of subset of file)
     */
    virtual std::unique_ptr<IndexInput> slice(
        const std::string& sliceDescription,
        int64_t offset,
        int64_t length) const = 0;
};
```

## IndexOutput (Abstract)

```cpp
/**
 * IndexOutput writes to an index file.
 *
 * Sequential writes with optional checksum.
 *
 * Based on: org.apache.lucene.store.IndexOutput
 */
class IndexOutput {
public:
    virtual ~IndexOutput() = default;

    // ==================== Writing ====================

    /**
     * Write single byte
     */
    virtual void writeByte(uint8_t b) = 0;

    /**
     * Write bytes from buffer
     */
    virtual void writeBytes(const uint8_t* buffer, size_t length) = 0;

    /**
     * Write short (16-bit)
     */
    virtual void writeShort(int16_t s) {
        writeByte(static_cast<uint8_t>(s >> 8));
        writeByte(static_cast<uint8_t>(s));
    }

    /**
     * Write int (32-bit)
     */
    virtual void writeInt(int32_t i) {
        writeShort(static_cast<int16_t>(i >> 16));
        writeShort(static_cast<int16_t>(i));
    }

    /**
     * Write long (64-bit)
     */
    virtual void writeLong(int64_t l) {
        writeInt(static_cast<int32_t>(l >> 32));
        writeInt(static_cast<int32_t>(l));
    }

    /**
     * Write VInt (variable-length int)
     */
    virtual void writeVInt(int32_t i) {
        while ((i & ~0x7F) != 0) {
            writeByte(static_cast<uint8_t>((i & 0x7F) | 0x80));
            i = static_cast<uint32_t>(i) >> 7;
        }
        writeByte(static_cast<uint8_t>(i));
    }

    /**
     * Write VLong (variable-length long)
     */
    virtual void writeVLong(int64_t l) {
        while ((l & ~0x7FL) != 0) {
            writeByte(static_cast<uint8_t>((l & 0x7FL) | 0x80L));
            l = static_cast<uint64_t>(l) >> 7;
        }
        writeByte(static_cast<uint8_t>(l));
    }

    /**
     * Write string
     */
    virtual void writeString(const std::string& s) {
        writeVInt(s.length());
        writeBytes(reinterpret_cast<const uint8_t*>(s.data()), s.length());
    }

    // ==================== Positioning ====================

    /**
     * Current file pointer position
     */
    virtual int64_t getFilePointer() const = 0;

    /**
     * Get checksum (if checksumming enabled)
     */
    virtual int64_t getChecksum() const {
        throw UnsupportedOperationException("Checksums not supported");
    }

    // ==================== Finalization ====================

    /**
     * Close and finalize file
     */
    virtual void close() = 0;
};
```

## FSDirectory

```cpp
/**
 * FSDirectory stores index on filesystem.
 *
 * Uses standard file I/O.
 *
 * Based on: org.apache.lucene.store.FSDirectory
 */
class FSDirectory : public Directory {
public:
    /**
     * Open directory at path
     */
    static std::unique_ptr<FSDirectory> open(const std::filesystem::path& path) {
        // Create directory if doesn't exist
        std::filesystem::create_directories(path);

        return std::make_unique<FSDirectory>(path);
    }

    explicit FSDirectory(const std::filesystem::path& path)
        : directory_(path) {}

    // ==================== File Listing ====================

    std::vector<std::string> listAll() const override {
        ensureOpen();

        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path().filename().string());
            }
        }
        return files;
    }

    // ==================== File Operations ====================

    void deleteFile(const std::string& name) override {
        ensureOpen();

        auto path = directory_ / name;
        if (!std::filesystem::remove(path)) {
            throw FileNotFoundException("File not found: " + name);
        }
    }

    int64_t fileLength(const std::string& name) const override {
        ensureOpen();

        auto path = directory_ / name;
        return std::filesystem::file_size(path);
    }

    std::unique_ptr<IndexOutput> createOutput(
        const std::string& name,
        const IOContext& context) override {

        ensureOpen();

        auto path = directory_ / name;
        return std::make_unique<FSIndexOutput>(path);
    }

    std::unique_ptr<IndexOutput> createTempOutput(
        const std::string& prefix,
        const std::string& suffix,
        const IOContext& context) override {

        ensureOpen();

        // Generate unique temp name
        std::string tempName = prefix + "_" + generateTempId() + suffix;
        return createOutput(tempName, context);
    }

    std::unique_ptr<IndexInput> openInput(
        const std::string& name,
        const IOContext& context) const override {

        ensureOpen();

        auto path = directory_ / name;
        return std::make_unique<FSIndexInput>(path);
    }

    void rename(const std::string& source,
               const std::string& dest) override {
        ensureOpen();

        auto srcPath = directory_ / source;
        auto destPath = directory_ / dest;

        std::filesystem::rename(srcPath, destPath);
    }

    void sync(const std::vector<std::string>& names) override {
        // fsync files
        for (const auto& name : names) {
            auto path = directory_ / name;
            fsyncFile(path);
        }
    }

    void syncMetaData() override {
        // fsync directory
        fsyncDirectory(directory_);
    }

    std::unique_ptr<Lock> obtainLock(const std::string& name) override {
        auto lockPath = directory_ / name;
        return FSLock::obtain(lockPath);
    }

    void close() override {
        closed_ = true;
    }

    std::optional<std::filesystem::path> getPath() const override {
        return directory_;
    }

private:
    std::filesystem::path directory_;

    std::string generateTempId() const {
        static std::atomic<uint64_t> counter{0};
        return std::to_string(counter.fetch_add(1));
    }

    void fsyncFile(const std::filesystem::path& path) {
#ifdef __linux__
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd != -1) {
            ::fsync(fd);
            ::close(fd);
        }
#endif
    }

    void fsyncDirectory(const std::filesystem::path& path) {
#ifdef __linux__
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd != -1) {
            ::fsync(fd);
            ::close(fd);
        }
#endif
    }
};
```

## MMapDirectory

```cpp
/**
 * MMapDirectory uses memory-mapped files.
 *
 * Efficient for read-heavy workloads.
 * OS manages paging.
 *
 * Based on: org.apache.lucene.store.MMapDirectory
 */
class MMapDirectory : public FSDirectory {
public:
    explicit MMapDirectory(const std::filesystem::path& path)
        : FSDirectory(path) {}

    std::unique_ptr<IndexInput> openInput(
        const std::string& name,
        const IOContext& context) const override {

        ensureOpen();

        auto path = getPath().value() / name;
        return std::make_unique<MMapIndexInput>(path);
    }

private:
    class MMapIndexInput : public IndexInput {
    public:
        explicit MMapIndexInput(const std::filesystem::path& path) {
            // Open file
            fd_ = ::open(path.c_str(), O_RDONLY);
            if (fd_ == -1) {
                throw IOException("Cannot open file: " + path.string());
            }

            // Get file size
            struct stat st;
            if (::fstat(fd_, &st) == -1) {
                ::close(fd_);
                throw IOException("Cannot stat file");
            }
            length_ = st.st_size;

            // Memory map
            if (length_ > 0) {
                data_ = static_cast<uint8_t*>(
                    ::mmap(nullptr, length_, PROT_READ, MAP_SHARED, fd_, 0)
                );

                if (data_ == MAP_FAILED) {
                    ::close(fd_);
                    throw IOException("Cannot mmap file");
                }
            }
        }

        ~MMapIndexInput() {
            if (data_ && data_ != MAP_FAILED) {
                ::munmap(data_, length_);
            }
            if (fd_ != -1) {
                ::close(fd_);
            }
        }

        uint8_t readByte() override {
            if (pos_ >= length_) {
                throw EOFException();
            }
            return data_[pos_++];
        }

        void readBytes(uint8_t* buffer, size_t length) override {
            if (pos_ + length > length_) {
                throw EOFException();
            }
            std::memcpy(buffer, data_ + pos_, length);
            pos_ += length;
        }

        int64_t getFilePointer() const override {
            return pos_;
        }

        void seek(int64_t pos) override {
            pos_ = pos;
        }

        int64_t length() const override {
            return length_;
        }

        std::unique_ptr<IndexInput> clone() const override {
            auto copy = std::make_unique<MMapIndexInput>(*this);
            copy->pos_ = 0;
            return copy;
        }

        std::unique_ptr<IndexInput> slice(
            const std::string& sliceDescription,
            int64_t offset,
            int64_t length) const override {

            auto sliced = clone();
            sliced->seek(offset);
            // TODO: Implement length limiting
            return sliced;
        }

    private:
        int fd_{-1};
        uint8_t* data_{nullptr};
        int64_t length_{0};
        int64_t pos_{0};
    };
};
```

## ByteBuffersDirectory (In-Memory)

```cpp
/**
 * ByteBuffersDirectory stores index in memory.
 *
 * For testing or small indexes.
 *
 * Based on: org.apache.lucene.store.ByteBuffersDirectory
 */
class ByteBuffersDirectory : public Directory {
public:
    ByteBuffersDirectory() = default;

    std::vector<std::string> listAll() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const auto& [name, _] : files_) {
            names.push_back(name);
        }
        return names;
    }

    void deleteFile(const std::string& name) override {
        std::lock_guard<std::mutex> lock(mutex_);
        files_.erase(name);
    }

    int64_t fileLength(const std::string& name) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = files_.find(name);
        if (it == files_.end()) {
            throw FileNotFoundException(name);
        }
        return it->second.size();
    }

    std::unique_ptr<IndexOutput> createOutput(
        const std::string& name,
        const IOContext& context) override {

        return std::make_unique<ByteBuffersIndexOutput>(this, name);
    }

    std::unique_ptr<IndexInput> openInput(
        const std::string& name,
        const IOContext& context) const override {

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = files_.find(name);
        if (it == files_.end()) {
            throw FileNotFoundException(name);
        }

        return std::make_unique<ByteBuffersIndexInput>(it->second);
    }

    void sync(const std::vector<std::string>& names) override {
        // No-op for in-memory
    }

    void syncMetaData() override {
        // No-op
    }

    void close() override {
        std::lock_guard<std::mutex> lock(mutex_);
        files_.clear();
        closed_ = true;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<uint8_t>> files_;

    friend class ByteBuffersIndexOutput;
};
```

## Lock

```cpp
/**
 * Lock prevents multiple writers.
 *
 * Based on: org.apache.lucene.store.Lock
 */
class Lock {
public:
    virtual ~Lock() = default;

    /**
     * Release lock
     */
    virtual void close() = 0;

    /**
     * Ensure lock is still valid
     */
    virtual void ensureValid() = 0;
};

/**
 * Filesystem lock using lock file
 */
class FSLock : public Lock {
public:
    static std::unique_ptr<FSLock> obtain(const std::filesystem::path& lockPath) {
        int fd = ::open(lockPath.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd == -1) {
            if (errno == EEXIST) {
                throw LockObtainFailedException("Lock already held: " +
                                               lockPath.string());
            }
            throw IOException("Cannot create lock file");
        }

        return std::make_unique<FSLock>(lockPath, fd);
    }

    FSLock(const std::filesystem::path& path, int fd)
        : path_(path), fd_(fd) {}

    ~FSLock() {
        close();
    }

    void close() override {
        if (fd_ != -1) {
            ::close(fd_);
            std::filesystem::remove(path_);
            fd_ = -1;
        }
    }

    void ensureValid() override {
        if (fd_ == -1) {
            throw AlreadyClosedException("Lock was closed");
        }
    }

private:
    std::filesystem::path path_;
    int fd_;
};
```

## Usage Example

```cpp
// Open FSDirectory
auto dir = FSDirectory::open("/var/lib/lucenepp/index");

// Create output
auto output = dir->createOutput("segment_0.data", IOContext::DEFAULT);
output->writeInt(42);
output->writeLong(123456);
output->writeString("hello");
output->close();

// Open input
auto input = dir->openInput("segment_0.data", IOContext::READONCE);
int value = input->readInt();
int64_t lvalue = input->readLong();
std::string str = input->readString();

// Obtain lock
auto lock = dir->obtainLock("write.lock");
// ... do work ...
lock->close();

// Use MMapDirectory for better read performance
auto mmapDir = std::make_unique<MMapDirectory>("/var/lib/lucenepp/index");

// In-memory for testing
auto memDir = std::make_unique<ByteBuffersDirectory>();
```
