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
const TSLanguage *tree_sitter_ruby(void);

/* Forward declarations for extractors */
typedef int (*extract_fn)(const TSTree *tree, const char *source, mcp_file_result *out);

extern int python_extract(const TSTree *tree, const char *source, mcp_file_result *out);
extern int js_extract(const TSTree *tree, const char *source, mcp_file_result *out);
extern int ruby_extract(const TSTree *tree, const char *source, mcp_file_result *out);

typedef struct {
    mcp_lang_id   id;
    const char   *name;
    const char   *tag;          /* MetaCall loader tag */
    const char   *extensions[8];
    const TSLanguage *(*grammar)(void);
    extract_fn    extract;
} mcp_lang_def;

static const mcp_lang_def LANGUAGES[] = {
    { MCP_LANG_PYTHON,     "python",     "py",
      { "py", NULL },
      tree_sitter_python,  python_extract },
    { MCP_LANG_JAVASCRIPT, "javascript", "node",
      { "js", "mjs", "cjs", "ts", "tsx", "jsx", NULL },
      tree_sitter_javascript, js_extract },
    { MCP_LANG_RUBY,       "ruby",       "rb",
      { "rb", NULL },
      tree_sitter_ruby,    ruby_extract }
};

#define LANG_COUNT (sizeof(LANGUAGES) / sizeof(LANGUAGES[0]))

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

    for (size_t i = 0; i < LANG_COUNT; i++) {
        const mcp_lang_def *lang = &LANGUAGES[i];
        for (size_t j = 0; lang->extensions[j]; j++) {
            if (strcmp(ext, lang->extensions[j]) == 0) {
                return lang->id;
            }
        }
    }

    return MCP_LANG_UNKNOWN;
}

const char *mcp_lang_name(mcp_lang_id lang)
{
    for (size_t i = 0; i < LANG_COUNT; i++) {
        if (LANGUAGES[i].id == lang)
            return LANGUAGES[i].name;
    }
    return "unknown";
}

const char *mcp_lang_tag(mcp_lang_id lang)
{
    for (size_t i = 0; i < LANG_COUNT; i++) {
        if (LANGUAGES[i].id == lang)
            return LANGUAGES[i].tag;
    }
    return "unknown";
}

/* Set parser language */
static int parser_set_lang(mcp_parser *parser, mcp_lang_id lang)
{
    const TSLanguage *ts_lang = NULL;
    for (size_t i = 0; i < LANG_COUNT; i++) {
        if (LANGUAGES[i].id == lang) {
            ts_lang = LANGUAGES[i].grammar();
            break;
        }
    }
    if (!ts_lang) return -1;
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

    int ok = -1;
    for (size_t i = 0; i < LANG_COUNT; i++) {
        if (LANGUAGES[i].id == lang) {
            ok = LANGUAGES[i].extract(tree, source, &result->file);
            break;
        }
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
        if (f->symbols[i].param_names) {
            for (size_t p = 0; p < f->symbols[i].param_count; p++)
                free(f->symbols[i].param_names[p]);
            free(f->symbols[i].param_names);
        }
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
