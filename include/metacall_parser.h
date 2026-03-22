/**
 * metacall-parser - Multi-Language Parser for MetaCall
 *
 * C API for static analysis of multi-language projects using Tree Sitter.
 * Extracts functions, classes, and builds dependency trees without runtime execution.
 *
 * GSoC Project: Implement Multi-Language Parser
 */

#ifndef METACALL_PARSER_H
#define METACALL_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* Opaque types */
typedef struct metacall_parser_s metacall_parser;
typedef struct metacall_result_s metacall_result;
typedef struct metacall_dep_graph_s metacall_dep_graph;

/* Language identifiers */
typedef enum {
    METACALL_LANG_UNKNOWN = 0,
    METACALL_LANG_PYTHON,
    METACALL_LANG_JAVASCRIPT,
    METACALL_LANG_RUBY,
} metacall_lang_id;

/* Symbol types */
typedef enum {
    METACALL_SYMBOL_FUNCTION,
    METACALL_SYMBOL_CLASS,
    METACALL_SYMBOL_METHOD,
} metacall_symbol_type;

/* Parsed symbol (function/class/method) */
typedef struct {
    metacall_symbol_type type;
    char *name;
    uint32_t line;
    uint32_t column;
    char *parent_class;  /* For methods: containing class name; NULL otherwise */
    char **param_names;  /* Parameter names (for functions/methods); NULL if none */
    size_t param_count;  /* Number of parameters */
    int is_async;        /* 1 if async def / async function, 0 otherwise */
} metacall_symbol;

/* Import/dependency */
typedef struct {
    char *module;       /* Imported module path */
    char *alias;        /* Optional alias (e.g., "np" for "numpy as np") */
    uint32_t line;
} metacall_import;

/* Parse result for a single file */
typedef struct {
    char *file_path;
    metacall_lang_id language;
    metacall_symbol *symbols;
    size_t symbol_count;
    metacall_import *imports;
    size_t import_count;
} metacall_file_result;

/* --- Parser lifecycle --- */

/**
 * Create a new parser instance.
 * Returns NULL on failure.
 */
metacall_parser *metacall_parser_create(void);

/**
 * Destroy parser and free all resources.
 */
void metacall_parser_destroy(metacall_parser *parser);

/* --- File parsing --- */

/**
 * Parse a single source file.
 * @param parser Parser instance
 * @param file_path Path to source file (used for language detection and result)
 * @param content File content (UTF-8); can be NULL to read from file_path
 * @param content_len Length of content, or 0 if content is NULL
 * @return Parse result (caller must free with metacall_result_free), or NULL on error
 */
metacall_result *metacall_parser_parse_file(metacall_parser *parser, const char *file_path,
                                   const char *content, size_t content_len);

/**
 * Free a parse result.
 */
void metacall_result_free(metacall_result *result);

/* --- Result accessors --- */

/**
 * Get the file result from a parse result.
 */
const metacall_file_result *metacall_result_get_file(metacall_result *result);

/**
 * Export parse result as JSON string.
 * Caller must free the returned string.
 */
char *metacall_result_to_json(const metacall_result *result);

/* --- Dependency graph --- */

/**
 * Build dependency graph for a directory.
 * Scans all supported source files and extracts import/require relationships.
 *
 * @param parser Parser instance
 * @param dir_path Root directory to scan
 * @param recursive 1 to scan subdirectories, 0 for top-level only
 * @return Dependency graph (caller must free with metacall_dep_graph_free), or NULL on error
 */
metacall_dep_graph *metacall_parser_build_deps(metacall_parser *parser, const char *dir_path, int recursive);

/**
 * Free dependency graph.
 */
void metacall_dep_graph_free(metacall_dep_graph *graph);

/**
 * Export dependency graph as JSON string.
 * Caller must free the returned string.
 */
char *metacall_dep_graph_to_json(metacall_dep_graph *graph);

/* --- Utility --- */

/**
 * Detect language from file path/extension.
 */
metacall_lang_id metacall_lang_from_path(const char *path);

/**
 * Get language name for display.
 */
const char *metacall_lang_name(metacall_lang_id lang);

/**
 * Get library version string.
 */
const char *metacall_parser_version(void);

/**
 * Get MetaCall loader tag for a language (e.g., "py", "node", "rb").
 */
const char *metacall_lang_tag(metacall_lang_id lang);

/**
 * Export parse result as JSON string in a MetaCall inspect-compatible format.
 * Caller must free the returned string.
 */
char *metacall_result_to_inspect_json(const metacall_result *result);

#ifdef __cplusplus
}
#endif

#endif /* METACALL_PARSER_H */
