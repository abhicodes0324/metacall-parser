/**
 * MCP CLI - Multi-Language Parser command-line interface
 *
 * Usage:
 *   mcp parse <file>           - Parse file and list functions/classes/imports
 *   mcp deps <directory>       - Build dependency graph for directory
 *   mcp list-functions <path>  - List all functions (file or directory)
 */

#include "mcp_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s parse <file>              Parse file and output JSON\n", prog);
    fprintf(stderr, "  %s deps <directory>          Build dependency graph (JSON)\n", prog);
    fprintf(stderr, "  %s list-functions <path>     List functions/classes\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --format json|text           Output format (default: json for parse/deps)\n");
}

static void print_result_text(const mcp_result *result)
{
    const mcp_file_result *fr = mcp_result_get_file((mcp_result *)result);
    if (!fr) return;

    printf("File: %s\n", fr->file_path ? fr->file_path : "(unknown)");
    printf("Language: %s\n\n", mcp_lang_name(fr->language));

    printf("Functions:\n");
    for (size_t i = 0; i < fr->symbol_count; i++) {
        if (fr->symbols[i].type == MCP_SYMBOL_FUNCTION || fr->symbols[i].type == MCP_SYMBOL_METHOD) {
            printf("  - %s (line %u)%s\n", fr->symbols[i].name,
                   (unsigned)fr->symbols[i].line,
                   fr->symbols[i].parent_class ? fr->symbols[i].parent_class : "");
        }
    }

    printf("\nClasses:\n");
    for (size_t i = 0; i < fr->symbol_count; i++) {
        if (fr->symbols[i].type == MCP_SYMBOL_CLASS) {
            printf("  - %s (line %u)\n", fr->symbols[i].name, (unsigned)fr->symbols[i].line);
        }
    }

    printf("\nImports:\n");
    for (size_t i = 0; i < fr->import_count; i++) {
        printf("  - %s\n", fr->imports[i].module ? fr->imports[i].module : "(unknown)");
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    const char *path = argv[2];
    int format_json = 1;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format_json = (strcmp(argv[i + 1], "json") == 0);
            i++;
        }
    }

    mcp_parser *parser = mcp_parser_create();
    if (!parser) {
        fprintf(stderr, "Failed to create parser\n");
        return 1;
    }

    int ret = 0;

    if (strcmp(cmd, "parse") == 0) {
        mcp_result *result = mcp_parser_parse_file(parser, path, NULL, 0);
        if (!result) {
            fprintf(stderr, "Failed to parse %s\n", path);
            ret = 1;
        } else {
            if (format_json) {
                char *json = mcp_result_to_json(result);
                if (json) {
                    printf("%s\n", json);
                    free(json);
                }
            } else {
                print_result_text(result);
            }
            mcp_result_free(result);
        }
    } else if (strcmp(cmd, "deps") == 0) {
        mcp_dep_graph *graph = mcp_parser_build_deps(parser, path, 1);
        if (!graph) {
            fprintf(stderr, "Failed to build dependency graph for %s\n", path);
            ret = 1;
        } else {
            char *json = mcp_dep_graph_to_json(graph);
            if (json) {
                printf("%s\n", json);
                free(json);
            }
            mcp_dep_graph_free(graph);
        }
    } else if (strcmp(cmd, "list-functions") == 0) {
        mcp_result *result = mcp_parser_parse_file(parser, path, NULL, 0);
        if (!result) {
            fprintf(stderr, "Failed to parse %s\n", path);
            ret = 1;
        } else {
            print_result_text(result);
            mcp_result_free(result);
        }
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_usage(argv[0]);
        ret = 1;
    }

    mcp_parser_destroy(parser);
    return ret;
}
