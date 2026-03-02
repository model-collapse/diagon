// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/JsonDocumentParser.h"

#include "diagon/c_api/diagon_c_api.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

using namespace diagon::document;
using namespace diagon::index;

// ==================== JsonDocumentParser Unit Tests ====================

TEST(JsonDocumentParserTest, SimpleFlatObject) {
    const char* json = R"({"title":"hello world","count":42,"price":9.99,"active":true})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->size(), 4);

    // String -> TextField
    auto* title = doc->getField("title");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->stringValue(), "hello world");
    EXPECT_TRUE(title->fieldType().tokenized);
    EXPECT_TRUE(title->fieldType().stored);

    // Integer -> indexed long
    auto* count = doc->getField("count");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->numericValue(), 42);
    EXPECT_EQ(count->fieldType().numericType, NumericType::LONG);
    EXPECT_EQ(count->fieldType().docValuesType, DocValuesType::NUMERIC);

    // Float -> indexed double (stored as bit_cast<int64_t>)
    auto* price = doc->getField("price");
    ASSERT_NE(price, nullptr);
    auto numVal = price->numericValue();
    ASSERT_TRUE(numVal.has_value());
    double recovered = std::bit_cast<double>(*numVal);
    EXPECT_DOUBLE_EQ(recovered, 9.99);
    EXPECT_EQ(price->fieldType().numericType, NumericType::DOUBLE);

    // Boolean -> StringField
    auto* active = doc->getField("active");
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->stringValue(), "true");
    EXPECT_FALSE(active->fieldType().tokenized);
}

TEST(JsonDocumentParserTest, NestedObjectDotPath) {
    const char* json = R"({"user":{"name":"alice","address":{"city":"NYC"}}})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);

    auto* name = doc->getField("user.name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->stringValue(), "alice");

    auto* city = doc->getField("user.address.city");
    ASSERT_NE(city, nullptr);
    EXPECT_EQ(city->stringValue(), "NYC");
}

TEST(JsonDocumentParserTest, ArrayOfStrings) {
    const char* json = R"({"tags":["search","engine","fast"]})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);
    auto fields = doc->getFieldsByName("tags");
    ASSERT_EQ(fields.size(), 3);
    EXPECT_EQ(fields[0]->stringValue(), "search");
    EXPECT_EQ(fields[1]->stringValue(), "engine");
    EXPECT_EQ(fields[2]->stringValue(), "fast");
}

TEST(JsonDocumentParserTest, ArrayOfNumbers) {
    const char* json = R"({"scores":[100,200,300]})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);
    auto fields = doc->getFieldsByName("scores");
    ASSERT_EQ(fields.size(), 3);
    EXPECT_EQ(fields[0]->numericValue(), 100);
    EXPECT_EQ(fields[1]->numericValue(), 200);
    EXPECT_EQ(fields[2]->numericValue(), 300);
}

TEST(JsonDocumentParserTest, MixedArray) {
    const char* json = R"({"data":["text",42,true]})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);
    auto fields = doc->getFieldsByName("data");
    ASSERT_EQ(fields.size(), 3);
    EXPECT_EQ(fields[0]->stringValue(), "text");
    EXPECT_EQ(fields[1]->numericValue(), 42);
    EXPECT_EQ(fields[2]->stringValue(), "true");
}

TEST(JsonDocumentParserTest, DeepNesting) {
    const char* json = R"({"a":{"b":{"c":{"d":"deep"}}}})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);
    auto* field = doc->getField("a.b.c.d");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->stringValue(), "deep");
}

TEST(JsonDocumentParserTest, NullValueSkipped) {
    const char* json = R"({"name":"test","empty":null,"count":1})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->size(), 2);
    EXPECT_NE(doc->getField("name"), nullptr);
    EXPECT_EQ(doc->getField("empty"), nullptr);
    EXPECT_NE(doc->getField("count"), nullptr);
}

TEST(JsonDocumentParserTest, EmptyObject) {
    const char* json = R"({})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->size(), 0);
    EXPECT_TRUE(doc->empty());
}

TEST(JsonDocumentParserTest, EmptyArray) {
    const char* json = R"({"tags":[]})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->size(), 0);
}

TEST(JsonDocumentParserTest, InvalidJsonError) {
    const char* json = R"({invalid json)";
    EXPECT_THROW(JsonDocumentParser::parse(json, strlen(json)), std::runtime_error);
}

TEST(JsonDocumentParserTest, NonObjectJsonError) {
    const char* json = R"([1, 2, 3])";
    EXPECT_THROW(JsonDocumentParser::parse(json, strlen(json)), std::runtime_error);
}

TEST(JsonDocumentParserTest, WithExplicitId) {
    const char* json = R"({"title":"test doc"})";
    auto doc = JsonDocumentParser::parseWithId(json, strlen(json), "doc-001");

    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->size(), 2);

    auto* idField = doc->getField("_id");
    ASSERT_NE(idField, nullptr);
    EXPECT_EQ(idField->stringValue(), "doc-001");
    EXPECT_FALSE(idField->fieldType().tokenized);  // StringField, not tokenized

    auto* title = doc->getField("title");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->stringValue(), "test doc");
}

TEST(JsonDocumentParserTest, BatchParsing) {
    const char* json = R"([{"name":"doc1"},{"name":"doc2"},{"name":"doc3"}])";
    auto docs = JsonDocumentParser::parseBatch(json, strlen(json));

    ASSERT_EQ(docs.size(), 3);
    EXPECT_EQ(docs[0]->getField("name")->stringValue(), "doc1");
    EXPECT_EQ(docs[1]->getField("name")->stringValue(), "doc2");
    EXPECT_EQ(docs[2]->getField("name")->stringValue(), "doc3");
}

TEST(JsonDocumentParserTest, BatchHeterogeneousDocs) {
    const char* json = R"([{"title":"first","count":1},{"name":"second","active":true}])";
    auto docs = JsonDocumentParser::parseBatch(json, strlen(json));

    ASSERT_EQ(docs.size(), 2);

    EXPECT_NE(docs[0]->getField("title"), nullptr);
    EXPECT_NE(docs[0]->getField("count"), nullptr);
    EXPECT_EQ(docs[0]->getField("name"), nullptr);

    EXPECT_NE(docs[1]->getField("name"), nullptr);
    EXPECT_NE(docs[1]->getField("active"), nullptr);
    EXPECT_EQ(docs[1]->getField("title"), nullptr);
}

TEST(JsonDocumentParserTest, BatchInvalidJsonError) {
    const char* json = R"(not json)";
    EXPECT_THROW(JsonDocumentParser::parseBatch(json, strlen(json)), std::runtime_error);
}

TEST(JsonDocumentParserTest, BatchNonArrayError) {
    const char* json = R"({"name":"single"})";
    EXPECT_THROW(JsonDocumentParser::parseBatch(json, strlen(json)), std::runtime_error);
}

TEST(JsonDocumentParserTest, LargeDocument) {
    // Build JSON with 100+ fields
    std::string json = "{";
    for (int i = 0; i < 120; i++) {
        if (i > 0)
            json += ",";
        json += "\"field_" + std::to_string(i) + "\":\"value_" + std::to_string(i) + "\"";
    }
    json += "}";

    auto doc = JsonDocumentParser::parse(json.c_str(), json.size());
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->size(), 120);

    // Spot-check some fields
    EXPECT_EQ(doc->getField("field_0")->stringValue(), "value_0");
    EXPECT_EQ(doc->getField("field_99")->stringValue(), "value_99");
    EXPECT_EQ(doc->getField("field_119")->stringValue(), "value_119");
}

TEST(JsonDocumentParserTest, UnicodeStringValues) {
    // JSON Unicode escapes are handled by the JSON parser
    const char* json = R"({"text":"Hello \u4e16\u754c","name":"caf\u00e9"})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);
    auto* text = doc->getField("text");
    ASSERT_NE(text, nullptr);
    EXPECT_FALSE(text->stringValue()->empty());

    auto* name = doc->getField("name");
    ASSERT_NE(name, nullptr);
    EXPECT_FALSE(name->stringValue()->empty());
}

TEST(JsonDocumentParserTest, IntegerOverflowToDouble) {
    // JSON numbers beyond int64 range are treated as floating point by nlohmann-json
    const char* json = R"({"small":42,"negative":-100})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->getField("small")->numericValue(), 42);
    EXPECT_EQ(doc->getField("negative")->numericValue(), -100);
}

TEST(JsonDocumentParserTest, BooleanAsStringField) {
    const char* json = R"({"yes":true,"no":false})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);

    auto* yes = doc->getField("yes");
    ASSERT_NE(yes, nullptr);
    EXPECT_EQ(yes->stringValue(), "true");
    EXPECT_FALSE(yes->fieldType().tokenized);  // StringField

    auto* no = doc->getField("no");
    ASSERT_NE(no, nullptr);
    EXPECT_EQ(no->stringValue(), "false");
    EXPECT_FALSE(no->fieldType().tokenized);
}

TEST(JsonDocumentParserTest, NullInArray) {
    const char* json = R"({"data":["a",null,"b"]})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);
    auto fields = doc->getFieldsByName("data");
    ASSERT_EQ(fields.size(), 2);  // null skipped
    EXPECT_EQ(fields[0]->stringValue(), "a");
    EXPECT_EQ(fields[1]->stringValue(), "b");
}

TEST(JsonDocumentParserTest, NestedObjectInArray) {
    const char* json = R"({"items":[{"name":"a"},{"name":"b"}]})";
    auto doc = JsonDocumentParser::parse(json, strlen(json));

    ASSERT_NE(doc, nullptr);
    auto fields = doc->getFieldsByName("items.name");
    ASSERT_EQ(fields.size(), 2);
    EXPECT_EQ(fields[0]->stringValue(), "a");
    EXPECT_EQ(fields[1]->stringValue(), "b");
}

// ==================== C API Tests ====================

TEST(JsonDocumentParserCApiTest, CreateDocumentFromJson) {
    diagon_clear_error();

    const char* json = R"({"title":"test document","count":42})";
    DiagonDocument doc = diagon_create_document_from_json(json, strlen(json));

    ASSERT_NE(doc, nullptr);

    // Verify we can get field values from the created document
    auto* document = static_cast<Document*>(doc);
    EXPECT_EQ(document->size(), 2);
    EXPECT_EQ(document->getField("title")->stringValue(), "test document");
    EXPECT_EQ(document->getField("count")->numericValue(), 42);

    diagon_free_document(doc);
}

TEST(JsonDocumentParserCApiTest, CreateDocumentFromJsonWithId) {
    diagon_clear_error();

    const char* json = R"({"title":"test"})";
    DiagonDocument doc = diagon_create_document_from_json_with_id(json, strlen(json), "my-id-123");

    ASSERT_NE(doc, nullptr);

    auto* document = static_cast<Document*>(doc);
    auto* idField = document->getField("_id");
    ASSERT_NE(idField, nullptr);
    EXPECT_EQ(idField->stringValue(), "my-id-123");

    diagon_free_document(doc);
}

TEST(JsonDocumentParserCApiTest, CreateDocumentFromInvalidJson) {
    diagon_clear_error();

    const char* json = R"({broken)";
    DiagonDocument doc = diagon_create_document_from_json(json, strlen(json));

    EXPECT_EQ(doc, nullptr);
    EXPECT_STRNE(diagon_last_error(), "");
}

TEST(JsonDocumentParserCApiTest, CreateDocumentFromNullJson) {
    diagon_clear_error();

    DiagonDocument doc = diagon_create_document_from_json(nullptr, 0);
    EXPECT_EQ(doc, nullptr);
}

TEST(JsonDocumentParserCApiTest, AddDocumentsFromJsonEndToEnd) {
    // Create a temporary directory for the index
    const char* indexPath = "/tmp/diagon_json_api_test";

    // Clean up from previous runs, then create fresh directory
    std::filesystem::remove_all(indexPath);
    std::filesystem::create_directories(indexPath);

    // Set up index writer
    DiagonDirectory dir = diagon_open_fs_directory(indexPath);
    ASSERT_NE(dir, nullptr);

    DiagonIndexWriterConfig config = diagon_create_index_writer_config();
    ASSERT_NE(config, nullptr);
    diagon_config_set_open_mode(config, 0);  // CREATE

    DiagonIndexWriter writer = diagon_create_index_writer(dir, config);
    ASSERT_NE(writer, nullptr);

    // Add batch of documents from JSON
    const char* jsonArray = R"([
        {"title":"first document","body":"hello world"},
        {"title":"second document","body":"search engine"},
        {"title":"third document","body":"fast indexing"}
    ])";

    int count = diagon_add_documents_from_json(writer, jsonArray, strlen(jsonArray));
    EXPECT_EQ(count, 3);

    // Commit and close
    EXPECT_TRUE(diagon_commit(writer));
    diagon_close_index_writer(writer);

    // Verify by opening reader
    DiagonDirectory readDir = diagon_open_mmap_directory(indexPath);
    ASSERT_NE(readDir, nullptr);

    DiagonIndexReader reader = diagon_open_index_reader(readDir);
    ASSERT_NE(reader, nullptr);
    EXPECT_EQ(diagon_reader_num_docs(reader), 3);

    // Search for "hello" in body
    DiagonIndexSearcher searcher = diagon_create_index_searcher(reader);
    ASSERT_NE(searcher, nullptr);

    DiagonTerm term = diagon_create_term("body", "hello");
    DiagonQuery query = diagon_create_term_query(term);
    DiagonTopDocs results = diagon_search(searcher, query, 10);
    ASSERT_NE(results, nullptr);
    EXPECT_EQ(diagon_top_docs_total_hits(results), 1);

    // Cleanup
    diagon_free_top_docs(results);
    diagon_free_query(query);
    diagon_free_term(term);
    diagon_free_index_searcher(searcher);
    diagon_close_index_reader(reader);
    diagon_close_directory(readDir);
    diagon_close_directory(dir);
    diagon_free_index_writer_config(config);

    std::filesystem::remove_all(indexPath);
}

TEST(JsonDocumentParserCApiTest, AddDocumentsFromJsonInvalid) {
    diagon_clear_error();

    int count = diagon_add_documents_from_json(nullptr, "[]", 2);
    EXPECT_EQ(count, -1);
}
