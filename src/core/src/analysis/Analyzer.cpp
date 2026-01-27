#include "analysis/Analyzer.h"
#include <sstream>

namespace diagon {
namespace analysis {

// Analyzer base class implementation

std::string Analyzer::description() const {
    std::ostringstream oss;
    oss << name() << " analyzer [tokenizer=" << getTokenizerName();

    auto filters = getFilterNames();
    if (!filters.empty()) {
        oss << ", filters=[";
        for (size_t i = 0; i < filters.size(); i++) {
            if (i > 0) oss << ", ";
            oss << filters[i];
        }
        oss << "]";
    }

    oss << "]";
    return oss.str();
}

// CompositeAnalyzer implementation

CompositeAnalyzer::CompositeAnalyzer(const std::string& name,
                                     std::unique_ptr<Tokenizer> tokenizer,
                                     std::vector<std::unique_ptr<TokenFilter>> filters)
    : name_(name)
    , tokenizer_(std::move(tokenizer))
    , filters_(std::move(filters)) {
}

std::vector<Token> CompositeAnalyzer::analyze(const std::string& text) {
    // Step 1: Tokenize
    std::vector<Token> tokens = tokenizer_->tokenize(text);

    // Step 2: Apply filters in sequence
    for (const auto& filter : filters_) {
        tokens = filter->filter(tokens);
    }

    return tokens;
}

std::string CompositeAnalyzer::getTokenizerName() const {
    return tokenizer_ ? tokenizer_->name() : "none";
}

std::vector<std::string> CompositeAnalyzer::getFilterNames() const {
    std::vector<std::string> names;
    names.reserve(filters_.size());
    for (const auto& filter : filters_) {
        names.push_back(filter->name());
    }
    return names;
}

} // namespace analysis
} // namespace diagon
