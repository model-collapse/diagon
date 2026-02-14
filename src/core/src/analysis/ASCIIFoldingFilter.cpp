#include "analysis/ASCIIFoldingFilter.h"

#include <unicode/normalizer2.h>
#include <unicode/translit.h>
#include <unicode/unistr.h>

namespace diagon {
namespace analysis {

ASCIIFoldingFilter::ASCIIFoldingFilter(bool preserveOriginal)
    : preserveOriginal_(preserveOriginal) {}

char ASCIIFoldingFilter::foldCharSimple(unsigned char c) {
    // Simple ASCII folding table for Latin-1 supplement (0x80-0xFF)
    // Maps accented characters to their ASCII equivalents
    static const char foldingTable[128] = {
        0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,  // 0x80-0x8F
        0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,  // 0x90-0x9F
        0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,  // 0xA0-0xAF
        0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,  // 0xB0-0xBF
        'A', 'A', 'A', 'A', 'A', 'A', 'A', 'C',
        'E', 'E', 'E', 'E', 'I', 'I', 'I', 'I',  // 0xC0-0xCF ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏ
        'D', 'N', 'O', 'O', 'O', 'O', 'O', 0,
        'O', 'U', 'U', 'U', 'U', 'Y', 0,   's',  // 0xD0-0xDF ÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞß
        'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c',
        'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',  // 0xE0-0xEF àáâãäåæçèéêëìíîï
        0,   'n', 'o', 'o', 'o', 'o', 'o', 0,
        'o', 'u', 'u', 'u', 'u', 'y', 0,   'y'  // 0xF0-0xFF ðñòóôõö÷øùúûüýþÿ
    };

    if (c < 128) {
        return c;  // Already ASCII
    }

    char folded = foldingTable[c - 128];
    return folded ? folded : c;
}

std::string ASCIIFoldingFilter::foldToASCII(const std::string& text) {
    if (text.empty()) {
        return text;
    }

    // Convert to ICU UnicodeString
    icu::UnicodeString ustr = icu::UnicodeString::fromUTF8(text);

    UErrorCode status = U_ZERO_ERROR;

    // First, decompose the text (NFD normalization)
    // This separates base characters from combining diacritics
    const icu::Normalizer2* nfd = icu::Normalizer2::getNFDInstance(status);
    if (U_SUCCESS(status)) {
        ustr = nfd->normalize(ustr, status);
    }

    // Create a transliterator to remove diacritics and convert to Latin
    // "NFD; [:Nonspacing Mark:] Remove; NFC" removes combining diacritics
    // "Latin-ASCII" converts Latin characters to ASCII
    std::unique_ptr<icu::Transliterator> trans(icu::Transliterator::createInstance(
        "NFD; [:Nonspacing Mark:] Remove; NFC; Latin-ASCII", UTRANS_FORWARD, status));

    if (U_SUCCESS(status) && trans) {
        trans->transliterate(ustr);
    }

    // Convert back to UTF-8
    std::string result;
    ustr.toUTF8String(result);

    return result;
}

std::vector<Token> ASCIIFoldingFilter::filter(const std::vector<Token>& tokens) {
    std::vector<Token> result;
    result.reserve(tokens.size() * (preserveOriginal_ ? 2 : 1));

    for (const auto& token : tokens) {
        const std::string& text = token.getText();

        // Fold to ASCII
        std::string folded = foldToASCII(text);

        // Create token with folded text
        Token foldedToken = token;
        foldedToken.setText(folded);
        result.push_back(std::move(foldedToken));

        // Optionally preserve original
        if (preserveOriginal_ && folded != text) {
            result.push_back(token);
        }
    }

    return result;
}

}  // namespace analysis
}  // namespace diagon
