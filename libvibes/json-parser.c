#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "string-list.h"
#include "libvibes/json-parser.h"

/*
 * Minimal recursive-descent JSON parser.
 *
 * Grammar:
 *   value   = object | array | string | number | "true" | "false" | "null"
 *   object  = '{' [ pair (',' pair)* ] '}'
 *   pair    = string ':' value
 *   array   = '[' [ value (',' value)* ] ']'
 *   string  = '"' chars '"'
 *   number  = [-] digits ['.' digits] [('e'|'E') [+-] digits]
 */

/* --- Lexer helpers --- */

static const char *skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	return p;
}

/*
 * Parse a JSON string (including surrounding quotes) into a strbuf.
 * Handles escape sequences: \", \\, \/, \n, \r, \t, \uXXXX.
 * Returns pointer past the closing quote, or NULL on error.
 */
static const char *parse_string_raw(const char *p, struct strbuf *out)
{
	if (*p != '"')
		return NULL;
	p++;

	while (*p && *p != '"') {
		if (*p == '\\') {
			p++;
			switch (*p) {
			case '"':  strbuf_addch(out, '"');  break;
			case '\\': strbuf_addch(out, '\\'); break;
			case '/':  strbuf_addch(out, '/');  break;
			case 'n':  strbuf_addch(out, '\n'); break;
			case 'r':  strbuf_addch(out, '\r'); break;
			case 't':  strbuf_addch(out, '\t'); break;
			case 'b':  strbuf_addch(out, '\b'); break;
			case 'f':  strbuf_addch(out, '\f'); break;
			case 'u': {
				/* \uXXXX — decode as ASCII if possible */
				unsigned int code = 0;
				int i;
				p++;
				for (i = 0; i < 4 && *p; i++, p++) {
					code <<= 4;
					if (*p >= '0' && *p <= '9')
						code |= *p - '0';
					else if (*p >= 'a' && *p <= 'f')
						code |= *p - 'a' + 10;
					else if (*p >= 'A' && *p <= 'F')
						code |= *p - 'A' + 10;
					else
						return NULL;
				}
				if (code < 0x80)
					strbuf_addch(out, (char)code);
				else if (code < 0x800) {
					strbuf_addch(out, 0xC0 | (code >> 6));
					strbuf_addch(out, 0x80 | (code & 0x3F));
				} else {
					strbuf_addch(out, 0xE0 | (code >> 12));
					strbuf_addch(out, 0x80 | ((code >> 6) & 0x3F));
					strbuf_addch(out, 0x80 | (code & 0x3F));
				}
				continue; /* p already advanced */
			}
			default:
				/* Unknown escape, keep literal */
				strbuf_addch(out, *p);
				break;
			}
			p++;
		} else {
			strbuf_addch(out, *p);
			p++;
		}
	}

	if (*p != '"')
		return NULL;
	return p + 1;
}

/* Forward declaration */
static const char *parse_value(const char *p, struct vibes_json *out);

static const char *parse_object(const char *p, struct vibes_json *out)
{
	int alloc = 4;
	int nr = 0;
	char **keys;
	struct vibes_json *vals;

	if (*p != '{')
		return NULL;
	p = skip_ws(p + 1);

	out->type = JSON_OBJECT;

	keys = xcalloc(alloc, sizeof(char *));
	vals = xcalloc(alloc, sizeof(struct vibes_json));

	if (*p == '}') {
		out->keys = keys;
		out->values = vals;
		out->nr_members = 0;
		return p + 1;
	}

	for (;;) {
		struct strbuf key = STRBUF_INIT;

		p = skip_ws(p);
		p = parse_string_raw(p, &key);
		if (!p) {
			strbuf_release(&key);
			goto err;
		}

		p = skip_ws(p);
		if (*p != ':') {
			strbuf_release(&key);
			goto err;
		}
		p = skip_ws(p + 1);

		/* Grow arrays if needed */
		if (nr >= alloc) {
			alloc *= 2;
			REALLOC_ARRAY(keys, alloc);
			REALLOC_ARRAY(vals, alloc);
			memset(vals + nr, 0,
			       (alloc - nr) * sizeof(struct vibes_json));
		}

		keys[nr] = strbuf_detach(&key, NULL);
		p = parse_value(p, &vals[nr]);
		if (!p)
			goto err;
		nr++;

		p = skip_ws(p);
		if (*p == ',') {
			p++;
			continue;
		}
		if (*p == '}') {
			p++;
			break;
		}
		goto err;
	}

	out->keys = keys;
	out->values = vals;
	out->nr_members = nr;
	return p;

err:
	{
		int i;
		for (i = 0; i < nr; i++) {
			free(keys[i]);
			vibes_json_free(&vals[i]);
		}
		free(keys);
		free(vals);
	}
	return NULL;
}

static const char *parse_array(const char *p, struct vibes_json *out)
{
	int alloc = 4;
	int nr = 0;
	struct vibes_json *elems;

	if (*p != '[')
		return NULL;
	p = skip_ws(p + 1);

	out->type = JSON_ARRAY;

	elems = xcalloc(alloc, sizeof(struct vibes_json));

	if (*p == ']') {
		out->elements = elems;
		out->nr_elements = 0;
		return p + 1;
	}

	for (;;) {
		p = skip_ws(p);

		if (nr >= alloc) {
			alloc *= 2;
			REALLOC_ARRAY(elems, alloc);
			memset(elems + nr, 0,
			       (alloc - nr) * sizeof(struct vibes_json));
		}

		p = parse_value(p, &elems[nr]);
		if (!p)
			goto err;
		nr++;

		p = skip_ws(p);
		if (*p == ',') {
			p++;
			continue;
		}
		if (*p == ']') {
			p++;
			break;
		}
		goto err;
	}

	out->elements = elems;
	out->nr_elements = nr;
	return p;

err:
	{
		int i;
		for (i = 0; i < nr; i++)
			vibes_json_free(&elems[i]);
		free(elems);
	}
	return NULL;
}

static const char *parse_number(const char *p, struct vibes_json *out)
{
	char *end;
	double val;

	val = strtod(p, &end);
	if (end == p)
		return NULL;

	out->type = JSON_NUMBER;
	out->num_val = val;
	return end;
}

static const char *parse_value(const char *p, struct vibes_json *out)
{
	p = skip_ws(p);

	memset(out, 0, sizeof(*out));

	if (*p == '"') {
		struct strbuf s = STRBUF_INIT;
		p = parse_string_raw(p, &s);
		if (!p) {
			strbuf_release(&s);
			return NULL;
		}
		out->type = JSON_STRING;
		out->str_val = strbuf_detach(&s, NULL);
		return p;
	}

	if (*p == '{')
		return parse_object(p, out);

	if (*p == '[')
		return parse_array(p, out);

	if (!strncmp(p, "true", 4)) {
		out->type = JSON_BOOL;
		out->bool_val = 1;
		return p + 4;
	}

	if (!strncmp(p, "false", 5)) {
		out->type = JSON_BOOL;
		out->bool_val = 0;
		return p + 5;
	}

	if (!strncmp(p, "null", 4)) {
		out->type = JSON_NULL;
		return p + 4;
	}

	if (*p == '-' || (*p >= '0' && *p <= '9'))
		return parse_number(p, out);

	return NULL;
}

/* --- Public API --- */

int vibes_json_parse(const char *input, struct vibes_json *out)
{
	const char *end;

	memset(out, 0, sizeof(*out));
	end = parse_value(input, out);

	if (!end) {
		vibes_json_free(out);
		return -1;
	}

	return 0;
}

int vibes_json_parse_any(const char *input, struct vibes_json *out)
{
	const char *p = input;

	/* Scan for the first JSON start character */
	while (*p) {
		if (*p == '{' || *p == '[') {
			if (vibes_json_parse(p, out) == 0)
				return 0;
		}
		p++;
	}
	return -1;
}

void vibes_json_free(struct vibes_json *node)
{
	int i;

	if (!node)
		return;

	switch (node->type) {
	case JSON_STRING:
		free(node->str_val);
		node->str_val = NULL;
		break;
	case JSON_OBJECT:
		for (i = 0; i < node->nr_members; i++) {
			free(node->keys[i]);
			vibes_json_free(&node->values[i]);
		}
		free(node->keys);
		free(node->values);
		node->keys = NULL;
		node->values = NULL;
		node->nr_members = 0;
		break;
	case JSON_ARRAY:
		for (i = 0; i < node->nr_elements; i++)
			vibes_json_free(&node->elements[i]);
		free(node->elements);
		node->elements = NULL;
		node->nr_elements = 0;
		break;
	default:
		break;
	}

	node->type = JSON_NULL;
}

/* --- Accessors --- */

const char *vibes_json_get_string(const struct vibes_json *obj,
				  const char *key)
{
	int i;

	if (!obj || obj->type != JSON_OBJECT)
		return NULL;

	for (i = 0; i < obj->nr_members; i++) {
		if (!strcmp(obj->keys[i], key) &&
		    obj->values[i].type == JSON_STRING)
			return obj->values[i].str_val;
	}
	return NULL;
}

int vibes_json_get_int(const struct vibes_json *obj,
		       const char *key, int default_val)
{
	int i;

	if (!obj || obj->type != JSON_OBJECT)
		return default_val;

	for (i = 0; i < obj->nr_members; i++) {
		if (!strcmp(obj->keys[i], key) &&
		    obj->values[i].type == JSON_NUMBER)
			return (int)obj->values[i].num_val;
	}
	return default_val;
}

int vibes_json_get_bool(const struct vibes_json *obj,
			const char *key, int default_val)
{
	int i;

	if (!obj || obj->type != JSON_OBJECT)
		return default_val;

	for (i = 0; i < obj->nr_members; i++) {
		if (!strcmp(obj->keys[i], key) &&
		    obj->values[i].type == JSON_BOOL)
			return obj->values[i].bool_val;
	}
	return default_val;
}

const struct vibes_json *vibes_json_get_array(const struct vibes_json *obj,
					      const char *key)
{
	int i;

	if (!obj || obj->type != JSON_OBJECT)
		return NULL;

	for (i = 0; i < obj->nr_members; i++) {
		if (!strcmp(obj->keys[i], key) &&
		    obj->values[i].type == JSON_ARRAY)
			return &obj->values[i];
	}
	return NULL;
}

const struct vibes_json *vibes_json_get_object(const struct vibes_json *obj,
					       const char *key)
{
	int i;

	if (!obj || obj->type != JSON_OBJECT)
		return NULL;

	for (i = 0; i < obj->nr_members; i++) {
		if (!strcmp(obj->keys[i], key) &&
		    obj->values[i].type == JSON_OBJECT)
			return &obj->values[i];
	}
	return NULL;
}

int vibes_json_array_to_strings(const struct vibes_json *arr,
				struct string_list *out)
{
	int i;

	if (!arr || arr->type != JSON_ARRAY)
		return 0;

	for (i = 0; i < arr->nr_elements; i++) {
		if (arr->elements[i].type == JSON_STRING)
			string_list_append(out, arr->elements[i].str_val);
	}

	return out->nr;
}

#endif /* VIBES_ENABLED */
