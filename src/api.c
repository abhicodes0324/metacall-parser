/**
 * MCP Parser - C API implementation
 * JSON export for parse results
 */

#include "mcp_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void json_escape(const char *s, char *out, size_t out_size)
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
}

char *mcp_result_to_json(const mcp_result *result)
{
    if (!result) return NULL;
    const mcp_file_result *fr = mcp_result_get_file((mcp_result *)result);
    if (!fr) return NULL;

    size_t cap = 4096;
    char *json = malloc(cap);
    if (!json) return NULL;
    size_t len = 0;

    char path_esc[2048];
    char name_esc[512];

#define APPEND(s) do { \
    size_t _n = strlen(s); \
    while (len + _n >= cap) { cap *= 2; char *_nj = realloc(json, cap); if (!_nj) { free(json); return NULL; } json = _nj; } \
    memcpy(json + len, s, _n + 1); len += _n; \
} while(0)

    json_escape(fr->file_path ? fr->file_path : "", path_esc, sizeof(path_esc));
    APPEND("{\"file\":\"");
    APPEND(path_esc);
    APPEND("\",\"language\":\"");
    APPEND(mcp_lang_name(fr->language));
    APPEND("\",\"functions\":[");

    int first = 1;
    for (size_t i = 0; i < fr->symbol_count; i++) {
        if (fr->symbols[i].type != MCP_SYMBOL_FUNCTION && fr->symbols[i].type != MCP_SYMBOL_METHOD)
            continue;
        if (!first) APPEND(",");
        first = 0;
        json_escape(fr->symbols[i].name ? fr->symbols[i].name : "", name_esc, sizeof(name_esc));
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"name\":\"%s\",\"line\":%u}", name_esc, (unsigned)fr->symbols[i].line);
        APPEND(buf);
    }
    APPEND("],\"classes\":[");
    first = 1;
    for (size_t i = 0; i < fr->symbol_count; i++) {
        if (fr->symbols[i].type != MCP_SYMBOL_CLASS) continue;
        if (!first) APPEND(",");
        first = 0;
        json_escape(fr->symbols[i].name ? fr->symbols[i].name : "", name_esc, sizeof(name_esc));
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"name\":\"%s\",\"line\":%u}", name_esc, (unsigned)fr->symbols[i].line);
        APPEND(buf);
    }
    APPEND("],\"imports\":[");
    first = 1;
    for (size_t i = 0; i < fr->import_count; i++) {
        if (!first) APPEND(",");
        first = 0;
        json_escape(fr->imports[i].module ? fr->imports[i].module : "", path_esc, sizeof(path_esc));
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"module\":\"%s\"}", path_esc);
        APPEND(buf);
    }
    APPEND("]}");

#undef APPEND
    return json;
}
