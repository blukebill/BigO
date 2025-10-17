#ifdef PARSE_H
#define PARSE_H

#include <jansson.h>

typedef struct {
	json_t *ast_json;
	json_t *summary_json;
} parse_result;

//parse code for a given lang and return AST/summary

parse_result parse_code(const char *language, const char *code);

//utility to free parse_result safely

void free_parse_result(parse_result *r);

#endif
