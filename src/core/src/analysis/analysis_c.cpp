#include "diagon/analysis_c.h"
#include "analysis/Analyzer.h"
#include "analysis/Token.h"
#include <string>
#include <vector>
#include <memory>
#include <cstring>

using namespace diagon::analysis;

// ==================== Thread-local Error Storage ====================

thread_local std::string g_last_error;

static void set_error(const std::string& error) {
    g_last_error = error;
}

static void clear_error() {
    g_last_error.clear();
}

// ==================== Opaque Type Implementations ====================

struct diagon_analyzer_t {
    std::unique_ptr<Analyzer> analyzer;
    std::string name;        // Cached for C API
    std::string description; // Cached for C API
};

struct diagon_token_t {
    Token token;
    std::string text;  // Cached for C API
    std::string type;  // Cached for C API
};

// ==================== Analyzer Creation ====================

extern "C" {

diagon_analyzer_t* diagon_create_standard_analyzer(void) {
    try {
        clear_error();
        auto analyzer = AnalyzerFactory::createStandard();
        if (!analyzer) {
            set_error("Failed to create standard analyzer");
            return nullptr;
        }

        auto* handle = new diagon_analyzer_t();
        handle->name = analyzer->name();
        handle->description = analyzer->description();
        handle->analyzer = std::move(analyzer);
        return handle;
    } catch (const std::exception& e) {
        set_error(std::string("Exception creating standard analyzer: ") + e.what());
        return nullptr;
    }
}

diagon_analyzer_t* diagon_create_simple_analyzer(void) {
    try {
        clear_error();
        auto analyzer = AnalyzerFactory::createSimple();
        if (!analyzer) {
            set_error("Failed to create simple analyzer");
            return nullptr;
        }

        auto* handle = new diagon_analyzer_t();
        handle->name = analyzer->name();
        handle->description = analyzer->description();
        handle->analyzer = std::move(analyzer);
        return handle;
    } catch (const std::exception& e) {
        set_error(std::string("Exception creating simple analyzer: ") + e.what());
        return nullptr;
    }
}

diagon_analyzer_t* diagon_create_whitespace_analyzer(void) {
    try {
        clear_error();
        auto analyzer = AnalyzerFactory::createWhitespace();
        if (!analyzer) {
            set_error("Failed to create whitespace analyzer");
            return nullptr;
        }

        auto* handle = new diagon_analyzer_t();
        handle->name = analyzer->name();
        handle->description = analyzer->description();
        handle->analyzer = std::move(analyzer);
        return handle;
    } catch (const std::exception& e) {
        set_error(std::string("Exception creating whitespace analyzer: ") + e.what());
        return nullptr;
    }
}

diagon_analyzer_t* diagon_create_keyword_analyzer(void) {
    try {
        clear_error();
        auto analyzer = AnalyzerFactory::createKeyword();
        if (!analyzer) {
            set_error("Failed to create keyword analyzer");
            return nullptr;
        }

        auto* handle = new diagon_analyzer_t();
        handle->name = analyzer->name();
        handle->description = analyzer->description();
        handle->analyzer = std::move(analyzer);
        return handle;
    } catch (const std::exception& e) {
        set_error(std::string("Exception creating keyword analyzer: ") + e.what());
        return nullptr;
    }
}

diagon_analyzer_t* diagon_create_chinese_analyzer(const char* dict_path) {
    try {
        clear_error();
        std::string path = dict_path ? dict_path : "";
        auto analyzer = AnalyzerFactory::createChinese(path);
        if (!analyzer) {
            set_error("Failed to create Chinese analyzer");
            return nullptr;
        }

        auto* handle = new diagon_analyzer_t();
        handle->name = analyzer->name();
        handle->description = analyzer->description();
        handle->analyzer = std::move(analyzer);
        return handle;
    } catch (const std::exception& e) {
        set_error(std::string("Exception creating Chinese analyzer: ") + e.what());
        return nullptr;
    }
}

diagon_analyzer_t* diagon_create_english_analyzer(void) {
    try {
        clear_error();
        auto analyzer = AnalyzerFactory::createEnglish();
        if (!analyzer) {
            set_error("Failed to create English analyzer");
            return nullptr;
        }

        auto* handle = new diagon_analyzer_t();
        handle->name = analyzer->name();
        handle->description = analyzer->description();
        handle->analyzer = std::move(analyzer);
        return handle;
    } catch (const std::exception& e) {
        set_error(std::string("Exception creating English analyzer: ") + e.what());
        return nullptr;
    }
}

diagon_analyzer_t* diagon_create_multilingual_analyzer(void) {
    try {
        clear_error();
        auto analyzer = AnalyzerFactory::createMultilingual();
        if (!analyzer) {
            set_error("Failed to create multilingual analyzer");
            return nullptr;
        }

        auto* handle = new diagon_analyzer_t();
        handle->name = analyzer->name();
        handle->description = analyzer->description();
        handle->analyzer = std::move(analyzer);
        return handle;
    } catch (const std::exception& e) {
        set_error(std::string("Exception creating multilingual analyzer: ") + e.what());
        return nullptr;
    }
}

diagon_analyzer_t* diagon_create_search_analyzer(void) {
    try {
        clear_error();
        auto analyzer = AnalyzerFactory::createSearch();
        if (!analyzer) {
            set_error("Failed to create search analyzer");
            return nullptr;
        }

        auto* handle = new diagon_analyzer_t();
        handle->name = analyzer->name();
        handle->description = analyzer->description();
        handle->analyzer = std::move(analyzer);
        return handle;
    } catch (const std::exception& e) {
        set_error(std::string("Exception creating search analyzer: ") + e.what());
        return nullptr;
    }
}

void diagon_destroy_analyzer(diagon_analyzer_t* analyzer) {
    delete analyzer;
}

// ==================== Text Analysis ====================

diagon_token_array_t* diagon_analyze_text(
    diagon_analyzer_t* analyzer,
    const char* text,
    size_t text_len) {

    if (!analyzer || !text) {
        set_error("Invalid analyzer or text");
        return nullptr;
    }

    try {
        clear_error();

        // Analyze text
        std::string input(text, text_len);
        std::vector<Token> tokens = analyzer->analyzer->analyze(input);

        // Create result array
        auto* result = new diagon_token_array_t();
        result->count = tokens.size();
        result->tokens = new diagon_token_t*[tokens.size()];

        // Convert tokens
        for (size_t i = 0; i < tokens.size(); i++) {
            auto* token = new diagon_token_t();
            token->token = tokens[i];
            token->text = tokens[i].getText();
            token->type = tokens[i].getType();
            result->tokens[i] = token;
        }

        return result;
    } catch (const std::exception& e) {
        set_error(std::string("Exception analyzing text: ") + e.what());
        return nullptr;
    }
}

void diagon_free_tokens(diagon_token_array_t* tokens) {
    if (!tokens) {
        return;
    }

    for (size_t i = 0; i < tokens->count; i++) {
        delete tokens->tokens[i];
    }
    delete[] tokens->tokens;
    delete tokens;
}

// ==================== Token Access ====================

const char* diagon_token_get_text(const diagon_token_t* token) {
    if (!token) {
        return nullptr;
    }
    return token->text.c_str();
}

size_t diagon_token_get_text_length(const diagon_token_t* token) {
    if (!token) {
        return 0;
    }
    return token->text.length();
}

int32_t diagon_token_get_position(const diagon_token_t* token) {
    if (!token) {
        return -1;
    }
    return token->token.getPosition();
}

int32_t diagon_token_get_start_offset(const diagon_token_t* token) {
    if (!token) {
        return -1;
    }
    return token->token.getStartOffset();
}

int32_t diagon_token_get_end_offset(const diagon_token_t* token) {
    if (!token) {
        return -1;
    }
    return token->token.getEndOffset();
}

const char* diagon_token_get_type(const diagon_token_t* token) {
    if (!token) {
        return nullptr;
    }
    return token->type.c_str();
}

// ==================== Analyzer Info ====================

const char* diagon_analyzer_get_name(const diagon_analyzer_t* analyzer) {
    if (!analyzer) {
        return nullptr;
    }
    return analyzer->name.c_str();
}

const char* diagon_analyzer_get_description(const diagon_analyzer_t* analyzer) {
    if (!analyzer) {
        return nullptr;
    }
    return analyzer->description.c_str();
}

// ==================== Error Handling ====================

const char* diagon_get_last_error(void) {
    if (g_last_error.empty()) {
        return nullptr;
    }
    return g_last_error.c_str();
}

// FIXME: Duplicate definition - using the one in diagon_c_api.cpp instead
// void diagon_clear_error(void) {
//     clear_error();
// }

} // extern "C"
