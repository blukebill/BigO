#include "http.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define MAX_ROUTES 8

typedef struct {
	char method[8];
	char path[64];
	route_handler handler;
} route_entry;

static route_entry ROUTES[MAX_ROUTES];
static int ROUTE_COUNT = 0;

static int starts_with(const char *s, const char *pfx) {
	return strncmp(s, pfx, strlen(pfx)) == 0;
}

static void trim_crlf(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}

http_server http_listen(int port) {
	http_server srv = { .port = port, .server_fd = -1 };

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	if(fd < 0) { perror("socket"); return srv; }

	int opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((uint16_t)port);

	if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(fd);
		return srv;
	}

	if(listen(fd, 16) < 0) {
		perror("listen");
		close(fd);
		return srv;
	}

	srv.server_fd = fd;
	fprintf(stderr, "[http] listening on :%d\n", port);
	return srv;
}

void http_close(http_server *srv) {
	if(!srv) return;
	if(srv->server_fd >= 0) {
		close(srv->server_fd);
		srv->server_fd = -1;
	}
}

void http_route(const char *method, const char *path, route_handler handler) {
	if(ROUTE_COUNT >= MAX_ROUTES) return;
	route_entry *e = &ROUTES[ROUTE_COUNT++];
	snprintf(e->method, sizeof(e->method), "%s", method);
	snprintf(e->path, sizeof(e->path), "%s", path);
	e->handler = handler;
}

static route_handler find_route(const char *method, const char *path) {
	for(int i = 0; i < ROUTE_COUNT; i++) {
		if(strcmp(ROUTES[i].method, method) == 0 && strcmp(ROUTES[i].path, path) == 0) {
			return ROUTES[i].handler;
		}
	}
	return NULL;
}

static int recv_line(int fd, char *buf, size_t maxlen) {
	size_t i = 0;
	while(i < maxlen - 1) {
		char c;
		int n = recv(fd, &c, 1, 0);
		if(n <= 0) return (int)n;
		buf[i++] = c;
		if(c == '\n') break;
	}
	buf[i] = '\0';
	return (int)i;
}

static void send_all(int fd, const char *buf, size_t len) {
	size_t off = 0;
	while (off < len) {
		ssize_t n = send(fd, buf + off, len - off, 0);
		if(n <= 0) return;
		off += (size_t)n;
	}
}

static void write_response(int fd, http_response *res) {
	char header[512];
	const char *ct = res->content_type ? res->content_type : "text/plain; charset=utf-8";
	int n = snprintf(header, sizeof(header),
			"HTTP/1.1 %d OK\r\n"
			"Content-Type: %s\r\n"
			"Content-Length: %zu\r\n"
			"Connection: close\r\n"
			"\r\n",
			res->status, ct, res->body_len);
	send_all(fd, header, (size_t)n);
	if(res->body && res->body_len > 0) {
		send_all(fd, res->body, res->body_len);
	}
}

static void handle_client(int cfd) {
    char line[4096];
    char method[8] = {0};
    char path[64]  = {0};
    char httpver[16] = {0};

    // request line
    if (recv_line(cfd, line, sizeof(line)) <= 0) {
        close(cfd);
        return;
    }
    trim_crlf(line);

    if (sscanf(line, "%7s %63s %15s", method, path, httpver) != 3) {
        // malformed request line
        http_response bad = http_text(400, "Bad Request");
        write_response(cfd, &bad);
        http_response_free(&bad);
        close(cfd);
        return;
    }
    fprintf(stderr, "[http] %s %s %s\n", method, path, httpver);

    // headers
size_t content_length = 0;
int header_lines = 0;

for (;;) {
    int n = recv_line(cfd, line, sizeof(line));
    if (n <= 0) {
        // client closed or error
        close(cfd);
        return;
    }
    trim_crlf(line);

    // debug: show each header as parsed
    fprintf(stderr, "[http] header: \"%s\"\n", line);

    if (line[0] == '\0') {
        // blank line -> end of headers
        break;
    }

    if (starts_with(line, "Content-Length:")) {
        const char *p = line + strlen("Content-Length:");
        while (*p == ' ' || *p == '\t') p++;
        content_length = (size_t)strtoul(p, NULL, 10);
    }

    // don't loop forever if blank line never arrives
    if (++header_lines > 200) {
        http_response bad = http_text(400, "Bad Request: header too long");
        write_response(cfd, &bad);
        http_response_free(&bad);
        close(cfd);
        return;
    }
}


    // body (only if content-length > 0, e.g., POST /parse)
    char *body = NULL;
    if (content_length > 0) {
        body = (char*)malloc(content_length + 1);
        if (!body) { close(cfd); return; }
        size_t got = 0;
        while (got < content_length) {
            ssize_t k = recv(cfd, body + got, content_length - got, 0);
            if (k <= 0) { free(body); close(cfd); return; }
            got += (size_t)k;
        }
        body[content_length] = '\0';
    }

    http_request req = {0};
    snprintf(req.method, sizeof(req.method), "%s", method);
    snprintf(req.path,   sizeof(req.path),   "%s", path);
    req.body = body;
    req.body_len = content_length;

    // route dispatch
    http_response res;
    route_handler h = find_route(req.method, req.path);
    if (h) res = h(&req);
    else   res = http_json(404, "{\"error\":\"not found\"}");

    // send response
    write_response(cfd, &res);
    http_response_free(&res);
    http_request_free(&req);
    close(cfd);
}


void http_serve(http_server *srv) {
  if (!srv || srv->server_fd < 0) return;

  for (;;) {
    struct sockaddr_in cli;
    socklen_t cl = sizeof(cli);
    int cfd = accept(srv->server_fd, (struct sockaddr*)&cli, &cl);
    if (cfd < 0) {
      if (errno == EINTR) continue;
      perror("accept");
      break;
    }
    handle_client(cfd);
  }
}

http_response http_json(int status, const char *json_utf8) {
  http_response r = {0};
  r.status = status;
  r.content_type = "application/json; charset=utf-8";
  if (json_utf8) {
    size_t n = strlen(json_utf8);
    r.body = (char*)malloc(n + 1);
    memcpy(r.body, json_utf8, n + 1);
    r.body_len = n;
  }
  return r;
}

http_response http_text(int status, const char *text) {
  http_response r = {0};
  r.status = status;
  r.content_type = "text/plain; charset=utf-8";
  if (text) {
    size_t n = strlen(text);
    r.body = (char*)malloc(n + 1);
    memcpy(r.body, text, n + 1);
    r.body_len = n;
  }
  return r;
}

void http_response_free(http_response *res) {
  if (!res) return;
  if (res->body) free(res->body);
  res->body = NULL;
  res->body_len = 0;
}

void http_request_free(http_request *req) {
  if (!req) return;
  if (req->body) free(req->body);
  req->body = NULL;
  req->body_len = 0;
}
		
