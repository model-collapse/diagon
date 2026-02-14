#pragma once

#include "Tokenizer.h"

namespace diagon {
namespace analysis {

/**
 * KeywordTokenizer treats the entire text as a single token.
 *
 * This tokenizer does not break text into smaller pieces.
 * It returns the full input as one token, which is useful for:
 * - ID fields (SKU, user ID, etc.)
 * - Tags and categories
 * - Exact match fields
 * - Enum values
 *
 * Whitespace and punctuation are preserved.
 *
 * Example:
 *   Input:  "hello world, foo-bar!"
 *   Output: ["hello world, foo-bar!"]
 *
 * Thread-safe and stateless.
 */
class KeywordTokenizer : public Tokenizer {
public:
    KeywordTokenizer() = default;
    virtual ~KeywordTokenizer() = default;

    // Tokenizer interface
    std::vector<Token> tokenize(const std::string& text) override;
    std::string name() const override { return "keyword"; }
    std::string description() const override { return "Treats entire input as a single token"; }
};

}  // namespace analysis
}  // namespace diagon
