#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ==================== Opaque Types ====================

/**
 * Opaque handle to an Analyzer instance.
 */
typedef struct diagon_analyzer_t diagon_analyzer_t;

/**
 * Opaque handle to a Token instance.
 */
typedef struct diagon_token_t diagon_token_t;

/**
 * Token array for returning analysis results.
 */
typedef struct {
    diagon_token_t** tokens;  // Array of token pointers
    size_t count;             // Number of tokens
} diagon_token_array_t;

// ==================== Analyzer Creation ====================

/**
 * Create a standard analyzer.
 * Returns NULL on error.
 */
diagon_analyzer_t* diagon_create_standard_analyzer(void);

/**
 * Create a simple analyzer.
 * Returns NULL on error.
 */
diagon_analyzer_t* diagon_create_simple_analyzer(void);

/**
 * Create a whitespace analyzer.
 * Returns NULL on error.
 */
diagon_analyzer_t* diagon_create_whitespace_analyzer(void);

/**
 * Create a keyword analyzer.
 * Returns NULL on error.
 */
diagon_analyzer_t* diagon_create_keyword_analyzer(void);

/**
 * Create a Chinese analyzer.
 *
 * @param dict_path Path to Jieba dictionary directory (NULL = use default)
 * Returns NULL on error.
 */
diagon_analyzer_t* diagon_create_chinese_analyzer(const char* dict_path);

/**
 * Create an English analyzer.
 * Returns NULL on error.
 */
diagon_analyzer_t* diagon_create_english_analyzer(void);

/**
 * Create a multilingual analyzer.
 * Returns NULL on error.
 */
diagon_analyzer_t* diagon_create_multilingual_analyzer(void);

/**
 * Create a search analyzer.
 * Returns NULL on error.
 */
diagon_analyzer_t* diagon_create_search_analyzer(void);

/**
 * Destroy an analyzer and free resources.
 */
void diagon_destroy_analyzer(diagon_analyzer_t* analyzer);

// ==================== Text Analysis ====================

/**
 * Analyze text and return tokens.
 *
 * @param analyzer Analyzer instance
 * @param text Text to analyze (UTF-8 encoded)
 * @param text_len Length of text in bytes
 * @return Token array (must be freed with diagon_free_tokens)
 */
diagon_token_array_t* diagon_analyze_text(diagon_analyzer_t* analyzer, const char* text,
                                          size_t text_len);

/**
 * Free token array returned by diagon_analyze_text.
 */
void diagon_free_tokens(diagon_token_array_t* tokens);

// ==================== Token Access ====================

/**
 * Get token text.
 * Returns pointer to internal string (do not free).
 */
const char* diagon_token_get_text(const diagon_token_t* token);

/**
 * Get token text length.
 */
size_t diagon_token_get_text_length(const diagon_token_t* token);

/**
 * Get token position in stream.
 */
int32_t diagon_token_get_position(const diagon_token_t* token);

/**
 * Get token start offset in original text.
 */
int32_t diagon_token_get_start_offset(const diagon_token_t* token);

/**
 * Get token end offset in original text.
 */
int32_t diagon_token_get_end_offset(const diagon_token_t* token);

/**
 * Get token type.
 * Returns pointer to internal string (do not free).
 */
const char* diagon_token_get_type(const diagon_token_t* token);

// ==================== Analyzer Info ====================

/**
 * Get analyzer name.
 * Returns pointer to internal string (do not free).
 */
const char* diagon_analyzer_get_name(const diagon_analyzer_t* analyzer);

/**
 * Get analyzer description.
 * Returns pointer to internal string (do not free).
 */
const char* diagon_analyzer_get_description(const diagon_analyzer_t* analyzer);

// ==================== Error Handling ====================

/**
 * Get last error message.
 * Returns NULL if no error.
 * String is valid until next API call.
 */
const char* diagon_get_last_error(void);

/**
 * Clear last error.
 */
void diagon_clear_error(void);

#ifdef __cplusplus
}
#endif
