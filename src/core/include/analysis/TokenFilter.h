#pragma once

#include "Token.h"

#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace analysis {

/**
 * TokenFilter processes a stream of tokens.
 *
 * Token filters can:
 * - Transform tokens (e.g., lowercase)
 * - Remove tokens (e.g., stop words)
 * - Add tokens (e.g., synonyms)
 * - Modify token attributes (e.g., type)
 *
 * Filters are applied sequentially in a chain.
 * They should be stateless and thread-safe.
 */
class TokenFilter {
public:
    virtual ~TokenFilter() = default;

    /**
     * Process a stream of tokens.
     *
     * @param tokens Input token stream
     * @return Filtered token stream
     */
    virtual std::vector<Token> filter(const std::vector<Token>& tokens) = 0;

    /**
     * Get the name of this filter for debugging and configuration.
     *
     * @return Filter name (e.g., "lowercase", "stop", "synonym")
     */
    virtual std::string name() const = 0;

    /**
     * Get a description of this filter.
     *
     * @return Human-readable description
     */
    virtual std::string description() const { return name() + " filter"; }
};

}  // namespace analysis
}  // namespace diagon
