// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/MatchAllDocsQuery.h"

#include "diagon/index/IndexReader.h"
#include "diagon/index/LeafReaderContext.h"

namespace diagon {
namespace search {

std::unique_ptr<Weight> MatchAllQuery::createWeight(IndexSearcher& searcher, ScoreMode scoreMode,
                                                    float boost) const {
    return std::make_unique<MatchAllWeight>(this, boost);
}

std::unique_ptr<Scorer> MatchAllWeight::scorer(const index::LeafReaderContext& context) const {
    int32_t maxDoc = context.reader->maxDoc();
    return std::make_unique<MatchAllScorer>(this, maxDoc, boost_);
}

}  // namespace search
}  // namespace diagon
