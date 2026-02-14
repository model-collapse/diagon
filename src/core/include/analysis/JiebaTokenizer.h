#pragma once

#include "Tokenizer.h"

#include <memory>
#include <string>

// Forward declare cppjieba classes to avoid including in header
namespace cppjieba {
class Jieba;
}

namespace diagon {
namespace analysis {

/**
 * Segmentation mode for Jieba tokenizer.
 *
 * MP (Maximum Probability): Uses dynamic programming to find the most probable segmentation.
 * Best for: General text, highest precision
 * Example: "我来到北京清华大学" -> ["我", "来到", "北京", "清华大学"]
 *
 * HMM (Hidden Markov Model): Uses HMM for new word recognition.
 * Best for: Text with new/unknown words
 * Example: "他来到了网易杭研大厦" -> ["他", "来到", "了", "网易", "杭研", "大厦"]
 *
 * MIX (MP + HMM): Combines both methods for best accuracy.
 * Best for: General use, balanced precision/recall (DEFAULT)
 * Example: "小明硕士毕业于中国科学院" -> ["小明", "硕士", "毕业", "于", "中国科学院"]
 *
 * FULL (Full Mode): Enumerates all possible words.
 * Best for: Search engines, synonym expansion, maximum recall
 * Example: "我来到北京清华大学" -> ["我", "来到", "北京", "清华", "清华大学", "华大", "大学"]
 *
 * SEARCH (Search Engine Mode): Optimized for search, splits long words.
 * Best for: Search indexing, better than FULL for most cases
 * Example: "南京市长江大桥" -> ["南京", "市", "长江", "大桥"]
 */
enum class JiebaMode {
    MP,     // Maximum Probability (most accurate)
    HMM,    // Hidden Markov Model (new words)
    MIX,    // MP + HMM combined (default, recommended)
    FULL,   // Full mode (all possible words)
    SEARCH  // Search engine mode (splits long words)
};

/**
 * JiebaTokenizer provides Chinese word segmentation using cppjieba.
 *
 * Features:
 * - Multiple segmentation modes (MP, HMM, MIX, FULL, SEARCH)
 * - Support for custom user dictionaries
 * - HMM model for new word recognition
 * - Stop word filtering (optional)
 * - Thread-safe (each instance has its own Jieba object)
 *
 * Performance:
 * - ~300KB/s throughput (MIX mode)
 * - Memory: ~100MB for dictionaries (shared across instances)
 *
 * Usage:
 *   auto tokenizer = std::make_unique<JiebaTokenizer>(JiebaMode::MIX);
 *   auto tokens = tokenizer->tokenize("我爱北京天安门");
 *   // Result: ["我", "爱", "北京", "天安门"]
 *
 * Thread-safety: Each thread should create its own instance.
 * The underlying dictionaries are loaded once and shared.
 */
class JiebaTokenizer : public Tokenizer {
public:
    /**
     * Create a JiebaTokenizer with specified mode and dictionary paths.
     *
     * @param mode Segmentation mode (default: MIX)
     * @param dictPath Path to main dictionary (default: use bundled)
     * @param hmmPath Path to HMM model (default: use bundled)
     * @param userDictPath Path to user dictionary (optional)
     * @param stopWordPath Path to stop word list (optional)
     */
    explicit JiebaTokenizer(JiebaMode mode = JiebaMode::MIX, const std::string& dictPath = "",
                            const std::string& hmmPath = "", const std::string& userDictPath = "",
                            const std::string& stopWordPath = "");

    virtual ~JiebaTokenizer();

    // Tokenizer interface
    std::vector<Token> tokenize(const std::string& text) override;
    void reset() override;
    std::string name() const override { return "jieba"; }
    std::string description() const override {
        return "Chinese word segmentation using Jieba (Mode: " + getModeString() + ")";
    }

    /**
     * Get current segmentation mode.
     */
    JiebaMode getMode() const { return mode_; }

    /**
     * Set segmentation mode (affects future tokenize() calls).
     */
    void setMode(JiebaMode mode) { mode_ = mode; }

    /**
     * Add custom words to user dictionary at runtime.
     * Higher weight = more likely to be segmented as a single word.
     *
     * @param word Word to add
     * @param weight Word weight (default: 100)
     */
    void addUserWord(const std::string& word, int weight = 100);

    /**
     * Get default dictionary directory path.
     * Returns the path where cppjieba dictionaries are installed.
     */
    static std::string getDefaultDictDir();

private:
    JiebaMode mode_;
    std::unique_ptr<cppjieba::Jieba> jieba_;

    // Dictionary paths (stored for debugging)
    std::string dictPath_;
    std::string hmmPath_;
    std::string userDictPath_;
    std::string stopWordPath_;

    /**
     * Initialize Jieba with dictionary files.
     */
    void initializeJieba(const std::string& dictPath, const std::string& hmmPath,
                         const std::string& userDictPath, const std::string& stopWordPath);

    /**
     * Get mode as string for debugging.
     */
    std::string getModeString() const;

    /**
     * Check if word is a stop word (if stop word list loaded).
     */
    bool isStopWord(const std::string& word) const;
};

}  // namespace analysis
}  // namespace diagon
