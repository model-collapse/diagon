// Micro-benchmark for scatter-add components
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <cstdint>

// RDTSC for cycle-accurate timing
static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

// Simulate window group structure
struct QuantizedBlock {
    std::vector<uint32_t> documents;
};

struct WindowGroup {
    std::vector<QuantizedBlock> windows;
};

int main() {
    const double CPU_FREQ_GHZ = 2.6;  // AMD EPYC 9R14
    const size_t WINDOW_SIZE = 500000;
    const size_t GROUP_SIZE = 15;
    const size_t NUM_BLOCKS = 25;  // Average at α=0.3
    const size_t AVG_POSTING_LEN = 5000;  // Average posting list length
    const size_t ITERATIONS = 1000;

    std::cout << "Micro-benchmark for scatter-add components" << std::endl;
    std::cout << "CPU: AMD EPYC 9R14 @ " << CPU_FREQ_GHZ << " GHz" << std::endl;
    std::cout << std::endl;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> doc_dist(0, WINDOW_SIZE - 1);

    // Setup test data
    std::vector<std::vector<std::vector<WindowGroup>>> index;
    index.resize(100);  // 100 terms
    for (auto& term : index) {
        term.resize(256);  // 256 blocks
        for (auto& block : term) {
            block.resize(2);  // 2 groups
            for (auto& group : block) {
                group.windows.resize(GROUP_SIZE);
                for (auto& window : group.windows) {
                    window.documents.resize(AVG_POSTING_LEN);
                    for (auto& doc : window.documents) {
                        doc = doc_dist(gen);
                    }
                }
            }
        }
    }

    std::vector<int32_t> score_buf(WINDOW_SIZE, 0);
    std::vector<uint32_t> touched_docs;
    touched_docs.reserve(NUM_BLOCKS * AVG_POSTING_LEN);

    // Test 1: Group lookup overhead
    {
        uint64_t total_cycles = 0;
        for (size_t iter = 0; iter < ITERATIONS; ++iter) {
            int group_id = 0;
            int sub_win = 5;

            uint64_t start = rdtsc();

            for (size_t b = 0; b < NUM_BLOCKS; ++b) {
                const auto& group = index[b % 100][b % 256][group_id];
                if (sub_win < (int)group.windows.size()) {
                    const auto& window = group.windows[sub_win];
                    const auto* docs = &window.documents;
                    (void)docs;  // Prevent optimization
                }
            }

            uint64_t end = rdtsc();
            total_cycles += (end - start);
        }

        double avg_cycles = (double)total_cycles / ITERATIONS;
        double cycles_per_block = avg_cycles / NUM_BLOCKS;
        double ns_per_block = cycles_per_block / CPU_FREQ_GHZ;

        std::cout << "Test 1: Group Lookup Overhead" << std::endl;
        std::cout << "  Total cycles per iteration: " << avg_cycles << std::endl;
        std::cout << "  Cycles per block lookup: " << cycles_per_block << std::endl;
        std::cout << "  Time per block lookup: " << ns_per_block << " ns" << std::endl;
        std::cout << std::endl;
    }

    // Test 2: Prefetch + accumulation loop (with tracking)
    {
        const auto& docs = index[0][0][0].windows[0].documents;
        const size_t n = docs.size();
        int32_t* __restrict buf = score_buf.data();
        const int32_t gain = 100;

        uint64_t total_cycles = 0;

        for (size_t iter = 0; iter < ITERATIONS; ++iter) {
            touched_docs.clear();

            uint64_t start = rdtsc();

            constexpr size_t kPrefetchDistance = 48;

            // Initial prefetch
            size_t pf_count = std::min(n, kPrefetchDistance);
            for (size_t p = 0; p < pf_count; ++p) {
                __builtin_prefetch(&buf[docs[p]], 1, 0);
            }

            // Main loop
            size_t j = 0;
            for (; j + kPrefetchDistance < n; ++j) {
                __builtin_prefetch(&buf[docs[j + kPrefetchDistance]], 1, 0);
                uint32_t local_doc_id = docs[j];
                buf[local_doc_id] += gain;
                touched_docs.push_back(local_doc_id);
            }

            // Tail
            for (; j < n; ++j) {
                uint32_t local_doc_id = docs[j];
                buf[local_doc_id] += gain;
                touched_docs.push_back(local_doc_id);
            }

            uint64_t end = rdtsc();
            total_cycles += (end - start);

            // Reset buffer
            for (auto doc_id : touched_docs) {
                buf[doc_id] = 0;
            }
        }

        double avg_cycles = (double)total_cycles / ITERATIONS;
        double cycles_per_doc = avg_cycles / n;
        double ns_per_doc = cycles_per_doc / CPU_FREQ_GHZ;

        std::cout << "Test 2: Accumulation Loop (with tracking)" << std::endl;
        std::cout << "  Posting list length: " << n << std::endl;
        std::cout << "  Total cycles: " << avg_cycles << std::endl;
        std::cout << "  Cycles per document: " << cycles_per_doc << std::endl;
        std::cout << "  Time per document: " << ns_per_doc << " ns" << std::endl;
        std::cout << std::endl;
    }

    // Test 3: Accumulation loop WITHOUT tracking (pure accumulation)
    {
        const auto& docs = index[0][0][0].windows[0].documents;
        const size_t n = docs.size();
        int32_t* __restrict buf = score_buf.data();
        const int32_t gain = 100;

        uint64_t total_cycles = 0;

        for (size_t iter = 0; iter < ITERATIONS; ++iter) {
            uint64_t start = rdtsc();

            constexpr size_t kPrefetchDistance = 48;

            // Initial prefetch
            size_t pf_count = std::min(n, kPrefetchDistance);
            for (size_t p = 0; p < pf_count; ++p) {
                __builtin_prefetch(&buf[docs[p]], 1, 0);
            }

            // Main loop (no tracking)
            size_t j = 0;
            for (; j + kPrefetchDistance < n; ++j) {
                __builtin_prefetch(&buf[docs[j + kPrefetchDistance]], 1, 0);
                buf[docs[j]] += gain;
            }

            // Tail
            for (; j < n; ++j) {
                buf[docs[j]] += gain;
            }

            uint64_t end = rdtsc();
            total_cycles += (end - start);
        }

        double avg_cycles = (double)total_cycles / ITERATIONS;
        double cycles_per_doc = avg_cycles / n;
        double ns_per_doc = cycles_per_doc / CPU_FREQ_GHZ;

        std::cout << "Test 3: Accumulation Loop (NO tracking, pure)" << std::endl;
        std::cout << "  Posting list length: " << n << std::endl;
        std::cout << "  Total cycles: " << avg_cycles << std::endl;
        std::cout << "  Cycles per document: " << cycles_per_doc << std::endl;
        std::cout << "  Time per document: " << ns_per_doc << " ns" << std::endl;
        std::cout << std::endl;
    }

    // Test 4: Part 2 deduplication overhead
    {
        // Setup: fill touched_docs with duplicates
        touched_docs.clear();
        const size_t NUM_UNIQUE = 1000;
        const size_t TOTAL_TOUCHES = NUM_BLOCKS * AVG_POSTING_LEN;

        for (size_t i = 0; i < TOTAL_TOUCHES; ++i) {
            touched_docs.push_back(i % NUM_UNIQUE);
        }

        // Fill score buffer
        int32_t* __restrict buf = score_buf.data();
        for (size_t i = 0; i < NUM_UNIQUE; ++i) {
            buf[i] = 100 + i;
        }

        uint64_t total_cycles = 0;

        for (size_t iter = 0; iter < ITERATIONS; ++iter) {
            size_t processed = 0;

            uint64_t start = rdtsc();

            for (uint32_t local_doc_id : touched_docs) {
                int32_t score = buf[local_doc_id];
                if (score > 0) {
                    processed++;
                    buf[local_doc_id] = 0;  // Reset
                }
            }

            uint64_t end = rdtsc();
            total_cycles += (end - start);

            // Restore scores for next iteration
            for (size_t i = 0; i < NUM_UNIQUE; ++i) {
                buf[i] = 100 + i;
            }
        }

        double avg_cycles = (double)total_cycles / ITERATIONS;
        double cycles_per_touch = avg_cycles / TOTAL_TOUCHES;
        double ns_per_touch = cycles_per_touch / CPU_FREQ_GHZ;

        std::cout << "Test 4: Part 2 Deduplication" << std::endl;
        std::cout << "  Total touches: " << TOTAL_TOUCHES << std::endl;
        std::cout << "  Unique docs: " << NUM_UNIQUE << std::endl;
        std::cout << "  Duplication factor: " << (double)TOTAL_TOUCHES / NUM_UNIQUE << "×" << std::endl;
        std::cout << "  Total cycles: " << avg_cycles << std::endl;
        std::cout << "  Cycles per touch: " << cycles_per_touch << std::endl;
        std::cout << "  Time per touch: " << ns_per_touch << " ns" << std::endl;
        std::cout << std::endl;
    }

    // Summary
    std::cout << "=== Summary ===" << std::endl;
    std::cout << "Estimated time for α=0.3 query (138K operations, 25 blocks):" << std::endl;
    std::cout << "  Test 1 (group lookup): Negligible" << std::endl;
    std::cout << "  Test 2 (with tracking): ~X cycles/doc × 138K docs = Y ms" << std::endl;
    std::cout << "  Test 3 (pure accum): ~X cycles/doc × 138K docs = Y ms" << std::endl;
    std::cout << "  Test 4 (dedup): Depends on duplication factor" << std::endl;

    return 0;
}
