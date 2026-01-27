#include "analysis/WhitespaceTokenizer.h"
#include <cctype>

namespace diagon {
namespace analysis {

bool WhitespaceTokenizer::isWhitespace(char c) {
    // Use standard library isspace which handles:
    // space, tab, newline, carriage return, form feed, vertical tab
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::vector<Token> WhitespaceTokenizer::tokenize(const std::string& text) {
    std::vector<Token> tokens;

    if (text.empty()) {
        return tokens;
    }

    int position = 0;
    size_t start = 0;
    bool inToken = false;

    for (size_t i = 0; i < text.length(); i++) {
        if (isWhitespace(text[i])) {
            if (inToken) {
                // End of token - emit it
                std::string tokenText = text.substr(start, i - start);
                tokens.emplace_back(tokenText, position, start, i);
                position++;
                inToken = false;
            }
            // Skip whitespace
        } else {
            if (!inToken) {
                // Start of new token
                start = i;
                inToken = true;
            }
            // Continue accumulating token
        }
    }

    // Handle last token if we ended in the middle of one
    if (inToken) {
        std::string tokenText = text.substr(start);
        tokens.emplace_back(tokenText, position, start, text.length());
    }

    return tokens;
}

} // namespace analysis
} // namespace diagon
