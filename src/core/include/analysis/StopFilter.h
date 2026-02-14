#pragma once

#include "TokenFilter.h"

#include <string>
#include <unordered_set>

namespace diagon {
namespace analysis {

/**
 * StopFilter removes common stop words from the token stream.
 *
 * Stop words are common words that typically don't carry significant meaning
 * for search relevance (e.g., "the", "a", "is", "are").
 *
 * Features:
 * - Pre-loaded English stop word list
 * - Pre-loaded Chinese stop word list
 * - Support for custom stop word lists
 * - Case-insensitive matching for English
 * - Case-sensitive for Chinese and other languages
 *
 * Usage:
 *   // Use English stop words
 *   auto filter = std::make_unique<StopFilter>(StopWordSet::ENGLISH);
 *
 *   // Use Chinese stop words
 *   auto filter = std::make_unique<StopFilter>(StopWordSet::CHINESE);
 *
 *   // Use custom stop words
 *   std::unordered_set<std::string> customStops = {"foo", "bar"};
 *   auto filter = std::make_unique<StopFilter>(customStops);
 *
 * Thread-safe and stateless.
 */
class StopFilter : public TokenFilter {
public:
    /**
     * Predefined stop word sets.
     */
    enum class StopWordSet {
        NONE,     // No stop words
        ENGLISH,  // English stop words (case-insensitive)
        CHINESE,  // Chinese stop words
        CUSTOM    // User-provided stop words
    };

    /**
     * Create a StopFilter with predefined stop word set.
     *
     * @param stopWordSet Predefined stop word set to use
     * @param caseSensitive Whether matching is case-sensitive (default: false for English)
     */
    explicit StopFilter(StopWordSet stopWordSet = StopWordSet::ENGLISH, bool caseSensitive = false);

    /**
     * Create a StopFilter with custom stop words.
     *
     * @param customStopWords Set of stop words
     * @param caseSensitive Whether matching is case-sensitive
     */
    explicit StopFilter(const std::unordered_set<std::string>& customStopWords,
                        bool caseSensitive = false);

    virtual ~StopFilter() = default;

    // TokenFilter interface
    std::vector<Token> filter(const std::vector<Token>& tokens) override;
    std::string name() const override { return "stop"; }
    std::string description() const override { return "Removes stop words from token stream"; }

    /**
     * Add a stop word at runtime.
     */
    void addStopWord(const std::string& word);

    /**
     * Remove a stop word at runtime.
     */
    void removeStopWord(const std::string& word);

    /**
     * Check if a word is a stop word.
     */
    bool isStopWord(const std::string& word) const;

    /**
     * Get current stop word count.
     */
    size_t stopWordCount() const { return stopWords_.size(); }

private:
    std::unordered_set<std::string> stopWords_;
    StopWordSet stopWordSet_;
    bool caseSensitive_;

    /**
     * Load predefined stop word set.
     */
    void loadStopWords(StopWordSet stopWordSet);

    /**
     * Get English stop words.
     */
    static std::unordered_set<std::string> getEnglishStopWords();

    /**
     * Get Chinese stop words.
     */
    static std::unordered_set<std::string> getChineseStopWords();

    /**
     * Normalize word for matching (lowercase if case-insensitive).
     */
    std::string normalizeWord(const std::string& word) const;
};

}  // namespace analysis
}  // namespace diagon
