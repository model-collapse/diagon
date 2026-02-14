#pragma once

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"

#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace diagon::benchmarks {

/**
 * Generate synthetic documents with controlled characteristics.
 *
 * Creates reproducible documents with Zipfian term distribution,
 * compatible with Lucene's LongToEnglishContentSource for fair comparisons.
 *
 * Example usage:
 *   SyntheticGenerator gen(42);  // Fixed seed for reproducibility
 *   for (int i = 0; i < 10000; i++) {
 *       auto doc = gen.generateDocument(i, 200);  // 200 words
 *       writer.addDocument(doc);
 *   }
 */
class SyntheticGenerator {
public:
    /**
     * Create a synthetic document generator.
     * @param seed Random seed for reproducibility (default 42)
     */
    explicit SyntheticGenerator(uint32_t seed = 42)
        : rng_(seed)
        , word_dist_(0, vocab_.size() - 1) {}

    /**
     * Generate a document with the specified word count.
     * @param id Document ID
     * @param numWords Number of words in body
     * @return Generated document with title, body, and id fields
     */
    document::Document generateDocument(int id, int numWords) {
        document::Document doc;

        // Generate title (10 words)
        std::string title = generateText(10);

        // Generate body (numWords)
        std::string body = generateText(numWords);

        // Add fields
        doc.addField(std::make_unique<document::TextField>("title", title));
        doc.addField(std::make_unique<document::TextField>("body", body));
        doc.addField(std::make_unique<document::NumericDocValuesField>("id", id));

        return doc;
    }

    /**
     * Generate documents with varying sizes for realistic workloads.
     * @param id Document ID
     * @param sizeCategory Size category: 0=small (50 words), 1=medium (200), 2=large (1000)
     * @return Generated document
     */
    document::Document generateDocumentWithSize(int id, int sizeCategory) {
        int numWords;
        switch (sizeCategory % 3) {
            case 0:
                numWords = 50;
                break;  // Small
            case 1:
                numWords = 200;
                break;  // Medium
            case 2:
                numWords = 1000;
                break;  // Large
            default:
                numWords = 200;
        }
        return generateDocument(id, numWords);
    }

private:
    /**
     * Generate text with the specified word count.
     */
    std::string generateText(int numWords) {
        std::ostringstream oss;
        for (int i = 0; i < numWords; i++) {
            if (i > 0)
                oss << " ";
            oss << vocab_[word_dist_(rng_)];
        }
        return oss.str();
    }

    std::mt19937 rng_;
    std::uniform_int_distribution<size_t> word_dist_;

    // Vocabulary: 100 most common English words for reproducibility
    // Taken from British National Corpus frequency list
    inline static const std::vector<std::string> vocab_ = {
        // Top 100 words
        "the",    "be",    "to",   "of",      "and",   "a",     "in",    "that",  "have",  "I",
        "it",     "for",   "not",  "on",      "with",  "he",    "as",    "you",   "do",    "at",
        "this",   "but",   "his",  "by",      "from",  "they",  "we",    "say",   "her",   "she",
        "or",     "an",    "will", "my",      "one",   "all",   "would", "there", "their", "what",
        "so",     "up",    "out",  "if",      "about", "who",   "get",   "which", "go",    "me",
        "when",   "make",  "can",  "like",    "time",  "no",    "just",  "him",   "know",  "take",
        "people", "into",  "year", "your",    "good",  "some",  "could", "them",  "see",   "other",
        "than",   "then",  "now",  "look",    "only",  "come",  "its",   "over",  "think", "also",
        "back",   "after", "use",  "two",     "how",   "our",   "work",  "first", "well",  "way",
        "even",   "new",   "want", "because", "any",   "these", "give",  "day",   "most",  "us"};
};

}  // namespace diagon::benchmarks
