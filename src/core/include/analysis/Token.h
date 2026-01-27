#pragma once

#include <string>

namespace diagon {
namespace analysis {

/**
 * Token represents a single token produced by a tokenizer.
 *
 * A token includes:
 * - text: The actual token text
 * - position: The position in the token stream (0-based)
 * - startOffset: Character offset where the token starts in the original text
 * - endOffset: Character offset where the token ends in the original text
 * - type: Optional type identifier (e.g., "word", "number", "synonym")
 */
class Token {
public:
    /**
     * Construct a token with all attributes.
     */
    Token(const std::string& text, int position, int startOffset, int endOffset);

    /**
     * Default constructor for empty token.
     */
    Token();

    /**
     * Copy constructor.
     */
    Token(const Token& other) = default;

    /**
     * Move constructor.
     */
    Token(Token&& other) noexcept = default;

    /**
     * Copy assignment.
     */
    Token& operator=(const Token& other) = default;

    /**
     * Move assignment.
     */
    Token& operator=(Token&& other) noexcept = default;

    // Getters
    const std::string& getText() const { return text_; }
    int getPosition() const { return position_; }
    int getStartOffset() const { return startOffset_; }
    int getEndOffset() const { return endOffset_; }
    const std::string& getType() const { return type_; }

    // Setters
    void setText(const std::string& text) { text_ = text; }
    void setPosition(int position) { position_ = position; }
    void setStartOffset(int startOffset) { startOffset_ = startOffset; }
    void setEndOffset(int endOffset) { endOffset_ = endOffset; }
    void setType(const std::string& type) { type_ = type; }

    /**
     * Get the length of the token in characters.
     */
    size_t length() const { return text_.length(); }

    /**
     * Check if this token is empty.
     */
    bool empty() const { return text_.empty(); }

    /**
     * Equality comparison.
     */
    bool operator==(const Token& other) const;

    /**
     * Inequality comparison.
     */
    bool operator!=(const Token& other) const {
        return !(*this == other);
    }

private:
    std::string text_;      // The token text
    int position_;          // Position in token stream
    int startOffset_;       // Start offset in original text
    int endOffset_;         // End offset in original text
    std::string type_;      // Token type (optional)
};

} // namespace analysis
} // namespace diagon
