#pragma once

#include "Token.h"
#include <vector>
#include <string>
#include <memory>

namespace diagon {
namespace analysis {

/**
 * Tokenizer breaks text into tokens.
 *
 * A tokenizer is responsible for:
 * - Breaking text into discrete tokens
 * - Assigning positions to tokens
 * - Tracking character offsets
 *
 * Tokenizers are stateless and thread-safe (can be reused).
 */
class Tokenizer {
public:
    virtual ~Tokenizer() = default;

    /**
     * Tokenize the input text into a sequence of tokens.
     *
     * @param text The input text to tokenize
     * @return Vector of tokens in order
     */
    virtual std::vector<Token> tokenize(const std::string& text) = 0;

    /**
     * Optional: Reset internal state for reuse.
     * Most tokenizers are stateless and don't need this.
     */
    virtual void reset() {}

    /**
     * Get the name of this tokenizer for debugging and configuration.
     *
     * @return Tokenizer name (e.g., "standard", "whitespace", "jieba")
     */
    virtual std::string name() const = 0;

    /**
     * Get a description of this tokenizer.
     *
     * @return Human-readable description
     */
    virtual std::string description() const {
        return name() + " tokenizer";
    }
};

} // namespace analysis
} // namespace diagon
