// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/PointRangeQuery.h"

#include "diagon/index/IndexReader.h"
#include "diagon/index/LeafReaderContext.h"
#include "diagon/index/PointValues.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/Scorer.h"
#include "diagon/util/BitSet.h"
#include "diagon/util/NumericUtils.h"

#include <cstring>
#include <sstream>

namespace diagon {
namespace search {

// ==================== BitSetScorer ====================

/**
 * Scorer that iterates over a BitSet of matching doc IDs.
 * Returns constant score for all matches.
 */
class BitSetScorer : public Scorer {
public:
    BitSetScorer(const Weight& weight, std::unique_ptr<util::BitSet> bitSet, float score)
        : weight_(weight)
        , bitSet_(std::move(bitSet))
        , constantScore_(score)
        , doc_(-1)
        , numMatches_(static_cast<int>(bitSet_->cardinality())) {}

    int docID() const override { return doc_; }

    int nextDoc() override {
        size_t next = bitSet_->nextSetBit(static_cast<size_t>(doc_ + 1));
        if (next == util::BitSet::NO_MORE_BITS) {
            doc_ = NO_MORE_DOCS;
        } else {
            doc_ = static_cast<int>(next);
        }
        return doc_;
    }

    int advance(int target) override {
        if (target >= static_cast<int>(bitSet_->length())) {
            doc_ = NO_MORE_DOCS;
            return doc_;
        }
        size_t next = bitSet_->nextSetBit(static_cast<size_t>(target));
        if (next == util::BitSet::NO_MORE_BITS) {
            doc_ = NO_MORE_DOCS;
        } else {
            doc_ = static_cast<int>(next);
        }
        return doc_;
    }

    int64_t cost() const override { return numMatches_; }

    float score() const override { return constantScore_; }

    const Weight& getWeight() const override { return weight_; }

private:
    const Weight& weight_;
    std::unique_ptr<util::BitSet> bitSet_;
    float constantScore_;
    int doc_;
    int numMatches_;
};

// ==================== PointRangeWeight ====================

class PointRangeWeight : public Weight {
public:
    PointRangeWeight(const PointRangeQuery& query, float boost)
        : query_(query)
        , constantScore_(boost) {}

    std::unique_ptr<Scorer> scorer(const index::LeafReaderContext& context) const override {
        auto* pointValues = context.reader->getPointValues(query_.getField());
        if (!pointValues) {
            return nullptr;
        }

        int maxDoc = context.reader->maxDoc();
        auto bitSet = std::make_unique<util::BitSet>(maxDoc);

        // Create IntersectVisitor that collects matching doc IDs
        struct RangeVisitor : public index::PointValues::IntersectVisitor {
            util::BitSet& bits;
            const uint8_t* lower;
            const uint8_t* upper;
            int bytesPerDim;

            RangeVisitor(util::BitSet& bits, const uint8_t* lower, const uint8_t* upper,
                         int bytesPerDim)
                : bits(bits)
                , lower(lower)
                , upper(upper)
                , bytesPerDim(bytesPerDim) {}

            void visit(int docID) override { bits.set(static_cast<size_t>(docID)); }

            void visit(int docID, const uint8_t* packedValue) override {
                // Check each dimension against bounds
                if (std::memcmp(packedValue, lower, bytesPerDim) >= 0 &&
                    std::memcmp(packedValue, upper, bytesPerDim) <= 0) {
                    bits.set(static_cast<size_t>(docID));
                }
            }

            index::PointValues::Relation compare(const uint8_t* minPackedValue,
                                                 const uint8_t* maxPackedValue) override {
                // For 1D: check if cell [min, max] overlaps query [lower, upper]
                // Cell is outside if: cellMax < lower OR cellMin > upper
                if (std::memcmp(maxPackedValue, lower, bytesPerDim) < 0) {
                    return index::PointValues::Relation::CELL_OUTSIDE_QUERY;
                }
                if (std::memcmp(minPackedValue, upper, bytesPerDim) > 0) {
                    return index::PointValues::Relation::CELL_OUTSIDE_QUERY;
                }

                // Cell is inside if: cellMin >= lower AND cellMax <= upper
                if (std::memcmp(minPackedValue, lower, bytesPerDim) >= 0 &&
                    std::memcmp(maxPackedValue, upper, bytesPerDim) <= 0) {
                    return index::PointValues::Relation::CELL_INSIDE_QUERY;
                }

                return index::PointValues::Relation::CELL_CROSSES_QUERY;
            }
        };

        RangeVisitor visitor(*bitSet, query_.getLowerPoint().data(), query_.getUpperPoint().data(),
                             query_.getBytesPerDim());
        pointValues->intersect(visitor);

        if (bitSet->cardinality() == 0) {
            return nullptr;
        }

        return std::make_unique<BitSetScorer>(*this, std::move(bitSet), constantScore_);
    }

    const Query& getQuery() const override { return query_; }

    std::string toString() const override {
        std::ostringstream oss;
        oss << "weight(" << query_.toString(query_.getField()) << ")";
        return oss.str();
    }

private:
    const PointRangeQuery& query_;
    float constantScore_;
};

// ==================== PointRangeQuery ====================

PointRangeQuery::PointRangeQuery(std::string field, std::vector<uint8_t> lowerPoint,
                                 std::vector<uint8_t> upperPoint, int numDims, int bytesPerDim)
    : field_(std::move(field))
    , lowerPoint_(std::move(lowerPoint))
    , upperPoint_(std::move(upperPoint))
    , numDims_(numDims)
    , bytesPerDim_(bytesPerDim) {}

std::unique_ptr<PointRangeQuery> PointRangeQuery::newLongRange(const std::string& field,
                                                               int64_t lower, int64_t upper) {
    std::vector<uint8_t> lowerBytes(8);
    std::vector<uint8_t> upperBytes(8);
    util::NumericUtils::longToBytesBE(lower, lowerBytes.data());
    util::NumericUtils::longToBytesBE(upper, upperBytes.data());
    return std::make_unique<PointRangeQuery>(field, std::move(lowerBytes), std::move(upperBytes), 1,
                                             8);
}

std::unique_ptr<PointRangeQuery> PointRangeQuery::newDoubleRange(const std::string& field,
                                                                 double lower, double upper) {
    int64_t lowerSortable = util::NumericUtils::doubleToSortableLong(lower);
    int64_t upperSortable = util::NumericUtils::doubleToSortableLong(upper);
    std::vector<uint8_t> lowerBytes(8);
    std::vector<uint8_t> upperBytes(8);
    util::NumericUtils::longToBytesBE(lowerSortable, lowerBytes.data());
    util::NumericUtils::longToBytesBE(upperSortable, upperBytes.data());
    return std::make_unique<PointRangeQuery>(field, std::move(lowerBytes), std::move(upperBytes), 1,
                                             8);
}

std::unique_ptr<Weight> PointRangeQuery::createWeight(IndexSearcher& /*searcher*/,
                                                      ScoreMode /*scoreMode*/, float boost) const {
    return std::make_unique<PointRangeWeight>(*this, boost);
}

std::string PointRangeQuery::toString(const std::string& field) const {
    std::ostringstream oss;
    if (field_ != field) {
        oss << field_ << ":";
    }
    oss << "[";

    // For 1D int64, decode the bytes for readable output
    if (numDims_ == 1 && bytesPerDim_ == 8) {
        int64_t lower = util::NumericUtils::bytesToLongBE(lowerPoint_.data());
        int64_t upper = util::NumericUtils::bytesToLongBE(upperPoint_.data());
        oss << lower << " TO " << upper;
    } else {
        oss << "... TO ...";
    }

    oss << "]";
    return oss.str();
}

bool PointRangeQuery::equals(const Query& other) const {
    if (auto* prq = dynamic_cast<const PointRangeQuery*>(&other)) {
        return field_ == prq->field_ && lowerPoint_ == prq->lowerPoint_ &&
               upperPoint_ == prq->upperPoint_ && numDims_ == prq->numDims_ &&
               bytesPerDim_ == prq->bytesPerDim_;
    }
    return false;
}

size_t PointRangeQuery::hashCode() const {
    size_t h = std::hash<std::string>{}(field_);
    for (auto b : lowerPoint_) {
        h ^= std::hash<uint8_t>{}(b) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    for (auto b : upperPoint_) {
        h ^= std::hash<uint8_t>{}(b) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
}

std::unique_ptr<Query> PointRangeQuery::clone() const {
    return std::make_unique<PointRangeQuery>(field_, lowerPoint_, upperPoint_, numDims_,
                                             bytesPerDim_);
}

}  // namespace search
}  // namespace diagon
