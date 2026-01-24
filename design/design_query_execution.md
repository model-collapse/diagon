# Query Execution - Low-Level Design

## Overview

The query execution engine processes various query types using both inverted indexes and column storage. It supports Boolean queries, range queries, phrase queries, and hybrid analytical queries.

## Query Hierarchy

```cpp
class Query {
public:
    virtual ~Query() = default;

    // Create scorer for a segment
    virtual std::unique_ptr<Scorer> create_scorer(const Segment* segment) const = 0;

    // Query rewriting (optimization)
    virtual std::unique_ptr<Query> rewrite(const IndexReader* reader) const;

    // String representation
    virtual std::string to_string() const = 0;

    // Circuit breaker integration
    virtual uint64_t estimated_cost() const = 0;
};

class Scorer {
public:
    virtual ~Scorer() = default;

    // Iteration
    virtual bool has_next() const = 0;
    virtual std::pair<uint32_t, float> next() = 0;  // (doc_id, score)

    // Skip to doc ID (using skip lists)
    virtual bool skip_to(uint32_t target_doc_id) = 0;

    // Current position
    virtual uint32_t current_doc_id() const = 0;

    // Score computation
    virtual float score() const = 0;
};
```

## Query Types

### 1. Term Query

```cpp
class TermQuery : public Query {
public:
    TermQuery(std::string field, std::string term)
        : field_(std::move(field)), term_(std::move(term)) {}

    std::unique_ptr<Scorer> create_scorer(const Segment* segment) const override {
        // Get term dictionary
        auto term_dict = segment->term_dictionary(field_);

        // Lookup term
        auto term_info = term_dict->get_term_info(term_);
        if (!term_info) {
            return std::make_unique<EmptyScorer>();
        }

        // Load posting list
        auto posting_list = segment->posting_list(field_, term_info->posting_list_offset);

        // Create scorer
        return std::make_unique<TermScorer>(
            posting_list->iterator(),
            term_info->doc_freq,
            segment->num_docs()
        );
    }

    std::string to_string() const override {
        return field_ + ":" + term_;
    }

private:
    std::string field_;
    std::string term_;
};

class TermScorer : public Scorer {
public:
    TermScorer(std::unique_ptr<PostingIterator> posting_it,
               uint32_t doc_freq,
               uint32_t num_docs)
        : posting_it_(std::move(posting_it))
        , idf_(compute_idf(doc_freq, num_docs)) {}

    bool has_next() const override {
        return posting_it_->has_next();
    }

    std::pair<uint32_t, float> next() override {
        auto posting = posting_it_->next();
        current_doc_id_ = posting.doc_id;
        current_score_ = compute_bm25(posting.term_freq);
        return {current_doc_id_, current_score_};
    }

    bool skip_to(uint32_t target_doc_id) override {
        return posting_it_->skip_to(target_doc_id);
    }

private:
    std::unique_ptr<PostingIterator> posting_it_;
    uint32_t current_doc_id_{0};
    float current_score_{0.0f};
    float idf_;

    float compute_bm25(uint32_t term_freq) const {
        // BM25: score = IDF * (tf * (k1 + 1)) / (tf + k1 * (1 - b + b * dl / avgdl))
        constexpr float k1 = 1.2f;
        constexpr float b = 0.75f;
        // Simplified for now
        return idf_ * (term_freq / (term_freq + k1));
    }

    static float compute_idf(uint32_t doc_freq, uint32_t num_docs) {
        return std::log(1.0f + (num_docs - doc_freq + 0.5f) / (doc_freq + 0.5f));
    }
};
```

### 2. Boolean Query (AND/OR/NOT)

```cpp
class BooleanQuery : public Query {
public:
    enum class Occur {
        MUST,       // AND
        SHOULD,     // OR
        MUST_NOT    // NOT
    };

    struct Clause {
        std::unique_ptr<Query> query;
        Occur occur;
    };

    void add_clause(std::unique_ptr<Query> query, Occur occur) {
        clauses_.push_back({std::move(query), occur});
    }

    std::unique_ptr<Scorer> create_scorer(const Segment* segment) const override {
        std::vector<std::unique_ptr<Scorer>> must_scorers;
        std::vector<std::unique_ptr<Scorer>> should_scorers;
        std::vector<std::unique_ptr<Scorer>> must_not_scorers;

        for (const auto& clause : clauses_) {
            auto scorer = clause.query->create_scorer(segment);

            switch (clause.occur) {
                case Occur::MUST:
                    must_scorers.push_back(std::move(scorer));
                    break;
                case Occur::SHOULD:
                    should_scorers.push_back(std::move(scorer));
                    break;
                case Occur::MUST_NOT:
                    must_not_scorers.push_back(std::move(scorer));
                    break;
            }
        }

        return std::make_unique<BooleanScorer>(
            std::move(must_scorers),
            std::move(should_scorers),
            std::move(must_not_scorers)
        );
    }

private:
    std::vector<Clause> clauses_;
};

class BooleanScorer : public Scorer {
public:
    BooleanScorer(
        std::vector<std::unique_ptr<Scorer>> must_scorers,
        std::vector<std::unique_ptr<Scorer>> should_scorers,
        std::vector<std::unique_ptr<Scorer>> must_not_scorers)
        : must_scorers_(std::move(must_scorers))
        , should_scorers_(std::move(should_scorers))
        , must_not_scorers_(std::move(must_not_scorers)) {
        advance();
    }

    bool has_next() const override {
        return has_next_;
    }

    std::pair<uint32_t, float> next() override {
        auto result = std::make_pair(current_doc_id_, current_score_);
        advance();
        return result;
    }

private:
    std::vector<std::unique_ptr<Scorer>> must_scorers_;
    std::vector<std::unique_ptr<Scorer>> should_scorers_;
    std::vector<std::unique_ptr<Scorer>> must_not_scorers_;

    uint32_t current_doc_id_{0};
    float current_score_{0.0f};
    bool has_next_{false};

    void advance() {
        // Find next matching doc using skip lists
        while (true) {
            // Get candidate from first MUST clause (or SHOULD if no MUST)
            uint32_t candidate = get_next_candidate();
            if (candidate == UINT32_MAX) {
                has_next_ = false;
                return;
            }

            // Check all MUST clauses
            bool all_must_match = true;
            for (auto& scorer : must_scorers_) {
                if (!scorer->skip_to(candidate)) {
                    has_next_ = false;
                    return;
                }
                if (scorer->current_doc_id() != candidate) {
                    all_must_match = false;
                    break;
                }
            }

            if (!all_must_match) {
                continue;
            }

            // Check MUST_NOT clauses
            bool any_must_not_match = false;
            for (auto& scorer : must_not_scorers_) {
                if (scorer->skip_to(candidate) &&
                    scorer->current_doc_id() == candidate) {
                    any_must_not_match = true;
                    break;
                }
            }

            if (any_must_not_match) {
                continue;
            }

            // Compute combined score
            current_doc_id_ = candidate;
            current_score_ = compute_score();
            has_next_ = true;
            return;
        }
    }

    float compute_score() const {
        float score = 0.0f;

        for (const auto& scorer : must_scorers_) {
            score += scorer->score();
        }

        for (const auto& scorer : should_scorers_) {
            if (scorer->current_doc_id() == current_doc_id_) {
                score += scorer->score();
            }
        }

        return score;
    }
};
```

### 3. Range Query

```cpp
class RangeQuery : public Query {
public:
    RangeQuery(std::string field, FieldValue min_value, FieldValue max_value)
        : field_(std::move(field))
        , min_value_(std::move(min_value))
        , max_value_(std::move(max_value)) {}

    std::unique_ptr<Scorer> create_scorer(const Segment* segment) const override {
        // Use column storage for range queries
        auto column_reader = segment->column_reader(field_);

        ScanFilter filter;
        filter.type = ScanFilter::FilterType::RANGE;
        filter.min_value = min_value_;
        filter.max_value = max_value_;

        auto scanner = column_reader->create_scanner(filter);

        return std::make_unique<RangeScorer>(std::move(scanner));
    }

    std::string to_string() const override {
        return field_ + ":[" + min_value_.to_string() + " TO " +
               max_value_.to_string() + "]";
    }

private:
    std::string field_;
    FieldValue min_value_;
    FieldValue max_value_;
};

class RangeScorer : public Scorer {
public:
    explicit RangeScorer(std::unique_ptr<ColumnScanner> scanner)
        : scanner_(std::move(scanner)) {}

    bool has_next() const override {
        return scanner_->has_next();
    }

    std::pair<uint32_t, float> next() override {
        auto [doc_id, value] = scanner_->next();
        current_doc_id_ = doc_id;
        return {doc_id, 1.0f};  // Constant score for range queries
    }

    bool skip_to(uint32_t target_doc_id) override {
        // Scan until we reach target
        while (has_next() && current_doc_id_ < target_doc_id) {
            next();
        }
        return current_doc_id_ == target_doc_id;
    }

private:
    std::unique_ptr<ColumnScanner> scanner_;
    uint32_t current_doc_id_{0};
};
```

### 4. Phrase Query

```cpp
class PhraseQuery : public Query {
public:
    PhraseQuery(std::string field, std::vector<std::string> terms)
        : field_(std::move(field)), terms_(std::move(terms)) {}

    std::unique_ptr<Scorer> create_scorer(const Segment* segment) const override {
        // Get posting lists with positions for all terms
        std::vector<std::unique_ptr<PostingIterator>> iterators;

        for (const auto& term : terms_) {
            auto term_dict = segment->term_dictionary(field_);
            auto term_info = term_dict->get_term_info(term);

            if (!term_info) {
                return std::make_unique<EmptyScorer>();
            }

            auto posting_list = segment->posting_list(
                field_, term_info->posting_list_offset
            );

            iterators.push_back(posting_list->iterator());
        }

        return std::make_unique<PhraseScorer>(
            std::move(iterators),
            terms_.size()
        );
    }

private:
    std::string field_;
    std::vector<std::string> terms_;
};

class PhraseScorer : public Scorer {
public:
    PhraseScorer(
        std::vector<std::unique_ptr<PostingIterator>> term_iterators,
        size_t phrase_length)
        : term_iterators_(std::move(term_iterators))
        , phrase_length_(phrase_length) {
        advance_to_next_phrase();
    }

    bool has_next() const override {
        return has_next_;
    }

    std::pair<uint32_t, float> next() override {
        auto result = std::make_pair(current_doc_id_, current_score_);
        advance_to_next_phrase();
        return result;
    }

private:
    std::vector<std::unique_ptr<PostingIterator>> term_iterators_;
    size_t phrase_length_;
    uint32_t current_doc_id_{0};
    float current_score_{0.0f};
    bool has_next_{false};

    void advance_to_next_phrase() {
        // Find docs where all terms appear
        while (true) {
            // Get candidate from first term
            if (!term_iterators_[0]->has_next()) {
                has_next_ = false;
                return;
            }

            auto posting = term_iterators_[0]->next();
            uint32_t candidate = posting.doc_id;

            // Check all other terms match
            bool all_match = true;
            for (size_t i = 1; i < term_iterators_.size(); i++) {
                if (!term_iterators_[i]->skip_to(candidate)) {
                    has_next_ = false;
                    return;
                }

                if (term_iterators_[i]->current_doc_id() != candidate) {
                    all_match = false;
                    break;
                }
            }

            if (!all_match) {
                continue;
            }

            // Check if positions form a phrase
            if (check_phrase_positions()) {
                current_doc_id_ = candidate;
                current_score_ = 1.0f;  // Simplified
                has_next_ = true;
                return;
            }
        }
    }

    bool check_phrase_positions() {
        // Get positions from all term postings
        std::vector<std::vector<uint32_t>> all_positions;
        for (const auto& it : term_iterators_) {
            all_positions.push_back(it->positions());
        }

        // Check if any combination forms consecutive positions
        return has_consecutive_positions(all_positions, 0);
    }

    bool has_consecutive_positions(
        const std::vector<std::vector<uint32_t>>& positions,
        uint32_t base_pos) {

        if (positions.empty()) return true;

        // Try each position of first term
        for (uint32_t pos : positions[0]) {
            bool match = true;

            // Check if remaining terms appear at consecutive positions
            for (size_t i = 1; i < positions.size(); i++) {
                uint32_t expected = pos + i;
                if (std::find(positions[i].begin(), positions[i].end(),
                             expected) == positions[i].end()) {
                    match = false;
                    break;
                }
            }

            if (match) return true;
        }

        return false;
    }
};
```

### 5. Wildcard Query

```cpp
class WildcardQuery : public Query {
public:
    WildcardQuery(std::string field, std::string pattern)
        : field_(std::move(field)), pattern_(std::move(pattern)) {}

    std::unique_ptr<Query> rewrite(const IndexReader* reader) const override {
        // Expand wildcard to matching terms
        auto terms = expand_wildcard(reader, field_, pattern_);

        if (terms.empty()) {
            return std::make_unique<MatchNoneQuery>();
        }

        if (terms.size() == 1) {
            return std::make_unique<TermQuery>(field_, terms[0]);
        }

        // Create Boolean OR query
        auto boolean_query = std::make_unique<BooleanQuery>();
        for (const auto& term : terms) {
            boolean_query->add_clause(
                std::make_unique<TermQuery>(field_, term),
                BooleanQuery::Occur::SHOULD
            );
        }

        return boolean_query;
    }

private:
    std::string field_;
    std::string pattern_;

    std::vector<std::string> expand_wildcard(
        const IndexReader* reader,
        const std::string& field,
        const std::string& pattern) const {

        std::vector<std::string> matching_terms;

        // Get term iterator from index
        // Trie-based dictionary is efficient for prefix patterns
        auto segments = reader->segments();
        for (const auto& segment : segments) {
            auto term_dict = segment.term_dictionary(field);
            auto it = term_dict->iterator();

            while (it->has_next()) {
                auto [term, term_info] = it->next();
                if (matches_pattern(term, pattern)) {
                    matching_terms.push_back(term);
                }
            }
        }

        return matching_terms;
    }

    bool matches_pattern(
        const std::string& term,
        const std::string& pattern) const {
        // Simple wildcard matching (* and ?)
        // Can be optimized with finite automaton
        return wildcard_match(term, pattern);
    }
};
```

## Query Optimization

### Query Rewriting

```cpp
class QueryRewriter {
public:
    static std::unique_ptr<Query> rewrite(
        const Query* query,
        const IndexReader* reader) {

        // 1. Expand wildcards
        if (auto* wildcard = dynamic_cast<const WildcardQuery*>(query)) {
            return wildcard->rewrite(reader);
        }

        // 2. Optimize Boolean queries
        if (auto* boolean = dynamic_cast<const BooleanQuery*>(query)) {
            return optimize_boolean_query(boolean, reader);
        }

        // 3. Constant score optimization
        // If query doesn't need scoring, use faster path
        if (!query->needs_score()) {
            return std::make_unique<ConstantScoreQuery>(query->clone());
        }

        return query->clone();
    }

private:
    static std::unique_ptr<Query> optimize_boolean_query(
        const BooleanQuery* query,
        const IndexReader* reader) {

        // Reorder clauses by selectivity (low doc freq first)
        std::vector<BooleanQuery::Clause> clauses;

        for (const auto& clause : query->clauses()) {
            auto cost = estimate_clause_cost(clause.query.get(), reader);
            clauses.push_back({clause.query->clone(), clause.occur, cost});
        }

        std::sort(clauses.begin(), clauses.end(),
            [](const auto& a, const auto& b) {
                return a.cost < b.cost;
            }
        );

        auto optimized = std::make_unique<BooleanQuery>();
        for (auto& clause : clauses) {
            optimized->add_clause(std::move(clause.query), clause.occur);
        }

        return optimized;
    }

    static uint64_t estimate_clause_cost(
        const Query* query,
        const IndexReader* reader) {

        if (auto* term_query = dynamic_cast<const TermQuery*>(query)) {
            return reader->doc_freq(Term(term_query->field(), term_query->term()));
        }

        // Default: high cost
        return UINT64_MAX;
    }
};
```

### Early Termination (WAND/BMW)

```cpp
class EarlyTerminationScorer : public Scorer {
public:
    EarlyTerminationScorer(
        std::vector<std::unique_ptr<Scorer>> scorers,
        uint32_t top_k)
        : scorers_(std::move(scorers))
        , top_k_(top_k) {

        // Pre-compute max scores
        for (const auto& scorer : scorers_) {
            max_scores_.push_back(scorer->max_score());
        }
    }

    bool has_next() const override {
        // Early termination: stop if current threshold is unreachable
        float max_remaining_score = compute_max_remaining_score();

        if (top_docs_.size() >= top_k_ &&
            max_remaining_score < top_docs_.min_score()) {
            return false;
        }

        return any_scorer_has_next();
    }

private:
    std::vector<std::unique_ptr<Scorer>> scorers_;
    std::vector<float> max_scores_;
    uint32_t top_k_;
    TopKQueue<ScoredDoc> top_docs_;

    float compute_max_remaining_score() const {
        float sum = 0.0f;
        for (size_t i = 0; i < scorers_.size(); i++) {
            if (scorers_[i]->has_next()) {
                sum += max_scores_[i];
            }
        }
        return sum;
    }
};
```

## Circuit Breaker Integration

```cpp
class CircuitBreakerQuery : public Query {
public:
    CircuitBreakerQuery(
        std::unique_ptr<Query> query,
        CircuitBreaker* breaker)
        : query_(std::move(query))
        , breaker_(breaker) {}

    std::unique_ptr<Scorer> create_scorer(const Segment* segment) const override {
        // Check circuit breaker before creating scorer
        if (breaker_) {
            breaker_->check_query_complexity(*query_);
        }

        auto scorer = query_->create_scorer(segment);

        return std::make_unique<CircuitBreakerScorer>(
            std::move(scorer),
            breaker_
        );
    }

private:
    std::unique_ptr<Query> query_;
    CircuitBreaker* breaker_;
};

class CircuitBreakerScorer : public Scorer {
public:
    std::pair<uint32_t, float> next() override {
        docs_scanned_++;

        if (docs_scanned_ % CHECK_INTERVAL == 0 && breaker_) {
            breaker_->check_result_size(docs_scanned_);
        }

        return scorer_->next();
    }

private:
    std::unique_ptr<Scorer> scorer_;
    CircuitBreaker* breaker_;
    uint64_t docs_scanned_{0};
    static constexpr uint64_t CHECK_INTERVAL = 10000;
};
```

## Testing

```cpp
class QueryExecutionTest {
    void test_boolean_and_correctness() {
        // Query: field1:term1 AND field2:term2
        BooleanQuery query;
        query.add_clause(
            std::make_unique<TermQuery>("field1", "term1"),
            BooleanQuery::Occur::MUST
        );
        query.add_clause(
            std::make_unique<TermQuery>("field2", "term2"),
            BooleanQuery::Occur::MUST
        );

        auto results = reader->search(query, 100);

        // Verify all results contain both terms
        for (const auto& hit : results.hits) {
            auto doc = reader->document(hit.doc_id);
            ASSERT_TRUE(doc.contains_term("field1", "term1"));
            ASSERT_TRUE(doc.contains_term("field2", "term2"));
        }
    }

    void test_phrase_query() {
        // Query: "quick brown fox"
        PhraseQuery query("text", {"quick", "brown", "fox"});

        auto results = reader->search(query, 10);

        // Verify phrase appears in results
        for (const auto& hit : results.hits) {
            auto doc = reader->document(hit.doc_id);
            ASSERT_TRUE(doc.get_field("text")
                .as_string()
                .find("quick brown fox") != std::string::npos);
        }
    }
};
```
