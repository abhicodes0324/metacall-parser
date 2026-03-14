/**
 * Ruby AST extractor using Tree Sitter
 * Extracts: methods (def), classes, and require/require_relative calls.
 */

#include "mcp_parser.h"
#include <tree_sitter/api.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SYMBOLS 256
#define MAX_IMPORTS 128

static char *copy_text(const char *source, uint32_t start, uint32_t end)
{
    size_t len = end - start;
    char *s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, source + start, len);
    s[len] = '\0';
    return s;
}

static void get_node_text(TSNode node, const char *source, char *out, size_t out_size)
{
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    size_t len = end - start;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, source + start, len);
    out[len] = '\0';
}

static int add_symbol(mcp_file_result *out, mcp_symbol_type type,
                      const char *name, uint32_t line, const char *parent_class)
{
    if (out->symbol_count >= MAX_SYMBOLS) return -1;
    mcp_symbol *s = &out->symbols[out->symbol_count];
    s->type = type;
    s->name = strdup(name);
    s->line = line;
    s->column = 0;
    s->parent_class = parent_class ? strdup(parent_class) : NULL;
    s->param_names = NULL;
    s->param_count = 0;
    s->is_async = 0;
    out->symbol_count++;
    return 0;
}

static int add_import(mcp_file_result *out, const char *module,
                      const char *alias, uint32_t line)
{
    (void)alias;
    if (out->import_count >= MAX_IMPORTS) return -1;
    mcp_import *imp = &out->imports[out->import_count];
    imp->module = strdup(module);
    imp->alias = NULL;
    imp->line = line;
    out->import_count++;
    return 0;
}

static void extract_ruby(TSNode node, const char *source,
                         mcp_file_result *out, const char *class_name)
{
    const char *type = ts_node_type(node);

    /* def method_name ... end */
    if (strcmp(type, "method") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            char name[256];
            get_node_text(name_node, source, name, sizeof(name));
            TSPoint pt = ts_node_start_point(node);
            add_symbol(out, class_name ? MCP_SYMBOL_METHOD : MCP_SYMBOL_FUNCTION,
                       name, pt.row + 1, class_name);
        }
        return;
    }

    /* class ClassName ... end */
    if (strcmp(type, "class") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            char name[256];
            get_node_text(name_node, source, name, sizeof(name));
            TSPoint pt = ts_node_start_point(node);
            add_symbol(out, MCP_SYMBOL_CLASS, name, pt.row + 1, NULL);

            TSNode body = ts_node_child_by_field_name(node, "body", 4);
            if (!ts_node_is_null(body)) {
                uint32_t nc = ts_node_child_count(body);
                for (uint32_t i = 0; i < nc; i++) {
                    extract_ruby(ts_node_child(body, i), source, out, name);
                }
            }
        }
        return;
    }

    /* require / require_relative "foo" */
    if (strcmp(type, "call") == 0) {
        TSNode method = ts_node_child_by_field_name(node, "method", 6);
        if (!ts_node_is_null(method)) {
            char mname[64];
            get_node_text(method, source, mname, sizeof(mname));
            if (strcmp(mname, "require") == 0 || strcmp(mname, "require_relative") == 0) {
                TSNode args = ts_node_child_by_field_name(node, "arguments", 9);
                if (!ts_node_is_null(args)) {
                    TSNode arg = ts_node_named_child(args, 0);
                    if (!ts_node_is_null(arg)) {
                        const char *arg_type = ts_node_type(arg);
                        if (strcmp(arg_type, "string") == 0) {
                            char *mod = copy_text(source,
                                ts_node_start_byte(arg) + 1,
                                ts_node_end_byte(arg) - 1);
                            if (mod) {
                                TSPoint pt = ts_node_start_point(node);
                                add_import(out, mod, NULL, pt.row + 1);
                                free(mod);
                            }
                        }
                    }
                }
            }
        }
        return;
    }

    /* Recurse */
    uint32_t nc = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < nc; i++) {
        extract_ruby(ts_node_named_child(node, i), source, out, class_name);
    }
}

int ruby_extract(const TSTree *tree, const char *source, mcp_file_result *out)
{
    TSNode root = ts_tree_root_node(tree);
    if (ts_node_is_null(root)) return -1;

    out->symbols = calloc(MAX_SYMBOLS, sizeof(mcp_symbol));
    out->imports = calloc(MAX_IMPORTS, sizeof(mcp_import));
    if (!out->symbols || !out->imports) {
        if (out->symbols) free(out->symbols);
        if (out->imports) free(out->imports);
        return -1;
    }
    out->symbol_count = 0;
    out->import_count = 0;

    extract_ruby(root, source, out, NULL);
    return 0;
}

