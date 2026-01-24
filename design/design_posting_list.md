# Posting List - Low-Level Design

## Overview

Posting lists store the document IDs (and optional positions) for each term in the inverted index. Key features:
- **Compression**: Delta encoding, variable-length integers, bit packing
- **Skip lists**: Fast intersection and union operations
- **Lazy loading**: Load only needed portions
- **Impact ordered**: Optional ordering by impact (BM25 score contribution)

## Structure

```cpp
class PostingList {
public:
    virtual ~PostingList() = default;

    // Iterator for traversing postings
    virtual std::unique_ptr<PostingIterator> iterator() const = 0;

    // Statistics
    virtual uint32_t doc_freq() const = 0;          // Number of documents
    virtual uint64_t total_term_freq() const = 0;   // Total occurrences
    virtual size_t bytes() const = 0;               // Compressed size

    // Serialization
    virtual void write_to_file(FileWriter& writer) const = 0;
    static std::unique_ptr<PostingList> read_from_file(FileReader& reader);
};

struct Posting {
    uint32_t doc_id;
    uint32_t term_freq;                  // Number of occurrences in doc
    std::vector<uint32_t> positions;     // Word positions (optional)
    std::vector<uint8_t> payloads;       // Custom data per position (optional)
};
```

## Posting Iterator

```cpp
class PostingIterator {
public:
    virtual ~PostingIterator() = default;

    // Navigation
    virtual bool has_next() const = 0;
    virtual Posting next() = 0;
    virtual uint32_t current_doc_id() const = 0;

    // Skip to doc ID (using skip lists)
    virtual bool skip_to(uint32_t target_doc_id) = 0;

    // Position access (if available)
    virtual bool has_positions() const { return false; }
    virtual const std::vector<uint32_t>& positions() const = 0;

    // Payload access (if available)
    virtual bool has_payloads() const { return false; }
    virtual const std::vector<uint8_t>& payloads() const = 0;
};
```

## Compression

### Doc ID Compression

```cpp
class DocIdCompressor {
public:
    // Compress doc IDs using delta encoding + VByte
    static std::vector<uint8_t> compress(const std::vector<uint32_t>& doc_ids) {
        std::vector<uint8_t> result;
        uint32_t prev = 0;

        for (uint32_t doc_id : doc_ids) {
            uint32_t delta = doc_id - prev;
            encode_vbyte(delta, result);
            prev = doc_id;
        }

        return result;
    }

    // Variable-byte encoding
    static void encode_vbyte(uint32_t value, std::vector<uint8_t>& output) {
        while (value >= 128) {
            output.push_back(static_cast<uint8_t>(value & 0x7F));
            value >>= 7;
        }
        output.push_back(static_cast<uint8_t>(value | 0x80));  // Set high bit
    }

    static uint32_t decode_vbyte(const uint8_t*& ptr) {
        uint32_t result = 0;
        uint32_t shift = 0;

        while (true) {
            uint8_t byte = *ptr++;
            if (byte & 0x80) {
                result |= static_cast<uint32_t>(byte & 0x7F) << shift;
                break;
            }
            result |= static_cast<uint32_t>(byte) << shift;
            shift += 7;
        }

        return result;
    }
};
```

### Block-Based Compression

```cpp
// PForDelta: Patched Frame of Reference
class PForDeltaCompressor {
public:
    static constexpr size_t BLOCK_SIZE = 128;

    static std::vector<uint8_t> compress(const std::vector<uint32_t>& doc_ids) {
        std::vector<uint8_t> result;

        for (size_t i = 0; i < doc_ids.size(); i += BLOCK_SIZE) {
            size_t block_size = std::min(BLOCK_SIZE, doc_ids.size() - i);
            compress_block(&doc_ids[i], block_size, result);
        }

        return result;
    }

private:
    static void compress_block(
        const uint32_t* block, size_t size,
        std::vector<uint8_t>& output) {

        // 1. Compute deltas
        std::vector<uint32_t> deltas(size);
        deltas[0] = block[0];
        for (size_t i = 1; i < size; i++) {
            deltas[i] = block[i] - block[i - 1];
        }

        // 2. Find reference value (e.g., 90th percentile)
        uint32_t reference = find_reference(deltas);

        // 3. Compute bit width for most values
        uint8_t bits = compute_bit_width(deltas, reference);

        // Write block header
        output.push_back(bits);
        write_uint32(reference, output);

        // 4. Bit-pack values <= reference
        bit_pack(deltas, bits, output);

        // 5. Write exceptions (values > reference) separately
        write_exceptions(deltas, reference, output);
    }

    static void bit_pack(
        const std::vector<uint32_t>& values,
        uint8_t bits,
        std::vector<uint8_t>& output) {

        uint64_t buffer = 0;
        int buffer_bits = 0;

        for (uint32_t value : values) {
            buffer |= (static_cast<uint64_t>(value) << buffer_bits);
            buffer_bits += bits;

            while (buffer_bits >= 8) {
                output.push_back(static_cast<uint8_t>(buffer & 0xFF));
                buffer >>= 8;
                buffer_bits -= 8;
            }
        }

        if (buffer_bits > 0) {
            output.push_back(static_cast<uint8_t>(buffer));
        }
    }
};
```

### Term Frequency Compression

```cpp
// Term frequencies are typically small (1-10), use simple VByte
class TermFreqCompressor {
public:
    static std::vector<uint8_t> compress(const std::vector<uint32_t>& freqs) {
        std::vector<uint8_t> result;
        for (uint32_t freq : freqs) {
            DocIdCompressor::encode_vbyte(freq, result);
        }
        return result;
    }
};
```

### Position Compression

```cpp
// Positions within document: delta encoding + VByte
class PositionCompressor {
public:
    static std::vector<uint8_t> compress(
        const std::vector<std::vector<uint32_t>>& all_positions) {

        std::vector<uint8_t> result;

        for (const auto& positions : all_positions) {
            // Write position count
            DocIdCompressor::encode_vbyte(positions.size(), result);

            // Write positions (delta encoded)
            uint32_t prev = 0;
            for (uint32_t pos : positions) {
                uint32_t delta = pos - prev;
                DocIdCompressor::encode_vbyte(delta, result);
                prev = pos;
            }
        }

        return result;
    }
};
```

## Skip Lists

### Skip List Structure

```cpp
class SkipList {
public:
    struct SkipEntry {
        uint32_t doc_id;              // Document ID at this skip point
        uint64_t posting_offset;      // Byte offset in posting list
        uint32_t doc_index;           // Index in uncompressed doc list
    };

    // Build skip list (every N documents)
    static std::vector<SkipEntry> build(
        const std::vector<uint32_t>& doc_ids,
        const std::vector<uint64_t>& offsets,
        uint32_t skip_interval = 128) {

        std::vector<SkipEntry> entries;

        for (size_t i = 0; i < doc_ids.size(); i += skip_interval) {
            SkipEntry entry;
            entry.doc_id = doc_ids[i];
            entry.posting_offset = offsets[i];
            entry.doc_index = i;
            entries.push_back(entry);
        }

        return entries;
    }

    // Find skip entry for target doc ID
    static const SkipEntry* find_skip_entry(
        const std::vector<SkipEntry>& entries,
        uint32_t target_doc_id) {

        // Binary search
        auto it = std::lower_bound(
            entries.begin(), entries.end(), target_doc_id,
            [](const SkipEntry& entry, uint32_t target) {
                return entry.doc_id < target;
            }
        );

        if (it == entries.begin()) {
            return nullptr;  // Target before first skip entry
        }

        return &*(it - 1);  // Return previous entry
    }
};
```

### Skip List Usage

```cpp
bool PostingIterator::skip_to(uint32_t target_doc_id) {
    if (target_doc_id <= current_doc_id_) {
        return true;  // Already at or past target
    }

    // Use skip list to jump closer to target
    const SkipEntry* entry = SkipList::find_skip_entry(
        skip_entries_, target_doc_id
    );

    if (entry) {
        // Seek to skip position
        file_reader_->seek(entry->posting_offset);
        current_doc_id_ = entry->doc_id;
        current_index_ = entry->doc_index;
    }

    // Linear scan to exact target
    while (has_next() && current_doc_id_ < target_doc_id) {
        next();
    }

    return current_doc_id_ == target_doc_id;
}
```

## Posting List Implementations

### Standard Posting List

```cpp
class StandardPostingList : public PostingList {
public:
    StandardPostingList(
        std::vector<uint32_t> doc_ids,
        std::vector<uint32_t> term_freqs,
        std::vector<std::vector<uint32_t>> positions = {});

    std::unique_ptr<PostingIterator> iterator() const override;

    void write_to_file(FileWriter& writer) const override {
        // Write header
        writer.write_uint32(doc_ids_.size());
        writer.write_uint64(total_term_freq_);

        // Write compressed doc IDs
        auto compressed_ids = DocIdCompressor::compress(doc_ids_);
        writer.write_uint32(compressed_ids.size());
        writer.write_bytes(compressed_ids);

        // Write compressed term freqs
        auto compressed_freqs = TermFreqCompressor::compress(term_freqs_);
        writer.write_uint32(compressed_freqs.size());
        writer.write_bytes(compressed_freqs);

        // Write positions if present
        if (!positions_.empty()) {
            auto compressed_pos = PositionCompressor::compress(positions_);
            writer.write_uint32(compressed_pos.size());
            writer.write_bytes(compressed_pos);
        }

        // Write skip list
        skip_list_.write_to_file(writer);
    }

private:
    std::vector<uint32_t> doc_ids_;
    std::vector<uint32_t> term_freqs_;
    std::vector<std::vector<uint32_t>> positions_;
    SkipList skip_list_;
    uint64_t total_term_freq_;
};
```

### Impact-Ordered Posting List

```cpp
// Order postings by BM25 score contribution (for WAND/BMW algorithms)
class ImpactOrderedPostingList : public PostingList {
public:
    struct ImpactPosting {
        uint32_t doc_id;
        uint32_t term_freq;
        float impact;  // Pre-computed BM25 contribution
    };

    ImpactOrderedPostingList(std::vector<ImpactPosting> postings);

    std::unique_ptr<PostingIterator> iterator() const override {
        return std::make_unique<ImpactOrderedIterator>(this);
    }

    // Early termination for top-k queries
    std::unique_ptr<PostingIterator> iterator_with_threshold(float min_score) const;

private:
    std::vector<ImpactPosting> postings_;  // Sorted by impact (descending)
    std::vector<SkipEntry> skip_entries_;
};
```

## Posting List Merging

### Union (OR operation)

```cpp
class PostingListUnion {
public:
    static std::vector<uint32_t> merge_union(
        const std::vector<PostingIterator*>& iterators) {

        std::vector<uint32_t> result;
        std::priority_queue<
            std::pair<uint32_t, PostingIterator*>,
            std::vector<std::pair<uint32_t, PostingIterator*>>,
            std::greater<>
        > pq;

        // Initialize priority queue
        for (auto* it : iterators) {
            if (it->has_next()) {
                pq.push({it->current_doc_id(), it});
            }
        }

        uint32_t last_doc_id = UINT32_MAX;

        while (!pq.empty()) {
            auto [doc_id, it] = pq.top();
            pq.pop();

            // Deduplicate
            if (doc_id != last_doc_id) {
                result.push_back(doc_id);
                last_doc_id = doc_id;
            }

            // Advance iterator
            it->next();
            if (it->has_next()) {
                pq.push({it->current_doc_id(), it});
            }
        }

        return result;
    }
};
```

### Intersection (AND operation)

```cpp
class PostingListIntersection {
public:
    static std::vector<uint32_t> merge_intersection(
        std::vector<PostingIterator*> iterators) {

        if (iterators.empty()) return {};

        // Sort by doc freq (shortest first for efficiency)
        std::sort(iterators.begin(), iterators.end(),
            [](const PostingIterator* a, const PostingIterator* b) {
                return a->doc_freq() < b->doc_freq();
            }
        );

        std::vector<uint32_t> result;
        PostingIterator* shortest = iterators[0];

        while (shortest->has_next()) {
            uint32_t candidate = shortest->current_doc_id();
            bool found_in_all = true;

            // Check if candidate exists in all other lists
            for (size_t i = 1; i < iterators.size(); i++) {
                if (!iterators[i]->skip_to(candidate)) {
                    // No more matches possible
                    return result;
                }

                if (iterators[i]->current_doc_id() != candidate) {
                    found_in_all = false;
                    break;
                }
            }

            if (found_in_all) {
                result.push_back(candidate);
            }

            shortest->next();
        }

        return result;
    }
};
```

## Memory-Mapped Posting Lists

```cpp
class MMapPostingList : public PostingList {
public:
    explicit MMapPostingList(const std::string& file_path, uint64_t offset);

    std::unique_ptr<PostingIterator> iterator() const override {
        return std::make_unique<MMapPostingIterator>(data_, size_);
    }

    ~MMapPostingList() {
        if (data_) {
            munmap(data_, size_);
        }
    }

private:
    void* data_;
    size_t size_;
    int fd_;
};

class MMapPostingIterator : public PostingIterator {
public:
    MMapPostingIterator(const void* data, size_t size)
        : data_(static_cast<const uint8_t*>(data))
        , end_(data_ + size) {
        // Parse header
        doc_freq_ = read_uint32();
        total_term_freq_ = read_uint64();

        // Initialize decompression
        decompress_next_block();
    }

    bool has_next() const override {
        return current_index_ < doc_freq_;
    }

    Posting next() override {
        if (current_block_index_ >= current_block_.size()) {
            decompress_next_block();
        }

        Posting posting;
        posting.doc_id = current_block_[current_block_index_].doc_id;
        posting.term_freq = current_block_[current_block_index_].term_freq;

        current_block_index_++;
        current_index_++;

        return posting;
    }

private:
    const uint8_t* data_;
    const uint8_t* end_;
    uint32_t doc_freq_;
    uint64_t total_term_freq_;

    // Current decompressed block
    std::vector<Posting> current_block_;
    size_t current_block_index_{0};
    uint32_t current_index_{0};

    void decompress_next_block() {
        // Decompress next 128 postings
        // ...
    }
};
```

## File Format

```
[Posting List File Layout]

Header:
  - Magic: "POST" (4 bytes)
  - Version: uint32_t

For each term (ordered by term ID):
  [Posting List Block]
  - doc_freq: uint32_t
  - total_term_freq: uint64_t

  [Compressed Doc IDs]
  - compression_type: uint8_t
  - compressed_size: uint32_t
  - compressed_data: bytes

  [Compressed Term Freqs]
  - compression_type: uint8_t
  - compressed_size: uint32_t
  - compressed_data: bytes

  [Compressed Positions] (optional)
  - has_positions: uint8_t
  - compression_type: uint8_t
  - compressed_size: uint32_t
  - compressed_data: bytes

  [Skip List]
  - skip_interval: uint32_t
  - num_skip_entries: uint32_t
  - skip_entries: [SkipEntry]
```

## Performance Optimizations

### 1. SIMD Decoding

```cpp
// Decode VByte using SIMD
class SIMDVByteDecoder {
public:
    static void decode_block(
        const uint8_t* input,
        uint32_t* output,
        size_t count) {

        // Use SIMD instructions for parallel decoding
        // See: "SIMD Compression and the Intersection of Sorted Integers"
        // (Lemire et al.)
    }
};
```

### 2. Block Caching

```cpp
class CachedPostingList : public PostingList {
private:
    mutable LRUCache<uint32_t, std::vector<Posting>> block_cache_;

    std::vector<Posting> load_block(uint32_t block_id) const {
        return block_cache_.get_or_compute(block_id, [&]() {
            return decompress_block(block_id);
        });
    }
};
```

### 3. Lazy Position Loading

```cpp
class LazyPositionPostingIterator : public PostingIterator {
public:
    const std::vector<uint32_t>& positions() const override {
        if (!positions_loaded_) {
            load_positions();
            positions_loaded_ = true;
        }
        return positions_;
    }

private:
    mutable bool positions_loaded_{false};
    mutable std::vector<uint32_t> positions_;

    void load_positions() const {
        // Load from disk only when needed
    }
};
```

## Testing

```cpp
class PostingListTest {
    void test_compression_ratio() {
        std::vector<uint32_t> doc_ids;
        for (uint32_t i = 0; i < 10000; i++) {
            doc_ids.push_back(i * 10);  // Sparse IDs
        }

        auto compressed = DocIdCompressor::compress(doc_ids);

        size_t original = doc_ids.size() * sizeof(uint32_t);
        size_t compressed_size = compressed.size();

        double ratio = static_cast<double>(original) / compressed_size;

        ASSERT_GT(ratio, 3.0);  // Expect >3x compression
    }

    void test_intersection_correctness() {
        // List 1: [1, 3, 5, 7, 9]
        // List 2: [2, 3, 5, 8]
        // Expected: [3, 5]

        auto result = PostingListIntersection::merge_intersection({it1, it2});
        ASSERT_EQ(result, std::vector<uint32_t>({3, 5}));
    }

    void test_skip_performance() {
        // Skip to doc 1,000,000 in list of 10M docs
        // Should use skip list, not scan all

        auto start = std::chrono::high_resolution_clock::now();
        it->skip_to(1000000);
        auto elapsed = std::chrono::high_resolution_clock::now() - start;

        ASSERT_LT(elapsed, std::chrono::microseconds(100));
    }
};
```
