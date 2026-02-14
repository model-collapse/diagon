#pragma once

#include "TokenFilter.h"

namespace diagon {
namespace analysis {

/**
 * ASCIIFoldingFilter converts non-ASCII characters to their ASCII equivalents.
 *
 * This filter removes diacritics and accents, making text easier to search
 * across different character encodings and languages.
 *
 * Examples:
 * - "café" → "cafe"
 * - "naïve" → "naive"
 * - "résumé" → "resume"
 * - "Zürich" → "Zurich"
 * - "Москва" → "Moskva" (partial transliteration)
 *
 * Uses ICU library for comprehensive Unicode folding, falling back to
 * simple table-based conversion if ICU is not available.
 *
 * Thread-safe and stateless.
 */
class ASCIIFoldingFilter : public TokenFilter {
public:
    /**
     * Create an ASCIIFoldingFilter.
     *
     * @param preserveOriginal If true, keep both original and folded tokens
     */
    explicit ASCIIFoldingFilter(bool preserveOriginal = false);

    virtual ~ASCIIFoldingFilter() = default;

    // TokenFilter interface
    std::vector<Token> filter(const std::vector<Token>& tokens) override;
    std::string name() const override { return "asciifolding"; }
    std::string description() const override {
        return "Converts non-ASCII characters to ASCII equivalents";
    }

private:
    bool preserveOriginal_;

    /**
     * Fold a single character to ASCII.
     * Returns the ASCII equivalent or the original character if no mapping exists.
     */
    static std::string foldToASCII(const std::string& text);

    /**
     * Simple table-based folding for common accented characters.
     * Used as fallback if ICU is not available.
     */
    static char foldCharSimple(unsigned char c);
};

}  // namespace analysis
}  // namespace diagon
