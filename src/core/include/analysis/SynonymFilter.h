#pragma once

#include "TokenFilter.h"
#include <unordered_map>
#include <vector>
#include <string>

namespace diagon {
namespace analysis {

/**
 * SynonymFilter expands tokens with their synonyms.
 *
 * This filter can be used to improve recall by matching related terms.
 * For example, searching for "laptop" can also match "notebook".
 *
 * Features:
 * - One-way synonym mappings (word → synonyms)
 * - Multi-word synonyms
 * - Position increment handling for query-time vs index-time use
 * - Case-insensitive matching (configurable)
 *
 * Examples:
 *   Input:  ["laptop", "computer"]
 *   Mapping: laptop → [laptop, notebook, portable computer]
 *   Output: ["laptop", "notebook", "portable", "computer", "computer"]
 *
 * Usage:
 *   SynonymMap synonyms;
 *   synonyms["laptop"] = {"laptop", "notebook"};
 *   synonyms["car"] = {"car", "automobile", "vehicle"};
 *   auto filter = std::make_unique<SynonymFilter>(synonyms);
 *
 * Thread-safe after construction (read-only synonym map).
 */
class SynonymFilter : public TokenFilter {
public:
    /**
     * Synonym map: word → list of synonyms (including original word).
     */
    using SynonymMap = std::unordered_map<std::string, std::vector<std::string>>;

    /**
     * Create a SynonymFilter with synonym mappings.
     *
     * @param synonyms Map of words to their synonyms
     * @param ignoreCase Whether to ignore case when matching (default: true)
     * @param expand If true, add synonyms as separate tokens; if false, replace (default: true)
     */
    explicit SynonymFilter(
        const SynonymMap& synonyms,
        bool ignoreCase = true,
        bool expand = true
    );

    virtual ~SynonymFilter() = default;

    // TokenFilter interface
    std::vector<Token> filter(const std::vector<Token>& tokens) override;
    std::string name() const override { return "synonym"; }
    std::string description() const override {
        return "Expands tokens with synonyms";
    }

    /**
     * Add a synonym mapping at runtime.
     *
     * @param word The original word
     * @param synonyms List of synonyms (should include original word)
     */
    void addSynonym(const std::string& word, const std::vector<std::string>& synonyms);

    /**
     * Remove a synonym mapping.
     */
    void removeSynonym(const std::string& word);

    /**
     * Get synonyms for a word (returns empty vector if no synonyms).
     */
    std::vector<std::string> getSynonyms(const std::string& word) const;

    /**
     * Load synonyms from a file.
     * File format: word => synonym1, synonym2, synonym3
     * or: word, synonym1, synonym2
     *
     * @param filePath Path to synonym file
     * @return Number of synonym mappings loaded
     */
    size_t loadFromFile(const std::string& filePath);

private:
    SynonymMap synonyms_;
    bool ignoreCase_;
    bool expand_;

    /**
     * Normalize word for matching (lowercase if ignoreCase).
     */
    std::string normalizeWord(const std::string& word) const;

    /**
     * Split multi-word synonym into tokens.
     */
    std::vector<std::string> splitMultiWord(const std::string& synonym) const;
};

} // namespace analysis
} // namespace diagon
