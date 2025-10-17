#ifndef JSON_HELPERS_H
#define JSON_HELPERS_H

#include <jansson.h>

json_t *json_loads_safe(const char *s);

const char *json_get_string_else(json_t *obj, const char *key, const char *fallback);

#endif
