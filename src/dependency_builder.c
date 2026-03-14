/**
 * Dependency graph builder for multi-language projects
 */

#include "mcp_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif

#define MAX_FILES 1024
#define MAX_EDGES 2048
#define PATH_MAX 4096

struct dep_node {
    char *path;
    mcp_file_result *data;
};

struct dep_edge {
    char *from;
    char *to;
    char *import_spec;
};

struct mcp_dep_graph_s {
    struct dep_node *nodes;
    size_t node_count;
    struct dep_edge *edges;
    size_t edge_count;
};

static int is_source_file(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return 0;
    ext++;
    if (strcmp(ext, "py") == 0) return 1;
    if (strcmp(ext, "js") == 0 || strcmp(ext, "mjs") == 0 || strcmp(ext, "cjs") == 0) return 1;
    if (strcmp(ext, "ts") == 0 || strcmp(ext, "tsx") == 0 || strcmp(ext, "jsx") == 0) return 1;
    return 0;
}

static mcp_file_result *copy_file_result(const mcp_file_result *src)
{
    mcp_file_result *dst = calloc(1, sizeof(*dst));
    if (!dst) return NULL;

    dst->file_path = src->file_path ? strdup(src->file_path) : NULL;
    dst->language = src->language;
    dst->symbol_count = 0;
    dst->symbols = NULL;
    dst->import_count = 0;
    dst->imports = NULL;

    if (src->symbol_count > 0) {
        dst->symbols = malloc(src->symbol_count * sizeof(mcp_symbol));
        if (dst->symbols) {
            for (size_t i = 0; i < src->symbol_count; i++) {
                dst->symbols[i] = src->symbols[i];
                dst->symbols[i].name = src->symbols[i].name ? strdup(src->symbols[i].name) : NULL;
                dst->symbols[i].parent_class = src->symbols[i].parent_class ? strdup(src->symbols[i].parent_class) : NULL;
            }
            dst->symbol_count = src->symbol_count;
        }
    }

    if (src->import_count > 0) {
        dst->imports = malloc(src->import_count * sizeof(mcp_import));
        if (dst->imports) {
            for (size_t i = 0; i < src->import_count; i++) {
                dst->imports[i].module = src->imports[i].module ? strdup(src->imports[i].module) : NULL;
                dst->imports[i].alias = src->imports[i].alias ? strdup(src->imports[i].alias) : NULL;
                dst->imports[i].line = src->imports[i].line;
            }
            dst->import_count = src->import_count;
        }
    }

    return dst;
}

static void free_file_result(mcp_file_result *f)
{
    if (!f) return;
    free(f->file_path);
    if (f->symbols) {
        for (size_t i = 0; i < f->symbol_count; i++) {
            free(f->symbols[i].name);
            free(f->symbols[i].parent_class);
        }
        free(f->symbols);
    }
    if (f->imports) {
        for (size_t i = 0; i < f->import_count; i++) {
            free(f->imports[i].module);
            free(f->imports[i].alias);
        }
        free(f->imports);
    }
    free(f);
}

static int add_edge(mcp_dep_graph *graph, const char *from, const char *to, const char *import_spec)
{
    if (graph->edge_count >= MAX_EDGES) return -1;
    struct dep_edge *e = &graph->edges[graph->edge_count];
    e->from = strdup(from);
    e->to = strdup(to);
    e->import_spec = import_spec ? strdup(import_spec) : NULL;
    graph->edge_count++;
    return 0;
}

/* Resolve import to file path - returns 1 if found in nodes */
static int resolve_import_to_node(const char *dir_path, const char *import_spec,
                                   mcp_lang_id lang, struct dep_node *nodes, size_t node_count,
                                   char *out_path, size_t out_size)
{
    char base_dir[PATH_MAX];
    strncpy(base_dir, dir_path, sizeof(base_dir) - 1);
    base_dir[sizeof(base_dir) - 1] = '\0';
    char *last_slash = strrchr(base_dir, '/');
    if (last_slash) *last_slash = '\0';

    /* Try: dir/import.py or dir/import.js, dir/import/__init__.py */
    char candidate[PATH_MAX];
    if (import_spec[0] == '.' || import_spec[0] == '/') {
        snprintf(candidate, sizeof(candidate), "%s/%s", base_dir, import_spec);
    } else {
        snprintf(candidate, sizeof(candidate), "%s/%s", base_dir, import_spec);
    }

    /* Try .py / .js extensions */
    const char *ext = strrchr(candidate, '.');
    if (!ext || (strcmp(ext, ".py") != 0 && strcmp(ext, ".js") != 0)) {
        if (lang == MCP_LANG_PYTHON) {
            strcat(candidate, ".py");
        } else {
            strcat(candidate, ".js");
        }
    }

    for (size_t i = 0; i < node_count; i++) {
        if (strstr(nodes[i].path, import_spec) != NULL ||
            strcmp(nodes[i].path, candidate) == 0) {
            strncpy(out_path, nodes[i].path, out_size - 1);
            out_path[out_size - 1] = '\0';
            return 1;
        }
    }

    /* Try without extension - match by base name */
    for (size_t i = 0; i < node_count; i++) {
        const char *fname = strrchr(nodes[i].path, '/');
        fname = fname ? fname + 1 : nodes[i].path;
        const char *imp_base = strrchr(import_spec, '/');
        imp_base = imp_base ? imp_base + 1 : import_spec;
        if (strcmp(fname, imp_base) == 0) {
            strncpy(out_path, nodes[i].path, out_size - 1);
            out_path[out_size - 1] = '\0';
            return 1;
        }
    }
    return 0;
}

#ifndef _WIN32
static void scan_dir_recursive(const char *dir_path, int recursive,
                                char **out_paths, size_t *out_count)
{
    DIR *d = opendir(dir_path);
    if (!d) return;

    char subpath[PATH_MAX];
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL && *out_count < MAX_FILES) {
        if (ent->d_name[0] == '.') continue;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        snprintf(subpath, sizeof(subpath), "%s/%s", dir_path, ent->d_name);

        struct stat st;
        if (stat(subpath, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (recursive) {
                scan_dir_recursive(subpath, 1, out_paths, out_count);
            }
        } else if (S_ISREG(st.st_mode) && is_source_file(ent->d_name)) {
            out_paths[*out_count] = strdup(subpath);
            if (out_paths[*out_count]) (*out_count)++;
        }
    }
    closedir(d);
}
#endif

mcp_dep_graph *mcp_parser_build_deps(mcp_parser *parser, const char *dir_path, int recursive)
{
    if (!parser || !dir_path) return NULL;

    char *paths[MAX_FILES];
    size_t path_count = 0;
    memset(paths, 0, sizeof(paths));

#ifndef _WIN32
    scan_dir_recursive(dir_path, recursive ? 1 : 0, paths, &path_count);
#else
    (void)recursive;
    /* Windows: minimal - just try dir_path if it's a file */
    if (is_source_file(dir_path)) {
        paths[0] = strdup(dir_path);
        if (paths[0]) path_count = 1;
    }
#endif

    if (path_count == 0) {
        return NULL;
    }

    mcp_dep_graph *graph = calloc(1, sizeof(*graph));
    if (!graph) goto fail;
    graph->nodes = calloc(MAX_FILES, sizeof(struct dep_node));
    graph->edges = calloc(MAX_EDGES, sizeof(struct dep_edge));
    if (!graph->nodes || !graph->edges) goto fail;

    for (size_t i = 0; i < path_count; i++) {
        mcp_result *res = mcp_parser_parse_file(parser, paths[i], NULL, 0);
        if (!res) {
            free(paths[i]);
            paths[i] = NULL;
            continue;
        }

        const mcp_file_result *fr = mcp_result_get_file(res);
        mcp_file_result *copy = copy_file_result(fr);
        mcp_result_free(res);
        if (!copy) {
            free(paths[i]);
            paths[i] = NULL;
            continue;
        }

        struct dep_node *node = &graph->nodes[graph->node_count];
        node->path = strdup(paths[i]);
        node->data = copy;
        if (!node->path) {
            free_file_result(copy);
            free(paths[i]);
            paths[i] = NULL;
            continue;
        }
        graph->node_count++;

        /* Create edges from imports */
        for (size_t j = 0; j < copy->import_count; j++) {
            const char *mod = copy->imports[j].module;
            if (!mod || mod[0] == '\0') continue;

            char resolved[PATH_MAX];
            if (resolve_import_to_node(dir_path, mod, copy->language,
                                       graph->nodes, graph->node_count,
                                       resolved, sizeof(resolved))) {
                add_edge(graph, paths[i], resolved, mod);
            }
        }

        free(paths[i]);
        paths[i] = NULL;
    }

    return graph;

fail:
    if (graph) {
        for (size_t i = 0; i < graph->node_count; i++) {
            free(graph->nodes[i].path);
            free_file_result(graph->nodes[i].data);
        }
        free(graph->nodes);
        for (size_t i = 0; i < graph->edge_count; i++) {
            free(graph->edges[i].from);
            free(graph->edges[i].to);
            free(graph->edges[i].import_spec);
        }
        free(graph->edges);
        free(graph);
    }
    for (size_t i = 0; i < path_count; i++) free(paths[i]);
    return NULL;
}

void mcp_dep_graph_free(mcp_dep_graph *graph)
{
    if (!graph) return;
    for (size_t i = 0; i < graph->node_count; i++) {
        free(graph->nodes[i].path);
        free_file_result(graph->nodes[i].data);
    }
    free(graph->nodes);
    for (size_t i = 0; i < graph->edge_count; i++) {
        free(graph->edges[i].from);
        free(graph->edges[i].to);
        free(graph->edges[i].import_spec);
    }
    free(graph->edges);
    free(graph);
}

static size_t json_escape(const char *s, char *out, size_t out_size)
{
    size_t j = 0;
    for (; *s && j < out_size - 2; s++) {
        if (*s == '"' || *s == '\\') { out[j++] = '\\'; out[j++] = *s; }
        else if (*s == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
        else if (*s == '\r') { out[j++] = '\\'; out[j++] = 'r'; }
        else if (*s == '\t') { out[j++] = '\\'; out[j++] = 't'; }
        else out[j++] = *s;
    }
    out[j] = '\0';
    return j;
}

char *mcp_dep_graph_to_json(mcp_dep_graph *graph)
{
    if (!graph) return NULL;

    size_t cap = 4096;
    char *json = malloc(cap);
    if (!json) return NULL;
    size_t len = 0;

#define APPEND(s) do { \
    size_t _n = strlen(s); \
    while (len + _n >= cap) { cap *= 2; char *_njson = realloc(json, cap); if (!_njson) { free(json); return NULL; } json = _njson; } \
    memcpy(json + len, s, _n + 1); len += _n; \
} while(0)

#define APPENDN(s, n) do { \
    while (len + (n) >= cap) { cap *= 2; char *_njson = realloc(json, cap); if (!_njson) { free(json); return NULL; } json = _njson; } \
    memcpy(json + len, s, n); json[len + (n)] = '\0'; len += (n); \
} while(0)

    APPEND("{\"nodes\":[");
    for (size_t i = 0; i < graph->node_count; i++) {
        if (i > 0) APPEND(",");
        char path_esc[2048];
        json_escape(graph->nodes[i].path, path_esc, sizeof(path_esc));
        char buf[2560];
        snprintf(buf, sizeof(buf), "{\"id\":\"%s\",\"path\":\"%s\",\"symbols\":[",
                 path_esc, path_esc);
        APPEND(buf);
        mcp_file_result *fr = graph->nodes[i].data;
        for (size_t j = 0; fr && j < fr->symbol_count; j++) {
            if (j > 0) APPEND(",");
            char name_esc[512];
            json_escape(fr->symbols[j].name ? fr->symbols[j].name : "", name_esc, sizeof(name_esc));
            const char *typ = fr->symbols[j].type == MCP_SYMBOL_FUNCTION ? "function" :
                             fr->symbols[j].type == MCP_SYMBOL_CLASS ? "class" : "method";
            snprintf(buf, sizeof(buf), "{\"name\":\"%s\",\"type\":\"%s\",\"line\":%u}",
                     name_esc, typ, (unsigned)fr->symbols[j].line);
            APPEND(buf);
        }
        APPEND("]}");
    }
    APPEND("],\"edges\":[");
    for (size_t i = 0; i < graph->edge_count; i++) {
        if (i > 0) APPEND(",");
        char from_esc[2048], to_esc[2048];
        json_escape(graph->edges[i].from, from_esc, sizeof(from_esc));
        json_escape(graph->edges[i].to, to_esc, sizeof(to_esc));
        char buf[4160];
        snprintf(buf, sizeof(buf), "{\"from\":\"%s\",\"to\":\"%s\"}", from_esc, to_esc);
        APPEND(buf);
    }
    APPEND("]}");

#undef APPEND
#undef APPENDN

    return json;
}
