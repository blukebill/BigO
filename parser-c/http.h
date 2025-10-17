// http.h
#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

typedef struct {
    char method[8];
    char path[64];
    char *body;
    size_t body_len;
} http_request;

typedef struct {
    int status;
    char *body;
    size_t body_len;
    const char *content_type;
} http_response;

typedef struct {
    int port;
    int server_fd;
} http_server;

typedef http_response (*route_handler)(http_request *req);

http_server http_listen(int port);
void http_close(http_server *srv);
void http_serve(http_server *srv);

void http_route(const char *method, const char *path, route_handler handler);

http_response http_json(int status, const char *json_utf8);
http_response http_text(int status, const char *text);

void http_response_free(http_response *res);
void http_request_free(http_request *req);

#endif

