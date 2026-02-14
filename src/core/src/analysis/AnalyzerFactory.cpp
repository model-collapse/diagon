#include "analysis/ASCIIFoldingFilter.h"
#include "analysis/Analyzer.h"
#include "analysis/JiebaTokenizer.h"
#include "analysis/KeywordTokenizer.h"
#include "analysis/LowercaseFilter.h"
#include "analysis/StandardTokenizer.h"
#include "analysis/StopFilter.h"
#include "analysis/SynonymFilter.h"
#include "analysis/WhitespaceTokenizer.h"

namespace diagon {
namespace analysis {

std::unique_ptr<Analyzer> AnalyzerFactory::createWhitespace() {
    // Whitespace tokenizer with no filters
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();
    std::vector<std::unique_ptr<TokenFilter>> filters;

    return std::make_unique<CompositeAnalyzer>("whitespace", std::move(tokenizer),
                                               std::move(filters));
}

std::unique_ptr<Analyzer> AnalyzerFactory::createKeyword() {
    // Keyword tokenizer with no filters
    auto tokenizer = std::make_unique<KeywordTokenizer>();
    std::vector<std::unique_ptr<TokenFilter>> filters;

    return std::make_unique<CompositeAnalyzer>("keyword", std::move(tokenizer), std::move(filters));
}

std::unique_ptr<Analyzer> AnalyzerFactory::createSimple() {
    // Whitespace tokenizer + lowercase filter
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());

    return std::make_unique<CompositeAnalyzer>("simple", std::move(tokenizer), std::move(filters));
}

std::unique_ptr<Analyzer> AnalyzerFactory::createStandard() {
    // Standard tokenizer (ICU-based) + lowercase + stop filter
    auto tokenizer = std::make_unique<StandardTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());
    filters.push_back(std::make_unique<StopFilter>(StopFilter::StopWordSet::ENGLISH));

    return std::make_unique<CompositeAnalyzer>("standard", std::move(tokenizer),
                                               std::move(filters));
}

std::unique_ptr<Analyzer> AnalyzerFactory::createChinese(const std::string& dictPath) {
    // Jieba tokenizer with MIX mode (MP + HMM for best accuracy)
    auto tokenizer = std::make_unique<JiebaTokenizer>(JiebaMode::MIX, dictPath);

    // Add Chinese stop word filter
    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<StopFilter>(StopFilter::StopWordSet::CHINESE, true));

    return std::make_unique<CompositeAnalyzer>("chinese", std::move(tokenizer), std::move(filters));
}

std::unique_ptr<Analyzer> AnalyzerFactory::createEnglish() {
    // Standard tokenizer + lowercase + english stop + ascii folding
    auto tokenizer = std::make_unique<StandardTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());
    filters.push_back(std::make_unique<ASCIIFoldingFilter>());
    filters.push_back(std::make_unique<StopFilter>(StopFilter::StopWordSet::ENGLISH));

    return std::make_unique<CompositeAnalyzer>("english", std::move(tokenizer), std::move(filters));
}

std::unique_ptr<Analyzer> AnalyzerFactory::createMultilingual() {
    // Standard tokenizer + lowercase + ascii folding
    // No stop words since they're language-specific
    auto tokenizer = std::make_unique<StandardTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());
    filters.push_back(std::make_unique<ASCIIFoldingFilter>());

    return std::make_unique<CompositeAnalyzer>("multilingual", std::move(tokenizer),
                                               std::move(filters));
}

std::unique_ptr<Analyzer> AnalyzerFactory::createSearch() {
    // Optimized for search queries: standard tokenizer + lowercase + ascii folding + stop
    auto tokenizer = std::make_unique<StandardTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());
    filters.push_back(std::make_unique<ASCIIFoldingFilter>());
    filters.push_back(std::make_unique<StopFilter>(StopFilter::StopWordSet::ENGLISH));

    return std::make_unique<CompositeAnalyzer>("search", std::move(tokenizer), std::move(filters));
}

}  // namespace analysis
}  // namespace diagon
