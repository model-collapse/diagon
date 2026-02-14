#include "analysis/Token.h"

namespace diagon {
namespace analysis {

Token::Token(const std::string& text, int position, int startOffset, int endOffset)
    : text_(text)
    , position_(position)
    , startOffset_(startOffset)
    , endOffset_(endOffset)
    , type_("word") {}

Token::Token()
    : text_()
    , position_(0)
    , startOffset_(0)
    , endOffset_(0)
    , type_() {}

bool Token::operator==(const Token& other) const {
    return text_ == other.text_ && position_ == other.position_ &&
           startOffset_ == other.startOffset_ && endOffset_ == other.endOffset_ &&
           type_ == other.type_;
}

}  // namespace analysis
}  // namespace diagon
