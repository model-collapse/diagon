#include "diagon/search/MatchAllDocsQuery.h"

namespace diagon {
namespace search {

std::unique_ptr<Weight> MatchAllQuery::createWeight(IndexSearcher* searcher, bool needsScores, float boost) {
    return std::make_unique<MatchAllWeight>(this, boost);
}

std::unique_ptr<Scorer> MatchAllWeight::scorer(index::LeafReaderContext* context) {
    int32_t maxDoc = context->reader()->maxDoc();
    return std::make_unique<MatchAllScorer>(this, maxDoc, boost_);
}

} // namespace search
} // namespace diagon
