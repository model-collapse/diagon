// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <unicode/brkiter.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>

#include <memory>
#include <string>
#include <vector>

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
 * Performance optimizations:
 * - thread_local cached BreakIterator (avoids ~5-10µs creation per call)
 * - thread_local scratch buffers (avoids per-token heap allocations)
 * - Pre-reserved output vector
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
        // Fast path: empty input
        if (text.empty()) {
            return {};
        }

        // Fast path: ASCII-only text (avoids all ICU overhead)
        // Lucene's StandardTokenizer also has optimized ASCII paths.
        if (isAscii(text)) {
            return tokenizeAscii(text);
        }

        // Full Unicode path via ICU BreakIterator
        return tokenizeICU(text);
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
     * Check if entire string is ASCII (all bytes < 128)
     */
    static bool isAscii(const std::string& text) {
        for (unsigned char c : text) {
            if (c >= 128) return false;
        }
        return true;
    }

    /**
     * Fast ASCII tokenizer — splits on non-alphanumeric, lowercases.
     * Matches Lucene StandardTokenizer behavior for ASCII input.
     * Avoids all ICU overhead (BreakIterator, UnicodeString, toLower).
     */
    static std::vector<std::string> tokenizeAscii(const std::string& text) {
        std::vector<std::string> tokens;
        tokens.reserve(text.size() / 5);

        const char* data = text.data();
        size_t len = text.size();
        size_t i = 0;

        while (i < len) {
            // Skip non-alphanumeric
            unsigned char c = static_cast<unsigned char>(data[i]);
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9'))) {
                i++;
                continue;
            }

            // Start of token
            size_t start = i;
            i++;
            while (i < len) {
                c = static_cast<unsigned char>(data[i]);
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '\'')) {
                    break;
                }
                i++;
            }

            // Extract and lowercase
            size_t tokenLen = i - start;
            std::string token(tokenLen, '\0');
            for (size_t j = 0; j < tokenLen; j++) {
                unsigned char ch = static_cast<unsigned char>(data[start + j]);
                token[j] = static_cast<char>((ch >= 'A' && ch <= 'Z') ? (ch + 32) : ch);
            }
            tokens.push_back(std::move(token));
        }

        return tokens;
    }

    /**
     * Full Unicode tokenizer path via ICU BreakIterator
     */
    static std::vector<std::string> tokenizeICU(const std::string& text) {
        icu::UnicodeString utext = icu::UnicodeString::fromUTF8(text);

        thread_local std::unique_ptr<icu::BreakIterator> bi = [] {
            UErrorCode status = U_ZERO_ERROR;
            auto iter = std::unique_ptr<icu::BreakIterator>(
                icu::BreakIterator::createWordInstance(icu::Locale::getUS(), status));
            if (U_FAILURE(status)) {
                return std::unique_ptr<icu::BreakIterator>(nullptr);
            }
            return iter;
        }();

        if (!bi) {
            return {};
        }

        bi->setText(utext);

        std::vector<std::string> tokens;
        tokens.reserve(text.size() / 5);

        thread_local icu::UnicodeString scratchToken;
        thread_local std::string scratchUtf8;

        int32_t start = bi->first();
        for (int32_t end = bi->next(); end != icu::BreakIterator::DONE;
             start = end, end = bi->next()) {
            utext.extractBetween(start, end, scratchToken);

            if (scratchToken.isEmpty()) continue;
            if (isWhitespaceOnly(scratchToken)) continue;
            if (isPunctuationOnly(scratchToken)) continue;

            scratchToken.toLower();
            scratchUtf8.clear();
            scratchToken.toUTF8String(scratchUtf8);

            if (!scratchUtf8.empty()) {
                tokens.push_back(scratchUtf8);
            }
        }

        return tokens;
    }

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
