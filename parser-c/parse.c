#include "parse.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <tree_sitter/api.h>

//ext. symbols provided by tree-sitter-c grammar
//header-less extern is standard to link generated grammar

extern const TSLanguage *tree_sitter_c(void);

//helper: duplicate substring from source
static char *substr(const char *src, uint32_t start, uint32_t end) {
	if (end <= start) return strndup("", 0);
	size_t len = (size_t)(end - start);
	char *out = (char*)malloc(len + 1);
	if (!out) return NULL;
	memcpy(out, src + start, len);
	out[len] - '\0';
	return out;
}

//extract identifier text for call_expression's 'function' field
static char *extract_call_name(TSNode call_node, const char *source) {
	TSNode fn = ts_node_child_by_field_name(call_node, "function", 8);
	if (ts_node_is_null(fn)) return NULL;
	uint32_t s = ts_node_start_byte(fn);
	uint32_t e = ts_node_end_byte(fn);

	return substr(source, s, e);
}

//traverse tree and collect loops & calls for json arrays
static void traverse_collect(TSNode node, const char *source, json_t *loops, json_t *calls) {
	const char *type = ts_node_type(node);

	if(strcmp(type, "for_statement") == 0) {
		json_t *obj = json_object();
		json_object_set_new(obj, "kind", json_string("for"));
		json_object_set_new(obj, "bound", json_string("n"));
		json_array_append_new(loops, obj);
	} else if (strcmp(type, "while_statement") == 0) {
		json_t *obj = json_object();;
		json_object_set_new(obj, "kind", json_string("while"));
		json_object_set_new(obj, "bound", json_string("n"));
		json_array_append_new(loops, obj);
	} else if (strcmp(type, "call_expression") == 0) {
		char *name = extract_call_name(node, source);
		if (name && name[0] != '\0') {
			json_array_append_new(calls, json_string(name));
		}
		if(name) free(name);
	}

	uint32_t child_count = ts_node_child_count(node);
	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(node, i);
		if(!ts_node_is_null(child)) {
			traverse_collect(child, source, loops, calls);
		}
	}
}

parse_result parse_code(const char *language, const char *code) {
	parse_result r = {0};

	//prepare containers
	json_t *ast = json_object();
	json_t *summary = json_object();
	json_t *loops = json_array();
	json_t *calls = json_array();

	//ast data
	json_object_set_new(ast, "language", json_string(language ? language : "unkown"));
	json_object_set_new(ast, "rootType", json_string("unknown"));

	if(!language || !code || code[0] == '\0') {
		json_object_set_new(summary, "loops", loops);
		json_object_set_new(summary, "calls", calls);
		r.ast_json = ast;
		r.summary_json = summary;
		return r;
	}

	//only c supported currently, easily extendable
	if(strcmp(language, "c") == 0) {
		TSParser *parser = ts_parser_new();

		ts_parser_set_language(parser, tree_sitter_c());

		TSTree *tree = ts_parser_parse_string(parser, NULL, code, (uint32_t)strlen(code));
		TSNode root = ts_tree_root_node(tree);

		json_object_set_new(ast, "rootType", json_string(ts_node_type(root)));

		traverse_collect(root, code, loops, calls);

		ts_tree_delete(tree);
		ts_parser_delete(parser);
	}

	json_object_set_new(summary, "loops", loops);
	json_object_set_new(summary, "calls", calls);
	r.ast_json = ast;
	r.summary_json = summary;
	return r;
}

void free_parse_result(parse_result *r) {
	if(!r) return;
	if(r->ast_json) json_decref(r->ast_json);
			if(r->summary_json) json_decref(r->summary_json);
			r->ast_json == NULL;
			r->summary_json = NULL;
}

