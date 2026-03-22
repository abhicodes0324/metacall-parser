/**
 * metacall-parser - C API implementation
 * JSON export for parse results
 */

#include "metacall_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *metacall_parser_version(void)
{
    return "0.1.0";
}

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

char *metacall_result_to_json(const metacall_result *result)
{
    if (!result) return NULL;
    const metacall_file_result *fr = metacall_result_get_file((metacall_result *)result);
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
    APPEND(metacall_lang_name(fr->language));
    APPEND("\",\"functions\":[");

    int first = 1;
    for (size_t i = 0; i < fr->symbol_count; i++) {
        if (fr->symbols[i].type != METACALL_SYMBOL_FUNCTION && fr->symbols[i].type != METACALL_SYMBOL_METHOD)
            continue;
        if (!first) APPEND(",");
        first = 0;
        json_escape(fr->symbols[i].name ? fr->symbols[i].name : "", name_esc, sizeof(name_esc));
        /* Build params array */
        char params_json[2048] = "[";
        for (size_t p = 0; p < fr->symbols[i].param_count && p < 32; p++) {
            if (p > 0) strcat(params_json, ",");
            char pesc[128];
            json_escape(fr->symbols[i].param_names && fr->symbols[i].param_names[p]
                        ? fr->symbols[i].param_names[p] : "", pesc, sizeof(pesc));
            char pbuf[160];
            snprintf(pbuf, sizeof(pbuf), "{\"name\":\"%s\"}", pesc);
            strcat(params_json, pbuf);
        }
        strcat(params_json, "]");
        char buf[512];
        snprintf(buf, sizeof(buf), "{\"name\":\"%s\",\"line\":%u,\"async\":%s,\"params\":%s}",
                 name_esc, (unsigned)fr->symbols[i].line,
                 fr->symbols[i].is_async ? "true" : "false",
                 params_json);
        APPEND(buf);
    }
    APPEND("],\"classes\":[");
    first = 1;
    for (size_t i = 0; i < fr->symbol_count; i++) {
        if (fr->symbols[i].type != METACALL_SYMBOL_CLASS) continue;
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

char *metacall_result_to_inspect_json(const metacall_result *result)
{
    if (!result) return NULL;
    const metacall_file_result *fr = metacall_result_get_file((metacall_result *)result);
    if (!fr) return NULL;

    /* Module name = filename without extension */
    const char *fname = strrchr(fr->file_path, '/');
    fname = fname ? fname + 1 : fr->file_path;
    char module_name[256];
    strncpy(module_name, fname, sizeof(module_name) - 1);
    module_name[sizeof(module_name) - 1] = '\0';
    char *dot = strrchr(module_name, '.');
    if (dot) *dot = '\0';

    size_t cap = 4096;
    char *json = malloc(cap);
    if (!json) return NULL;
    size_t len = 0;

    char path_esc[2048];
    char name_esc[512];

#define APPEND2(s) do { \
    size_t _n = strlen(s); \
    while (len + _n >= cap) { cap *= 2; char *_nj = realloc(json, cap); if (!_nj) { free(json); return NULL; } json = _nj; } \
    memcpy(json + len, s, _n + 1); len += _n; \
} while(0)

    const char *tag = metacall_lang_tag(fr->language);

    APPEND2("{\"");
    APPEND2(tag);
    APPEND2("\":[{\"name\":\"");
    json_escape(fr->file_path ? fr->file_path : "", path_esc, sizeof(path_esc));
    APPEND2(path_esc);
    APPEND2("\",\"scope\":{\"name\":\"");
    json_escape(module_name, name_esc, sizeof(name_esc));
    APPEND2(name_esc);
    APPEND2("\",\"funcs\":[");

    int first = 1;
    for (size_t i = 0; i < fr->symbol_count; i++) {
        if (fr->symbols[i].type != METACALL_SYMBOL_FUNCTION &&
            fr->symbols[i].type != METACALL_SYMBOL_METHOD)
            continue;
        if (!first) APPEND2(",");
        first = 0;
        json_escape(fr->symbols[i].name ? fr->symbols[i].name : "", name_esc, sizeof(name_esc));

        char args_json[2048] = "[";
        for (size_t p = 0; p < fr->symbols[i].param_count && p < 32; p++) {
            if (p > 0) strcat(args_json, ",");
            char pesc[128];
            json_escape(fr->symbols[i].param_names && fr->symbols[i].param_names[p]
                        ? fr->symbols[i].param_names[p] : "", pesc, sizeof(pesc));
            char pbuf[200];
            snprintf(pbuf, sizeof(pbuf), "{\"name\":\"%s\",\"type\":\"Unknown\"}", pesc);
            strcat(args_json, pbuf);
        }
        strcat(args_json, "]");

        char buf[1200];
        snprintf(buf, sizeof(buf),
                 "{\"name\":\"%s\",\"async\":%s,"
                 "\"signature\":{\"ret\":{\"type\":\"Unknown\"},\"args\":%s}}",
                 name_esc,
                 fr->symbols[i].is_async ? "true" : "false",
                 args_json);
        APPEND2(buf);
    }

    APPEND2("],\"classes\":[");
    first = 1;
    for (size_t i = 0; i < fr->symbol_count; i++) {
        if (fr->symbols[i].type != METACALL_SYMBOL_CLASS) continue;
        if (!first) APPEND2(",");
        first = 0;
        json_escape(fr->symbols[i].name ? fr->symbols[i].name : "", name_esc, sizeof(name_esc));
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"name\":\"%s\"}", name_esc);
        APPEND2(buf);
    }
    APPEND2("],\"objects\":[]}}]}");

#undef APPEND2
    return json;
}
