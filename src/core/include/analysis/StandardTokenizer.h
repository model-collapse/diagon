#pragma once

#include "Tokenizer.h"
#include <unicode/brkiter.h>
#include <unicode/unistr.h>
#include <memory>

namespace diagon {
namespace analysis {

/**
 * StandardTokenizer uses ICU BreakIterator for Unicode-aware text segmentation.
 *
 * This tokenizer properly handles:
 * - Word boundaries across multiple languages (English, Chinese, Japanese, etc.)
 * - Punctuation and special characters
 * - Numbers and mixed alphanumeric tokens
 * - Contractions (don't → don't as single token)
 * - Hyphenated words (e-mail → e-mail as single token)
 *
 * Uses ICU's word break iterator which follows Unicode Standard Annex #29
 * for word boundary detection.
 *
 * Thread-safe when each thread uses its own instance.
 *
 * Example:
 *   Input:  "Hello, world! This costs $5.99."
 *   Output: ["Hello", "world", "This", "costs", "5.99"]
 *
 *   Input:  "你好世界！Hello world."
 *   Output: ["你好", "世界", "Hello", "world"]
 */
class StandardTokenizer : public Tokenizer {
public:
    /**
     * Create a StandardTokenizer with specified locale.
     *
     * @param locale ICU locale string (e.g., "en_US", "zh_CN", "ja_JP")
     *               Empty string uses system default locale.
     */
    explicit StandardTokenizer(const std::string& locale = "");

    virtual ~StandardTokenizer();

    // Tokenizer interface
    std::vector<Token> tokenize(const std::string& text) override;
    void reset() override;
    std::string name() const override { return "standard"; }
    std::string description() const override {
        return "Unicode-aware standard tokenizer using ICU";
    }

private:
    std::string locale_;
    std::unique_ptr<icu::BreakIterator> breakIterator_;

    /**
     * Check if a token should be kept (not punctuation-only or whitespace-only).
     */
    bool shouldKeepToken(const icu::UnicodeString& token) const;

    /**
     * Convert ICU UnicodeString to UTF-8 std::string.
     */
    std::string unicodeToUtf8(const icu::UnicodeString& ustr) const;
};

} // namespace analysis
} // namespace diagon
