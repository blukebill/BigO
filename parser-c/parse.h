#ifndef PARSE_H
#define PARSE_H

#include <jansson.h>

typedef struct {
    json_t *ast_json;
    json_t *summary_json;
} parse_result;

parse_result parse_code(const char *language, const char *code);
void free_parse_result(parse_result *r);

#endif

