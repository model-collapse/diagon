#pragma once

#include "TokenFilter.h"
#include "Tokenizer.h"

#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace analysis {

/**
 * Analyzer coordinates tokenization and filtering.
 *
 * An analyzer:
 * - Uses a tokenizer to break text into tokens
 * - Applies a chain of filters to process tokens
 * - Produces final analyzed tokens
 *
 * Analyzers are the main interface for text analysis in indexing and search.
 */
class Analyzer {
public:
    virtual ~Analyzer() = default;

    /**
     * Analyze text: tokenize and filter.
     *
     * @param text Input text to analyze
     * @return Vector of analyzed tokens
     */
    virtual std::vector<Token> analyze(const std::string& text) = 0;

    /**
     * Get the name of this analyzer.
     *
     * @return Analyzer name (e.g., "standard", "chinese", "custom")
     */
    virtual std::string name() const = 0;

    /**
     * Get the tokenizer name used by this analyzer.
     *
     * @return Tokenizer name
     */
    virtual std::string getTokenizerName() const = 0;

    /**
     * Get the names of filters used by this analyzer.
     *
     * @return Vector of filter names in order
     */
    virtual std::vector<std::string> getFilterNames() const = 0;

    /**
     * Get a description of this analyzer's configuration.
     *
     * @return Human-readable description
     */
    virtual std::string description() const;
};

/**
 * CompositeAnalyzer is a standard implementation that composes
 * a tokenizer with a chain of filters.
 *
 * This is the base class for most analyzers.
 */
class CompositeAnalyzer : public Analyzer {
public:
    /**
     * Construct an analyzer from a tokenizer and filters.
     *
     * @param name Analyzer name
     * @param tokenizer Tokenizer to use
     * @param filters Vector of filters to apply in order
     */
    CompositeAnalyzer(const std::string& name, std::unique_ptr<Tokenizer> tokenizer,
                      std::vector<std::unique_ptr<TokenFilter>> filters);

    virtual ~CompositeAnalyzer() = default;

    // Analyzer interface
    std::vector<Token> analyze(const std::string& text) override;
    std::string name() const override { return name_; }
    std::string getTokenizerName() const override;
    std::vector<std::string> getFilterNames() const override;

protected:
    std::string name_;
    std::unique_ptr<Tokenizer> tokenizer_;
    std::vector<std::unique_ptr<TokenFilter>> filters_;
};

/**
 * Factory function to create built-in analyzers.
 */
class AnalyzerFactory {
public:
    /**
     * Create a standard analyzer (standard tokenizer + lowercase + stop).
     */
    static std::unique_ptr<Analyzer> createStandard();

    /**
     * Create a simple analyzer (letter tokenizer + lowercase).
     */
    static std::unique_ptr<Analyzer> createSimple();

    /**
     * Create a whitespace analyzer (whitespace tokenizer only).
     */
    static std::unique_ptr<Analyzer> createWhitespace();

    /**
     * Create a keyword analyzer (keyword tokenizer, no filtering).
     */
    static std::unique_ptr<Analyzer> createKeyword();

    /**
     * Create a Chinese analyzer (jieba tokenizer + chinese stop).
     *
     * @param dictPath Path to Jieba dictionary files (empty = use default)
     */
    static std::unique_ptr<Analyzer> createChinese(const std::string& dictPath = "");

    /**
     * Create an English analyzer (standard tokenizer + lowercase + english stop + ascii folding).
     */
    static std::unique_ptr<Analyzer> createEnglish();

    /**
     * Create a multilingual analyzer (standard tokenizer + lowercase + ascii folding).
     * Good for mixed-language text.
     */
    static std::unique_ptr<Analyzer> createMultilingual();

    /**
     * Create a search analyzer (standard tokenizer + lowercase + stop + ascii folding).
     * Optimized for search queries.
     */
    static std::unique_ptr<Analyzer> createSearch();
};

}  // namespace analysis
}  // namespace diagon
