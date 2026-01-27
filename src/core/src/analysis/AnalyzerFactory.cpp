#include "analysis/Analyzer.h"
#include "analysis/WhitespaceTokenizer.h"
#include "analysis/KeywordTokenizer.h"
#include "analysis/StandardTokenizer.h"
#include "analysis/JiebaTokenizer.h"
#include "analysis/LowercaseFilter.h"

namespace diagon {
namespace analysis {

std::unique_ptr<Analyzer> AnalyzerFactory::createWhitespace() {
    // Whitespace tokenizer with no filters
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();
    std::vector<std::unique_ptr<TokenFilter>> filters;

    return std::make_unique<CompositeAnalyzer>(
        "whitespace",
        std::move(tokenizer),
        std::move(filters)
    );
}

std::unique_ptr<Analyzer> AnalyzerFactory::createKeyword() {
    // Keyword tokenizer with no filters
    auto tokenizer = std::make_unique<KeywordTokenizer>();
    std::vector<std::unique_ptr<TokenFilter>> filters;

    return std::make_unique<CompositeAnalyzer>(
        "keyword",
        std::move(tokenizer),
        std::move(filters)
    );
}

std::unique_ptr<Analyzer> AnalyzerFactory::createSimple() {
    // Whitespace tokenizer + lowercase filter
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());

    return std::make_unique<CompositeAnalyzer>(
        "simple",
        std::move(tokenizer),
        std::move(filters)
    );
}

std::unique_ptr<Analyzer> AnalyzerFactory::createStandard() {
    // Standard tokenizer (ICU-based) + lowercase filter
    auto tokenizer = std::make_unique<StandardTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());

    return std::make_unique<CompositeAnalyzer>(
        "standard",
        std::move(tokenizer),
        std::move(filters)
    );
}

std::unique_ptr<Analyzer> AnalyzerFactory::createChinese(const std::string& dictPath) {
    // Jieba tokenizer with MIX mode (MP + HMM for best accuracy)
    auto tokenizer = std::make_unique<JiebaTokenizer>(JiebaMode::MIX, dictPath);

    // No filters for now - Chinese text doesn't need lowercase
    // (can add StopFilter later for Chinese stop words)
    std::vector<std::unique_ptr<TokenFilter>> filters;

    return std::make_unique<CompositeAnalyzer>(
        "chinese",
        std::move(tokenizer),
        std::move(filters)
    );
}

} // namespace analysis
} // namespace diagon
