#include "analysis/LowercaseFilter.h"
#include <algorithm>
#include <cctype>

namespace diagon {
namespace analysis {

std::string LowercaseFilter::toLowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::vector<Token> LowercaseFilter::filter(const std::vector<Token>& tokens) {
    std::vector<Token> result;
    result.reserve(tokens.size());

    for (const auto& token : tokens) {
        Token lowercased = token;
        lowercased.setText(toLowercase(token.getText()));
        result.push_back(std::move(lowercased));
    }

    return result;
}

} // namespace analysis
} // namespace diagon
