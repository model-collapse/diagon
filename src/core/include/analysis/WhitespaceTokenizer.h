#pragma once

#include "Tokenizer.h"

namespace diagon {
namespace analysis {

/**
 * WhitespaceTokenizer splits text on whitespace characters.
 *
 * This is the simplest tokenizer. It breaks text wherever it finds:
 * - Space (0x20)
 * - Tab (0x09)
 * - Newline (0x0A)
 * - Carriage return (0x0D)
 * - And other Unicode whitespace characters
 *
 * Consecutive whitespace is treated as a single separator.
 * Leading and trailing whitespace is ignored.
 *
 * Example:
 *   Input:  "hello  world\t\tfoo"
 *   Output: ["hello", "world", "foo"]
 *
 * Thread-safe and stateless.
 */
class WhitespaceTokenizer : public Tokenizer {
public:
    WhitespaceTokenizer() = default;
    virtual ~WhitespaceTokenizer() = default;

    // Tokenizer interface
    std::vector<Token> tokenize(const std::string& text) override;
    std::string name() const override { return "whitespace"; }
    std::string description() const override {
        return "Splits text on whitespace characters";
    }

private:
    /**
     * Check if a character is whitespace.
     * Uses standard C++ isspace() which handles ASCII whitespace.
     */
    static bool isWhitespace(char c);
};

} // namespace analysis
} // namespace diagon
