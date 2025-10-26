#include "parse.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

#include <tree_sitter/api.h>

// ext. symbols provided by tree-sitter-c grammar
extern const TSLanguage *tree_sitter_c(void);

/* --------------------------- small utilities --------------------------- */

static char *substr(const char *src, uint32_t start, uint32_t end) {
    if (!src || end <= start) return strndup("", 0);
    size_t len = (size_t)(end - start);
    char *out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, src + start, len);
    out[len] = '\0';
    return out;
}

static void str_trim(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    size_t i = 0, j = n;
    while (i < n && isspace((unsigned char)s[i])) i++;
    while (j > i && isspace((unsigned char)s[j-1])) j--;
    if (i > 0) memmove(s, s + i, j - i);
    s[j - i] = '\0';
}

static TSNode find_first_descendant_of_type(TSNode node, const char *type) {
    if (ts_node_is_null(node)) return (TSNode){0};
    const char *t = ts_node_type(node);
    if (t && strcmp(t, type) == 0) return node;
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_child(node, i);
        TSNode r = find_first_descendant_of_type(c, type);
        if (!ts_node_is_null(r)) return r;
    }
    return (TSNode){0};
}

static char *extract_identifier_text(TSNode ident, const char *source) {
    if (ts_node_is_null(ident)) return NULL;
    return substr(source, ts_node_start_byte(ident), ts_node_end_byte(ident));
}

// call_expression.function text
static char *extract_call_name(TSNode call_node, const char *source) {
    TSNode fn = ts_node_child_by_field_name(call_node, "function", 8);
    if (ts_node_is_null(fn)) return NULL;
    return substr(source, ts_node_start_byte(fn), ts_node_end_byte(fn));
}

// call_expression.arguments raw text "( ... )"
static char *extract_call_args_text(TSNode call_node, const char *source) {
    TSNode args = ts_node_child_by_field_name(call_node, "arguments", 9);
    if (ts_node_is_null(args)) return NULL;
    return substr(source, ts_node_start_byte(args), ts_node_end_byte(args));
}

// get parameter_list node from declarator
static TSNode get_parameter_list(TSNode func_def) {
    TSNode decl = ts_node_child_by_field_name(func_def, "declarator", 10);
    if (ts_node_is_null(decl)) return (TSNode){0};
    return find_first_descendant_of_type(decl, "parameter_list");
}

// return number of parameter_declaration children
static int parameter_count(TSNode param_list) {
    if (ts_node_is_null(param_list)) return 0;
    int cnt = 0;
    uint32_t n = ts_node_child_count(param_list);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_child(param_list, i);
        const char *ct = ts_node_type(c);
        if (ct && strcmp(ct, "parameter_declaration") == 0) cnt++;
    }
    return cnt;
}

// get i-th parameter_declaration node (0-based among such nodes)
static TSNode parameter_decl_at(TSNode param_list, int index) {
    if (ts_node_is_null(param_list)) return (TSNode){0};
    int k = -1;
    uint32_t n = ts_node_child_count(param_list);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_child(param_list, i);
        const char *ct = ts_node_type(c);
        if (ct && strcmp(ct, "parameter_declaration") == 0) {
            k++;
            if (k == index) return c;
        }
    }
    return (TSNode){0};
}

// naive pointer check: does the declarator for this parameter contain '*'?
static bool param_is_pointer(TSNode param_decl, const char *source) {
    TSNode decltor = find_first_descendant_of_type(param_decl, "pointer_declarator");
    if (!ts_node_is_null(decltor)) return true;
    // fallback: scan raw text for '*'
    char *txt = substr(source, ts_node_start_byte(param_decl), ts_node_end_byte(param_decl));
    if (!txt) return false;
    bool has_star = strchr(txt, '*') != NULL;
    free(txt);
    return has_star;
}

// extract identifier inside a function_definition
static char *extract_function_name_from_definition(TSNode func_def, const char *source) {
    TSNode decl = ts_node_child_by_field_name(func_def, "declarator", 10);
    if (ts_node_is_null(decl)) return NULL;
    TSNode ident = find_first_descendant_of_type(decl, "identifier");
    return extract_identifier_text(ident, source);
}

/* --------------------------- alias map ---------------------------
   Track simple assignments of the form:
     alias = n / k;
     alias = n >> k;
     alias = n - c;
   We only need last-seen value for each name to infer b (or decrease c).
*/

typedef enum { AL_NONE=0, AL_DIVIDE, AL_SHR, AL_DEC } AliasKind;
typedef struct {
    char *name;
    AliasKind kind;
    int k;    // for DIVIDE: b=k ; for SHR: k = shift amount ; for DEC: c = decrement
} AliasEntry;

typedef struct {
    AliasEntry *items;
    size_t len;
    size_t cap;
} AliasTable;

static void alias_init(AliasTable *T) { T->items=NULL; T->len=0; T->cap=0; }
static void alias_free(AliasTable *T) {
    for (size_t i=0;i<T->len;i++) free(T->items[i].name);
    free(T->items);
    T->items=NULL; T->len=0; T->cap=0;
}

static AliasEntry* alias_get_or_add(AliasTable *T, const char *name) {
    for (size_t i=0;i<T->len;i++) if (strcmp(T->items[i].name, name)==0) return &T->items[i];
    if (T->len==T->cap) {
        size_t ncap = T->cap? T->cap*2 : 8;
        T->items = (AliasEntry*)realloc(T->items, ncap*sizeof(AliasEntry));
        T->cap = ncap;
    }
    T->items[T->len] = (AliasEntry){strdup(name), AL_NONE, 0};
    return &T->items[T->len++];
}

static AliasEntry* alias_find(AliasTable *T, const char *name) {
    for (size_t i=0;i<T->len;i++) if (strcmp(T->items[i].name, name)==0) return &T->items[i];
    return NULL;
}

/* --------------------------- recurrence helpers --------------------------- */

static bool parse_pos_int(const char *s, int *out) {
    if (!s) return false;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return false;
    char *end=NULL; long v=strtol(s,&end,10);
    if (end==s || v<=0) return false;
    *out = (int)v; return true;
}

static int pow2_int(int k) { return (k>=0 && k<30) ? (1<<k) : 1; }

// analyze expression like "n/2", "n >> 1", "n-1" (spaces allowed)
static void analyze_expr_wrt_param(const char *expr, const char *param, bool *has_div_b, int *div_b,
                                   bool *has_dec, int *dec_c) {
    if (!expr || !param) return;
    char *p = strdup(expr);
    if (!p) return;
    str_trim(p);

    // strip trailing ';' if present
    size_t L = strlen(p);
    if (L && p[L-1]==';') p[L-1]='\0';

    // ensure param appears
    if (!strstr(p, param)) { free(p); return; }

    // n / k
    char *slash = strchr(p, '/');
    if (slash) {
        int k=0;
        if (parse_pos_int(slash+1, &k) && k>1) {
            *has_div_b = true;
            if (*div_b==0 || k<*div_b) *div_b = k; // keep smallest for upper bound
            free(p); return;
        }
    }
    // n >> k  (divide by 2^k)
    char *shr = strstr(p, ">>");
    if (shr) {
        int k=0;
        if (parse_pos_int(shr+2, &k) && k>0) {
            int b = pow2_int(k);
            *has_div_b = true;
            if (*div_b==0 || b<*div_b) *div_b = b;
            free(p); return;
        }
    }
    // n - c
    char *minus = strchr(p, '-');
    if (minus) {
        int c=0;
        if (parse_pos_int(minus+1, &c) && c>0) {
            *has_dec = true;
            if (*dec_c==0 || c<*dec_c) *dec_c = c;
            free(p); return;
        }
    }

    free(p);
}

// split "(a, b, c)" into vector of arg strings (caller frees each, and the array)
static char **split_args(const char *paren_args, int *out_count) {
    *out_count = 0;
    if (!paren_args) return NULL;
    char *s = strdup(paren_args);
    if (!s) return NULL;
    // remove outer parens
    if (s[0]=='(') {
        size_t L=strlen(s);
        if (L>=2 && s[L-1]==')') { s[L-1]='\0'; memmove(s, s+1, L-1); }
    }
    // simple csv split (no nested commas in our patterns)
    int cap=4, len=0;
    char **arr = (char**)malloc(cap*sizeof(char*));
    char *tok = strtok(s, ",");
    while (tok) {
        if (len==cap) { cap*=2; arr=(char**)realloc(arr, cap*sizeof(char*)); }
        char *t = strdup(tok); str_trim(t);
        arr[len++] = t;
        tok = strtok(NULL, ",");
    }
    free(s);
    *out_count = len;
    return arr;
}

/* --------------------------- traversal state --------------------------- */

typedef struct {
    // summary
    json_t *loops;
    json_t *calls;
    json_t *functions;

    // function frame
    char   *current_fn;
    int     loop_depth;
    int     max_loop_depth;
    int     loop_count;
    bool    saw_recursive_call;
    json_t *current_fn_calls;

    // size param inference
    char   *size_param_name;
    int     size_param_index; // -1 if unknown

    // alias table (mid = n/2)
    AliasTable aliases;

    // per-function recurrence inference
    int     self_calls_a;
    bool    has_divide_b;
    int     divide_b;
    bool    b_ambiguous;
    bool    has_decrease;
    int     decrease_c;
} WalkState;

static void enter_function(WalkState *S, const char *name) {
    if (S->current_fn) free(S->current_fn);
    S->current_fn = name ? strdup(name) : NULL;
    S->loop_depth = 0;
    S->max_loop_depth = 0;
    S->loop_count = 0;
    S->saw_recursive_call = false;
    if (S->current_fn_calls) json_decref(S->current_fn_calls);
    S->current_fn_calls = json_array();

    if (S->size_param_name) free(S->size_param_name);
    S->size_param_name = NULL;
    S->size_param_index = -1;

    alias_free(&S->aliases);
    alias_init(&S->aliases);

    S->self_calls_a = 0;
    S->has_divide_b = false;
    S->divide_b = 0;
    S->b_ambiguous = false;
    S->has_decrease = false;
    S->decrease_c = 0;
}

static void consider_divide_b(WalkState *S, int b) {
    if (b <= 1) return;
    if (!S->has_divide_b) {
        S->has_divide_b = true;
        S->divide_b = b;
    } else if (S->divide_b != b) {
        if (b < S->divide_b) S->divide_b = b; // keep smallest
        S->b_ambiguous = true;
    }
}

static void choose_size_param(TSNode func_def, const char *source, WalkState *S) {
    TSNode plist = get_parameter_list(func_def);
    int n = parameter_count(plist);
    if (n <= 0) return;

    // strategy: prefer a param named 'n'; otherwise rightmost non-pointer
    int candidate = -1;
    for (int i=0;i<n;i++) {
        TSNode pd = parameter_decl_at(plist, i);
        TSNode ident = find_first_descendant_of_type(pd, "identifier");
        if (ts_node_is_null(ident)) continue;
        char *nm = extract_identifier_text(ident, source);
        if (!nm) continue;
        if (strcmp(nm, "n")==0) {
            S->size_param_index = i;
            S->size_param_name = nm;
            return;
        }
        bool is_ptr = param_is_pointer(pd, source);
        if (!is_ptr) candidate = i; // keep rightmost non-pointer
        free(nm);
    }
    if (candidate >= 0) {
        TSNode pd = parameter_decl_at(plist, candidate);
        TSNode ident = find_first_descendant_of_type(pd, "identifier");
        if (!ts_node_is_null(ident)) {
            S->size_param_index = candidate;
            S->size_param_name = extract_identifier_text(ident, source);
        }
    }
}

static void leave_function(WalkState *S, json_t *top_recurrences, json_t *functions_out) {
    if (!S->current_fn) return;

    json_t *fn_obj = json_object();
    json_object_set_new(fn_obj, "name", json_string(S->current_fn));
    json_object_set_new(fn_obj, "is_recursive", json_boolean(S->saw_recursive_call));
    json_object_set_new(fn_obj, "calls", json_incref(S->current_fn_calls));
    json_object_set_new(fn_obj, "loopCount", json_integer(S->loop_count));
    json_object_set_new(fn_obj, "maxLoopDepth", json_integer(S->max_loop_depth));
    if (S->size_param_name) json_object_set_new(fn_obj, "sizeParam", json_string(S->size_param_name));
    if (S->size_param_index >= 0) json_object_set_new(fn_obj, "sizeParamIndex", json_integer(S->size_param_index));

    if (S->saw_recursive_call) {
        // f(n) from loop nesting
        const char *f_expr = "1";
        char buf[32];
        if (S->max_loop_depth == 1) f_expr = "n";
        else if (S->max_loop_depth >= 2) { snprintf(buf, sizeof(buf), "n^%d", S->max_loop_depth); f_expr = buf; }

        json_t *rec = json_object();
        json_object_set_new(rec, "a", json_integer(S->self_calls_a));
        json_object_set_new(rec, "f", json_string(f_expr));
        if (S->has_decrease) {
            json_object_set_new(rec, "model", json_string("decrease"));
            json_object_set_new(rec, "c", json_integer(S->decrease_c));
        }
        if (S->has_divide_b && S->divide_b > 1) {
            json_object_set_new(rec, "b", json_integer(S->divide_b));
            json_object_set_new(rec, "model", json_string("divide"));
            if (S->b_ambiguous) json_object_set_new(rec, "b_ambiguous", json_boolean(true));
        }

        json_object_set_new(fn_obj, "recurrence", json_incref(rec));

        // push into top-level recurrences with function name
        json_t *entry = json_object();
        json_object_set_new(entry, "function", json_string(S->current_fn));
        json_object_set(entry, "a", json_object_get(rec, "a"));
        json_object_set(entry, "f", json_object_get(rec, "f"));
        if (json_object_get(rec, "b")) json_object_set(entry, "b", json_object_get(rec, "b"));
        if (json_object_get(rec, "model")) json_object_set(entry, "model", json_object_get(rec, "model"));
        if (json_object_get(rec, "c")) json_object_set(entry, "c", json_object_get(rec, "c"));
        if (S->b_ambiguous) json_object_set_new(entry, "b_ambiguous", json_boolean(true));
        json_array_append_new(top_recurrences, entry);

        json_decref(rec);
    }

    json_array_append_new(functions_out, fn_obj);

    // cleanup frame
    free(S->current_fn); S->current_fn=NULL;
    if (S->size_param_name) { free(S->size_param_name); S->size_param_name=NULL; }
    if (S->current_fn_calls) { json_decref(S->current_fn_calls); S->current_fn_calls=NULL; }
    alias_free(&S->aliases);
}

/* --------------------------- node analysis --------------------------- */

// Extract assignment/initializer patterns for alias := n/2, n>>k, n-c
static void maybe_record_alias(TSNode node, const char *source, const char *size_param, AliasTable *aliases) {
    if (!size_param) return;
    const char *t = ts_node_type(node);
    if (!t) return;

    // handle simple "identifier = expr" and "declaration with initializer"
    bool matched = false;
    char *lhs_name = NULL;
    char *rhs_text = NULL;

    if (strcmp(t, "assignment_expression") == 0) {
        // child_by_field_name: left, right
        TSNode L = ts_node_child_by_field_name(node, "left", 4);
        TSNode R = ts_node_child_by_field_name(node, "right", 5);
        if (!ts_node_is_null(L) && !ts_node_is_null(R)) {
            TSNode id = find_first_descendant_of_type(L, "identifier");
            if (!ts_node_is_null(id)) {
                lhs_name = extract_identifier_text(id, source);
                rhs_text = substr(source, ts_node_start_byte(R), ts_node_end_byte(R));
                matched = (lhs_name && rhs_text);
            }
        }
    } else if (strcmp(t, "init_declarator") == 0) {
        // for declarations like "int mid = n/2;"
        TSNode id = find_first_descendant_of_type(node, "identifier");
        TSNode init = ts_node_child_by_field_name(node, "value", 5);
        if (!ts_node_is_null(id) && !ts_node_is_null(init)) {
            lhs_name = extract_identifier_text(id, source);
            rhs_text = substr(source, ts_node_start_byte(init), ts_node_end_byte(init));
            matched = (lhs_name && rhs_text);
        }
    }

    if (!matched) return;
    char *expr = strdup(rhs_text);
    free(rhs_text);
    str_trim(expr);

    bool has_div=false, has_dec=false;
    int div_b=0, dec_c=0;
    analyze_expr_wrt_param(expr, size_param, &has_div, &div_b, &has_dec, &dec_c);

    if (has_div || has_dec) {
        AliasEntry *E = alias_get_or_add(aliases, lhs_name);
        if (has_div && div_b>1)      { E->kind = AL_DIVIDE; E->k = div_b; }
        else if (has_dec && dec_c>0) { E->kind = AL_DEC;    E->k = dec_c; }
    }

    free(lhs_name);
    free(expr);
}

static void analyze_self_call(TSNode call_node, const char *source, WalkState *S) {
    S->self_calls_a += 1;

    // If we don't know the size param, we can't infer b from args
    if (S->size_param_index < 0 || !S->size_param_name) return;

    char *args_txt = extract_call_args_text(call_node, source);
    if (!args_txt) return;

    int argc = 0;
    char **argv = split_args(args_txt, &argc);
    free(args_txt);

    if (argc > S->size_param_index) {
        const char *arg = argv[S->size_param_index];

        // direct forms: n/2, n>>1, n-1
        bool has_div=false, has_dec=false; int div_b=0, dec_c=0;
        analyze_expr_wrt_param(arg, S->size_param_name, &has_div, &div_b, &has_dec, &dec_c);
        if (has_div && div_b>1) consider_divide_b(S, div_b);
        if (has_dec && dec_c>0) { S->has_decrease = true; if (S->decrease_c==0 || dec_c<S->decrease_c) S->decrease_c=dec_c; }

        // alias form: argument is an identifier like "mid"
        if (!has_div && !has_dec) {
            TSNode func = ts_node_parent(call_node);
            (void)func; // not needed: we already recorded aliases during traversal
            // check alias table
            const char *id = arg;
            // strip any address/arithmetic noise
            while (*id && isspace((unsigned char)*id)) id++;
            // only simple identifiers
            bool simple=true;
            for (const char *c=id; *c; ++c) { if (!(isalnum((unsigned char)*c) || *c=='_')) { simple=false; break; } }
            if (simple) {
                AliasEntry *E = alias_find(&S->aliases, id);
                if (E) {
                    if (E->kind==AL_DIVIDE && E->k>1) consider_divide_b(S, E->k);
                    else if (E->kind==AL_SHR && E->k>0) consider_divide_b(S, pow2_int(E->k));
                    else if (E->kind==AL_DEC && E->k>0) { S->has_decrease = true; if (S->decrease_c==0 || E->k<S->decrease_c) S->decrease_c=E->k; }
                }
            }
        }
    }

    for (int i=0;i<argc;i++) free(argv[i]);
    free(argv);
}

/* --------------------------- traversal --------------------------- */

static void traverse_collect(TSNode node, const char *source, WalkState *S, json_t *top_recs, json_t *functions_out) {
    if (ts_node_is_null(node)) return;
    const char *type = ts_node_type(node);

    if (strcmp(type, "function_definition") == 0) {
        char *fn_name = extract_function_name_from_definition(node, source);
        enter_function(S, fn_name);
        free(fn_name);

        // choose size parameter (name + index)
        choose_size_param(node, source, S);

        // visit children
        uint32_t N = ts_node_child_count(node);
        for (uint32_t i=0;i<N;i++) {
            TSNode c = ts_node_child(node, i);
            traverse_collect(c, source, S, top_recs, functions_out);
        }

        // finish this function
        leave_function(S, top_recs, functions_out);
        return;
    }

    // record loops and nesting depth
    if (strcmp(type, "for_statement") == 0 || strcmp(type, "while_statement") == 0) {
        json_t *obj = json_object();
        json_object_set_new(obj, "kind", json_string(strcmp(type,"for_statement")==0 ? "for" : "while"));
        json_object_set_new(obj, "bound", json_string("n")); // simple placeholder
        json_object_set_new(obj, "depth", json_integer(S->loop_depth + 1));
        json_array_append_new(S->loops, obj);

        if (S->current_fn) {
            S->loop_count += 1;
            if (S->loop_depth + 1 > S->max_loop_depth) S->max_loop_depth = S->loop_depth + 1;
        }

        S->loop_depth += 1;
        uint32_t N = ts_node_child_count(node);
        for (uint32_t i=0;i<N;i++) traverse_collect(ts_node_child(node,i), source, S, top_recs, functions_out);
        S->loop_depth -= 1;
        return;
    }

    // track simple aliases (mid = n/2)
    if (strcmp(type, "assignment_expression") == 0 || strcmp(type, "init_declarator") == 0) {
        if (S->current_fn && S->size_param_name) {
            maybe_record_alias(node, source, S->size_param_name, &S->aliases);
        }
    }

    // calls
    if (strcmp(type, "call_expression") == 0) {
        char *name = extract_call_name(node, source);
        if (name && name[0]) {
            json_array_append_new(S->calls, json_string(name));
            if (S->current_fn) {
                json_array_append_new(S->current_fn_calls, json_string(name));
                if (strcmp(name, S->current_fn) == 0) {
                    S->saw_recursive_call = true;
                    analyze_self_call(node, source, S);
                }
            }
        }
        free(name);
    }

    // default: descend
    uint32_t N = ts_node_child_count(node);
    for (uint32_t i=0;i<N;i++) traverse_collect(ts_node_child(node,i), source, S, top_recs, functions_out);
}

/* --------------------------- public api --------------------------- */

parse_result parse_code(const char *language, const char *code) {
    parse_result r = (parse_result){0};

    json_t *ast = json_object();
    json_t *summary = json_object();
    json_t *loops = json_array();
    json_t *calls = json_array();
    json_t *functions = json_array();
    json_t *top_recurrences = json_array();

    json_object_set_new(ast, "language", json_string(language ? language : "unknown"));
    json_object_set_new(ast, "rootType", json_string("unknown"));

    if (!language || !code || !*code) {
        json_object_set_new(summary, "loops", loops);
        json_object_set_new(summary, "calls", calls);
        json_object_set_new(summary, "functions", functions);
        json_object_set_new(summary, "recurrences", top_recurrences);
        r.ast_json = ast; r.summary_json = summary; return r;
    }

    if (strcmp(language, "c") == 0) {
        TSParser *parser = ts_parser_new();
        ts_parser_set_language(parser, tree_sitter_c());
        TSTree *tree = ts_parser_parse_string(parser, NULL, code, (uint32_t)strlen(code));
        TSNode root = ts_tree_root_node(tree);
        json_object_set_new(ast, "rootType", json_string(ts_node_type(root)));

        WalkState S = {0};
        S.loops = loops; S.calls = calls; S.functions = functions;
        alias_init(&S.aliases);

        traverse_collect(root, code, &S, top_recurrences, functions);

        ts_tree_delete(tree);
        ts_parser_delete(parser);
        alias_free(&S.aliases);
    }

    json_object_set_new(summary, "loops", loops);
    json_object_set_new(summary, "calls", calls);
    json_object_set_new(summary, "functions", functions);
    json_object_set_new(summary, "recurrences", top_recurrences);

    // Convenience: if exactly one divide recurrence found, expose summary.recurrence {a,b,f}
    if (json_array_size(top_recurrences) == 1) {
        json_t *only = json_array_get(top_recurrences, 0);
        json_t *model = json_object_get(only, "model");
        json_t *b = json_object_get(only, "b");
        if (model && b) {
            const char *m = json_string_value(model);
            if (m && strcmp(m,"divide")==0 && json_integer_value(b) > 1) {
                json_t *rec = json_object();
                json_object_set(rec, "a", json_object_get(only, "a"));
                json_object_set(rec, "b", json_object_get(only, "b"));
                json_object_set(rec, "f", json_object_get(only, "f"));
                json_object_set_new(summary, "recurrence", rec);
            }
        }
    }

    r.ast_json = ast;
    r.summary_json = summary;
    return r;
}

void free_parse_result(parse_result *r) {
    if (!r) return;
    if (r->ast_json) json_decref(r->ast_json);
    if (r->summary_json) json_decref(r->summary_json);
    r->ast_json = NULL;
    r->summary_json = NULL;
}
