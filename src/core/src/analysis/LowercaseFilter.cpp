#include "analysis/LowercaseFilter.h"

#include <unicode/unistr.h>
#include <unicode/ustring.h>

#include <algorithm>
#include <cctype>

namespace diagon {
namespace analysis {

std::string LowercaseFilter::toLowercase(const std::string& str) {
    // Use ICU for proper Unicode lowercasing
    icu::UnicodeString ustr = icu::UnicodeString::fromUTF8(str);
    ustr.toLower();

    std::string result;
    ustr.toUTF8String(result);
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

}  // namespace analysis
}  // namespace diagon
