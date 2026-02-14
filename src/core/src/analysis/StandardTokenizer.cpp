#include "analysis/StandardTokenizer.h"

#include <unicode/brkiter.h>
#include <unicode/locid.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>

namespace diagon {
namespace analysis {

StandardTokenizer::StandardTokenizer(const std::string& locale)
    : locale_(locale) {
    UErrorCode status = U_ZERO_ERROR;

    // Create ICU Locale
    icu::Locale icuLocale;
    if (!locale_.empty()) {
        icuLocale = icu::Locale(locale_.c_str());
    } else {
        icuLocale = icu::Locale::getDefault();
    }

    // Create word break iterator
    breakIterator_.reset(icu::BreakIterator::createWordInstance(icuLocale, status));

    if (U_FAILURE(status)) {
        // Fallback to default locale if specified locale fails
        status = U_ZERO_ERROR;
        breakIterator_.reset(
            icu::BreakIterator::createWordInstance(icu::Locale::getDefault(), status));
    }
}

StandardTokenizer::~StandardTokenizer() = default;

void StandardTokenizer::reset() {
    // Nothing to reset - BreakIterator is reset on each tokenize() call
}

bool StandardTokenizer::shouldKeepToken(const icu::UnicodeString& token) const {
    if (token.length() == 0) {
        return false;
    }

    // Check if token is only whitespace
    bool hasNonWhitespace = false;
    for (int32_t i = 0; i < token.length(); i++) {
        UChar32 c = token.char32At(i);
        if (!u_isspace(c)) {
            hasNonWhitespace = true;
            break;
        }
    }

    if (!hasNonWhitespace) {
        return false;
    }

    // Check if token is only punctuation
    bool hasAlphanumeric = false;
    for (int32_t i = 0; i < token.length(); i++) {
        UChar32 c = token.char32At(i);
        if (u_isalnum(c)) {
            hasAlphanumeric = true;
            break;
        }
    }

    // Keep tokens that have at least one alphanumeric character
    // This filters out pure punctuation like ".", ",", "!", etc.
    // but keeps hyphenated words, numbers with decimals, etc.
    return hasAlphanumeric;
}

std::string StandardTokenizer::unicodeToUtf8(const icu::UnicodeString& ustr) const {
    std::string result;
    ustr.toUTF8String(result);
    return result;
}

std::vector<Token> StandardTokenizer::tokenize(const std::string& text) {
    std::vector<Token> tokens;

    if (text.empty() || !breakIterator_) {
        return tokens;
    }

    // Convert UTF-8 input to ICU UnicodeString
    icu::UnicodeString utext = icu::UnicodeString::fromUTF8(text);

    // Set the text for the break iterator
    breakIterator_->setText(utext);

    int32_t start = breakIterator_->first();
    int32_t end = breakIterator_->next();
    int position = 0;

    while (end != icu::BreakIterator::DONE) {
        // Extract token substring
        icu::UnicodeString tokenStr;
        utext.extractBetween(start, end, tokenStr);

        // Check if this is a word token (not punctuation or whitespace only)
        if (shouldKeepToken(tokenStr)) {
            // Convert UnicodeString to UTF-8
            std::string tokenText = unicodeToUtf8(tokenStr);

            // Calculate byte offsets in original UTF-8 string
            // We need to count UTF-8 bytes from start of string
            std::string prefix;
            utext.tempSubString(0, start).toUTF8String(prefix);
            int startOffset = static_cast<int>(prefix.length());

            std::string substring;
            utext.tempSubString(0, end).toUTF8String(substring);
            int endOffset = static_cast<int>(substring.length());

            // Create token
            tokens.emplace_back(tokenText, position, startOffset, endOffset);
            position++;
        }

        start = end;
        end = breakIterator_->next();
    }

    return tokens;
}

}  // namespace analysis
}  // namespace diagon
