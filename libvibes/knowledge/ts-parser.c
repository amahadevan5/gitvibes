#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include <tree_sitter/api.h>
#include "strbuf.h"
#include "string-list.h"
#include "libvibes/knowledge/knowledge.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/* External grammar constructors from tree-sitter-* submodules */
extern const TSLanguage *tree_sitter_c(void);
extern const TSLanguage *tree_sitter_python(void);
extern const TSLanguage *tree_sitter_javascript(void);
extern const TSLanguage *tree_sitter_go(void);
extern const TSLanguage *tree_sitter_rust(void);
extern const TSLanguage *tree_sitter_java(void);

static TSParser *ts_global_parser;
static int ts_initialized;

int vibes_ts_init(void)
{
	if (ts_initialized)
		return 0;
	ts_global_parser = ts_parser_new();
	if (!ts_global_parser)
		return error("gitvibes: failed to create tree-sitter parser");
	ts_initialized = 1;
	return 0;
}

void vibes_ts_shutdown(void)
{
	if (ts_global_parser) {
		ts_parser_delete(ts_global_parser);
		ts_global_parser = NULL;
	}
	ts_initialized = 0;
}

const char *vibes_ts_detect_language(const char *path)
{
	const char *ext;

	if (!path)
		return NULL;

	ext = strrchr(path, '.');
	if (!ext)
		return NULL;
	ext++;

	if (!strcmp(ext, "c") || !strcmp(ext, "h"))
		return "c";
	if (!strcmp(ext, "py") || !strcmp(ext, "pyw"))
		return "python";
	if (!strcmp(ext, "js") || !strcmp(ext, "jsx") ||
	    !strcmp(ext, "ts") || !strcmp(ext, "tsx") ||
	    !strcmp(ext, "mjs") || !strcmp(ext, "cjs"))
		return "javascript";
	if (!strcmp(ext, "go"))
		return "go";
	if (!strcmp(ext, "rs"))
		return "rust";
	if (!strcmp(ext, "java"))
		return "java";
	return NULL;
}

static const TSLanguage *get_language(const char *lang)
{
	if (!strcmp(lang, "c"))
		return tree_sitter_c();
	if (!strcmp(lang, "python"))
		return tree_sitter_python();
	if (!strcmp(lang, "javascript"))
		return tree_sitter_javascript();
	if (!strcmp(lang, "go"))
		return tree_sitter_go();
	if (!strcmp(lang, "rust"))
		return tree_sitter_rust();
	if (!strcmp(lang, "java"))
		return tree_sitter_java();
	return NULL;
}

/*
 * Check if a tree-sitter node type represents a function/method definition.
 */
static enum kg_node_type classify_node(const char *type, const char *lang)
{
	/* Function definitions */
	if (!strcmp(type, "function_definition") ||
	    !strcmp(type, "function_declaration") ||
	    !strcmp(type, "function_item") ||
	    !strcmp(type, "method_declaration") ||
	    !strcmp(type, "method_definition"))
		return KG_NODE_FUNCTION;

	/* Class/struct definitions */
	if (!strcmp(type, "class_definition") ||
	    !strcmp(type, "class_declaration") ||
	    !strcmp(type, "struct_specifier") ||
	    !strcmp(type, "struct_item") ||
	    !strcmp(type, "type_declaration") ||
	    !strcmp(type, "type_spec"))
		return KG_NODE_CLASS;

	/* Import statements */
	if (!strcmp(type, "import_statement") ||
	    !strcmp(type, "import_declaration") ||
	    !strcmp(type, "use_declaration") ||
	    !strcmp(type, "preproc_include"))
		return KG_NODE_IMPORT;

	return (enum kg_node_type)-1;
}

/*
 * Extract the name from a tree-sitter node.
 */
static char *extract_name(TSNode node, const char *source)
{
	uint32_t child_count = ts_node_child_count(node);
	uint32_t i;

	for (i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(node, i);
		const char *type = ts_node_type(child);

		if (!strcmp(type, "identifier") ||
		    !strcmp(type, "name") ||
		    !strcmp(type, "type_identifier") ||
		    !strcmp(type, "field_identifier")) {
			uint32_t start = ts_node_start_byte(child);
			uint32_t end = ts_node_end_byte(child);
			return xstrndup(source + start, end - start);
		}

		/* For function_declarator in C */
		if (!strcmp(type, "function_declarator") ||
		    !strcmp(type, "declarator")) {
			char *name = extract_name(child, source);
			if (name)
				return name;
		}
	}
	return NULL;
}

/*
 * Extract the import path/name from an import node.
 */
static char *extract_import_name(TSNode node, const char *source)
{
	uint32_t start = ts_node_start_byte(node);
	uint32_t end = ts_node_end_byte(node);
	uint32_t len = end - start;
	char *text;

	/* Cap at 256 chars */
	if (len > 256)
		len = 256;

	text = xstrndup(source + start, len);

	/* Strip #include, import, use keywords */
	{
		char *p = text;
		while (*p && !isspace(*p))
			p++;
		while (*p && isspace(*p))
			p++;
		if (p > text && *p) {
			char *result = xstrdup(p);
			free(text);
			/* Remove trailing semicolons/newlines */
			{
				size_t rlen = strlen(result);
				while (rlen > 0 && (result[rlen-1] == ';' ||
				       result[rlen-1] == '\n' ||
				       result[rlen-1] == '\r'))
					result[--rlen] = '\0';
			}
			return result;
		}
	}
	return text;
}

static int insert_kg_node(struct vibes_db *db, enum kg_node_type type,
			  const char *name, const char *file_path,
			  int start_line, int end_line, const char *language)
{
	sqlite3_stmt *stmt;
	int ret;

	stmt = vibes_db_prepare(db,
		"INSERT OR REPLACE INTO kg_nodes "
		"(type, name, file_path, start_line, end_line, language, last_indexed) "
		"VALUES (?, ?, ?, ?, ?, ?, strftime('%%s','now'));");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, kg_node_type_str(type), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, file_path, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 4, start_line);
	sqlite3_bind_int(stmt, 5, end_line);
	sqlite3_bind_text(stmt, 6, language, -1, SQLITE_STATIC);

	ret = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	return (ret == SQLITE_DONE) ? 0 : -1;
}

/*
 * Walk the AST and extract symbols.
 */
static int walk_tree(TSNode node, const char *source, const char *file_path,
		     const char *language, struct vibes_db *db, int depth)
{
	const char *type;
	enum kg_node_type ntype;
	int count = 0;
	uint32_t i, child_count;

	if (depth > 10)
		return 0;

	type = ts_node_type(node);
	ntype = classify_node(type, language);

	if ((int)ntype >= 0) {
		char *name;
		TSPoint start = ts_node_start_point(node);
		TSPoint end = ts_node_end_point(node);

		if (ntype == KG_NODE_IMPORT)
			name = extract_import_name(node, source);
		else
			name = extract_name(node, source);

		if (name) {
			insert_kg_node(db, ntype, name, file_path,
				       start.row + 1, end.row + 1, language);
			free(name);
			count++;
		}
	}

	child_count = ts_node_child_count(node);
	for (i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(node, i);
		count += walk_tree(child, source, file_path, language, db,
				   depth + 1);
	}

	return count;
}

int vibes_ts_extract_symbols(struct vibes_db *db, const char *file_path,
			     const char *language)
{
	const TSLanguage *lang;
	TSTree *tree;
	TSNode root;
	struct strbuf content = STRBUF_INIT;
	int count;

	if (vibes_ts_init() < 0)
		return -1;

	lang = get_language(language);
	if (!lang)
		return 0; /* unsupported language, not an error */

	ts_parser_set_language(ts_global_parser, lang);

	/* Read file content */
	if (strbuf_read_file(&content, file_path, 0) < 0)
		return -1;

	tree = ts_parser_parse_string(ts_global_parser, NULL,
				      content.buf, content.len);
	if (!tree) {
		strbuf_release(&content);
		return error("gitvibes: failed to parse %s", file_path);
	}

	/* First, insert a FILE node */
	insert_kg_node(db, KG_NODE_FILE, file_path, file_path, 1,
		       0, language);

	root = ts_tree_root_node(tree);
	count = walk_tree(root, content.buf, file_path, language, db, 0);

	ts_tree_delete(tree);
	strbuf_release(&content);

	return count + 1; /* +1 for the FILE node */
}

const char *kg_node_type_str(enum kg_node_type t)
{
	switch (t) {
	case KG_NODE_FILE: return "file";
	case KG_NODE_MODULE: return "module";
	case KG_NODE_FUNCTION: return "function";
	case KG_NODE_CLASS: return "class";
	case KG_NODE_TYPE: return "type";
	case KG_NODE_IMPORT: return "import";
	case KG_NODE_TEST: return "test";
	}
	return "unknown";
}

const char *kg_edge_type_str(enum kg_edge_type t)
{
	switch (t) {
	case KG_EDGE_IMPORTS: return "imports";
	case KG_EDGE_CALLS: return "calls";
	case KG_EDGE_EXTENDS: return "extends";
	case KG_EDGE_IMPLEMENTS: return "implements";
	case KG_EDGE_TESTS: return "tests";
	case KG_EDGE_DEPENDS_ON: return "depends_on";
	}
	return "unknown";
}

const char *risk_level_str(enum risk_level r)
{
	switch (r) {
	case RISK_LOW: return "low";
	case RISK_MEDIUM: return "medium";
	case RISK_HIGH: return "high";
	case RISK_CRITICAL: return "critical";
	}
	return "unknown";
}

#endif /* VIBES_ENABLED */
