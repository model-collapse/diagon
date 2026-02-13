// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <unicode/brkiter.h>
#include <unicode/unistr.h>
#include <unicode/uchar.h>

#include <string>
#include <vector>
#include <memory>

namespace diagon {
namespace util {

/**
 * StandardTokenizer - Lucene-compatible tokenizer using ICU
 *
 * Matches behavior of org.apache.lucene.analysis.standard.StandardAnalyzer:
 * - Uses Unicode word boundaries (UAX#29)
 * - Lowercases all tokens
 * - Splits on hyphens and punctuation
 * - Preserves numbers with decimals
 * - Filters out punctuation-only tokens
 *
 * Performance: ~500-800 MB/s (vs FastTokenizer ~2-3 GB/s)
 * Trade-off: Correctness and Lucene compatibility over raw speed
 *
 * Usage:
 * ```cpp
 * std::string text = "The company's stock-market performance";
 * std::vector<std::string> tokens = StandardTokenizer::tokenize(text);
 * // tokens = ["the", "company's", "stock", "market", "performance"]
 * ```
 */
class StandardTokenizer {
public:
    /**
     * Tokenize text using Unicode word boundaries and lowercase
     *
     * Algorithm:
     * 1. Convert UTF-8 to ICU UnicodeString
     * 2. Use BreakIterator to find word boundaries (UAX#29)
     * 3. Extract each word token
     * 4. Filter whitespace-only and punctuation-only tokens
     * 5. Lowercase all tokens
     * 6. Convert back to UTF-8
     *
     * @param text Text to tokenize (UTF-8 encoded)
     * @return Vector of lowercase tokens
     */
    static std::vector<std::string> tokenize(const std::string& text) {
        std::vector<std::string> tokens;

        // Fast path: empty input
        if (text.empty()) {
            return tokens;
        }

        // Convert UTF-8 to ICU UnicodeString
        icu::UnicodeString utext = icu::UnicodeString::fromUTF8(text);

        UErrorCode status = U_ZERO_ERROR;

        // Create word boundary iterator
        std::unique_ptr<icu::BreakIterator> bi(
            icu::BreakIterator::createWordInstance(icu::Locale::getUS(), status));

        if (U_FAILURE(status)) {
            // Fallback: return empty (error handling)
            return tokens;
        }

        bi->setText(utext);

        // Iterate over word boundaries
        int32_t start = bi->first();
        for (int32_t end = bi->next(); end != icu::BreakIterator::DONE;
             start = end, end = bi->next()) {

            // Extract token between boundaries
            icu::UnicodeString token;
            utext.extractBetween(start, end, token);

            // Skip if empty
            if (token.isEmpty()) {
                continue;
            }

            // Filter whitespace-only tokens
            if (isWhitespaceOnly(token)) {
                continue;
            }

            // Filter punctuation-only tokens (except numbers with punctuation like "2.5")
            if (isPunctuationOnly(token)) {
                continue;
            }

            // Lowercase token
            token.toLower();

            // Convert back to UTF-8
            std::string utf8Token;
            token.toUTF8String(utf8Token);

            // Skip if conversion resulted in empty string
            if (!utf8Token.empty()) {
                tokens.push_back(utf8Token);
            }
        }

        return tokens;
    }

    /**
     * Check if token matches specific patterns for filtering
     *
     * This is used to implement Lucene-compatible filtering rules.
     */
    static bool shouldKeepToken(const icu::UnicodeString& token) {
        // Already checked: not empty, not whitespace-only

        // Keep numbers (including decimals)
        if (isNumeric(token)) {
            return true;
        }

        // Keep tokens with at least one letter
        for (int32_t i = 0; i < token.length(); i++) {
            UChar32 c = token.char32At(i);
            if (u_isalpha(c)) {
                return true;
            }
        }

        // Filter out pure punctuation/symbols
        return false;
    }

private:
    /**
     * Check if token contains only whitespace
     */
    static bool isWhitespaceOnly(const icu::UnicodeString& token) {
        for (int32_t i = 0; i < token.length(); i++) {
            UChar32 c = token.char32At(i);
            if (!u_isWhitespace(c)) {
                return false;
            }
        }
        return true;
    }

    /**
     * Check if token contains only punctuation/symbols (no letters or digits)
     *
     * Exceptions: Keep if it looks like a number with punctuation (e.g., "2.5", "3,000")
     */
    static bool isPunctuationOnly(const icu::UnicodeString& token) {
        bool hasDigit = false;
        bool hasLetter = false;

        for (int32_t i = 0; i < token.length(); i++) {
            UChar32 c = token.char32At(i);

            if (u_isalpha(c)) {
                hasLetter = true;
            } else if (u_isdigit(c)) {
                hasDigit = true;
            }
        }

        // Keep if has letters or digits
        if (hasLetter || hasDigit) {
            return false;
        }

        // Pure punctuation/symbols - filter out
        return true;
    }

    /**
     * Check if token is numeric (digits with optional decimal point/comma)
     */
    static bool isNumeric(const icu::UnicodeString& token) {
        bool hasDigit = false;

        for (int32_t i = 0; i < token.length(); i++) {
            UChar32 c = token.char32At(i);

            if (u_isdigit(c)) {
                hasDigit = true;
            } else if (c != '.' && c != ',' && c != '-') {
                // Allow decimal point, comma (thousands separator), and minus sign
                return false;
            }
        }

        return hasDigit;
    }
};

}  // namespace util
}  // namespace diagon
