/**
 * MCP Parser - Core parsing logic
 */

#include "mcp_parser.h"
#include <tree_sitter/api.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Language function declarations from grammar repos */
const TSLanguage *tree_sitter_python(void);
const TSLanguage *tree_sitter_javascript(void);

struct mcp_parser_s {
    TSParser *ts_parser;
};

struct mcp_result_s {
    mcp_file_result file;
};

mcp_parser *mcp_parser_create(void)
{
    mcp_parser *p = calloc(1, sizeof(*p));
    if (!p) return NULL;

    p->ts_parser = ts_parser_new();
    if (!p->ts_parser) {
        free(p);
        return NULL;
    }
    return p;
}

void mcp_parser_destroy(mcp_parser *parser)
{
    if (!parser) return;
    if (parser->ts_parser) {
        ts_parser_delete(parser->ts_parser);
    }
    free(parser);
}

mcp_lang_id mcp_lang_from_path(const char *path)
{
    if (!path) return MCP_LANG_UNKNOWN;
    const char *ext = strrchr(path, '.');
    if (!ext) return MCP_LANG_UNKNOWN;
    ext++;

    if (strcmp(ext, "py") == 0) return MCP_LANG_PYTHON;
    if (strcmp(ext, "js") == 0 || strcmp(ext, "mjs") == 0 ||
        strcmp(ext, "cjs") == 0) return MCP_LANG_JAVASCRIPT;
    if (strcmp(ext, "ts") == 0 || strcmp(ext, "tsx") == 0 ||
        strcmp(ext, "jsx") == 0) return MCP_LANG_JAVASCRIPT;  /* Use JS parser for TS/JSX */

    return MCP_LANG_UNKNOWN;
}

const char *mcp_lang_name(mcp_lang_id lang)
{
    switch (lang) {
        case MCP_LANG_PYTHON:    return "python";
        case MCP_LANG_JAVASCRIPT: return "javascript";
        default: return "unknown";
    }
}

/* Set parser language */
static int parser_set_lang(mcp_parser *parser, mcp_lang_id lang)
{
    const TSLanguage *ts_lang = NULL;
    switch (lang) {
        case MCP_LANG_PYTHON:    ts_lang = tree_sitter_python(); break;
        case MCP_LANG_JAVASCRIPT: ts_lang = tree_sitter_javascript(); break;
        default: return -1;
    }
    return ts_parser_set_language(parser->ts_parser, ts_lang) ? 0 : -1;
}

/* Read file into buffer; caller frees */
static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    *out_len = n;
    return buf;
}

/* Forward declarations for extractors */
extern int python_extract(const TSTree *tree, const char *source, mcp_file_result *out);
extern int js_extract(const TSTree *tree, const char *source, mcp_file_result *out);

mcp_result *mcp_parser_parse_file(mcp_parser *parser, const char *file_path,
                                   const char *content, size_t content_len)
{
    if (!parser || !file_path) return NULL;

    char *source = NULL;
    int alloc_source = 0;

    if (content && content_len > 0) {
        source = (char *)content;
    } else {
        source = read_file(file_path, &content_len);
        alloc_source = 1;
        if (!source) return NULL;
    }

    mcp_lang_id lang = mcp_lang_from_path(file_path);
    if (lang == MCP_LANG_UNKNOWN) {
        if (alloc_source) free(source);
        return NULL;
    }

    if (parser_set_lang(parser, lang) != 0) {
        if (alloc_source) free(source);
        return NULL;
    }

    TSTree *tree = ts_parser_parse_string(parser->ts_parser, NULL, source, (uint32_t)content_len);
    if (!tree) {
        if (alloc_source) free(source);
        return NULL;
    }

    mcp_result *result = calloc(1, sizeof(*result));
    if (!result) {
        ts_tree_delete(tree);
        if (alloc_source) free(source);
        return NULL;
    }

    result->file.file_path = strdup(file_path);
    result->file.language = lang;
    result->file.symbols = NULL;
    result->file.symbol_count = 0;
    result->file.imports = NULL;
    result->file.import_count = 0;

    int ok = 0;
    switch (lang) {
        case MCP_LANG_PYTHON:    ok = python_extract(tree, source, &result->file); break;
        case MCP_LANG_JAVASCRIPT: ok = js_extract(tree, source, &result->file); break;
        default: break;
    }

    ts_tree_delete(tree);
    if (alloc_source) free(source);

    if (ok != 0) {
        mcp_result_free(result);
        return NULL;
    }
    return result;
}

void mcp_result_free(mcp_result *result)
{
    if (!result) return;
    mcp_file_result *f = &result->file;
    if (f->file_path) free(f->file_path);
    for (size_t i = 0; i < f->symbol_count; i++) {
        if (f->symbols[i].name) free(f->symbols[i].name);
        if (f->symbols[i].parent_class) free(f->symbols[i].parent_class);
    }
    if (f->symbols) free(f->symbols);
    for (size_t i = 0; i < f->import_count; i++) {
        if (f->imports[i].module) free(f->imports[i].module);
        if (f->imports[i].alias) free(f->imports[i].alias);
    }
    if (f->imports) free(f->imports);
    free(result);
}

const mcp_file_result *mcp_result_get_file(mcp_result *result)
{
    return result ? &result->file : NULL;
}
