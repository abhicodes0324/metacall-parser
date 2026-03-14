/**
 * MCP Parser - Multi-Language Parser for MetaCall
 *
 * C API for static analysis of multi-language projects using Tree Sitter.
 * Extracts functions, classes, and builds dependency trees without runtime execution.
 *
 * GSoC Project: Implement Multi-Language Parser
 */

#ifndef MCP_PARSER_H
#define MCP_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* Opaque types */
typedef struct mcp_parser_s mcp_parser;
typedef struct mcp_result_s mcp_result;
typedef struct mcp_dep_graph_s mcp_dep_graph;

/* Language identifiers */
typedef enum {
    MCP_LANG_UNKNOWN = 0,
    MCP_LANG_PYTHON,
    MCP_LANG_JAVASCRIPT,
    MCP_LANG_RUBY,
} mcp_lang_id;

/* Symbol types */
typedef enum {
    MCP_SYMBOL_FUNCTION,
    MCP_SYMBOL_CLASS,
    MCP_SYMBOL_METHOD,
} mcp_symbol_type;

/* Parsed symbol (function/class/method) */
typedef struct {
    mcp_symbol_type type;
    char *name;
    uint32_t line;
    uint32_t column;
    char *parent_class;  /* For methods: containing class name; NULL otherwise */
    char **param_names;  /* Parameter names (for functions/methods); NULL if none */
    size_t param_count;  /* Number of parameters */
    int is_async;        /* 1 if async def / async function, 0 otherwise */
} mcp_symbol;

/* Import/dependency */
typedef struct {
    char *module;       /* Imported module path */
    char *alias;        /* Optional alias (e.g., "np" for "numpy as np") */
    uint32_t line;
} mcp_import;

/* Parse result for a single file */
typedef struct {
    char *file_path;
    mcp_lang_id language;
    mcp_symbol *symbols;
    size_t symbol_count;
    mcp_import *imports;
    size_t import_count;
} mcp_file_result;

/* --- Parser lifecycle --- */

/**
 * Create a new parser instance.
 * Returns NULL on failure.
 */
mcp_parser *mcp_parser_create(void);

/**
 * Destroy parser and free all resources.
 */
void mcp_parser_destroy(mcp_parser *parser);

/* --- File parsing --- */

/**
 * Parse a single source file.
 * @param parser Parser instance
 * @param file_path Path to source file (used for language detection and result)
 * @param content File content (UTF-8); can be NULL to read from file_path
 * @param content_len Length of content, or 0 if content is NULL
 * @return Parse result (caller must free with mcp_result_free), or NULL on error
 */
mcp_result *mcp_parser_parse_file(mcp_parser *parser, const char *file_path,
                                   const char *content, size_t content_len);

/**
 * Free a parse result.
 */
void mcp_result_free(mcp_result *result);

/* --- Result accessors --- */

/**
 * Get the file result from a parse result.
 */
const mcp_file_result *mcp_result_get_file(mcp_result *result);

/**
 * Export parse result as JSON string.
 * Caller must free the returned string.
 */
char *mcp_result_to_json(const mcp_result *result);

/* --- Dependency graph --- */

/**
 * Build dependency graph for a directory.
 * Scans all supported source files and extracts import/require relationships.
 *
 * @param parser Parser instance
 * @param dir_path Root directory to scan
 * @param recursive 1 to scan subdirectories, 0 for top-level only
 * @return Dependency graph (caller must free with mcp_dep_graph_free), or NULL on error
 */
mcp_dep_graph *mcp_parser_build_deps(mcp_parser *parser, const char *dir_path, int recursive);

/**
 * Free dependency graph.
 */
void mcp_dep_graph_free(mcp_dep_graph *graph);

/**
 * Export dependency graph as JSON string.
 * Caller must free the returned string.
 */
char *mcp_dep_graph_to_json(mcp_dep_graph *graph);

/* --- Utility --- */

/**
 * Detect language from file path/extension.
 */
mcp_lang_id mcp_lang_from_path(const char *path);

/**
 * Get language name for display.
 */
const char *mcp_lang_name(mcp_lang_id lang);

/**
 * Get library version string.
 */
const char *mcp_parser_version(void);

/**
 * Get MetaCall loader tag for a language (e.g., "py", "node", "rb").
 */
const char *mcp_lang_tag(mcp_lang_id lang);

/**
 * Export parse result as JSON string in a MetaCall inspect-compatible format.
 * Caller must free the returned string.
 */
char *mcp_result_to_inspect_json(const mcp_result *result);

#ifdef __cplusplus
}
#endif

#endif /* MCP_PARSER_H */
