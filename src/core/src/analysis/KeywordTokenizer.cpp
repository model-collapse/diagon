#include "analysis/KeywordTokenizer.h"

namespace diagon {
namespace analysis {

std::vector<Token> KeywordTokenizer::tokenize(const std::string& text) {
    std::vector<Token> tokens;

    // Return empty if input is empty
    if (text.empty()) {
        return tokens;
    }

    // Create a single token with the entire text
    tokens.emplace_back(text, 0, 0, text.length());

    // Mark as keyword type
    tokens[0].setType("keyword");

    return tokens;
}

}  // namespace analysis
}  // namespace diagon
