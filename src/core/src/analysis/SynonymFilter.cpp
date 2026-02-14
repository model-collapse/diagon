#include "analysis/SynonymFilter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace diagon {
namespace analysis {

SynonymFilter::SynonymFilter(const SynonymMap& synonyms, bool ignoreCase, bool expand)
    : synonyms_(synonyms)
    , ignoreCase_(ignoreCase)
    , expand_(expand) {
    // Normalize all keys in the synonym map if case-insensitive
    if (ignoreCase_) {
        SynonymMap normalized;
        for (const auto& [word, syns] : synonyms_) {
            normalized[normalizeWord(word)] = syns;
        }
        synonyms_ = std::move(normalized);
    }
}

std::string SynonymFilter::normalizeWord(const std::string& word) const {
    if (!ignoreCase_) {
        return word;
    }

    std::string normalized = word;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return normalized;
}

std::vector<std::string> SynonymFilter::splitMultiWord(const std::string& synonym) const {
    std::vector<std::string> words;
    std::istringstream iss(synonym);
    std::string word;

    while (iss >> word) {
        words.push_back(word);
    }

    return words;
}

std::vector<std::string> SynonymFilter::getSynonyms(const std::string& word) const {
    std::string normalized = normalizeWord(word);
    auto it = synonyms_.find(normalized);

    if (it != synonyms_.end()) {
        return it->second;
    }

    return {};
}

std::vector<Token> SynonymFilter::filter(const std::vector<Token>& tokens) {
    std::vector<Token> result;
    result.reserve(tokens.size() * 2);  // Reserve space for potential expansions

    for (const auto& token : tokens) {
        const std::string& text = token.getText();
        std::string normalized = normalizeWord(text);

        // Check if this word has synonyms
        auto it = synonyms_.find(normalized);

        if (it == synonyms_.end()) {
            // No synonyms, keep original token
            result.push_back(token);
            continue;
        }

        const std::vector<std::string>& syns = it->second;

        if (expand_) {
            // Expansion mode: add all synonyms as separate tokens
            // All synonyms share the same position (position increment = 0 for synonyms)
            bool first = true;
            for (const auto& syn : syns) {
                // Handle multi-word synonyms
                std::vector<std::string> words = splitMultiWord(syn);

                if (words.size() == 1) {
                    // Single-word synonym
                    Token synToken = token;
                    synToken.setText(words[0]);
                    result.push_back(std::move(synToken));
                } else {
                    // Multi-word synonym: create multiple tokens
                    // First token keeps original position, rest increment
                    for (size_t i = 0; i < words.size(); i++) {
                        Token synToken = token;
                        synToken.setText(words[i]);

                        // Adjust position for multi-word synonyms
                        if (!first || i > 0) {
                            synToken.setPosition(token.getPosition() + static_cast<int>(i));
                        }

                        result.push_back(std::move(synToken));
                    }
                }

                first = false;
            }
        } else {
            // Replacement mode: use first synonym only
            Token synToken = token;
            if (!syns.empty()) {
                synToken.setText(syns[0]);
            }
            result.push_back(std::move(synToken));
        }
    }

    return result;
}

void SynonymFilter::addSynonym(const std::string& word, const std::vector<std::string>& synonyms) {
    std::string normalized = normalizeWord(word);
    synonyms_[normalized] = synonyms;
}

void SynonymFilter::removeSynonym(const std::string& word) {
    std::string normalized = normalizeWord(word);
    synonyms_.erase(normalized);
}

size_t SynonymFilter::loadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return 0;
    }

    size_t count = 0;
    std::string line;

    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Parse line format: "word => synonym1, synonym2, synonym3"
        // or: "word, synonym1, synonym2, synonym3"
        size_t arrowPos = line.find("=>");
        std::string word;
        std::string synsStr;

        if (arrowPos != std::string::npos) {
            // Format: word => synonyms
            word = line.substr(0, arrowPos);
            synsStr = line.substr(arrowPos + 2);
        } else {
            // Format: word, synonyms
            size_t commaPos = line.find(',');
            if (commaPos == std::string::npos) {
                continue;  // Invalid format
            }
            word = line.substr(0, commaPos);
            synsStr = line.substr(commaPos + 1);
        }

        // Trim word
        word.erase(0, word.find_first_not_of(" \t"));
        word.erase(word.find_last_not_of(" \t") + 1);

        // Parse synonyms (comma-separated)
        std::vector<std::string> syns;
        std::istringstream synStream(synsStr);
        std::string syn;

        while (std::getline(synStream, syn, ',')) {
            // Trim synonym
            syn.erase(0, syn.find_first_not_of(" \t"));
            syn.erase(syn.find_last_not_of(" \t") + 1);

            if (!syn.empty()) {
                syns.push_back(syn);
            }
        }

        if (!word.empty() && !syns.empty()) {
            addSynonym(word, syns);
            count++;
        }
    }

    return count;
}

}  // namespace analysis
}  // namespace diagon
