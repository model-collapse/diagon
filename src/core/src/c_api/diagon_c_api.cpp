/**
 * Diagon C API Implementation
 *
 * Bridges C API to Diagon C++ engine
 *
 * Copyright 2024 Quidditch Project
 * Licensed under the Apache License, Version 2.0
 */

#include "diagon/c_api/diagon_c_api.h"

// Diagon C++ headers
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/search/BooleanClause.h"
#include "diagon/search/BooleanQuery.h"
#include "diagon/search/DoubleRangeQuery.h"
#include "diagon/search/IndexSearcher.h"
#include "diagon/search/MatchAllDocsQuery.h"
#include "diagon/search/NumericRangeQuery.h"
#include "diagon/search/TermQuery.h"
#include "diagon/search/TopDocs.h"
#include "diagon/store/Directory.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/store/MMapDirectory.h"

#include <bit>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

// ==================== Error Handling ====================

static thread_local std::string g_last_error;

static void set_error(const std::string& error) {
    g_last_error = error;
}

static void set_error(const std::exception& e) {
    g_last_error = e.what();
}

extern "C" {

const char* diagon_last_error() {
    return g_last_error.c_str();
}

void diagon_clear_error() {
    g_last_error.clear();
}

// ==================== Directory Management ====================

DiagonDirectory diagon_open_fs_directory(const char* path) {
    try {
        auto dir = std::make_unique<diagon::store::FSDirectory>(path);
        return static_cast<DiagonDirectory>(dir.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

DiagonDirectory diagon_open_mmap_directory(const char* path) {
    try {
        auto dir = std::make_unique<diagon::store::MMapDirectory>(path);
        return static_cast<DiagonDirectory>(dir.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

void diagon_close_directory(DiagonDirectory dir) {
    if (dir) {
        delete static_cast<diagon::store::Directory*>(dir);
    }
}

// ==================== IndexWriterConfig ====================

DiagonIndexWriterConfig diagon_create_index_writer_config() {
    try {
        auto config = std::make_unique<diagon::index::IndexWriterConfig>();
        return static_cast<DiagonIndexWriterConfig>(config.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

void diagon_config_set_ram_buffer_size(DiagonIndexWriterConfig config, double size_mb) {
    if (config) {
        static_cast<diagon::index::IndexWriterConfig*>(config)->setRAMBufferSizeMB(size_mb);
    }
}

void diagon_config_set_max_buffered_docs(DiagonIndexWriterConfig config, int max_docs) {
    if (config) {
        static_cast<diagon::index::IndexWriterConfig*>(config)->setMaxBufferedDocs(max_docs);
    }
}

void diagon_config_set_open_mode(DiagonIndexWriterConfig config, int mode) {
    if (config) {
        diagon::index::IndexWriterConfig::OpenMode open_mode;
        switch (mode) {
            case 0:
                open_mode = diagon::index::IndexWriterConfig::OpenMode::CREATE;
                break;
            case 1:
                open_mode = diagon::index::IndexWriterConfig::OpenMode::APPEND;
                break;
            case 2:
                open_mode = diagon::index::IndexWriterConfig::OpenMode::CREATE_OR_APPEND;
                break;
            default:
                return;
        }
        static_cast<diagon::index::IndexWriterConfig*>(config)->setOpenMode(open_mode);
    }
}

void diagon_config_set_commit_on_close(DiagonIndexWriterConfig config, bool commit) {
    if (config) {
        static_cast<diagon::index::IndexWriterConfig*>(config)->setCommitOnClose(commit);
    }
}

void diagon_config_set_use_compound_file(DiagonIndexWriterConfig config, bool use_compound) {
    if (config) {
        static_cast<diagon::index::IndexWriterConfig*>(config)->setUseCompoundFile(use_compound);
    }
}

void diagon_free_index_writer_config(DiagonIndexWriterConfig config) {
    if (config) {
        delete static_cast<diagon::index::IndexWriterConfig*>(config);
    }
}

// ==================== IndexWriter ====================

DiagonIndexWriter diagon_create_index_writer(DiagonDirectory dir, DiagonIndexWriterConfig config) {
    if (!dir || !config) {
        set_error("Invalid directory or config");
        return nullptr;
    }

    try {
        auto* directory = static_cast<diagon::store::Directory*>(dir);
        auto* writer_config = static_cast<diagon::index::IndexWriterConfig*>(config);

        auto writer = std::make_unique<diagon::index::IndexWriter>(*directory, *writer_config);
        return static_cast<DiagonIndexWriter>(writer.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

bool diagon_add_document(DiagonIndexWriter writer, DiagonDocument doc) {
    if (!writer || !doc) {
        set_error("Invalid writer or document");
        return false;
    }

    try {
        auto* index_writer = static_cast<diagon::index::IndexWriter*>(writer);
        auto* document = static_cast<diagon::document::Document*>(doc);

        index_writer->addDocument(*document);
        return true;
    } catch (const std::exception& e) {
        set_error(e);
        return false;
    }
}

int diagon_add_documents(DiagonIndexWriter writer, DiagonDocument* docs, int count) {
    if (!writer || !docs || count <= 0) {
        set_error("Invalid writer, docs array, or count");
        return -1;
    }

    try {
        auto* index_writer = static_cast<diagon::index::IndexWriter*>(writer);

        // Build vector of document pointers
        std::vector<const diagon::document::Document*> doc_ptrs;
        doc_ptrs.reserve(count);
        for (int i = 0; i < count; i++) {
            if (!docs[i]) {
                set_error("NULL document at index " + std::to_string(i));
                return -1;
            }
            doc_ptrs.push_back(static_cast<const diagon::document::Document*>(docs[i]));
        }

        index_writer->addDocuments(doc_ptrs);
        return count;
    } catch (const std::exception& e) {
        set_error(e);
        return -1;
    }
}

bool diagon_flush(DiagonIndexWriter writer) {
    if (!writer) {
        set_error("Invalid writer");
        return false;
    }

    try {
        static_cast<diagon::index::IndexWriter*>(writer)->flush();
        return true;
    } catch (const std::exception& e) {
        set_error(e);
        return false;
    }
}

bool diagon_commit(DiagonIndexWriter writer) {
    if (!writer) {
        set_error("Invalid writer");
        return false;
    }

    try {
        static_cast<diagon::index::IndexWriter*>(writer)->commit();
        return true;
    } catch (const std::exception& e) {
        set_error(e);
        return false;
    }
}

bool diagon_force_merge(DiagonIndexWriter writer, int max_segments) {
    if (!writer) {
        set_error("Invalid writer");
        return false;
    }

    try {
        static_cast<diagon::index::IndexWriter*>(writer)->forceMerge(max_segments);
        return true;
    } catch (const std::exception& e) {
        set_error(e);
        return false;
    }
}

void diagon_close_index_writer(DiagonIndexWriter writer) {
    if (writer) {
        try {
            auto* index_writer = static_cast<diagon::index::IndexWriter*>(writer);
            index_writer->close();
            delete index_writer;
        } catch (const std::exception& e) {
            set_error(e);
        }
    }
}

// ==================== Document ====================

DiagonDocument diagon_create_document() {
    try {
        auto doc = std::make_unique<diagon::document::Document>();
        return static_cast<DiagonDocument>(doc.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

void diagon_document_add_field(DiagonDocument doc, DiagonField field) {
    if (!doc || !field) {
        return;
    }

    try {
        auto* document = static_cast<diagon::document::Document*>(doc);
        auto* index_field = static_cast<diagon::document::IndexableField*>(field);

        // Transfer ownership to document
        document->add(std::unique_ptr<diagon::document::IndexableField>(index_field));
    } catch (const std::exception& e) {
        set_error(e);
    }
}

void diagon_free_document(DiagonDocument doc) {
    if (doc) {
        delete static_cast<diagon::document::Document*>(doc);
    }
}

// ==================== Field Creation ====================

DiagonField diagon_create_text_field(const char* name, const char* value) {
    if (!name || !value) {
        set_error("Invalid field name or value");
        return nullptr;
    }

    try {
        // TextField: analyzed, indexed, stored
        auto field = std::make_unique<diagon::document::TextField>(name, value, true);
        return static_cast<DiagonField>(field.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

DiagonField diagon_create_string_field(const char* name, const char* value) {
    if (!name || !value) {
        set_error("Invalid field name or value");
        return nullptr;
    }

    try {
        // StringField: not analyzed, indexed, stored
        auto field = std::make_unique<diagon::document::StringField>(name, value, true);
        return static_cast<DiagonField>(field.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

DiagonField diagon_create_stored_field(const char* name, const char* value) {
    if (!name || !value) {
        set_error("Invalid field name or value");
        return nullptr;
    }

    try {
        // Stored-only field: not indexed, only stored
        auto field = std::make_unique<diagon::document::Field>(
            name, value, diagon::document::FieldType::storedOnly());
        return static_cast<DiagonField>(field.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

DiagonField diagon_create_long_field(const char* name, int64_t value) {
    if (!name) {
        set_error("Invalid field name");
        return nullptr;
    }

    try {
        auto field = std::make_unique<diagon::document::NumericDocValuesField>(name, value);
        return static_cast<DiagonField>(field.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

DiagonField diagon_create_double_field(const char* name, double value) {
    if (!name) {
        set_error("Invalid field name");
        return nullptr;
    }

    try {
        // Cast double to int64 for NumericDocValuesField
        // TODO: Add proper double field support
        auto field = std::make_unique<diagon::document::NumericDocValuesField>(
            name, static_cast<int64_t>(value));
        return static_cast<DiagonField>(field.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

DiagonField diagon_create_indexed_long_field(const char* name, int64_t value) {
    if (!name) {
        set_error("Invalid field name");
        return nullptr;
    }

    try {
        // Create FieldType for indexed numeric field
        diagon::document::FieldType fieldType;
        fieldType.indexOptions = diagon::index::IndexOptions::DOCS;  // Index for searching
        fieldType.stored = true;                                     // Store for retrieval
        fieldType.tokenized = false;                                 // Don't tokenize numbers
        fieldType.docValuesType =
            diagon::index::DocValuesType::NUMERIC;  // Enable doc values for range queries
        fieldType.numericType = diagon::document::NumericType::LONG;  // Track as LONG type

        // Create field with numeric value
        auto field = std::make_unique<diagon::document::Field>(name, value, fieldType);
        return static_cast<DiagonField>(field.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

DiagonField diagon_create_indexed_double_field(const char* name, double value) {
    if (!name) {
        set_error("Invalid field name");
        return nullptr;
    }

    try {
        // Create FieldType for indexed numeric field
        diagon::document::FieldType fieldType;
        fieldType.indexOptions = diagon::index::IndexOptions::DOCS;  // Index for searching
        fieldType.stored = true;                                     // Store for retrieval
        fieldType.tokenized = false;                                 // Don't tokenize numbers
        fieldType.docValuesType =
            diagon::index::DocValuesType::NUMERIC;  // Enable doc values for range queries
        fieldType.numericType = diagon::document::NumericType::DOUBLE;  // Track as DOUBLE type

        // Convert double to int64_t using bit_cast to preserve full precision
        // This stores the bit representation of the double in int64_t without loss
        int64_t longBits = std::bit_cast<int64_t>(value);

        // Create field with numeric value (stored as bit representation)
        auto field = std::make_unique<diagon::document::Field>(name, longBits, fieldType);
        return static_cast<DiagonField>(field.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

void diagon_free_field(DiagonField field) {
    if (field) {
        delete static_cast<diagon::document::IndexableField*>(field);
    }
}

// ==================== IndexReader ====================

DiagonIndexReader diagon_open_index_reader(DiagonDirectory dir) {
    if (!dir) {
        set_error("Invalid directory");
        return nullptr;
    }

    try {
        auto* directory = static_cast<diagon::store::Directory*>(dir);
        std::shared_ptr<diagon::index::DirectoryReader> reader =
            diagon::index::DirectoryReader::open(*directory);

        // Store shared_ptr in heap to manage lifetime
        auto* reader_ptr = new std::shared_ptr<diagon::index::DirectoryReader>(reader);
        return static_cast<DiagonIndexReader>(reader_ptr);
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

int64_t diagon_reader_num_docs(DiagonIndexReader reader) {
    if (!reader) {
        return 0;
    }

    try {
        auto* reader_ptr = static_cast<std::shared_ptr<diagon::index::DirectoryReader>*>(reader);
        return (*reader_ptr)->numDocs();
    } catch (const std::exception& e) {
        set_error(e);
        return 0;
    }
}

int64_t diagon_reader_max_doc(DiagonIndexReader reader) {
    if (!reader) {
        return 0;
    }

    try {
        auto* reader_ptr = static_cast<std::shared_ptr<diagon::index::DirectoryReader>*>(reader);
        return (*reader_ptr)->maxDoc();
    } catch (const std::exception& e) {
        set_error(e);
        return 0;
    }
}

void diagon_close_index_reader(DiagonIndexReader reader) {
    if (reader) {
        // Delete shared_ptr - reader will be closed when ref count reaches 0
        delete static_cast<std::shared_ptr<diagon::index::DirectoryReader>*>(reader);
    }
}

// ==================== IndexSearcher ====================

DiagonIndexSearcher diagon_create_index_searcher(DiagonIndexReader reader) {
    if (!reader) {
        set_error("Invalid reader");
        return nullptr;
    }

    try {
        auto* reader_ptr = static_cast<std::shared_ptr<diagon::index::DirectoryReader>*>(reader);
        auto searcher = std::make_unique<diagon::search::IndexSearcher>(**reader_ptr);
        return static_cast<DiagonIndexSearcher>(searcher.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

DiagonTopDocs diagon_search(DiagonIndexSearcher searcher, DiagonQuery query, int num_hits) {
    if (!searcher || !query) {
        set_error("Invalid searcher or query");
        return nullptr;
    }

    try {
        auto* index_searcher = static_cast<diagon::search::IndexSearcher*>(searcher);
        auto* search_query = static_cast<diagon::search::Query*>(query);

        diagon::search::TopDocs results = index_searcher->search(*search_query, num_hits);

        // Allocate TopDocs on heap to return
        auto* top_docs = new diagon::search::TopDocs(std::move(results));
        return static_cast<DiagonTopDocs>(top_docs);
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

void diagon_free_index_searcher(DiagonIndexSearcher searcher) {
    if (searcher) {
        delete static_cast<diagon::search::IndexSearcher*>(searcher);
    }
}

// ==================== Query Construction ====================

DiagonTerm diagon_create_term(const char* field, const char* text) {
    if (!field || !text) {
        set_error("Invalid field or text");
        return nullptr;
    }

    try {
        auto term = std::make_unique<diagon::search::Term>(field, text);
        return static_cast<DiagonTerm>(term.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

void diagon_free_term(DiagonTerm term) {
    if (term) {
        delete static_cast<diagon::search::Term*>(term);
    }
}

DiagonQuery diagon_create_term_query(DiagonTerm term) {
    if (!term) {
        set_error("Invalid term");
        return nullptr;
    }

    try {
        auto* search_term = static_cast<diagon::search::Term*>(term);
        auto query = std::make_unique<diagon::search::TermQuery>(*search_term);
        return static_cast<DiagonQuery>(query.release());
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

// MatchAllQuery implementation - now uses proper MatchAllQuery from diagon/search
DiagonQuery diagon_create_match_all_query() {
    try {
        auto query = std::make_unique<diagon::search::MatchAllQuery>();
        return query.release();
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

DiagonQuery diagon_create_numeric_range_query(const char* field_name, double lower_value,
                                              double upper_value, bool include_lower,
                                              bool include_upper) {
    if (!field_name) {
        set_error("Field name is required");
        return nullptr;
    }

    try {
        // Convert double to int64_t using bit_cast to preserve full precision
        // This allows the same function to work for both LONG and DOUBLE fields:
        // - For LONG fields: Pass integers as doubles (e.g., 100.0), they'll be
        //   converted and match int64_t comparisons
        // - For DOUBLE fields: Pass doubles (e.g., 150.5), bit representation is
        //   preserved and matches double comparisons
        int64_t lower = std::bit_cast<int64_t>(lower_value);
        int64_t upper = std::bit_cast<int64_t>(upper_value);

        auto query = std::make_unique<diagon::search::NumericRangeQuery>(
            std::string(field_name), lower, upper, include_lower, include_upper);

        return query.release();
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

DiagonQuery diagon_create_double_range_query(const char* field_name, double lower_value,
                                             double upper_value, bool include_lower,
                                             bool include_upper) {
    if (!field_name) {
        set_error("Field name is required");
        return nullptr;
    }

    try {
        auto query = std::make_unique<diagon::search::DoubleRangeQuery>(
            std::string(field_name), lower_value, upper_value, include_lower, include_upper);

        return query.release();
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

DiagonQuery diagon_create_bool_query() {
    try {
        // Create boolean query using builder
        auto builder = std::make_unique<diagon::search::BooleanQuery::Builder>();

        // Return the builder wrapped as a Query
        // Note: We'll store this as a special marker that gets converted to BooleanQuery on build
        return builder.release();
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

void diagon_bool_query_add_must(DiagonQuery bool_query, DiagonQuery clause) {
    if (!bool_query || !clause) {
        set_error("Both bool_query and clause are required");
        return;
    }

    try {
        // Cast to Builder
        auto* builder = static_cast<diagon::search::BooleanQuery::Builder*>(bool_query);
        auto* clause_query = static_cast<diagon::search::Query*>(clause);

        // Clone the clause query (since we need shared_ptr)
        std::shared_ptr<diagon::search::Query> clause_shared(clause_query->clone().release());

        builder->add(clause_shared, diagon::search::Occur::MUST);
    } catch (const std::exception& e) {
        set_error(e);
    }
}

void diagon_bool_query_add_should(DiagonQuery bool_query, DiagonQuery clause) {
    if (!bool_query || !clause) {
        set_error("Both bool_query and clause are required");
        return;
    }

    try {
        auto* builder = static_cast<diagon::search::BooleanQuery::Builder*>(bool_query);
        auto* clause_query = static_cast<diagon::search::Query*>(clause);

        std::shared_ptr<diagon::search::Query> clause_shared(clause_query->clone().release());

        builder->add(clause_shared, diagon::search::Occur::SHOULD);
    } catch (const std::exception& e) {
        set_error(e);
    }
}

void diagon_bool_query_add_filter(DiagonQuery bool_query, DiagonQuery clause) {
    if (!bool_query || !clause) {
        set_error("Both bool_query and clause are required");
        return;
    }

    try {
        auto* builder = static_cast<diagon::search::BooleanQuery::Builder*>(bool_query);
        auto* clause_query = static_cast<diagon::search::Query*>(clause);

        std::shared_ptr<diagon::search::Query> clause_shared(clause_query->clone().release());

        builder->add(clause_shared, diagon::search::Occur::FILTER);
    } catch (const std::exception& e) {
        set_error(e);
    }
}

void diagon_bool_query_add_must_not(DiagonQuery bool_query, DiagonQuery clause) {
    if (!bool_query || !clause) {
        set_error("Both bool_query and clause are required");
        return;
    }

    try {
        auto* builder = static_cast<diagon::search::BooleanQuery::Builder*>(bool_query);
        auto* clause_query = static_cast<diagon::search::Query*>(clause);

        std::shared_ptr<diagon::search::Query> clause_shared(clause_query->clone().release());

        builder->add(clause_shared, diagon::search::Occur::MUST_NOT);
    } catch (const std::exception& e) {
        set_error(e);
    }
}

void diagon_bool_query_set_minimum_should_match(DiagonQuery bool_query, int minimum) {
    if (!bool_query) {
        set_error("bool_query is required");
        return;
    }

    try {
        auto* builder = static_cast<diagon::search::BooleanQuery::Builder*>(bool_query);
        builder->setMinimumNumberShouldMatch(minimum);
    } catch (const std::exception& e) {
        set_error(e);
    }
}

DiagonQuery diagon_bool_query_build(DiagonQuery bool_query_builder) {
    if (!bool_query_builder) {
        set_error("bool_query_builder is required");
        return nullptr;
    }

    try {
        auto* builder = static_cast<diagon::search::BooleanQuery::Builder*>(bool_query_builder);

        // Build the query
        std::unique_ptr<diagon::search::BooleanQuery> query = builder->build();

        // Delete the builder (no longer needed)
        delete builder;

        // Return the built query
        return query.release();
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

void diagon_free_query(DiagonQuery query) {
    if (query) {
        // Note: This could be either a Query or a Builder
        // For safety, we cast to Query (which is the base class)
        delete static_cast<diagon::search::Query*>(query);
    }
}

// ==================== Search Results ====================

int64_t diagon_top_docs_total_hits(DiagonTopDocs top_docs) {
    if (!top_docs) {
        return 0;
    }

    return static_cast<diagon::search::TopDocs*>(top_docs)->totalHits.value;
}

float diagon_top_docs_max_score(DiagonTopDocs top_docs) {
    if (!top_docs) {
        return 0.0f;
    }

    return static_cast<diagon::search::TopDocs*>(top_docs)->maxScore;
}

int diagon_top_docs_score_docs_length(DiagonTopDocs top_docs) {
    if (!top_docs) {
        return 0;
    }

    return static_cast<int>(static_cast<diagon::search::TopDocs*>(top_docs)->scoreDocs.size());
}

DiagonScoreDoc diagon_top_docs_score_doc_at(DiagonTopDocs top_docs, int index) {
    if (!top_docs) {
        return nullptr;
    }

    try {
        auto* results = static_cast<diagon::search::TopDocs*>(top_docs);
        if (index < 0 || static_cast<size_t>(index) >= results->scoreDocs.size()) {
            set_error("Index out of bounds");
            return nullptr;
        }

        return static_cast<DiagonScoreDoc>(&results->scoreDocs[index]);
    } catch (const std::exception& e) {
        set_error(e);
        return nullptr;
    }
}

int diagon_score_doc_get_doc(DiagonScoreDoc score_doc) {
    if (!score_doc) {
        return -1;
    }

    return static_cast<diagon::search::ScoreDoc*>(score_doc)->doc;
}

float diagon_score_doc_get_score(DiagonScoreDoc score_doc) {
    if (!score_doc) {
        return 0.0f;
    }

    return static_cast<diagon::search::ScoreDoc*>(score_doc)->score;
}

void diagon_free_top_docs(DiagonTopDocs top_docs) {
    if (top_docs) {
        delete static_cast<diagon::search::TopDocs*>(top_docs);
    }
}

// ==================== Document Retrieval ====================

DiagonDocument diagon_reader_get_document(DiagonIndexReader reader, int doc_id) {
    fprintf(stderr, "[C API] diagon_reader_get_document called for doc_id=%d\n", doc_id);
    fflush(stderr);

    if (!reader) {
        set_error("Invalid reader");
        fprintf(stderr, "[C API] Invalid reader\n");
        fflush(stderr);
        return nullptr;
    }

    try {
        fprintf(stderr, "[C API] Starting document retrieval, reader=%p\n", reader);
        fflush(stderr);

        if (!reader) {
            fprintf(stderr, "[C API] ERROR: reader is NULL!\n");
            fflush(stderr);
            set_error("Reader is NULL");
            return nullptr;
        }

        // CRITICAL FIX: diagon_open_index_reader returns std::shared_ptr<DirectoryReader>*
        // We need to dereference the shared_ptr to get the actual DirectoryReader*
        fprintf(stderr, "[C API] Casting to shared_ptr\n");
        fflush(stderr);
        auto* reader_ptr = static_cast<std::shared_ptr<diagon::index::DirectoryReader>*>(reader);
        auto* dir_reader = reader_ptr->get();  // Get raw pointer from shared_ptr

        fprintf(stderr, "[C API] Got DirectoryReader=%p from shared_ptr\n",
                static_cast<void*>(dir_reader));
        fflush(stderr);

        if (!dir_reader) {
            fprintf(stderr, "[C API] ERROR: DirectoryReader is NULL!\n");
            fflush(stderr);
            set_error("DirectoryReader is NULL");
            return nullptr;
        }

        // Get leaf readers from the composite reader
        fprintf(stderr, "[C API] Calling leaves()\n");
        fflush(stderr);
        auto leaves = dir_reader->leaves();
        fprintf(stderr, "[C API] Got %zu leaves\n", leaves.size());
        fflush(stderr);

        if (leaves.empty()) {
            set_error("No leaves in directory reader");
            return nullptr;
        }

        // Find the correct segment containing the global doc_id
        // Lucene/Diagon use two-level document IDs:
        // 1. Global ID: 0-based across entire index
        // 2. Segment-local ID: 0-based within each segment
        // Each segment has docBase (starting global ID) and maxDoc (document count)

        diagon::index::LeafReader* leaf_reader = nullptr;
        int segment_local_doc_id = -1;

        fprintf(stderr, "[C API] Finding segment for global doc_id=%d\n", doc_id);
        fflush(stderr);

        for (const auto& ctx : leaves) {
            int maxDoc = ctx.reader->maxDoc();
            int docBase = ctx.docBase;

            fprintf(stderr,
                    "[C API] Checking segment ord=%d, docBase=%d, maxDoc=%d, range=[%d, %d)\n",
                    ctx.ord, docBase, maxDoc, docBase, docBase + maxDoc);
            fflush(stderr);

            // Check if global doc_id falls within this segment's range
            if (doc_id >= docBase && doc_id < docBase + maxDoc) {
                leaf_reader = ctx.reader;

                // Convert global ID to segment-local ID
                segment_local_doc_id = doc_id - docBase;

                fprintf(stderr,
                        "[C API] Found! Segment ord=%d contains global doc_id=%d (local_id=%d)\n",
                        ctx.ord, doc_id, segment_local_doc_id);
                fflush(stderr);
                break;
            }
        }

        if (!leaf_reader) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                     "Document ID %d not found in any segment (total segments: %zu)", doc_id,
                     leaves.size());
            set_error(error_msg);
            fprintf(stderr, "[C API] ERROR: %s\n", error_msg);
            fflush(stderr);
            return nullptr;
        }

        fprintf(stderr, "[C API] Got leaf_reader=%p for segment\n",
                static_cast<void*>(leaf_reader));
        fflush(stderr);

        // Get stored fields reader from the leaf reader
        fprintf(stderr, "[C API] Getting stored fields reader\n");
        fflush(stderr);
        auto* stored_fields_reader = leaf_reader->storedFieldsReader();

        fprintf(stderr, "[C API] Got stored_fields_reader=%p\n",
                static_cast<void*>(stored_fields_reader));
        fflush(stderr);

        if (!stored_fields_reader) {
            set_error("No stored fields reader available (no stored fields in index)");
            fprintf(stderr, "[C API] No stored fields reader available\n");
            fflush(stderr);
            return nullptr;
        }

        // Read document fields using SEGMENT-LOCAL doc_id
        fprintf(stderr,
                "[C API] Reading document fields for global_doc_id=%d, segment_local_doc_id=%d\n",
                doc_id, segment_local_doc_id);
        fflush(stderr);
        auto fields = stored_fields_reader->document(segment_local_doc_id);
        fprintf(stderr, "[C API] Got %zu fields\n", fields.size());
        fflush(stderr);

        // Create Diagon document and populate with stored fields
        fprintf(stderr, "[C API] Creating Document object\n");
        fflush(stderr);
        auto* doc = new diagon::document::Document();

        for (const auto& [field_name, field_value] : fields) {
            if (std::holds_alternative<std::string>(field_value)) {
                const auto& str_val = std::get<std::string>(field_value);
                // Create TextField with STORED type
                auto field = std::make_unique<diagon::document::TextField>(
                    field_name, str_val, diagon::document::TextField::TYPE_STORED);
                doc->add(std::move(field));
            } else if (std::holds_alternative<int32_t>(field_value)) {
                auto int_val = std::get<int32_t>(field_value);
                // Store as TextField with string representation
                auto field = std::make_unique<diagon::document::TextField>(
                    field_name, std::to_string(int_val), diagon::document::TextField::TYPE_STORED);
                doc->add(std::move(field));
            } else if (std::holds_alternative<int64_t>(field_value)) {
                auto int_val = std::get<int64_t>(field_value);
                auto field = std::make_unique<diagon::document::TextField>(
                    field_name, std::to_string(int_val), diagon::document::TextField::TYPE_STORED);
                doc->add(std::move(field));
            }
        }

        fprintf(stderr, "[C API] Document created successfully with fields\n");
        fflush(stderr);
        return doc;
    } catch (const std::exception& e) {
        fprintf(stderr, "[C API] Exception caught: %s\n", e.what());
        fflush(stderr);
        set_error(e);
        return nullptr;
    }
}

bool diagon_document_get_field_value(DiagonDocument doc, const char* field_name, char* out_value,
                                     size_t out_value_len) {
    if (!doc || !field_name || !out_value) {
        return false;
    }

    try {
        auto* document = static_cast<diagon::document::Document*>(doc);
        auto value = document->get(field_name);

        if (!value.has_value()) {
            return false;
        }

        strncpy(out_value, value->c_str(), out_value_len - 1);
        out_value[out_value_len - 1] = '\0';
        return true;
    } catch (const std::exception& e) {
        set_error(e);
        return false;
    }
}

bool diagon_document_get_long_value(DiagonDocument doc, const char* field_name,
                                    int64_t* out_value) {
    if (!doc || !field_name || !out_value) {
        return false;
    }

    try {
        // TODO: Implement numeric field retrieval when available
        set_error("Numeric field retrieval not yet implemented");
        return false;
    } catch (const std::exception& e) {
        set_error(e);
        return false;
    }
}

bool diagon_document_get_double_value(DiagonDocument doc, const char* field_name,
                                      double* out_value) {
    if (!doc || !field_name || !out_value) {
        return false;
    }

    try {
        // TODO: Implement numeric field retrieval when available
        set_error("Numeric field retrieval not yet implemented");
        return false;
    } catch (const std::exception& e) {
        set_error(e);
        return false;
    }
}

// ==================== Index Statistics ====================

int diagon_reader_get_segment_count(DiagonIndexReader reader) {
    if (!reader) {
        return 0;
    }

    try {
        auto* reader_ptr = static_cast<std::shared_ptr<diagon::index::DirectoryReader>*>(reader);
        return static_cast<int>((*reader_ptr)->getSequentialSubReaders().size());
    } catch (const std::exception& e) {
        set_error(e);
        return 0;
    }
}

int64_t diagon_directory_get_size(DiagonDirectory dir) {
    if (!dir) {
        return 0;
    }

    try {
        // TODO: Implement directory size calculation
        set_error("Directory size not yet implemented");
        return 0;
    } catch (const std::exception& e) {
        set_error(e);
        return 0;
    }
}

// ==================== Advanced: Terms/Postings ====================

DiagonTermsEnum diagon_reader_get_terms(DiagonIndexReader reader, const char* field) {
    // TODO: Implement when Terms/TermsEnum API available
    set_error("Terms enumeration not yet implemented in Diagon Phase 4");
    return nullptr;
}

bool diagon_terms_enum_next(DiagonTermsEnum terms_enum) {
    set_error("Terms enumeration not yet implemented in Diagon Phase 4");
    return false;
}

bool diagon_terms_enum_get_term(DiagonTermsEnum terms_enum, char* out_term, size_t out_term_len) {
    set_error("Terms enumeration not yet implemented in Diagon Phase 4");
    return false;
}

int diagon_terms_enum_doc_freq(DiagonTermsEnum terms_enum) {
    set_error("Terms enumeration not yet implemented in Diagon Phase 4");
    return 0;
}

void diagon_free_terms_enum(DiagonTermsEnum terms_enum) {
    // No-op
}

DiagonPostingsEnum diagon_terms_enum_get_postings(DiagonTermsEnum terms_enum) {
    set_error("Postings enumeration not yet implemented in Diagon Phase 4");
    return nullptr;
}

int diagon_postings_next_doc(DiagonPostingsEnum postings) {
    set_error("Postings enumeration not yet implemented in Diagon Phase 4");
    return -1;
}

int diagon_postings_freq(DiagonPostingsEnum postings) {
    set_error("Postings enumeration not yet implemented in Diagon Phase 4");
    return 0;
}

void diagon_free_postings_enum(DiagonPostingsEnum postings) {
    // No-op
}

}  // extern "C"
