/**
 * Python AST extractor using Tree Sitter
 * Extracts: function_definition, class_definition, import/from
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

/* Find identifier name node under a parent */
static TSNode find_name_node(TSNode parent, const char *source)
{
    uint32_t n = ts_node_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_child(parent, i);
        const char *type = ts_node_type(child);
        if (strcmp(type, "identifier") == 0) {
            return child;
        }
        /* Python: dotted_name for "foo.bar" */
        if (strcmp(type, "dotted_name") == 0) {
            return child;
        }
    }
    return (TSNode){0};
}

/* Extract parameter names from Python parameters node */
static void extract_python_params(TSNode params_node, const char *source,
                                  char ***out_names, size_t *out_count)
{
    *out_names = NULL;
    *out_count = 0;
    if (ts_node_is_null(params_node)) return;

    uint32_t pn = ts_node_child_count(params_node);
    char **param_names = malloc(pn * sizeof(char *));
    if (!param_names) return;

    size_t param_count = 0;
    for (uint32_t pi = 0; pi < pn; pi++) {
        TSNode p = ts_node_child(params_node, pi);
        const char *pt_type = ts_node_type(p);
        char pname[128];

        if (strcmp(pt_type, "identifier") == 0) {
            get_node_text(p, source, pname, sizeof(pname));
            if (strcmp(pname, "self") != 0 && strcmp(pname, "cls") != 0) {
                param_names[param_count] = strdup(pname);
                if (param_names[param_count]) param_count++;
            }
        } else if (strcmp(pt_type, "typed_parameter") == 0) {
            TSNode pid = ts_node_child_by_field_name(p, "name", 4);
            if (ts_node_is_null(pid)) pid = ts_node_child(p, 0);
            if (!ts_node_is_null(pid)) {
                get_node_text(pid, source, pname, sizeof(pname));
                if (strcmp(pname, "self") != 0 && strcmp(pname, "cls") != 0) {
                    param_names[param_count] = strdup(pname);
                    if (param_names[param_count]) param_count++;
                }
            }
        }
    }
    *out_names = param_count > 0 ? param_names : (free(param_names), NULL);
    *out_count = param_count;
}

static void extract_functions_and_classes(TSNode node, const char *source, metacall_file_result *out,
                                          const char *class_name)
{
    const char *type = ts_node_type(node);

    if (strcmp(type, "async_function_definition") == 0 ||
        strcmp(type, "function_definition") == 0) {
        int is_async = (strcmp(type, "async_function_definition") == 0);
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            char name[256];
            get_node_text(name_node, source, name, sizeof(name));
            TSPoint pt = ts_node_start_point(node);
            if (add_symbol(out, class_name ? METACALL_SYMBOL_METHOD : METACALL_SYMBOL_FUNCTION,
                          name, pt.row + 1, class_name) == 0) {
                metacall_symbol *s = &out->symbols[out->symbol_count - 1];
                s->is_async = is_async;
                TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
                extract_python_params(params, source, &s->param_names, &s->param_count);
            }
        }
        return;
    }

    /* decorated_definition wraps async/regular function (e.g. @decorator above def) */
    if (strcmp(type, "decorated_definition") == 0) {
        uint32_t nc = ts_node_child_count(node);
        for (uint32_t i = 0; i < nc; i++) {
            TSNode child = ts_node_child(node, i);
            const char *ct = ts_node_type(child);
            if (strcmp(ct, "async_function_definition") == 0 ||
                strcmp(ct, "function_definition") == 0) {
                extract_functions_and_classes(child, source, out, class_name);
                break;
            }
        }
        return;
    }

    if (strcmp(type, "class_definition") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            char name[256];
            get_node_text(name_node, source, name, sizeof(name));
            TSPoint pt = ts_node_start_point(node);
            add_symbol(out, METACALL_SYMBOL_CLASS, name, pt.row + 1, NULL);

            /* Recurse into class body for methods */
            TSNode body = ts_node_child_by_field_name(node, "body", 4);
            if (!ts_node_is_null(body)) {
                uint32_t nc = ts_node_child_count(body);
                for (uint32_t i = 0; i < nc; i++) {
                    extract_functions_and_classes(ts_node_child(body, i), source, out, name);
                }
            }
        }
        return;
    }

    /* Recurse into block-like nodes */
    if (strcmp(type, "module") == 0 || strcmp(type, "block") == 0 ||
        strcmp(type, "expression_statement") == 0) {
        uint32_t nc = ts_node_child_count(node);
        for (uint32_t i = 0; i < nc; i++) {
            extract_functions_and_classes(ts_node_child(node, i), source, out, class_name);
        }
    }
}

static void extract_imports(TSNode node, const char *source, metacall_file_result *out)
{
    const char *type = ts_node_type(node);

    if (strcmp(type, "import_statement") == 0) {
        /* import foo, bar as b */
        TSNode name_list = ts_node_child_by_field_name(node, "name", 4);
        if (ts_node_is_null(name_list)) {
            TSNode dotted = ts_node_child(node, 1);
            if (!ts_node_is_null(dotted)) {
                char *mod = copy_text(source, ts_node_start_byte(dotted), ts_node_end_byte(dotted));
                if (mod) {
                    TSPoint pt = ts_node_start_point(node);
                    add_import(out, mod, NULL, pt.row + 1);
                    free(mod);
                }
            }
        } else {
            uint32_t nc = ts_node_child_count(name_list);
            for (uint32_t i = 0; i < nc; i++) {
                TSNode child = ts_node_child(name_list, i);
                if (strcmp(ts_node_type(child), "dotted_name") == 0) {
                    char *mod = copy_text(source, ts_node_start_byte(child), ts_node_end_byte(child));
                    if (mod) {
                        TSPoint pt = ts_node_start_point(node);
                        add_import(out, mod, NULL, pt.row + 1);
                        free(mod);
                    }
                }
            }
        }
        return;
    }

    if (strcmp(type, "import_from_statement") == 0) {
        /* from x import y, z as a */
        TSNode module_node = ts_node_child_by_field_name(node, "module_name", 12);
        if (ts_node_is_null(module_node)) {
            TSNode dots = ts_node_child(node, 1);
            if (strcmp(ts_node_type(dots), "dotted_name") == 0) {
                module_node = dots;
            }
        }
        if (!ts_node_is_null(module_node)) {
            char *mod = copy_text(source, ts_node_start_byte(module_node), ts_node_end_byte(module_node));
            if (mod) {
                TSPoint pt = ts_node_start_point(node);
                add_import(out, mod, NULL, pt.row + 1);
                free(mod);
            }
        }
        return;
    }

    /* Recurse */
    uint32_t nc = ts_node_child_count(node);
    for (uint32_t i = 0; i < nc; i++) {
        extract_imports(ts_node_child(node, i), source, out);
    }
}

int python_extract(const TSTree *tree, const char *source, metacall_file_result *out)
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

    /* First pass: imports */
    extract_imports(root, source, out);

    /* Second pass: functions and classes */
    extract_functions_and_classes(root, source, out, NULL);

    return 0;
}
