#include "analysis/JiebaTokenizer.h"

#include "cppjieba/Jieba.hpp"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace analysis {

// Default dictionary paths (set at compile time via CMake)
#ifndef CPPJIEBA_DICT_DIR
#    define CPPJIEBA_DICT_DIR ""
#endif

std::string JiebaTokenizer::getDefaultDictDir() {
    const char* envPath = std::getenv("CPPJIEBA_DICT_DIR");
    if (envPath && *envPath) {
        return std::string(envPath);
    }

    std::string compiledPath = CPPJIEBA_DICT_DIR;
    if (!compiledPath.empty()) {
        return compiledPath;
    }

    // Fallback: try common installation paths
    const char* commonPaths[] = {
        "/usr/local/share/cppjieba/dict",
        "/usr/share/cppjieba/dict",
        "./dict",
        "../dict",
    };

    for (const char* path : commonPaths) {
        std::string dictFile = std::string(path) + "/jieba.dict.utf8";
        std::ifstream f(dictFile);
        if (f.good()) {
            return path;
        }
    }

    throw std::runtime_error(
        "Failed to locate cppjieba dictionaries. "
        "Please set CPPJIEBA_DICT_DIR environment variable or pass dict path explicitly.");
}

JiebaTokenizer::JiebaTokenizer(JiebaMode mode, const std::string& dictPath,
                               const std::string& hmmPath, const std::string& userDictPath,
                               const std::string& stopWordPath)
    : mode_(mode)
    , dictPath_(dictPath)
    , hmmPath_(hmmPath)
    , userDictPath_(userDictPath)
    , stopWordPath_(stopWordPath) {
    initializeJieba(dictPath, hmmPath, userDictPath, stopWordPath);
}

JiebaTokenizer::~JiebaTokenizer() = default;

void JiebaTokenizer::initializeJieba(const std::string& dictPath, const std::string& hmmPath,
                                     const std::string& userDictPath,
                                     const std::string& stopWordPath) {
    // Get dictionary directory
    std::string dictDir;
    if (!dictPath.empty()) {
        dictDir = dictPath.substr(0, dictPath.rfind('/'));
    } else {
        dictDir = getDefaultDictDir();
    }

    // Build full paths to dictionary files
    std::string mainDict = dictPath.empty() ? dictDir + "/jieba.dict.utf8" : dictPath;
    std::string hmmModel = hmmPath.empty() ? dictDir + "/hmm_model.utf8" : hmmPath;
    std::string userDict = userDictPath;  // Optional, can be empty
    std::string idfPath = dictDir + "/idf.utf8";
    std::string stopWordFile = stopWordPath;  // Optional, can be empty

    // Store actual paths used
    dictPath_ = mainDict;
    hmmPath_ = hmmModel;
    userDictPath_ = userDict;
    stopWordPath_ = stopWordFile;

    // Create Jieba instance
    try {
        jieba_ = std::make_unique<cppjieba::Jieba>(mainDict, hmmModel, userDict, idfPath,
                                                   stopWordFile);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to initialize Jieba: ") + e.what() +
                                 "\nMain dict: " + mainDict + "\nHMM model: " + hmmModel);
    }
}

void JiebaTokenizer::reset() {
    // Nothing to reset - Jieba is stateless
}

std::string JiebaTokenizer::getModeString() const {
    switch (mode_) {
        case JiebaMode::MP:
            return "MP";
        case JiebaMode::HMM:
            return "HMM";
        case JiebaMode::MIX:
            return "MIX";
        case JiebaMode::FULL:
            return "FULL";
        case JiebaMode::SEARCH:
            return "SEARCH";
        default:
            return "UNKNOWN";
    }
}

std::vector<Token> JiebaTokenizer::tokenize(const std::string& text) {
    std::vector<Token> result;

    if (text.empty() || !jieba_) {
        return result;
    }

    // Use cppjieba to segment text
    std::vector<std::string> words;

    switch (mode_) {
        case JiebaMode::MP:
            // Maximum Probability mode
            jieba_->Cut(text, words, false);
            break;

        case JiebaMode::HMM:
            // HMM mode (new word recognition)
            jieba_->Cut(text, words, true);
            break;

        case JiebaMode::MIX:
            // Mix mode (MP + HMM, default)
            jieba_->Cut(text, words, true);
            break;

        case JiebaMode::FULL:
            // Full mode (all possible words)
            jieba_->CutAll(text, words);
            break;

        case JiebaMode::SEARCH:
            // Search engine mode
            jieba_->CutForSearch(text, words);
            break;
    }

    // Convert words to tokens with positions and offsets
    int position = 0;
    size_t currentOffset = 0;

    for (const auto& word : words) {
        // Skip empty words
        if (word.empty()) {
            continue;
        }

        // Skip stop words if configured
        if (isStopWord(word)) {
            // Still need to advance offset
            currentOffset += word.length();
            continue;
        }

        // Find actual position in original text
        size_t wordPos = text.find(word, currentOffset);
        if (wordPos == std::string::npos) {
            // Word not found at expected position (shouldn't happen)
            // Use current offset as fallback
            wordPos = currentOffset;
        }

        // Create token
        Token token(word, position, static_cast<int>(wordPos),
                    static_cast<int>(wordPos + word.length()));

        // Set token type (can be used for POS tagging in future)
        token.setType("word");

        result.push_back(std::move(token));

        position++;
        currentOffset = wordPos + word.length();
    }

    return result;
}

bool JiebaTokenizer::isStopWord(const std::string& word) const {
    // TODO: Implement stop word checking
    // For now, return false (no filtering)
    // In future: load stop word list and check membership
    return false;
}

void JiebaTokenizer::addUserWord(const std::string& word, int weight) {
    if (jieba_) {
        // cppjieba doesn't have a direct API to add words at runtime
        // Would need to reload with updated user dict
        // For now, this is a no-op
        // TODO: Implement by maintaining in-memory user dict and reloading
    }
}

}  // namespace analysis
}  // namespace diagon
