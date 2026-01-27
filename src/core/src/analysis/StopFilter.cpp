#include "analysis/StopFilter.h"
#include <algorithm>
#include <cctype>

namespace diagon {
namespace analysis {

StopFilter::StopFilter(StopWordSet stopWordSet, bool caseSensitive)
    : stopWordSet_(stopWordSet)
    , caseSensitive_(caseSensitive) {
    loadStopWords(stopWordSet);
}

StopFilter::StopFilter(
    const std::unordered_set<std::string>& customStopWords,
    bool caseSensitive)
    : stopWords_(customStopWords)
    , stopWordSet_(StopWordSet::CUSTOM)
    , caseSensitive_(caseSensitive) {
}

void StopFilter::loadStopWords(StopWordSet stopWordSet) {
    switch (stopWordSet) {
        case StopWordSet::ENGLISH:
            stopWords_ = getEnglishStopWords();
            break;
        case StopWordSet::CHINESE:
            stopWords_ = getChineseStopWords();
            break;
        case StopWordSet::CUSTOM:
        case StopWordSet::NONE:
        default:
            // No stop words loaded
            break;
    }
}

std::unordered_set<std::string> StopFilter::getEnglishStopWords() {
    // Common English stop words (Lucene standard set)
    return {
        "a", "an", "and", "are", "as", "at", "be", "been", "but", "by",
        "for", "had", "has", "have", "if", "in", "into", "is", "it",
        "no", "not", "of", "on", "or", "such",
        "that", "the", "their", "then", "there", "these",
        "they", "this", "to", "was", "were", "will", "with"
    };
}

std::unordered_set<std::string> StopFilter::getChineseStopWords() {
    // Common Chinese stop words
    return {
        // Articles and particles
        "的", "了", "在", "是", "我", "有", "和", "就",
        "不", "人", "都", "一", "一个", "上", "也", "很",
        "到", "说", "要", "去", "你", "会", "着", "没有",
        "看", "好", "自己", "这"

, // Conjunctions
        "或", "而", "但", "因", "为", "与", "及", "等",
        "之", "于", "以", "由", "从", "向", "对", "把",

        // Common verbs/auxilliaries
        "是", "在", "有", "和", "为", "对", "与", "到",

        // Pronouns
        "我", "你", "他", "她", "它", "我们", "你们", "他们",
        "这", "那", "哪", "谁", "什么", "怎么", "怎样",

        // Time/place
        "时", "年", "月", "日", "时候", "这里", "那里",
        "里", "中", "下", "上", "前", "后", "间",

        // Quantifiers
        "个", "些", "每", "各", "某", "任",

        // Others
        "就是", "只是", "所以", "因为", "虽然", "但是",
        "如果", "那么", "可以", "能够", "应该"
    };
}

std::string StopFilter::normalizeWord(const std::string& word) const {
    if (caseSensitive_) {
        return word;
    }

    // Convert to lowercase for case-insensitive matching
    std::string normalized = word;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return normalized;
}

bool StopFilter::isStopWord(const std::string& word) const {
    std::string normalized = normalizeWord(word);
    return stopWords_.find(normalized) != stopWords_.end();
}

std::vector<Token> StopFilter::filter(const std::vector<Token>& tokens) {
    std::vector<Token> result;
    result.reserve(tokens.size()); // Reserve to avoid reallocations

    for (const auto& token : tokens) {
        // Skip stop words
        if (!isStopWord(token.getText())) {
            result.push_back(token);
        }
    }

    return result;
}

void StopFilter::addStopWord(const std::string& word) {
    std::string normalized = normalizeWord(word);
    stopWords_.insert(normalized);
}

void StopFilter::removeStopWord(const std::string& word) {
    std::string normalized = normalizeWord(word);
    stopWords_.erase(normalized);
}

} // namespace analysis
} // namespace diagon
