#ifndef LIBVIBES_JSON_PARSER_H
#define LIBVIBES_JSON_PARSER_H
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "string-list.h"

/*
 * Minimal recursive-descent JSON parser for gitvibes.
 *
 * Supports the JSON subset we need:
 *   - Objects with string, number, boolean, null, array, object values
 *   - Arrays of strings, numbers, objects
 *   - String escapes: \", \\, \/, \n, \r, \t, \uXXXX
 *
 * Parse model: read-then-query. Parse the JSON into a vibes_json
 * node tree, then query it with typed accessors.
 */

enum vibes_json_type {
	JSON_NULL = 0,
	JSON_BOOL,
	JSON_NUMBER,
	JSON_STRING,
	JSON_ARRAY,
	JSON_OBJECT,
};

struct vibes_json {
	enum vibes_json_type type;

	/* For JSON_STRING */
	char *str_val;

	/* For JSON_NUMBER */
	double num_val;

	/* For JSON_BOOL */
	int bool_val;

	/* For JSON_OBJECT: parallel arrays of keys + values */
	char **keys;
	struct vibes_json *values;
	int nr_members;

	/* For JSON_ARRAY: array of elements */
	struct vibes_json *elements;
	int nr_elements;
};

/*
 * Parse a JSON string into a vibes_json tree.
 * Returns 0 on success, -1 on parse error.
 * On success, *out points to the root node (caller must free with
 * vibes_json_free).
 */
int vibes_json_parse(const char *input, struct vibes_json *out);

/*
 * Find the first valid JSON structure in a larger text.
 * Useful for extracting JSON from AI responses that may contain
 * surrounding prose. Scans for the first '{' or '[' and parses from there.
 * Returns 0 on success, -1 if no valid JSON found.
 */
int vibes_json_parse_any(const char *input, struct vibes_json *out);

/* Free a parsed JSON tree */
void vibes_json_free(struct vibes_json *node);

/* --- Accessors --- */

/*
 * Get a string value from an object by key.
 * Returns the string (owned by the JSON node, do not free) or NULL.
 */
const char *vibes_json_get_string(const struct vibes_json *obj,
				  const char *key);

/*
 * Get an integer value from an object by key.
 * Returns the value, or default_val if not found.
 */
int vibes_json_get_int(const struct vibes_json *obj,
		       const char *key, int default_val);

/*
 * Get a boolean value from an object by key.
 * Returns 1/0, or default_val if not found.
 */
int vibes_json_get_bool(const struct vibes_json *obj,
			const char *key, int default_val);

/*
 * Get an array member from an object by key.
 * Returns the array node (owned by parent) or NULL.
 */
const struct vibes_json *vibes_json_get_array(const struct vibes_json *obj,
					      const char *key);

/*
 * Get an object member from an object by key.
 * Returns the child object node (owned by parent) or NULL.
 */
const struct vibes_json *vibes_json_get_object(const struct vibes_json *obj,
					       const char *key);

/*
 * Collect string elements from a JSON array into a string_list.
 * Non-string elements are skipped.
 */
int vibes_json_array_to_strings(const struct vibes_json *arr,
				struct string_list *out);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_JSON_PARSER_H */
