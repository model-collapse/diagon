// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/NumericRangeQuery.h"

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

// ==================== NumericRangeScorer ====================

/**
 * NumericRangeScorer - Scorer for NumericRangeQuery
 *
 * Iterates through documents with numeric values and filters by range.
 * Returns constant score for all matches (no term frequency scoring).
 */
class NumericRangeScorer : public Scorer {
public:
    NumericRangeScorer(const Weight& weight, index::NumericDocValues* values, int maxDoc,
                       int64_t lowerValue, int64_t upperValue, bool includeLower, bool includeUpper,
                       bool isDoubleField, float constantScore)
        : weight_(weight)
        , values_(values)
        , maxDoc_(maxDoc)
        , lowerValue_(lowerValue)
        , upperValue_(upperValue)
        , includeLower_(includeLower)
        , includeUpper_(includeUpper)
        , isDoubleField_(isDoubleField)
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
            int64_t value = values_->longValue();
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
            int64_t value = values_->longValue();
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
     * Handles both int64_t (LONG) and double (DOUBLE) types
     */
    bool matchesRange(int64_t longValue) const {
        if (isDoubleField_) {
            // Convert int64_t bit representation to double
            double value = std::bit_cast<double>(longValue);
            double lower = std::bit_cast<double>(lowerValue_);
            double upper = std::bit_cast<double>(upperValue_);

            // Reject NaN values
            if (std::isnan(value)) {
                return false;
            }

            // Check lower bound
            if (includeLower_) {
                if (value < lower) return false;
            } else {
                if (value <= lower) return false;
            }

            // Check upper bound
            if (includeUpper_) {
                if (value > upper) return false;
            } else {
                if (value >= upper) return false;
            }

            return true;
        } else {
            // LONG type: direct int64_t comparison
            // Check lower bound
            if (includeLower_) {
                if (longValue < lowerValue_) return false;
            } else {
                if (longValue <= lowerValue_) return false;
            }

            // Check upper bound
            if (includeUpper_) {
                if (longValue > upperValue_) return false;
            } else {
                if (longValue >= upperValue_) return false;
            }

            return true;
        }
    }

    const Weight& weight_;
    index::NumericDocValues* values_;  // Non-owning pointer
    int maxDoc_;
    int64_t lowerValue_;
    int64_t upperValue_;
    bool includeLower_;
    bool includeUpper_;
    bool isDoubleField_;  // True if field contains double values
    float constantScore_;
    int doc_;
};

// ==================== NumericRangeWeight ====================

/**
 * NumericRangeWeight - Weight for NumericRangeQuery
 *
 * Creates NumericRangeScorer for each segment
 */
class NumericRangeWeight : public Weight {
public:
    NumericRangeWeight(const NumericRangeQuery& query, IndexSearcher& searcher, float boost)
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

        // Detect field type from FieldInfo attributes
        bool isDoubleField = false;
        auto* fieldInfos = context.reader->getFieldInfos();
        if (fieldInfos) {
            auto* fieldInfo = fieldInfos->fieldInfo(query_.getField());
            if (fieldInfo) {
                auto numericType = fieldInfo->getAttribute("numeric_type");
                if (numericType && *numericType == "DOUBLE") {
                    isDoubleField = true;
                }
            }
        }

        int maxDoc = context.reader->maxDoc();

        // Create scorer with type detection
        return std::make_unique<NumericRangeScorer>(*this, values, maxDoc, query_.getLowerValue(),
                                                     query_.getUpperValue(),
                                                     query_.getIncludeLower(),
                                                     query_.getIncludeUpper(), isDoubleField,
                                                     constantScore_);
    }

    const Query& getQuery() const override { return query_; }

    std::string toString() const override {
        std::ostringstream oss;
        oss << "weight(" << query_.toString(query_.getField()) << ")";
        return oss.str();
    }

private:
    const NumericRangeQuery& query_;
    IndexSearcher& searcher_;
    float constantScore_;
};

// ==================== NumericRangeQuery ====================

NumericRangeQuery::NumericRangeQuery(const std::string& field, int64_t lowerValue,
                                     int64_t upperValue, bool includeLower, bool includeUpper)
    : field_(field)
    , lowerValue_(lowerValue)
    , upperValue_(upperValue)
    , includeLower_(includeLower)
    , includeUpper_(includeUpper) {
    if (lowerValue > upperValue) {
        throw std::invalid_argument("Lower value cannot be greater than upper value");
    }
}

std::unique_ptr<NumericRangeQuery> NumericRangeQuery::newUpperBoundQuery(const std::string& field,
                                                                          int64_t upperValue,
                                                                          bool includeUpper) {
    return std::make_unique<NumericRangeQuery>(
        field, std::numeric_limits<int64_t>::min(), upperValue, true, includeUpper);
}

std::unique_ptr<NumericRangeQuery> NumericRangeQuery::newLowerBoundQuery(const std::string& field,
                                                                          int64_t lowerValue,
                                                                          bool includeLower) {
    return std::make_unique<NumericRangeQuery>(
        field, lowerValue, std::numeric_limits<int64_t>::max(), includeLower, true);
}

std::unique_ptr<NumericRangeQuery> NumericRangeQuery::newExactQuery(const std::string& field,
                                                                     int64_t value) {
    return std::make_unique<NumericRangeQuery>(field, value, value, true, true);
}

std::unique_ptr<Weight> NumericRangeQuery::createWeight(IndexSearcher& searcher,
                                                         ScoreMode scoreMode, float boost) const {
    return std::make_unique<NumericRangeWeight>(*this, searcher, boost);
}

std::string NumericRangeQuery::toString(const std::string& field) const {
    std::ostringstream oss;

    if (field_ != field) {
        oss << field_ << ":";
    }

    oss << (includeLower_ ? "[" : "{");

    if (lowerValue_ == std::numeric_limits<int64_t>::min()) {
        oss << "*";
    } else {
        oss << lowerValue_;
    }

    oss << " TO ";

    if (upperValue_ == std::numeric_limits<int64_t>::max()) {
        oss << "*";
    } else {
        oss << upperValue_;
    }

    oss << (includeUpper_ ? "]" : "}");

    return oss.str();
}

bool NumericRangeQuery::equals(const Query& other) const {
    if (auto* nrq = dynamic_cast<const NumericRangeQuery*>(&other)) {
        return field_ == nrq->field_ && lowerValue_ == nrq->lowerValue_ &&
               upperValue_ == nrq->upperValue_ && includeLower_ == nrq->includeLower_ &&
               includeUpper_ == nrq->includeUpper_;
    }
    return false;
}

size_t NumericRangeQuery::hashCode() const {
    size_t h = std::hash<std::string>{}(field_);
    h ^= std::hash<int64_t>{}(lowerValue_) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int64_t>{}(upperValue_) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(includeLower_) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(includeUpper_) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

std::unique_ptr<Query> NumericRangeQuery::clone() const {
    return std::make_unique<NumericRangeQuery>(field_, lowerValue_, upperValue_, includeLower_,
                                                includeUpper_);
}

}  // namespace search
}  // namespace diagon
