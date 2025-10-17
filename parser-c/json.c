#include "json.h"

json_t *json_loads_safe(const char *s) {
	if(!s) return NULL;
	json_error_t err;
	json_t *root = json_loads(s, 0, &err);
	if(!root) {
		return NULL;
	}
	return root;
	}

const char *json_get_string_else(json_t *obj, const char *key, const char *fallback) {
	if(!obj || !key) return fallback;
	json_t *v = json_object_get(obj, key);
	if(!json_is_string(v)) return fallback;
	return json_string_value(v);
}

