#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

#include "http.h"   // defines http_request, http_response, http_server + http_* funcs
#include "json.h"   // json_loads_safe, json_get_string_else
#include "parse.h"  // parse_code, parse_result

static int g_port = 7001;

/* GET /health */
static http_response handle_health(http_request *req) {
    (void)req;
    return http_json(200, "{\"status\":\"ok\"}");
}

/* POST /parse  { "language":"c", "code":"..."} */
static http_response handle_parse(http_request *req) {
    json_t *in = json_loads_safe(req->body);
    if (!in) return http_json(400, "{\"error\":\"invalid JSON\"}");

    const char *language = json_get_string_else(in, "language", "c");
    const char *code     = json_get_string_else(in, "code", "");

    parse_result r = parse_code(language, code);

    json_t *out = json_object();
    json_object_set_new(out, "ast",     r.ast_json);     // ownership transferred
    json_object_set_new(out, "summary", r.summary_json); // ownership transferred

    char *payload = json_dumps(out, JSON_COMPACT);
    json_decref(out);
    json_decref(in);

    http_response resp;
    if (!payload) {
        resp = http_json(500, "{\"error\":\"json encode failed\"}");
    } else {
        resp = http_json(200, payload);
        free(payload);
    }
    return resp;
}

/* parse CLI args like: --port 7001 */
static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            g_port = atoi(argv[++i]);
        }
    }
}

int main(int argc, char **argv) {
    parse_args(argc, argv);

    http_server srv = http_listen(g_port);
    if (srv.server_fd < 0) {
        fprintf(stderr, "Failed to bind to port %d\n", g_port);
        return 1;
    }

    http_route("GET",  "/health", handle_health);
    http_route("POST", "/parse",  handle_parse);

    http_serve(&srv);
    http_close(&srv);
    return 0;
}

