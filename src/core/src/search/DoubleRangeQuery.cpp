// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/DoubleRangeQuery.h"

#include "diagon/index/DocValues.h"
#include "diagon/index/IndexReader.h"
#include "diagon/index/LeafReaderContext.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/Scorer.h"

#include <bit>
#include <cmath>
#include <limits>
#include <sstream>

namespace diagon {
namespace search {

// ==================== DoubleRangeScorer ====================

/**
 * DoubleRangeScorer - Scorer for DoubleRangeQuery
 *
 * Iterates through documents with double values and filters by range.
 * Returns constant score for all matches (no term frequency scoring).
 */
class DoubleRangeScorer : public Scorer {
public:
    DoubleRangeScorer(const Weight& weight, index::NumericDocValues* values, int maxDoc,
                      double lowerValue, double upperValue, bool includeLower, bool includeUpper,
                      float constantScore)
        : weight_(weight)
        , values_(values)
        , maxDoc_(maxDoc)
        , lowerValue_(lowerValue)
        , upperValue_(upperValue)
        , includeLower_(includeLower)
        , includeUpper_(includeUpper)
        , constantScore_(constantScore)
        , doc_(-1) {}

    // ==================== DocIdSetIterator ====================

    int docID() const override { return doc_; }

    int nextDoc() override {
        if (!values_) {
            // No values for this field in this segment
            doc_ = NO_MORE_DOCS;
            return doc_;
        }

        // Advance to next document with a value
        while (true) {
            doc_ = values_->nextDoc();

            if (doc_ == NO_MORE_DOCS) {
                return doc_;
            }

            // Check if value is in range
            // NumericDocValues stores doubles as int64_t bit representation
            // Use bit_cast to convert back to double
            int64_t longBits = values_->longValue();
            double value = std::bit_cast<double>(longBits);
            if (matchesRange(value)) {
                return doc_;
            }
        }
    }

    int advance(int target) override {
        if (!values_) {
            doc_ = NO_MORE_DOCS;
            return doc_;
        }

        if (target >= maxDoc_) {
            doc_ = NO_MORE_DOCS;
            return doc_;
        }

        // Advance values iterator to target
        if (values_->docID() < target) {
            values_->advance(target);
        }

        // Find next matching document
        while (values_->docID() < maxDoc_) {
            // NumericDocValues stores doubles as int64_t bit representation
            int64_t longBits = values_->longValue();
            double value = std::bit_cast<double>(longBits);
            if (matchesRange(value)) {
                doc_ = values_->docID();
                return doc_;
            }

            if (values_->nextDoc() == NO_MORE_DOCS) {
                break;
            }
        }

        doc_ = NO_MORE_DOCS;
        return doc_;
    }

    int64_t cost() const override {
        // Cost is number of documents (worst case: scan all docs)
        return values_ ? values_->cost() : 0;
    }

    // ==================== Scorer ====================

    float score() const override {
        // Constant score - range queries don't use term frequency
        return constantScore_;
    }

    const Weight& getWeight() const override { return weight_; }

private:
    /**
     * Check if value matches the range
     * Handles NaN by rejecting (NaN comparisons always return false)
     */
    bool matchesRange(double value) const {
        // Reject NaN values
        if (std::isnan(value)) {
            return false;
        }

        // Check lower bound
        if (includeLower_) {
            if (value < lowerValue_) {
                return false;
            }
        } else {
            if (value <= lowerValue_) {
                return false;
            }
        }

        // Check upper bound
        if (includeUpper_) {
            if (value > upperValue_) {
                return false;
            }
        } else {
            if (value >= upperValue_) {
                return false;
            }
        }

        return true;
    }

    const Weight& weight_;
    index::NumericDocValues* values_;  // Non-owning pointer
    int maxDoc_;
    double lowerValue_;
    double upperValue_;
    bool includeLower_;
    bool includeUpper_;
    float constantScore_;
    int doc_;
};

// ==================== DoubleRangeWeight ====================

/**
 * DoubleRangeWeight - Weight for DoubleRangeQuery
 *
 * Creates DoubleRangeScorer for each segment
 */
class DoubleRangeWeight : public Weight {
public:
    DoubleRangeWeight(const DoubleRangeQuery& query, IndexSearcher& searcher, float boost)
        : query_(query)
        , searcher_(searcher)
        , constantScore_(boost) {}

    std::unique_ptr<Scorer> scorer(const index::LeafReaderContext& context) const override {
        // Get numeric doc values for field (raw pointer, non-owning)
        auto* values = context.reader->getNumericDocValues(query_.getField());
        if (!values) {
            // Field doesn't exist or has no values in this segment
            return nullptr;
        }

        int maxDoc = context.reader->maxDoc();

        // Create scorer (passes non-owning pointer)
        return std::make_unique<DoubleRangeScorer>(*this, values, maxDoc, query_.getLowerValue(),
                                                    query_.getUpperValue(),
                                                    query_.getIncludeLower(),
                                                    query_.getIncludeUpper(), constantScore_);
    }

    const Query& getQuery() const override { return query_; }

    std::string toString() const override {
        std::ostringstream oss;
        oss << "weight(" << query_.toString(query_.getField()) << ")";
        return oss.str();
    }

private:
    const DoubleRangeQuery& query_;
    IndexSearcher& searcher_;
    float constantScore_;
};

// ==================== DoubleRangeQuery ====================

DoubleRangeQuery::DoubleRangeQuery(const std::string& field, double lowerValue,
                                   double upperValue, bool includeLower, bool includeUpper)
    : field_(field)
    , lowerValue_(lowerValue)
    , upperValue_(upperValue)
    , includeLower_(includeLower)
    , includeUpper_(includeUpper) {
    // Reject NaN bounds
    if (std::isnan(lowerValue) || std::isnan(upperValue)) {
        throw std::invalid_argument("Range bounds cannot be NaN");
    }

    if (lowerValue > upperValue) {
        throw std::invalid_argument("Lower value cannot be greater than upper value");
    }
}

std::unique_ptr<DoubleRangeQuery> DoubleRangeQuery::newUpperBoundQuery(const std::string& field,
                                                                        double upperValue,
                                                                        bool includeUpper) {
    return std::make_unique<DoubleRangeQuery>(
        field, std::numeric_limits<double>::lowest(), upperValue, true, includeUpper);
}

std::unique_ptr<DoubleRangeQuery> DoubleRangeQuery::newLowerBoundQuery(const std::string& field,
                                                                        double lowerValue,
                                                                        bool includeLower) {
    return std::make_unique<DoubleRangeQuery>(
        field, lowerValue, std::numeric_limits<double>::max(), includeLower, true);
}

std::unique_ptr<DoubleRangeQuery> DoubleRangeQuery::newExactQuery(const std::string& field,
                                                                   double value) {
    return std::make_unique<DoubleRangeQuery>(field, value, value, true, true);
}

std::unique_ptr<Weight> DoubleRangeQuery::createWeight(IndexSearcher& searcher,
                                                        ScoreMode scoreMode, float boost) const {
    return std::make_unique<DoubleRangeWeight>(*this, searcher, boost);
}

std::string DoubleRangeQuery::toString(const std::string& field) const {
    std::ostringstream oss;

    if (field_ != field) {
        oss << field_ << ":";
    }

    oss << (includeLower_ ? "[" : "{");

    if (lowerValue_ == std::numeric_limits<double>::lowest()) {
        oss << "*";
    } else {
        oss << lowerValue_;
    }

    oss << " TO ";

    if (upperValue_ == std::numeric_limits<double>::max()) {
        oss << "*";
    } else {
        oss << upperValue_;
    }

    oss << (includeUpper_ ? "]" : "}");

    return oss.str();
}

bool DoubleRangeQuery::equals(const Query& other) const {
    if (auto* drq = dynamic_cast<const DoubleRangeQuery*>(&other)) {
        return field_ == drq->field_ && lowerValue_ == drq->lowerValue_ &&
               upperValue_ == drq->upperValue_ && includeLower_ == drq->includeLower_ &&
               includeUpper_ == drq->includeUpper_;
    }
    return false;
}

size_t DoubleRangeQuery::hashCode() const {
    size_t h = std::hash<std::string>{}(field_);
    h ^= std::hash<double>{}(lowerValue_) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<double>{}(upperValue_) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(includeLower_) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(includeUpper_) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

std::unique_ptr<Query> DoubleRangeQuery::clone() const {
    return std::make_unique<DoubleRangeQuery>(field_, lowerValue_, upperValue_, includeLower_,
                                               includeUpper_);
}

}  // namespace search
}  // namespace diagon
