/**
 * Dependency graph builder for multi-language projects
 */

#include "metacall_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#else
#include <windows.h>
#endif

#define MAX_FILES 1024
#define MAX_EDGES 2048
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

struct dep_node {
    char *path;
    metacall_file_result *data;
};

struct dep_edge {
    char *from;
    char *to;
    char *import_spec;
};

struct metacall_dep_graph_s {
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
    if (strcmp(ext, "rb") == 0) return 1;
    return 0;
}

static metacall_file_result *copy_file_result(const metacall_file_result *src)
{
    metacall_file_result *dst = calloc(1, sizeof(*dst));
    if (!dst) return NULL;

    dst->file_path = src->file_path ? strdup(src->file_path) : NULL;
    dst->language = src->language;
    dst->symbol_count = 0;
    dst->symbols = NULL;
    dst->import_count = 0;
    dst->imports = NULL;

    if (src->symbol_count > 0) {
        dst->symbols = malloc(src->symbol_count * sizeof(metacall_symbol));
        if (dst->symbols) {
            for (size_t i = 0; i < src->symbol_count; i++) {
                dst->symbols[i] = src->symbols[i];
                dst->symbols[i].name = src->symbols[i].name ? strdup(src->symbols[i].name) : NULL;
                dst->symbols[i].parent_class = src->symbols[i].parent_class ? strdup(src->symbols[i].parent_class) : NULL;
                dst->symbols[i].param_names = NULL;
                dst->symbols[i].param_count = 0;
                if (src->symbols[i].param_count > 0 && src->symbols[i].param_names) {
                    dst->symbols[i].param_names = malloc(src->symbols[i].param_count * sizeof(char *));
                    if (dst->symbols[i].param_names) {
                        for (size_t p = 0; p < src->symbols[i].param_count; p++)
                            dst->symbols[i].param_names[p] = src->symbols[i].param_names[p]
                                ? strdup(src->symbols[i].param_names[p]) : NULL;
                        dst->symbols[i].param_count = src->symbols[i].param_count;
                    }
                }
            }
            dst->symbol_count = src->symbol_count;
        }
    }

    if (src->import_count > 0) {
        dst->imports = malloc(src->import_count * sizeof(metacall_import));
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

static void free_file_result(metacall_file_result *f)
{
    if (!f) return;
    free(f->file_path);
    if (f->symbols) {
        for (size_t i = 0; i < f->symbol_count; i++) {
            free(f->symbols[i].name);
            free(f->symbols[i].parent_class);
            if (f->symbols[i].param_names) {
                for (size_t p = 0; p < f->symbols[i].param_count; p++)
                    free(f->symbols[i].param_names[p]);
                free(f->symbols[i].param_names);
            }
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

static int add_edge(metacall_dep_graph *graph, const char *from, const char *to, const char *import_spec)
{
    if (graph->edge_count >= MAX_EDGES) {
        static int warned;
        if (!warned) {
            fprintf(stderr, "metacall-parser: warning: dependency edge limit (%d) reached, some edges may be missing\n", MAX_EDGES);
            warned = 1;
        }
        return -1;
    }
    struct dep_edge *e = &graph->edges[graph->edge_count];
    e->from = strdup(from);
    e->to = strdup(to);
    e->import_spec = import_spec ? strdup(import_spec) : NULL;
    graph->edge_count++;
    return 0;
}

/* Compare two paths, treating multiple slashes as one */
static int path_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a == '/') {
            if (*b != '/') return 0;
            while (*a == '/') a++;
            while (*b == '/') b++;
        } else if (*b == '/') {
            return 0;
        } else if (*a != *b) {
            return 0;
        } else {
            a++;
            b++;
        }
    }
    return (*a == '\0' && *b == '\0');
}

/* Resolve import to file path - returns 1 if found in nodes.
 * from_file: full path of the file doing the import (e.g. tests/sample.js)
 * import_spec: the import string (e.g. "./utils", "utils", "../foo")
 */
static int resolve_import_to_node(const char *from_file, const char *import_spec,
                                   metacall_lang_id lang, struct dep_node *nodes, size_t node_count,
                                   char *out_path, size_t out_size)
{
    /* Get directory of the importing file */
    char from_dir[PATH_MAX];
    strncpy(from_dir, from_file, sizeof(from_dir) - 1);
    from_dir[sizeof(from_dir) - 1] = '\0';
    char *last_slash = strrchr(from_dir, '/');
#ifdef _WIN32
    {
        char *bs = strrchr(from_dir, '\\');
        if (bs && (!last_slash || bs > last_slash)) last_slash = bs;
    }
#endif
    if (last_slash)
        *last_slash = '\0';
    else
        strncpy(from_dir, ".", sizeof(from_dir) - 1);

    /* Build candidate path from importing file's directory.
     * Normalize: strip leading ./ from import_spec for cleaner path */
    char import_normalized[PATH_MAX];
    const char *imp = import_spec;
    while (imp[0] == '.' && imp[1] == '/') imp += 2;
    strncpy(import_normalized, imp, sizeof(import_normalized) - 1);
    import_normalized[sizeof(import_normalized) - 1] = '\0';

    char candidate[PATH_MAX];
    snprintf(candidate, sizeof(candidate), "%s/%s", from_dir, import_normalized);

    /* Add extension if missing - prefer extension matching importing file's language */
    const char *ext = strrchr(candidate, '.');
    if (!ext || (strcmp(ext, ".py") != 0 && strcmp(ext, ".js") != 0 && strcmp(ext, ".ts") != 0 &&
                 strcmp(ext, ".tsx") != 0 && strcmp(ext, ".jsx") != 0 && strcmp(ext, ".rb") != 0)) {
        const char *prefer = (lang == METACALL_LANG_PYTHON) ? ".py" : (lang == METACALL_LANG_RUBY) ? ".rb" : ".js";
        const char *try_exts[] = {".py", ".js", ".ts", ".rb", NULL};
        /* Try preferred extension first */
        for (int round = 0; round < 2; round++) {
            for (int e = 0; try_exts[e]; e++) {
                if (round == 0 && strcmp(try_exts[e], prefer) != 0) continue;
                if (round == 1 && strcmp(try_exts[e], prefer) == 0) continue;
                char with_ext[PATH_MAX];
                snprintf(with_ext, sizeof(with_ext), "%s%s", candidate, try_exts[e]);
                for (size_t i = 0; i < node_count; i++) {
                    if (path_eq(nodes[i].path, with_ext)) {
                        strncpy(out_path, nodes[i].path, out_size - 1);
                        out_path[out_size - 1] = '\0';
                        return 1;
                    }
                }
            }
        }
    }

    /* Exact match on candidate */
    for (size_t i = 0; i < node_count; i++) {
        if (path_eq(nodes[i].path, candidate)) {
            strncpy(out_path, nodes[i].path, out_size - 1);
            out_path[out_size - 1] = '\0';
            return 1;
        }
    }

    /* Match by basename (stem) - strip extension from both */
    const char *imp_base = strrchr(import_normalized, '/');
    imp_base = imp_base ? imp_base + 1 : import_normalized;
    char imp_stem[256];
    strncpy(imp_stem, imp_base, sizeof(imp_stem) - 1);
    imp_stem[sizeof(imp_stem) - 1] = '\0';
    char *dot = strrchr(imp_stem, '.');
    if (dot) *dot = '\0';

    for (size_t i = 0; i < node_count; i++) {
        const char *fname = strrchr(nodes[i].path, '/');
#ifdef _WIN32
        {
            const char *bs = strrchr(nodes[i].path, '\\');
            if (bs && (!fname || bs > fname)) fname = bs;
        }
#endif
        fname = fname ? fname + 1 : nodes[i].path;
        char fstem[256];
        strncpy(fstem, fname, sizeof(fstem) - 1);
        fstem[sizeof(fstem) - 1] = '\0';
        char *fdot = strrchr(fstem, '.');
        if (fdot) *fdot = '\0';
        if (strcmp(fstem, imp_stem) == 0) {
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

    while ((ent = readdir(d)) != NULL) {
        if (*out_count >= MAX_FILES) {
            fprintf(stderr, "metacall-parser: warning: file limit (%d) reached, some files may be omitted\n", MAX_FILES);
            break;
        }
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
#else
static void scan_dir_recursive_win(const char *dir_path, int recursive,
                                    char **out_paths, size_t *out_count)
{
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (*out_count >= MAX_FILES) {
            fprintf(stderr, "metacall-parser: warning: file limit (%d) reached, some files may be omitted\n", MAX_FILES);
            break;
        }
        if (fd.cFileName[0] == '.') continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        char subpath[PATH_MAX];
        snprintf(subpath, sizeof(subpath), "%s\\%s", dir_path, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive)
                scan_dir_recursive_win(subpath, 1, out_paths, out_count);
        } else if (is_source_file(fd.cFileName)) {
            out_paths[*out_count] = strdup(subpath);
            if (out_paths[*out_count]) (*out_count)++;
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}
#endif

metacall_dep_graph *metacall_parser_build_deps(metacall_parser *parser, const char *dir_path, int recursive)
{
    if (!parser || !dir_path) return NULL;

    /* Normalize directory path (strip trailing slash) */
    char base_dir[PATH_MAX];
    strncpy(base_dir, dir_path, sizeof(base_dir) - 1);
    base_dir[sizeof(base_dir) - 1] = '\0';
    size_t dlen = strlen(base_dir);
    if (dlen > 1 && (base_dir[dlen - 1] == '/' || base_dir[dlen - 1] == '\\'))
        base_dir[dlen - 1] = '\0';

    char *paths[MAX_FILES];
    size_t path_count = 0;
    memset(paths, 0, sizeof(paths));

#ifndef _WIN32
    scan_dir_recursive(base_dir, recursive ? 1 : 0, paths, &path_count);
#else
    /* Windows: use FindFirstFile/FindNextFile for directory scanning */
    {
        DWORD attrs = GetFileAttributesA(base_dir);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
            scan_dir_recursive_win(base_dir, recursive ? 1 : 0, paths, &path_count);
        else if (is_source_file(base_dir)) {
            paths[0] = strdup(base_dir);
            if (paths[0]) path_count = 1;
        }
    }
#endif

    if (path_count == 0) {
        return NULL;
    }

    metacall_dep_graph *graph = calloc(1, sizeof(*graph));
    if (!graph) goto fail;
    graph->nodes = calloc(MAX_FILES, sizeof(struct dep_node));
    graph->edges = calloc(MAX_EDGES, sizeof(struct dep_edge));
    if (!graph->nodes || !graph->edges) goto fail;

    for (size_t i = 0; i < path_count; i++) {
        metacall_result *res = metacall_parser_parse_file(parser, paths[i], NULL, 0);
        if (!res) {
            free(paths[i]);
            paths[i] = NULL;
            continue;
        }

        const metacall_file_result *fr = metacall_result_get_file(res);
        metacall_file_result *copy = copy_file_result(fr);
        metacall_result_free(res);
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
            if (resolve_import_to_node(paths[i], mod, copy->language,
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

void metacall_dep_graph_free(metacall_dep_graph *graph)
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

char *metacall_dep_graph_to_json(metacall_dep_graph *graph)
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
        metacall_file_result *fr = graph->nodes[i].data;
        for (size_t j = 0; fr && j < fr->symbol_count; j++) {
            if (j > 0) APPEND(",");
            char name_esc[512];
            json_escape(fr->symbols[j].name ? fr->symbols[j].name : "", name_esc, sizeof(name_esc));
            const char *typ = fr->symbols[j].type == METACALL_SYMBOL_FUNCTION ? "function" :
                             fr->symbols[j].type == METACALL_SYMBOL_CLASS ? "class" : "method";
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
