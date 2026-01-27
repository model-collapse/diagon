#pragma once

#include "TokenFilter.h"

namespace diagon {
namespace analysis {

/**
 * LowercaseFilter converts token text to lowercase.
 *
 * This filter transforms all alphabetic characters to lowercase
 * using standard ASCII lowercase conversion.
 *
 * For proper Unicode lowercase handling (e.g., Turkish İ → i),
 * a more sophisticated implementation using ICU would be needed.
 * This simple version handles ASCII A-Z → a-z.
 *
 * Example:
 *   Input tokens:  ["Hello", "WORLD", "Test"]
 *   Output tokens: ["hello", "world", "test"]
 *
 * Thread-safe and stateless.
 */
class LowercaseFilter : public TokenFilter {
public:
    LowercaseFilter() = default;
    virtual ~LowercaseFilter() = default;

    // TokenFilter interface
    std::vector<Token> filter(const std::vector<Token>& tokens) override;
    std::string name() const override { return "lowercase"; }
    std::string description() const override {
        return "Converts tokens to lowercase";
    }

private:
    /**
     * Convert a string to lowercase (ASCII only for now).
     */
    static std::string toLowercase(const std::string& str);
};

} // namespace analysis
} // namespace diagon
