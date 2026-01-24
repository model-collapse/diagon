# Term Dictionary - Low-Level Design

## Overview

The term dictionary provides fast term lookup in the inverted index. It supports two implementations: **Trie (FST)** for prefix queries and sorted iteration, and **Hash Map** for exact lookups. The choice is configurable per-field.

## Interface

```cpp
class TermDictionary {
public:
    virtual ~TermDictionary() = default;

    // Lookup operations
    virtual bool contains(std::string_view term) const = 0;
    virtual std::optional<TermInfo> get_term_info(std::string_view term) const = 0;

    // Iterator operations
    virtual std::unique_ptr<TermIterator> iterator() const = 0;
    virtual std::unique_ptr<TermIterator> iterator_from(std::string_view prefix) const = 0;

    // Statistics
    virtual size_t num_terms() const = 0;
    virtual size_t memory_bytes() const = 0;

    // Serialization
    virtual void write_to_file(FileWriter& writer) const = 0;
    static std::unique_ptr<TermDictionary> read_from_file(FileReader& reader);
};

struct TermInfo {
    uint64_t posting_list_offset;  // Offset in postings file
    uint32_t doc_freq;              // Number of docs containing this term
    uint64_t total_term_freq;       // Total occurrences across all docs
    uint32_t posting_list_bytes;    // Compressed size
};
```

## Implementation 1: Trie (FST - Finite State Transducer)

### Structure

```cpp
class TrieTermDictionary : public TermDictionary {
public:
    TrieTermDictionary();
    explicit TrieTermDictionary(const std::vector<std::pair<std::string, TermInfo>>& sorted_terms);

    bool contains(std::string_view term) const override;
    std::optional<TermInfo> get_term_info(std::string_view term) const override;

    std::unique_ptr<TermIterator> iterator() const override;
    std::unique_ptr<TermIterator> iterator_from(std::string_view prefix) const override;

private:
    // Compact FST representation
    struct FSTNode {
        uint32_t transitions_offset;  // Offset to transitions array
        uint8_t num_transitions;      // Number of outgoing edges
        bool is_final;                // Is this an accepting state?
        uint64_t output;              // Output value (TermInfo packed)
    };

    // Transitions are stored separately for cache efficiency
    struct FSTTransition {
        uint8_t label;                // Input character
        uint32_t target_node;         // Target node index
        uint64_t output;              // Output accumulated on this edge
    };

    std::vector<FSTNode> nodes_;
    std::vector<FSTTransition> transitions_;
    std::vector<TermInfo> term_infos_;  // Indexed by output value

    // Metadata
    size_t num_terms_{0};
};
```

### FST Construction

```cpp
class FSTBuilder {
public:
    // Build FST from sorted terms
    TrieTermDictionary build(const std::vector<std::pair<std::string, TermInfo>>& sorted_terms);

private:
    struct BuildNode {
        std::unordered_map<char, std::unique_ptr<BuildNode>> children;
        std::optional<TermInfo> term_info;  // If this is a terminal node
    };

    std::unique_ptr<BuildNode> root_;

    // Insert term into trie
    void insert(const std::string& term, const TermInfo& info) {
        BuildNode* node = root_.get();
        for (char c : term) {
            auto& child = node->children[c];
            if (!child) {
                child = std::make_unique<BuildNode>();
            }
            node = child.get();
        }
        node->term_info = info;
    }

    // Compile trie to compact FST
    TrieTermDictionary compile() {
        TrieTermDictionary fst;
        compile_node(root_.get(), fst);
        return fst;
    }

    uint32_t compile_node(BuildNode* node, TrieTermDictionary& fst) {
        uint32_t node_id = fst.nodes_.size();
        FSTNode fst_node;
        fst_node.is_final = node->term_info.has_value();

        if (fst_node.is_final) {
            // Store term info and get index
            fst_node.output = fst.term_infos_.size();
            fst.term_infos_.push_back(node->term_info.value());
        }

        // Add transitions
        fst_node.transitions_offset = fst.transitions_.size();
        fst_node.num_transitions = node->children.size();

        // Sort children for binary search
        std::vector<std::pair<char, BuildNode*>> sorted_children(
            node->children.begin(), node->children.end()
        );
        std::sort(sorted_children.begin(), sorted_children.end());

        for (const auto& [label, child] : sorted_children) {
            FSTTransition trans;
            trans.label = static_cast<uint8_t>(label);
            trans.target_node = compile_node(child, fst);
            trans.output = 0;  // Can pack common prefix here
            fst.transitions_.push_back(trans);
        }

        fst.nodes_.push_back(fst_node);
        return node_id;
    }
};
```

### FST Lookup

```cpp
std::optional<TermInfo> TrieTermDictionary::get_term_info(std::string_view term) const {
    uint32_t node_id = 0;  // Start at root

    for (char c : term) {
        const FSTNode& node = nodes_[node_id];

        // Binary search in transitions
        const FSTTransition* transitions = &transitions_[node.transitions_offset];
        int left = 0, right = node.num_transitions - 1;
        bool found = false;

        while (left <= right) {
            int mid = (left + right) / 2;
            if (transitions[mid].label == c) {
                node_id = transitions[mid].target_node;
                found = true;
                break;
            } else if (transitions[mid].label < c) {
                left = mid + 1;
            } else {
                right = mid - 1;
            }
        }

        if (!found) {
            return std::nullopt;  // Term not found
        }
    }

    // Check if final state
    const FSTNode& final_node = nodes_[node_id];
    if (final_node.is_final) {
        return term_infos_[final_node.output];
    }

    return std::nullopt;
}
```

### Prefix Iterator

```cpp
class TrieIterator : public TermIterator {
public:
    TrieIterator(const TrieTermDictionary* trie, std::string_view prefix)
        : trie_(trie) {
        // Navigate to prefix node
        node_stack_.push({0, 0});  // (node_id, transition_index)
        current_term_ = std::string(prefix);

        if (!advance_to_prefix(prefix)) {
            at_end_ = true;
        }
    }

    bool has_next() const override { return !at_end_; }

    std::pair<std::string, TermInfo> next() override {
        auto result = std::make_pair(current_term_, current_term_info_);
        advance();
        return result;
    }

private:
    const TrieTermDictionary* trie_;
    std::stack<std::pair<uint32_t, uint32_t>> node_stack_;
    std::string current_term_;
    TermInfo current_term_info_;
    bool at_end_{false};

    void advance() {
        // DFS traversal of trie
        while (!node_stack_.empty()) {
            auto& [node_id, trans_idx] = node_stack_.top();
            const FSTNode& node = trie_->nodes_[node_id];

            if (trans_idx < node.num_transitions) {
                // Follow this transition
                const FSTTransition& trans =
                    trie_->transitions_[node.transitions_offset + trans_idx];
                trans_idx++;

                current_term_.push_back(trans.label);
                node_stack_.push({trans.target_node, 0});

                // Check if terminal
                const FSTNode& target = trie_->nodes_[trans.target_node];
                if (target.is_final) {
                    current_term_info_ = trie_->term_infos_[target.output];
                    return;
                }
            } else {
                // Backtrack
                node_stack_.pop();
                if (!current_term_.empty()) {
                    current_term_.pop_back();
                }
            }
        }

        at_end_ = true;
    }
};
```

## Implementation 2: Hash Map

### Structure

```cpp
class HashMapTermDictionary : public TermDictionary {
public:
    HashMapTermDictionary();
    explicit HashMapTermDictionary(
        const std::vector<std::pair<std::string, TermInfo>>& terms);

    bool contains(std::string_view term) const override;
    std::optional<TermInfo> get_term_info(std::string_view term) const override;

    std::unique_ptr<TermIterator> iterator() const override;
    std::unique_ptr<TermIterator> iterator_from(std::string_view prefix) const override;

private:
    // Use robin_hood for better cache performance
    robin_hood::unordered_flat_map<std::string, TermInfo> term_map_;

    // For iteration (sorted keys)
    std::vector<std::string> sorted_terms_;
};
```

### Lookup

```cpp
std::optional<TermInfo> HashMapTermDictionary::get_term_info(
    std::string_view term) const {
    auto it = term_map_.find(std::string(term));
    if (it != term_map_.end()) {
        return it->second;
    }
    return std::nullopt;
}
```

### Prefix Iterator (Less Efficient)

```cpp
class HashMapPrefixIterator : public TermIterator {
public:
    HashMapPrefixIterator(const HashMapTermDictionary* dict,
                          std::string_view prefix)
        : dict_(dict), prefix_(prefix) {
        // Binary search in sorted terms
        current_ = std::lower_bound(
            dict_->sorted_terms_.begin(),
            dict_->sorted_terms_.end(),
            prefix_
        );
        advance_to_next_match();
    }

    bool has_next() const override {
        return current_ != dict_->sorted_terms_.end() &&
               current_->starts_with(prefix_);
    }

    std::pair<std::string, TermInfo> next() override {
        auto term = *current_;
        auto info = dict_->term_map_.at(term);
        ++current_;
        advance_to_next_match();
        return {term, info};
    }

private:
    const HashMapTermDictionary* dict_;
    std::string prefix_;
    std::vector<std::string>::const_iterator current_;

    void advance_to_next_match() {
        while (current_ != dict_->sorted_terms_.end() &&
               !current_->starts_with(prefix_)) {
            ++current_;
        }
    }
};
```

## Serialization Format

### Trie Format

```
[Header]
  - Magic: "TRIE" (4 bytes)
  - Version: uint32_t
  - Num Terms: uint64_t
  - Num Nodes: uint32_t
  - Num Transitions: uint32_t

[Nodes Array]
  For each node:
    - transitions_offset: uint32_t
    - num_transitions: uint8_t
    - is_final: uint8_t (boolean)
    - output: uint64_t (term info index)

[Transitions Array]
  For each transition:
    - label: uint8_t
    - target_node: uint32_t
    - output: uint64_t

[Term Infos Array]
  For each term info:
    - posting_list_offset: uint64_t
    - doc_freq: uint32_t
    - total_term_freq: uint64_t
    - posting_list_bytes: uint32_t
```

### Hash Map Format

```
[Header]
  - Magic: "HASH" (4 bytes)
  - Version: uint32_t
  - Num Terms: uint64_t

[Term Entries]
  For each term (sorted):
    - Term Length: uint32_t
    - Term Bytes: variable
    - posting_list_offset: uint64_t
    - doc_freq: uint32_t
    - total_term_freq: uint64_t
    - posting_list_bytes: uint32_t

[Hash Index] (optional - for faster loading)
  - Hash table structure for O(1) reconstruction
```

## Memory Layout (mmap-friendly)

```cpp
// Memory-mapped layout for zero-copy loading
struct TrieMemoryLayout {
    struct Header {
        char magic[4];
        uint32_t version;
        uint64_t num_terms;
        uint32_t num_nodes;
        uint32_t num_transitions;
        uint64_t nodes_offset;
        uint64_t transitions_offset;
        uint64_t term_infos_offset;
    };

    // Access via pointer arithmetic
    const FSTNode* nodes() const {
        return reinterpret_cast<const FSTNode*>(
            reinterpret_cast<const char*>(this) + header_.nodes_offset
        );
    }

    Header header_;
    // Variable-length data follows
};

// Usage with mmap
class MMapTrieDictionary : public TermDictionary {
public:
    explicit MMapTrieDictionary(const std::string& file_path) {
        // mmap file
        fd_ = open(file_path.c_str(), O_RDONLY);
        size_ = get_file_size(fd_);
        data_ = mmap(nullptr, size_, PROT_READ, MAP_SHARED, fd_, 0);

        // Cast to layout
        layout_ = reinterpret_cast<const TrieMemoryLayout*>(data_);
    }

    ~MMapTrieDictionary() {
        munmap(data_, size_);
        close(fd_);
    }

    std::optional<TermInfo> get_term_info(std::string_view term) const override {
        // Direct access to mmapped structures
        const FSTNode* nodes = layout_->nodes();
        // ... lookup logic using nodes pointer
    }

private:
    int fd_;
    size_t size_;
    void* data_;
    const TrieMemoryLayout* layout_;
};
```

## Performance Comparison

### Lookup Performance

```
Trie (FST):
  - Exact lookup: O(k) where k = term length
  - Memory: ~2-5 bytes per term (highly compressed)
  - Best for: Text fields with many terms, prefix queries

Hash Map:
  - Exact lookup: O(1) average
  - Memory: ~40-60 bytes per term (overhead from hash table)
  - Best for: Keyword fields, exact-match queries
```

### Benchmark Results (Expected)

```
Dataset: 10M unique terms, average length 15 chars

Operation         | Trie (FST) | Hash Map
------------------|------------|----------
Exact Lookup      | 80 ns      | 40 ns
Prefix Query      | 5 µs       | 500 µs
Memory Usage      | 40 MB      | 600 MB
Build Time        | 15 sec     | 8 sec
```

## Configuration

```cpp
enum class TermDictionaryType {
    TRIE,      // FST-based, memory efficient
    HASH_MAP   // Hash-based, faster lookups
};

struct TermDictionaryConfig {
    TermDictionaryType type;

    // Trie-specific settings
    bool use_output_packing = true;     // Pack common suffixes
    bool use_minimal_dfa = true;        // Minimize FST size

    // Hash map settings
    float load_factor = 0.7;            // Hash table load factor
    bool build_sorted_index = true;     // For prefix iteration
};
```

## Field-Specific Configuration

```cpp
// Configure per field type
TermDictionaryType get_default_type(FieldType field_type) {
    switch (field_type) {
        case FieldType::TEXT:
            return TermDictionaryType::TRIE;  // For prefix queries

        case FieldType::KEYWORD:
            return TermDictionaryType::HASH_MAP;  // For exact lookups

        default:
            return TermDictionaryType::TRIE;
    }
}
```

## Testing

```cpp
class TermDictionaryTest {
    void test_exact_lookup() {
        std::vector<std::pair<std::string, TermInfo>> terms = {
            {"apple", {0, 10, 15, 128}},
            {"banana", {128, 5, 8, 64}},
            {"cherry", {192, 3, 3, 32}}
        };

        auto trie = TrieTermDictionary(terms);
        auto hash = HashMapTermDictionary(terms);

        // Both should return same results
        auto info1 = trie.get_term_info("banana");
        auto info2 = hash.get_term_info("banana");

        ASSERT_EQ(info1->doc_freq, 5);
        ASSERT_EQ(info2->doc_freq, 5);
    }

    void test_prefix_iteration() {
        // Trie should be efficient
        auto it = trie.iterator_from("app");
        ASSERT_TRUE(it->has_next());
        auto [term, info] = it->next();
        ASSERT_EQ(term, "apple");
    }
};
```
