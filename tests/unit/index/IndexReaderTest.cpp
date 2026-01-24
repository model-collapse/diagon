// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexReader.h"

#include "diagon/index/FieldInfo.h"

#include <gtest/gtest.h>

using namespace diagon::index;

// ==================== Mock Implementations ====================

/**
 * Mock LeafReader for testing
 */
class MockLeafReader : public LeafReader {
public:
    explicit MockLeafReader(int maxDoc, int numDocs, bool hasDeletions = false)
        : maxDoc_(maxDoc)
        , numDocs_(numDocs)
        , hasDeletions_(hasDeletions) {
        // Create minimal FieldInfos
        std::vector<FieldInfo> infos;
        fieldInfos_ = std::make_unique<FieldInfos>(std::move(infos));
    }

    // Statistics
    int maxDoc() const override { return maxDoc_; }
    int numDocs() const override { return numDocs_; }
    bool hasDeletions() const override { return hasDeletions_; }

    // Pure virtual implementations (return nullptr)
    Terms* terms(const std::string& /*field*/) const override { return nullptr; }
    NumericDocValues* getNumericDocValues(const std::string& /*field*/) const override {
        return nullptr;
    }
    BinaryDocValues* getBinaryDocValues(const std::string& /*field*/) const override {
        return nullptr;
    }
    SortedDocValues* getSortedDocValues(const std::string& /*field*/) const override {
        return nullptr;
    }
    SortedSetDocValues* getSortedSetDocValues(const std::string& /*field*/) const override {
        return nullptr;
    }
    SortedNumericDocValues* getSortedNumericDocValues(const std::string& /*field*/) const override {
        return nullptr;
    }
    StoredFieldsReader* storedFieldsReader() const override { return nullptr; }
    NumericDocValues* getNormValues(const std::string& /*field*/) const override { return nullptr; }
    const FieldInfos& getFieldInfos() const override { return *fieldInfos_; }
    const Bits* getLiveDocs() const override { return nullptr; }
    PointValues* getPointValues(const std::string& /*field*/) const override { return nullptr; }
    CacheHelper* getCoreCacheHelper() const override { return nullptr; }
    CacheHelper* getReaderCacheHelper() const override { return nullptr; }

private:
    int maxDoc_;
    int numDocs_;
    bool hasDeletions_;
    std::unique_ptr<FieldInfos> fieldInfos_;
};

/**
 * Mock CompositeReader for testing
 */
class MockCompositeReader : public CompositeReader {
public:
    explicit MockCompositeReader(std::vector<std::shared_ptr<IndexReader>> subReaders)
        : subReaders_(std::move(subReaders)) {}

    std::vector<std::shared_ptr<IndexReader>> getSequentialSubReaders() const override {
        return subReaders_;
    }

    CacheHelper* getReaderCacheHelper() const override { return nullptr; }

private:
    std::vector<std::shared_ptr<IndexReader>> subReaders_;
};

// ==================== Test Fixture ====================

class IndexReaderTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ==================== Reference Counting Tests ====================

TEST_F(IndexReaderTest, InitialRefCount) {
    auto reader = std::make_shared<MockLeafReader>(100, 100);
    EXPECT_EQ(1, reader->getRefCount());
}

TEST_F(IndexReaderTest, IncRef) {
    auto reader = std::make_shared<MockLeafReader>(100, 100);

    reader->incRef();
    EXPECT_EQ(2, reader->getRefCount());

    reader->incRef();
    EXPECT_EQ(3, reader->getRefCount());
}

TEST_F(IndexReaderTest, DecRef) {
    auto reader = std::make_shared<MockLeafReader>(100, 100);

    reader->incRef();
    EXPECT_EQ(2, reader->getRefCount());

    reader->decRef();
    EXPECT_EQ(1, reader->getRefCount());
}

TEST_F(IndexReaderTest, DecRefToZero) {
    auto reader = std::make_shared<MockLeafReader>(100, 100);
    EXPECT_EQ(1, reader->getRefCount());

    // This will call doClose() when refCount reaches 0
    // Reader should be closed but not destroyed (shared_ptr still holds it)
    reader->decRef();
    EXPECT_EQ(0, reader->getRefCount());
}

TEST_F(IndexReaderTest, TryIncRef) {
    auto reader = std::make_shared<MockLeafReader>(100, 100);

    // Should succeed
    EXPECT_TRUE(reader->tryIncRef());
    EXPECT_EQ(2, reader->getRefCount());

    // Decrement back to 1
    reader->decRef();
    EXPECT_EQ(1, reader->getRefCount());
}

TEST_F(IndexReaderTest, TryIncRefAfterClose) {
    auto reader = std::make_shared<MockLeafReader>(100, 100);

    // Close by decrementing to 0
    reader->decRef();
    EXPECT_EQ(0, reader->getRefCount());

    // tryIncRef should fail on closed reader
    EXPECT_FALSE(reader->tryIncRef());
    EXPECT_EQ(0, reader->getRefCount());
}

TEST_F(IndexReaderTest, RefCountUnderflowThrows) {
    auto reader = std::make_shared<MockLeafReader>(100, 100);

    reader->decRef();  // Goes to 0, closes reader

    // Second decRef should throw
    EXPECT_THROW(reader->decRef(), std::runtime_error);
}

// ==================== LeafReader Tests ====================

TEST_F(IndexReaderTest, LeafReaderStatistics) {
    auto reader = std::make_shared<MockLeafReader>(100, 95, true);

    EXPECT_EQ(100, reader->maxDoc());
    EXPECT_EQ(95, reader->numDocs());
    EXPECT_TRUE(reader->hasDeletions());
}

TEST_F(IndexReaderTest, LeafReaderNoDeletions) {
    auto reader = std::make_shared<MockLeafReader>(100, 100, false);

    EXPECT_EQ(100, reader->maxDoc());
    EXPECT_EQ(100, reader->numDocs());
    EXPECT_FALSE(reader->hasDeletions());
}

TEST_F(IndexReaderTest, LeafReaderLeaves) {
    auto reader = std::make_shared<MockLeafReader>(100, 100);

    auto leaves = reader->leaves();
    EXPECT_EQ(1, leaves.size());
    EXPECT_EQ(reader.get(), leaves[0].reader);
    EXPECT_EQ(0, leaves[0].docBase);
    EXPECT_EQ(0, leaves[0].ord);
}

TEST_F(IndexReaderTest, LeafReaderContext) {
    auto reader = std::make_shared<MockLeafReader>(100, 100);

    auto context = reader->getContext();
    EXPECT_NE(nullptr, context.get());

    auto leaves = context->leaves();
    EXPECT_EQ(1, leaves.size());
    EXPECT_EQ(reader.get(), leaves[0].reader);
}

// ==================== CompositeReader Tests ====================

TEST_F(IndexReaderTest, CompositeReaderStatistics) {
    std::vector<std::shared_ptr<IndexReader>> subReaders;
    subReaders.push_back(std::make_shared<MockLeafReader>(100, 100, false));
    subReaders.push_back(std::make_shared<MockLeafReader>(200, 180, true));
    subReaders.push_back(std::make_shared<MockLeafReader>(50, 50, false));

    auto composite = std::make_shared<MockCompositeReader>(subReaders);

    EXPECT_EQ(350, composite->maxDoc());     // 100 + 200 + 50
    EXPECT_EQ(330, composite->numDocs());    // 100 + 180 + 50
    EXPECT_TRUE(composite->hasDeletions());  // Second segment has deletions
}

TEST_F(IndexReaderTest, CompositeReaderNoDeletions) {
    std::vector<std::shared_ptr<IndexReader>> subReaders;
    subReaders.push_back(std::make_shared<MockLeafReader>(100, 100, false));
    subReaders.push_back(std::make_shared<MockLeafReader>(200, 200, false));

    auto composite = std::make_shared<MockCompositeReader>(subReaders);

    EXPECT_EQ(300, composite->maxDoc());
    EXPECT_EQ(300, composite->numDocs());
    EXPECT_FALSE(composite->hasDeletions());
}

TEST_F(IndexReaderTest, CompositeReaderEmpty) {
    std::vector<std::shared_ptr<IndexReader>> subReaders;
    auto composite = std::make_shared<MockCompositeReader>(subReaders);

    EXPECT_EQ(0, composite->maxDoc());
    EXPECT_EQ(0, composite->numDocs());
    EXPECT_FALSE(composite->hasDeletions());
}

TEST_F(IndexReaderTest, CompositeReaderLeaves) {
    std::vector<std::shared_ptr<IndexReader>> subReaders;
    subReaders.push_back(std::make_shared<MockLeafReader>(100, 100));
    subReaders.push_back(std::make_shared<MockLeafReader>(200, 200));
    subReaders.push_back(std::make_shared<MockLeafReader>(50, 50));

    auto composite = std::make_shared<MockCompositeReader>(subReaders);

    auto leaves = composite->leaves();
    EXPECT_EQ(3, leaves.size());

    // Check leaf 0
    EXPECT_EQ(0, leaves[0].docBase);
    EXPECT_EQ(0, leaves[0].ord);
    EXPECT_EQ(100, leaves[0].reader->maxDoc());

    // Check leaf 1
    EXPECT_EQ(100, leaves[1].docBase);  // Offset by first segment
    EXPECT_EQ(1, leaves[1].ord);
    EXPECT_EQ(200, leaves[1].reader->maxDoc());

    // Check leaf 2
    EXPECT_EQ(300, leaves[2].docBase);  // Offset by first + second
    EXPECT_EQ(2, leaves[2].ord);
    EXPECT_EQ(50, leaves[2].reader->maxDoc());
}

TEST_F(IndexReaderTest, CompositeReaderGetSubReaders) {
    std::vector<std::shared_ptr<IndexReader>> subReaders;
    auto leaf1 = std::make_shared<MockLeafReader>(100, 100);
    auto leaf2 = std::make_shared<MockLeafReader>(200, 200);

    subReaders.push_back(leaf1);
    subReaders.push_back(leaf2);

    auto composite = std::make_shared<MockCompositeReader>(subReaders);

    auto subs = composite->getSequentialSubReaders();
    EXPECT_EQ(2, subs.size());
    EXPECT_EQ(leaf1.get(), subs[0].get());
    EXPECT_EQ(leaf2.get(), subs[1].get());
}

// ==================== Nested CompositeReader Tests ====================

TEST_F(IndexReaderTest, NestedCompositeReader) {
    // Create inner composite
    std::vector<std::shared_ptr<IndexReader>> innerSubs;
    innerSubs.push_back(std::make_shared<MockLeafReader>(100, 100));
    innerSubs.push_back(std::make_shared<MockLeafReader>(50, 50));
    auto innerComposite = std::make_shared<MockCompositeReader>(innerSubs);

    // Create outer composite
    std::vector<std::shared_ptr<IndexReader>> outerSubs;
    outerSubs.push_back(innerComposite);
    outerSubs.push_back(std::make_shared<MockLeafReader>(200, 200));
    auto outerComposite = std::make_shared<MockCompositeReader>(outerSubs);

    // Check statistics
    EXPECT_EQ(350, outerComposite->maxDoc());  // 100 + 50 + 200
    EXPECT_EQ(350, outerComposite->numDocs());

    // Check leaves flattening
    auto leaves = outerComposite->leaves();
    EXPECT_EQ(3, leaves.size());

    EXPECT_EQ(0, leaves[0].docBase);
    EXPECT_EQ(0, leaves[0].ord);

    EXPECT_EQ(100, leaves[1].docBase);
    EXPECT_EQ(1, leaves[1].ord);

    EXPECT_EQ(150, leaves[2].docBase);
    EXPECT_EQ(2, leaves[2].ord);
}

// ==================== Context Tests ====================

TEST_F(IndexReaderTest, LeafReaderContextConstruction) {
    auto reader = std::make_shared<MockLeafReader>(100, 100);

    LeafReaderContext ctx(reader.get(), 50, 2);

    EXPECT_EQ(reader.get(), ctx.reader);
    EXPECT_EQ(50, ctx.docBase);
    EXPECT_EQ(2, ctx.ord);
}

TEST_F(IndexReaderTest, LeafReaderContextDefaults) {
    auto reader = std::make_shared<MockLeafReader>(100, 100);

    LeafReaderContext ctx(reader.get());

    EXPECT_EQ(reader.get(), ctx.reader);
    EXPECT_EQ(0, ctx.docBase);
    EXPECT_EQ(0, ctx.ord);
}

TEST_F(IndexReaderTest, CompositeReaderContextCreation) {
    std::vector<std::shared_ptr<IndexReader>> subReaders;
    subReaders.push_back(std::make_shared<MockLeafReader>(100, 100));
    subReaders.push_back(std::make_shared<MockLeafReader>(200, 200));

    auto composite = std::make_shared<MockCompositeReader>(subReaders);

    auto context = composite->getContext();
    EXPECT_NE(nullptr, context.get());
    EXPECT_TRUE(context->isTopLevel());

    auto leaves = context->leaves();
    EXPECT_EQ(2, leaves.size());
}

// ==================== Reference Counting with Composite ====================

TEST_F(IndexReaderTest, CompositeReaderRefCount) {
    std::vector<std::shared_ptr<IndexReader>> subReaders;
    subReaders.push_back(std::make_shared<MockLeafReader>(100, 100));
    subReaders.push_back(std::make_shared<MockLeafReader>(200, 200));

    auto composite = std::make_shared<MockCompositeReader>(subReaders);

    EXPECT_EQ(1, composite->getRefCount());

    composite->incRef();
    EXPECT_EQ(2, composite->getRefCount());

    composite->decRef();
    EXPECT_EQ(1, composite->getRefCount());
}

// ==================== Edge Cases ====================

TEST_F(IndexReaderTest, SingleDocumentReader) {
    auto reader = std::make_shared<MockLeafReader>(1, 1);

    EXPECT_EQ(1, reader->maxDoc());
    EXPECT_EQ(1, reader->numDocs());
    EXPECT_FALSE(reader->hasDeletions());

    auto leaves = reader->leaves();
    EXPECT_EQ(1, leaves.size());
}

TEST_F(IndexReaderTest, EmptyReader) {
    auto reader = std::make_shared<MockLeafReader>(0, 0);

    EXPECT_EQ(0, reader->maxDoc());
    EXPECT_EQ(0, reader->numDocs());
    EXPECT_FALSE(reader->hasDeletions());
}

TEST_F(IndexReaderTest, AllDocumentsDeleted) {
    auto reader = std::make_shared<MockLeafReader>(100, 0, true);

    EXPECT_EQ(100, reader->maxDoc());
    EXPECT_EQ(0, reader->numDocs());
    EXPECT_TRUE(reader->hasDeletions());
}

TEST_F(IndexReaderTest, LargeCompositeReader) {
    std::vector<std::shared_ptr<IndexReader>> subReaders;

    // Add 100 segments
    for (int i = 0; i < 100; i++) {
        subReaders.push_back(std::make_shared<MockLeafReader>(1000, 1000));
    }

    auto composite = std::make_shared<MockCompositeReader>(subReaders);

    EXPECT_EQ(100000, composite->maxDoc());
    EXPECT_EQ(100000, composite->numDocs());

    auto leaves = composite->leaves();
    EXPECT_EQ(100, leaves.size());

    // Check first and last leaf
    EXPECT_EQ(0, leaves[0].docBase);
    EXPECT_EQ(99000, leaves[99].docBase);
}

TEST_F(IndexReaderTest, MultipleIncDecCycles) {
    auto reader = std::make_shared<MockLeafReader>(100, 100);

    for (int i = 0; i < 10; i++) {
        reader->incRef();
        EXPECT_EQ(i + 2, reader->getRefCount());
    }

    for (int i = 0; i < 10; i++) {
        reader->decRef();
        EXPECT_EQ(11 - i - 1, reader->getRefCount());
    }

    EXPECT_EQ(1, reader->getRefCount());
}

TEST_F(IndexReaderTest, CompositeWithMixedDeletions) {
    std::vector<std::shared_ptr<IndexReader>> subReaders;
    subReaders.push_back(std::make_shared<MockLeafReader>(100, 100, false));
    subReaders.push_back(std::make_shared<MockLeafReader>(100, 90, true));
    subReaders.push_back(std::make_shared<MockLeafReader>(100, 100, false));
    subReaders.push_back(std::make_shared<MockLeafReader>(100, 80, true));

    auto composite = std::make_shared<MockCompositeReader>(subReaders);

    EXPECT_EQ(400, composite->maxDoc());
    EXPECT_EQ(370, composite->numDocs());
    EXPECT_TRUE(composite->hasDeletions());
}

TEST_F(IndexReaderTest, ThreeLevelNesting) {
    // Level 1: leaves
    auto leaf1 = std::make_shared<MockLeafReader>(100, 100);
    auto leaf2 = std::make_shared<MockLeafReader>(200, 200);

    // Level 2: composite of leaves
    std::vector<std::shared_ptr<IndexReader>> level2Subs;
    level2Subs.push_back(leaf1);
    level2Subs.push_back(leaf2);
    auto level2 = std::make_shared<MockCompositeReader>(level2Subs);

    // Level 3: composite of composite
    auto leaf3 = std::make_shared<MockLeafReader>(50, 50);
    std::vector<std::shared_ptr<IndexReader>> level3Subs;
    level3Subs.push_back(level2);
    level3Subs.push_back(leaf3);
    auto level3 = std::make_shared<MockCompositeReader>(level3Subs);

    EXPECT_EQ(350, level3->maxDoc());
    EXPECT_EQ(350, level3->numDocs());

    auto leaves = level3->leaves();
    EXPECT_EQ(3, leaves.size());

    EXPECT_EQ(0, leaves[0].docBase);
    EXPECT_EQ(100, leaves[1].docBase);
    EXPECT_EQ(300, leaves[2].docBase);
}
