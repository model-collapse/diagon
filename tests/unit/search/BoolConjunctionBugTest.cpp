// Targeted test for BooleanQuery conjunction bug:
// TermQuery AND PointRangeQuery returns wrong results.
// Expected: intersection of both. Actual: only one clause's results.

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/search/BooleanClause.h"
#include "diagon/search/BooleanQuery.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/PointRangeQuery.h"
#include "diagon/search/TermQuery.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/util/NumericUtils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <set>

namespace fs = std::filesystem;
using namespace diagon;

class BoolConjunctionBugTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / ("bool_conj_bug_" + std::to_string(getpid()));
        fs::create_directories(testDir_);
        directory_ = store::FSDirectory::open(testDir_.string());
    }

    void TearDown() override {
        directory_.reset();
        fs::remove_all(testDir_);
    }

    fs::path testDir_;
    std::unique_ptr<store::Directory> directory_;
};

// Test: TermQuery AND PointRangeQuery
// This is the EXACT pattern that fails in Big5 benchmarks.
TEST_F(BoolConjunctionBugTest, TermANDPointRange) {
    // Index 100 docs:
    //   - "category" field: "A" for even docs, "B" for odd docs
    //   - "price" field (LongPointField): doc_id * 10
    {
        index::IndexWriterConfig config;
        index::IndexWriter writer(*directory_, config);

        for (int i = 0; i < 100; i++) {
            document::Document doc;
            std::string category = (i % 2 == 0) ? "A" : "B";
            doc.add(std::make_unique<document::StringField>("category", category));
            doc.add(
                std::make_unique<document::LongPointField>("price", static_cast<int64_t>(i * 10)));
            doc.add(std::make_unique<document::TextField>("body", "filler"));
            writer.addDocument(doc);
        }
        writer.commit();
        writer.close();
    }

    auto reader = index::DirectoryReader::open(*directory_);
    search::IndexSearcher searcher(*reader);

    // Standalone TermQuery: category=A -> 50 docs (even: 0,2,4,...,98)
    {
        search::TermQuery termQ(search::Term("category", "A"));
        int termCount = searcher.count(termQ);
        EXPECT_EQ(50, termCount) << "TermQuery category=A should match 50 docs";
    }

    // Standalone PointRangeQuery: price in [200, 500] -> 31 docs (20,21,...,50)
    {
        auto rangeQ = search::PointRangeQuery::newLongRange("price", 200, 500);
        int rangeCount = searcher.count(*rangeQ);
        EXPECT_EQ(31, rangeCount) << "PointRangeQuery [200,500] should match 31 docs";
    }

    // Conjunction: category=A AND price in [200, 500]
    // Expected: docs where i is even AND i*10 in [200,500]
    // i in [20..50] AND i%2==0 -> i = 20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50 = 16 docs
    {
        search::BooleanQuery::Builder builder;
        builder.add(std::make_unique<search::TermQuery>(search::Term("category", "A")),
                    search::Occur::MUST);
        builder.add(search::PointRangeQuery::newLongRange("price", 200, 500), search::Occur::MUST);
        auto boolQ = builder.build();

        int conjCount = searcher.count(*boolQ);
        EXPECT_EQ(16, conjCount)
            << "Bool MUST [term=A, range=[200,500]] should return 16 (intersection), "
            << "got " << conjCount
            << ". If 31, only range clause is being used. If 50, only term clause.";
    }

    // Also test with FILTER (same semantics as MUST without scoring)
    {
        search::BooleanQuery::Builder builder;
        builder.add(std::make_unique<search::TermQuery>(search::Term("category", "A")),
                    search::Occur::FILTER);
        builder.add(search::PointRangeQuery::newLongRange("price", 200, 500),
                    search::Occur::FILTER);
        auto boolQ = builder.build();

        int filterCount = searcher.count(*boolQ);
        EXPECT_EQ(16, filterCount)
            << "Bool FILTER [term=A, range=[200,500]] should return 16, got " << filterCount;
    }
}

// Test with multiple segments to match Big5 scenario
TEST_F(BoolConjunctionBugTest, TermANDPointRange_MultiSegment) {
    {
        index::IndexWriterConfig config;
        config.setMaxBufferedDocs(20);  // Force frequent flushes -> multiple segments
        index::IndexWriter writer(*directory_, config);

        for (int i = 0; i < 200; i++) {
            document::Document doc;
            std::string category = (i % 3 == 0) ? "X" : ((i % 3 == 1) ? "Y" : "Z");
            doc.add(std::make_unique<document::StringField>("category", category));
            doc.add(std::make_unique<document::DoublePointField>("score", static_cast<double>(i)));
            doc.add(std::make_unique<document::TextField>("body", "filler"));
            writer.addDocument(doc);
        }
        writer.commit();
        writer.close();
    }

    auto reader = index::DirectoryReader::open(*directory_);
    search::IndexSearcher searcher(*reader);

    // Standalone counts
    search::TermQuery termQ(search::Term("category", "X"));
    int termCount = searcher.count(termQ);
    // X = i%3==0: 0,3,6,...,198 -> 67 docs
    EXPECT_EQ(67, termCount) << "category=X count";

    auto rangeQ = search::PointRangeQuery::newDoubleRange("score", 50.0, 149.0);
    int rangeCount = searcher.count(*rangeQ);
    // score in [50, 149] -> docs 50..149 = 100 docs
    EXPECT_EQ(100, rangeCount) << "score in [50,149] count";

    // Conjunction
    search::BooleanQuery::Builder builder;
    builder.add(std::make_unique<search::TermQuery>(search::Term("category", "X")),
                search::Occur::MUST);
    builder.add(search::PointRangeQuery::newDoubleRange("score", 50.0, 149.0), search::Occur::MUST);
    auto boolQ = builder.build();

    int conjCount = searcher.count(*boolQ);
    // X docs in [50,149]: 51,54,57,...,147 -> (147-51)/3 + 1 = 33 docs
    EXPECT_EQ(33, conjCount)
        << "Bool MUST [term=X, range=[50,149]] should return 33 (intersection), got " << conjCount;
}

// Test: Two TermQueries (this already works, sanity check)
TEST_F(BoolConjunctionBugTest, TwoTermQueries_SanityCheck) {
    {
        index::IndexWriterConfig config;
        index::IndexWriter writer(*directory_, config);

        for (int i = 0; i < 50; i++) {
            document::Document doc;
            std::string content;
            if (i % 2 == 0)
                content += "apple ";
            if (i % 3 == 0)
                content += "banana ";
            content += "filler";
            doc.add(std::make_unique<document::TextField>("content", content));
            writer.addDocument(doc);
        }
        writer.commit();
        writer.close();
    }

    auto reader = index::DirectoryReader::open(*directory_);
    search::IndexSearcher searcher(*reader);

    search::BooleanQuery::Builder builder;
    builder.add(std::make_unique<search::TermQuery>(search::Term("content", "apple")),
                search::Occur::MUST);
    builder.add(std::make_unique<search::TermQuery>(search::Term("content", "banana")),
                search::Occur::MUST);
    auto boolQ = builder.build();

    int count = searcher.count(*boolQ);
    // apple: i%2==0 -> 25 docs. banana: i%3==0 -> 17 docs.
    // Both: i%6==0 -> 0,6,12,18,24,30,36,42,48 = 9 docs
    EXPECT_EQ(9, count) << "Two TermQueries conjunction should return 9, got " << count;
}
