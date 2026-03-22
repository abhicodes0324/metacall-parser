/**
 * JavaScript/TypeScript AST extractor using Tree Sitter
 * Extracts: function declarations, class declarations, methods, require/import
 */

#include "metacall_parser.h"
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

static int add_symbol(metacall_file_result *out, metacall_symbol_type type, const char *name,
                     uint32_t line, const char *parent_class)
{
    if (out->symbol_count >= MAX_SYMBOLS) return -1;
    metacall_symbol *s = &out->symbols[out->symbol_count];
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

static int add_import(metacall_file_result *out, const char *module, const char *alias, uint32_t line)
{
    if (out->import_count >= MAX_IMPORTS) return -1;
    metacall_import *imp = &out->imports[out->import_count];
    imp->module = strdup(module);
    imp->alias = alias ? strdup(alias) : NULL;
    imp->line = line;
    out->import_count++;
    return 0;
}

/* Get identifier from various JS node types */
static void get_identifier(TSNode node, const char *source, char *out, size_t out_size)
{
    const char *type = ts_node_type(node);
    if (strcmp(type, "identifier") == 0) {
        get_node_text(node, source, out, out_size);
        return;
    }
    /* property_identifier, etc. */
    get_node_text(node, source, out, out_size);
}

static void extract_functions_and_classes(TSNode node, const char *source, metacall_file_result *out,
                                          const char *class_name)
{
    const char *type = ts_node_type(node);

    /* Handle async function declarations */
    if (strcmp(type, "async_function_declaration") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            char name[256];
            get_identifier(name_node, source, name, sizeof(name));
            TSPoint pt = ts_node_start_point(node);
            if (add_symbol(out, METACALL_SYMBOL_FUNCTION, name, pt.row + 1, NULL) == 0)
                out->symbols[out->symbol_count - 1].is_async = 1;
        }
        return;
    }

    if (strcmp(type, "function_declaration") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            char name[256];
            get_identifier(name_node, source, name, sizeof(name));
            TSPoint pt = ts_node_start_point(node);
            add_symbol(out, METACALL_SYMBOL_FUNCTION, name, pt.row + 1, NULL);
        }
        return;
    }

    if (strcmp(type, "method_definition") == 0 && class_name) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            char name[256];
            get_identifier(name_node, source, name, sizeof(name));
            /* Skip constructor */
            if (strcmp(name, "constructor") != 0) {
                TSPoint pt = ts_node_start_point(node);
                add_symbol(out, METACALL_SYMBOL_METHOD, name, pt.row + 1, class_name);
            }
        }
        return;
    }

    if (strcmp(type, "class_declaration") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            char name[256];
            get_identifier(name_node, source, name, sizeof(name));
            TSPoint pt = ts_node_start_point(node);
            add_symbol(out, METACALL_SYMBOL_CLASS, name, pt.row + 1, NULL);

            TSNode body = ts_node_child_by_field_name(node, "body", 4);
            if (!ts_node_is_null(body)) {
                uint32_t nc = ts_node_named_child_count(body);
                for (uint32_t i = 0; i < nc; i++) {
                    TSNode child = ts_node_named_child(body, i);
                    extract_functions_and_classes(child, source, out, name);
                }
            }
        }
        return;
    }

    /* Arrow function at top level: (const x = () => {}) - harder to get name, skip for now */
    /* Variable declarator with function: const fn = function() {} */
    if (strcmp(type, "lexical_declaration") == 0 || strcmp(type, "variable_declaration") == 0) {
        uint32_t nc = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode child = ts_node_named_child(node, i);
            if (strcmp(ts_node_type(child), "variable_declarator") == 0) {
                TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
                TSNode value_node = ts_node_child_by_field_name(child, "value", 5);
                if (!ts_node_is_null(value_node) && !ts_node_is_null(name_node)) {
                    const char *val_type = ts_node_type(value_node);
                    if (strcmp(val_type, "function") == 0 || strcmp(val_type, "arrow_function") == 0) {
                        char name[256];
                        get_identifier(name_node, source, name, sizeof(name));
                        TSPoint pt = ts_node_start_point(child);
                        add_symbol(out, METACALL_SYMBOL_FUNCTION, name, pt.row + 1, NULL);
                    }
                }
            }
        }
        return;
    }

    /* Recurse */
    uint32_t nc = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < nc; i++) {
        extract_functions_and_classes(ts_node_named_child(node, i), source, out, class_name);
    }
}

static void extract_imports(TSNode node, const char *source, metacall_file_result *out)
{
    const char *type = ts_node_type(node);

    if (strcmp(type, "call_expression") == 0) {
        /* require("module") - use "function" field for the callee */
        TSNode fn = ts_node_child_by_field_name(node, "function", 8);
        if (ts_node_is_null(fn)) fn = ts_node_named_child(node, 0);
        if (!ts_node_is_null(fn) && strcmp(ts_node_type(fn), "identifier") == 0) {
            char fn_name[64];
            get_node_text(fn, source, fn_name, sizeof(fn_name));
            if (strcmp(fn_name, "require") == 0) {
                TSNode args = ts_node_child_by_field_name(node, "arguments", 9);
                if (!ts_node_is_null(args)) {
                    TSNode arg = ts_node_named_child(args, 0);
                    if (!ts_node_is_null(arg)) {
                        const char *arg_type = ts_node_type(arg);
                        if (strcmp(arg_type, "string") == 0 || strcmp(arg_type, "template_string") == 0) {
                            char *mod = copy_text(source, ts_node_start_byte(arg) + 1,
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

    if (strcmp(type, "import_statement") == 0) {
        /* import x from "mod" or import "mod" or import { a } from "mod" */
        TSNode source_node = ts_node_child_by_field_name(node, "source", 6);
        if (!ts_node_is_null(source_node)) {
            char *mod = copy_text(source, ts_node_start_byte(source_node) + 1,
                                  ts_node_end_byte(source_node) - 1);
            if (mod) {
                TSPoint pt = ts_node_start_point(node);
                add_import(out, mod, NULL, pt.row + 1);
                free(mod);
            }
        }
        return;
    }

    /* Recurse */
    uint32_t nc = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < nc; i++) {
        extract_imports(ts_node_named_child(node, i), source, out);
    }
}

int js_extract(const TSTree *tree, const char *source, metacall_file_result *out)
{
    TSNode root = ts_tree_root_node(tree);
    if (ts_node_is_null(root)) return -1;

    out->symbols = calloc(MAX_SYMBOLS, sizeof(metacall_symbol));
    out->imports = calloc(MAX_IMPORTS, sizeof(metacall_import));
    if (!out->symbols || !out->imports) {
        if (out->symbols) free(out->symbols);
        if (out->imports) free(out->imports);
        return -1;
    }
    out->symbol_count = 0;
    out->import_count = 0;

    extract_imports(root, source, out);
    extract_functions_and_classes(root, source, out, NULL);

    return 0;
}
