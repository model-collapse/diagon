/**
 * Diagon C API - Complete wrapper for CGO integration
 *
 * This C API exposes ALL Diagon functionality:
 * - Index creation and management
 * - Document indexing with all field types
 * - Full-text search with multiple query types
 * - Result iteration and scoring
 * - Directory management
 *
 * Copyright 2024 Quidditch Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef DIAGON_C_API_H
#define DIAGON_C_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ==================== Opaque Handle Types ====================

typedef void* DiagonDirectory;
typedef void* DiagonIndexWriter;
typedef void* DiagonIndexWriterConfig;
typedef void* DiagonIndexReader;
typedef void* DiagonIndexSearcher;
typedef void* DiagonDocument;
typedef void* DiagonField;
typedef void* DiagonQuery;
typedef void* DiagonTopDocs;
typedef void* DiagonScoreDoc;
typedef void* DiagonTerm;
typedef void* DiagonTermsEnum;
typedef void* DiagonPostingsEnum;

// ==================== Error Handling ====================

/**
 * Get last error message (thread-local)
 * @return Error message string (do not free)
 */
const char* diagon_last_error();

/**
 * Clear last error
 */
void diagon_clear_error();

// ==================== Directory Management ====================

/**
 * Open an FSDirectory at the given path
 * @param path Directory path
 * @return Directory handle or NULL on error
 */
DiagonDirectory diagon_open_fs_directory(const char* path);

/**
 * Open an MMapDirectory (memory-mapped) at the given path
 * @param path Directory path
 * @return Directory handle or NULL on error
 */
DiagonDirectory diagon_open_mmap_directory(const char* path);

/**
 * Close and free directory
 */
void diagon_close_directory(DiagonDirectory dir);

// ==================== IndexWriterConfig ====================

/**
 * Create default IndexWriterConfig
 * @return Config handle
 */
DiagonIndexWriterConfig diagon_create_index_writer_config();

/**
 * Set RAM buffer size in MB (default: 16)
 * @param config Config handle
 * @param size_mb Buffer size in megabytes
 */
void diagon_config_set_ram_buffer_size(DiagonIndexWriterConfig config, double size_mb);

/**
 * Set max buffered documents (default: -1, disabled)
 * @param config Config handle
 * @param max_docs Max documents before flush
 */
void diagon_config_set_max_buffered_docs(DiagonIndexWriterConfig config, int max_docs);

/**
 * Set open mode
 * @param config Config handle
 * @param mode 0=CREATE, 1=APPEND, 2=CREATE_OR_APPEND
 */
void diagon_config_set_open_mode(DiagonIndexWriterConfig config, int mode);

/**
 * Set commit on close (default: true)
 * @param config Config handle
 * @param commit Whether to commit on close
 */
void diagon_config_set_commit_on_close(DiagonIndexWriterConfig config, bool commit);

/**
 * Set use compound file format (default: true)
 * @param config Config handle
 * @param use_compound Use compound file
 */
void diagon_config_set_use_compound_file(DiagonIndexWriterConfig config, bool use_compound);

/**
 * Free IndexWriterConfig
 */
void diagon_free_index_writer_config(DiagonIndexWriterConfig config);

// ==================== IndexWriter - Document Indexing ====================

/**
 * Create IndexWriter
 * @param dir Directory handle
 * @param config Config handle
 * @return IndexWriter handle or NULL on error
 */
DiagonIndexWriter diagon_create_index_writer(DiagonDirectory dir, DiagonIndexWriterConfig config);

/**
 * Add document to index
 * @param writer IndexWriter handle
 * @param doc Document handle
 * @return true on success, false on error
 */
bool diagon_add_document(DiagonIndexWriter writer, DiagonDocument doc);

/**
 * Flush buffered documents to disk
 * @param writer IndexWriter handle
 * @return true on success, false on error
 */
bool diagon_flush(DiagonIndexWriter writer);

/**
 * Commit all pending changes
 * @param writer IndexWriter handle
 * @return true on success, false on error
 */
bool diagon_commit(DiagonIndexWriter writer);

/**
 * Force merge segments (optimize index)
 * @param writer IndexWriter handle
 * @param max_segments Target number of segments
 * @return true on success, false on error
 */
bool diagon_force_merge(DiagonIndexWriter writer, int max_segments);

/**
 * Close IndexWriter (commits if configured)
 * @param writer IndexWriter handle
 */
void diagon_close_index_writer(DiagonIndexWriter writer);

// ==================== Document Construction ====================

/**
 * Create empty document
 * @return Document handle
 */
DiagonDocument diagon_create_document();

/**
 * Add field to document
 * @param doc Document handle
 * @param field Field handle (ownership transferred)
 */
void diagon_document_add_field(DiagonDocument doc, DiagonField field);

/**
 * Free document (does not free fields)
 */
void diagon_free_document(DiagonDocument doc);

// ==================== Field Creation ====================

/**
 * Create text field (analyzed, indexed, stored)
 * @param name Field name
 * @param value Field value
 * @return Field handle
 */
DiagonField diagon_create_text_field(const char* name, const char* value);

/**
 * Create string field (not analyzed, indexed, stored)
 * @param name Field name
 * @param value Field value
 * @return Field handle
 */
DiagonField diagon_create_string_field(const char* name, const char* value);

/**
 * Create stored field (stored only, not indexed)
 * @param name Field name
 * @param value Field value
 * @return Field handle
 */
DiagonField diagon_create_stored_field(const char* name, const char* value);

/**
 * Create numeric field (int64)
 * @param name Field name
 * @param value Numeric value
 * @return Field handle
 */
DiagonField diagon_create_long_field(const char* name, int64_t value);

/**
 * Create numeric field (double)
 * @param name Field name
 * @param value Numeric value
 * @return Field handle
 */
DiagonField diagon_create_double_field(const char* name, double value);

/**
 * Create indexed numeric field (int64) - searchable with range queries
 * This creates a field that can be searched, unlike diagon_create_long_field which only stores doc values
 * @param name Field name
 * @param value Numeric value
 * @return Field handle
 */
DiagonField diagon_create_indexed_long_field(const char* name, int64_t value);

/**
 * Create indexed numeric field (double) - searchable with range queries
 * This creates a field that can be searched, unlike diagon_create_double_field which only stores doc values
 * @param name Field name
 * @param value Numeric value
 * @return Field handle
 */
DiagonField diagon_create_indexed_double_field(const char* name, double value);

/**
 * Free field
 */
void diagon_free_field(DiagonField field);

// ==================== IndexReader - Reading Index ====================

/**
 * Open DirectoryReader
 * @param dir Directory handle
 * @return IndexReader handle or NULL on error
 */
DiagonIndexReader diagon_open_index_reader(DiagonDirectory dir);

/**
 * Get number of documents in index
 * @param reader IndexReader handle
 * @return Number of documents
 */
int64_t diagon_reader_num_docs(DiagonIndexReader reader);

/**
 * Get maximum document ID
 * @param reader IndexReader handle
 * @return Max doc ID
 */
int64_t diagon_reader_max_doc(DiagonIndexReader reader);

/**
 * Close IndexReader
 */
void diagon_close_index_reader(DiagonIndexReader reader);

// ==================== IndexSearcher - Search Execution ====================

/**
 * Create IndexSearcher
 * @param reader IndexReader handle
 * @return IndexSearcher handle
 */
DiagonIndexSearcher diagon_create_index_searcher(DiagonIndexReader reader);

/**
 * Execute search query
 * @param searcher IndexSearcher handle
 * @param query Query handle
 * @param num_hits Number of top hits to return
 * @return TopDocs handle or NULL on error
 */
DiagonTopDocs diagon_search(DiagonIndexSearcher searcher, DiagonQuery query, int num_hits);

/**
 * Free IndexSearcher
 */
void diagon_free_index_searcher(DiagonIndexSearcher searcher);

// ==================== Query Construction ====================

/**
 * Create Term for queries
 * @param field Field name
 * @param text Term text
 * @return Term handle
 */
DiagonTerm diagon_create_term(const char* field, const char* text);

/**
 * Free Term
 */
void diagon_free_term(DiagonTerm term);

/**
 * Create TermQuery (exact term match)
 * @param term Term handle
 * @return Query handle
 */
DiagonQuery diagon_create_term_query(DiagonTerm term);

/**
 * Create match-all query
 * @return Query handle
 */
DiagonQuery diagon_create_match_all_query();

/**
 * Create numeric range query (int64)
 * @param field_name Field name
 * @param lower_value Lower bound value
 * @param upper_value Upper bound value
 * @param include_lower true for >= (gte), false for > (gt)
 * @param include_upper true for <= (lte), false for < (lt)
 * @return Query handle or NULL on error
 */
DiagonQuery diagon_create_numeric_range_query(
    const char* field_name,
    double lower_value,
    double upper_value,
    bool include_lower,
    bool include_upper
);

/**
 * Create double range query (double precision)
 * Queries documents where double field value is in range [lower_value, upper_value]
 *
 * @param field_name Field to query
 * @param lower_value Lower bound
 * @param upper_value Upper bound
 * @param include_lower Include lower bound? (true for >=, false for >)
 * @param include_upper Include upper bound? (true for <=, false for <)
 * @return Query handle or NULL on error
 */
DiagonQuery diagon_create_double_range_query(
    const char* field_name,
    double lower_value,
    double upper_value,
    bool include_lower,
    bool include_upper
);

/**
 * Create boolean query
 * @return Query handle or NULL on error
 */
DiagonQuery diagon_create_bool_query();

/**
 * Add MUST clause to boolean query
 * Must clauses are AND'ed together and contribute to score
 * @param bool_query Boolean query handle
 * @param clause Clause query handle (will be cloned)
 */
void diagon_bool_query_add_must(DiagonQuery bool_query, DiagonQuery clause);

/**
 * Add SHOULD clause to boolean query
 * Should clauses are OR'ed together and contribute to score
 * @param bool_query Boolean query handle
 * @param clause Clause query handle (will be cloned)
 */
void diagon_bool_query_add_should(DiagonQuery bool_query, DiagonQuery clause);

/**
 * Add FILTER clause to boolean query
 * Filter clauses are AND'ed together but do NOT contribute to score
 * @param bool_query Boolean query handle
 * @param clause Clause query handle (will be cloned)
 */
void diagon_bool_query_add_filter(DiagonQuery bool_query, DiagonQuery clause);

/**
 * Add MUST_NOT clause to boolean query
 * Must_not clauses exclude matching documents
 * @param bool_query Boolean query handle
 * @param clause Clause query handle (will be cloned)
 */
void diagon_bool_query_add_must_not(DiagonQuery bool_query, DiagonQuery clause);

/**
 * Set minimum number of SHOULD clauses that must match
 * Default is 0 if must/filter clauses exist, otherwise 1
 * @param bool_query Boolean query handle
 * @param minimum Minimum number of should clauses that must match
 */
void diagon_bool_query_set_minimum_should_match(DiagonQuery bool_query, int minimum);

/**
 * Build the boolean query from builder
 * This must be called after adding all clauses
 * @param bool_query_builder Boolean query builder handle
 * @return Built query handle or NULL on error
 */
DiagonQuery diagon_bool_query_build(DiagonQuery bool_query_builder);

/**
 * Free Query
 */
void diagon_free_query(DiagonQuery query);

// ==================== Search Results ====================

/**
 * Get total hits from TopDocs
 * @param top_docs TopDocs handle
 * @return Total number of hits
 */
int64_t diagon_top_docs_total_hits(DiagonTopDocs top_docs);

/**
 * Get max score from TopDocs
 * @param top_docs TopDocs handle
 * @return Maximum score
 */
float diagon_top_docs_max_score(DiagonTopDocs top_docs);

/**
 * Get number of score docs in TopDocs
 * @param top_docs TopDocs handle
 * @return Number of score docs
 */
int diagon_top_docs_score_docs_length(DiagonTopDocs top_docs);

/**
 * Get ScoreDoc at index
 * @param top_docs TopDocs handle
 * @param index Index in score docs array
 * @return ScoreDoc handle (borrowed, do not free)
 */
DiagonScoreDoc diagon_top_docs_score_doc_at(DiagonTopDocs top_docs, int index);

/**
 * Get document ID from ScoreDoc
 * @param score_doc ScoreDoc handle
 * @return Document ID
 */
int diagon_score_doc_get_doc(DiagonScoreDoc score_doc);

/**
 * Get score from ScoreDoc
 * @param score_doc ScoreDoc handle
 * @return Score value
 */
float diagon_score_doc_get_score(DiagonScoreDoc score_doc);

/**
 * Free TopDocs
 */
void diagon_free_top_docs(DiagonTopDocs top_docs);

// ==================== Document Retrieval ====================

/**
 * Get stored document by ID
 * @param reader IndexReader handle
 * @param doc_id Document ID
 * @return Document handle or NULL on error
 */
DiagonDocument diagon_reader_get_document(DiagonIndexReader reader, int doc_id);

/**
 * Get field value from document
 * @param doc Document handle
 * @param field_name Field name
 * @param out_value Output buffer for value
 * @param out_value_len Size of output buffer
 * @return true if field found, false otherwise
 */
bool diagon_document_get_field_value(DiagonDocument doc, const char* field_name,
                                     char* out_value, size_t out_value_len);

/**
 * Get numeric field value (int64)
 * @param doc Document handle
 * @param field_name Field name
 * @param out_value Output for numeric value
 * @return true if field found, false otherwise
 */
bool diagon_document_get_long_value(DiagonDocument doc, const char* field_name,
                                    int64_t* out_value);

/**
 * Get numeric field value (double)
 * @param doc Document handle
 * @param field_name Field name
 * @param out_value Output for numeric value
 * @return true if field found, false otherwise
 */
bool diagon_document_get_double_value(DiagonDocument doc, const char* field_name,
                                      double* out_value);

// ==================== Index Statistics ====================

/**
 * Get number of segments in index
 * @param reader IndexReader handle
 * @return Number of segments
 */
int diagon_reader_get_segment_count(DiagonIndexReader reader);

/**
 * Get index size in bytes
 * @param dir Directory handle
 * @return Size in bytes
 */
int64_t diagon_directory_get_size(DiagonDirectory dir);

// ==================== Advanced: Terms Enumeration ====================

/**
 * Get terms enum for field
 * @param reader IndexReader handle
 * @param field Field name
 * @return TermsEnum handle or NULL if field not found
 */
DiagonTermsEnum diagon_reader_get_terms(DiagonIndexReader reader, const char* field);

/**
 * Move to next term
 * @param terms_enum TermsEnum handle
 * @return true if moved to next term, false if exhausted
 */
bool diagon_terms_enum_next(DiagonTermsEnum terms_enum);

/**
 * Get current term text
 * @param terms_enum TermsEnum handle
 * @param out_term Output buffer
 * @param out_term_len Size of output buffer
 * @return true on success, false on error
 */
bool diagon_terms_enum_get_term(DiagonTermsEnum terms_enum, char* out_term, size_t out_term_len);

/**
 * Get document frequency of current term
 * @param terms_enum TermsEnum handle
 * @return Document frequency
 */
int diagon_terms_enum_doc_freq(DiagonTermsEnum terms_enum);

/**
 * Free TermsEnum
 */
void diagon_free_terms_enum(DiagonTermsEnum terms_enum);

// ==================== Advanced: Postings Enumeration ====================

/**
 * Get postings for current term
 * @param terms_enum TermsEnum handle
 * @return PostingsEnum handle
 */
DiagonPostingsEnum diagon_terms_enum_get_postings(DiagonTermsEnum terms_enum);

/**
 * Move to next document in postings
 * @param postings PostingsEnum handle
 * @return Document ID or -1 if exhausted
 */
int diagon_postings_next_doc(DiagonPostingsEnum postings);

/**
 * Get term frequency in current document
 * @param postings PostingsEnum handle
 * @return Term frequency
 */
int diagon_postings_freq(DiagonPostingsEnum postings);

/**
 * Free PostingsEnum
 */
void diagon_free_postings_enum(DiagonPostingsEnum postings);

#ifdef __cplusplus
}
#endif

#endif // DIAGON_C_API_H
