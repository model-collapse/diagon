// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace diagon {
namespace util {

/**
 * FastTokenizer - Zero-copy whitespace tokenization using string_view
 *
 * Performance optimization over std::istringstream:
 * - No string copying (uses string_view)
 * - No intermediate allocations during parsing
 * - Pre-allocates result vector based on estimated token count
 * - Single-pass algorithm with minimal branching
 *
 * Expected speedup: 3-4x faster than std::istringstream
 * Target: Reduce string/IO from 24.65% to <10% CPU
 *
 * Usage:
 * ```cpp
 * std::string text = "hello world foo bar";
 * std::vector<std::string> tokens = FastTokenizer::tokenize(text);
 * // tokens = ["hello", "world", "foo", "bar"]
 * ```
 */
class FastTokenizer {
public:
    /**
     * Tokenize text by whitespace (space, tab, newline, carriage return)
     *
     * Algorithm:
     * 1. Estimate token count by counting spaces
     * 2. Pre-allocate result vector
     * 3. Single pass: skip whitespace, find token boundaries
     * 4. Create tokens with std::string(string_view) - single allocation per token
     *
     * @param text Text to tokenize
     * @return Vector of tokens (copies made only once per token)
     */
    static std::vector<std::string> tokenize(std::string_view text) {
        std::vector<std::string> tokens;

        // Fast path: empty input
        if (text.empty()) {
            return tokens;
        }

        // Estimate token count (spaces + 1, capped at text length)
        // This reduces vector reallocations during push_back
        size_t estimatedTokens = estimateTokenCount(text);
        tokens.reserve(estimatedTokens);

        size_t pos = 0;
        const size_t length = text.size();

        // Single-pass tokenization
        while (pos < length) {
            // Skip leading whitespace
            while (pos < length && isWhitespace(text[pos])) {
                pos++;
            }

            // Find end of token
            if (pos < length) {
                size_t start = pos;
                while (pos < length && !isWhitespace(text[pos])) {
                    pos++;
                }

                // Extract token (single allocation)
                // string_view -> string conversion creates one allocation
                tokens.emplace_back(text.substr(start, pos - start));
            }
        }

        return tokens;
    }

    /**
     * Tokenize text into string_view references (zero-copy)
     *
     * WARNING: The returned string_views reference the original text.
     * The original string must remain valid while using the views.
     *
     * Use this when you can process tokens immediately and don't need to store them.
     *
     * @param text Text to tokenize (must remain valid)
     * @return Vector of string_view tokens (no allocations for token content)
     */
    static std::vector<std::string_view> tokenizeViews(std::string_view text) {
        std::vector<std::string_view> tokens;

        if (text.empty()) {
            return tokens;
        }

        size_t estimatedTokens = estimateTokenCount(text);
        tokens.reserve(estimatedTokens);

        size_t pos = 0;
        const size_t length = text.size();

        while (pos < length) {
            // Skip leading whitespace
            while (pos < length && isWhitespace(text[pos])) {
                pos++;
            }

            // Find end of token
            if (pos < length) {
                size_t start = pos;
                while (pos < length && !isWhitespace(text[pos])) {
                    pos++;
                }

                // Add view (no allocation)
                tokens.emplace_back(text.substr(start, pos - start));
            }
        }

        return tokens;
    }

private:
    /**
     * Check if character is whitespace
     *
     * Matches: space (0x20), tab (0x09), newline (0x0A), carriage return (0x0D)
     *
     * Optimized: Uses explicit checks instead of std::isspace()
     * for better performance and consistent behavior across locales.
     */
    static inline bool isWhitespace(char c) {
        // Explicit checks are faster than std::isspace() and locale-independent
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }

    /**
     * Estimate token count for pre-allocation
     *
     * Heuristic: Count spaces and add 1
     * - Underestimate is fine (vector will grow)
     * - Overestimate wastes a bit of memory but avoids reallocations
     *
     * @param text Text to analyze
     * @return Estimated number of tokens
     */
    static size_t estimateTokenCount(std::string_view text) {
        if (text.empty()) {
            return 0;
        }

        // Count whitespace characters
        size_t whitespaceCount = 0;
        for (char c : text) {
            if (isWhitespace(c)) {
                whitespaceCount++;
            }
        }

        // Estimate: whitespace count + 1
        // Cap at reasonable maximum to avoid over-allocation for text with many spaces
        size_t estimate = whitespaceCount + 1;

        // If text is all whitespace, estimate would be wrong, so clamp to text length
        return std::min(estimate, text.size());
    }
};

}  // namespace util
}  // namespace diagon
