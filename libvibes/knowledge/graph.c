#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "repository.h"
#include "strbuf.h"
#include "string-list.h"
#include "run-command.h"
#include "libvibes/knowledge/knowledge.h"
#include "libvibes/storage/db.h"

/*
 * Knowledge Graph: indexes repository files using Tree-sitter,
 * stores symbols and relationships in SQLite for querying.
 */

/*
 * Index a single file: detect language, parse with tree-sitter,
 * extract symbols, and store in database.
 */
int vibes_kg_index_file(struct vibes_db *db, const char *file_path)
{
	const char *language;
	int ret;

	language = vibes_ts_detect_language(file_path);
	if (!language)
		return 0; /* Skip unsupported file types */

	/* Remove old entries for this file */
	{
		sqlite3_stmt *stmt = vibes_db_prepare(db,
			"DELETE FROM kg_nodes WHERE file_path = ?;");
		if (stmt) {
			sqlite3_bind_text(stmt, 1, file_path, -1, SQLITE_STATIC);
			sqlite3_step(stmt);
			sqlite3_finalize(stmt);
		}
	}

	ret = vibes_ts_extract_symbols(db, file_path, language);
	return ret < 0 ? -1 : 0;
}

/*
 * Index all tracked files in the repository.
 */
int vibes_kg_index_repo(struct vibes_db *db, struct repository *repo,
			int max_files)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf output = STRBUF_INIT;
	const char *p;
	int count = 0;
	int indexed = 0;

	strvec_pushl(&cp.args, "ls-files", NULL);
	cp.git_cmd = 1;
	cp.no_stdin = 1;

	if (capture_command(&cp, &output, 0) < 0)
		return error("gitvibes: failed to list repository files");

	if (vibes_ts_init() < 0) {
		strbuf_release(&output);
		return -1;
	}

	vibes_db_begin(db);

	p = output.buf;
	while (*p && (max_files <= 0 || count < max_files)) {
		struct strbuf file = STRBUF_INIT;
		const char *eol = strchr(p, '\n');
		if (!eol)
			eol = p + strlen(p);

		strbuf_add(&file, p, eol - p);

		if (file.len > 0) {
			const char *lang = vibes_ts_detect_language(file.buf);
			if (lang) {
				vibes_kg_index_file(db, file.buf);
				indexed++;
			}
		}

		strbuf_release(&file);
		count++;
		p = *eol ? eol + 1 : eol;
	}

	/* Build import edges: link import nodes to file nodes */
	vibes_db_exec(db,
		"INSERT OR IGNORE INTO kg_edges (source_id, target_id, type) "
		"SELECT imp.id, f.id, 'imports' "
		"FROM kg_nodes imp "
		"JOIN kg_nodes f ON f.type = 'file' "
		"  AND imp.name LIKE '%' || REPLACE(f.name, './', '') || '%' "
		"WHERE imp.type = 'import';");

	/* Build test edges: link test files to source files */
	vibes_db_exec(db,
		"INSERT OR IGNORE INTO kg_edges (source_id, target_id, type) "
		"SELECT t.id, s.id, 'tests' "
		"FROM kg_nodes t "
		"JOIN kg_nodes s ON s.type = 'file' "
		"  AND t.type = 'file' "
		"  AND (t.file_path LIKE '%test%' OR t.file_path LIKE '%spec%') "
		"  AND s.file_path NOT LIKE '%test%' "
		"  AND s.file_path NOT LIKE '%spec%' "
		"  AND REPLACE(REPLACE(t.file_path, 'test_', ''), '_test', '') "
		"      LIKE '%' || REPLACE(s.name, s.language, '') || '%';");

	vibes_db_commit(db);

	strbuf_release(&output);
	return indexed;
}

/*
 * Get files that depend on (import) the given file.
 */
int vibes_kg_get_dependents(struct vibes_db *db, const char *file_path,
			    struct string_list *out)
{
	sqlite3_stmt *stmt;

	stmt = vibes_db_prepare(db,
		"SELECT DISTINCT n2.file_path "
		"FROM kg_nodes n1 "
		"JOIN kg_edges e ON e.target_id = n1.id "
		"JOIN kg_nodes n2 ON n2.id = e.source_id "
		"WHERE n1.file_path = ? AND n1.type = 'file' "
		"  AND e.type = 'imports';");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, file_path, -1, SQLITE_STATIC);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *dep = (const char *)sqlite3_column_text(stmt, 0);
		if (dep)
			string_list_append(out, dep);
	}

	sqlite3_finalize(stmt);
	return out->nr;
}

/*
 * Get files that the given file depends on (imports).
 */
int vibes_kg_get_dependencies(struct vibes_db *db, const char *file_path,
			      struct string_list *out)
{
	sqlite3_stmt *stmt;

	stmt = vibes_db_prepare(db,
		"SELECT DISTINCT n2.file_path "
		"FROM kg_nodes n1 "
		"JOIN kg_edges e ON e.source_id = n1.id "
		"JOIN kg_nodes n2 ON n2.id = e.target_id "
		"WHERE n1.file_path = ? AND e.type IN ('imports', 'depends_on');");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, file_path, -1, SQLITE_STATIC);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *dep = (const char *)sqlite3_column_text(stmt, 0);
		if (dep)
			string_list_append(out, dep);
	}

	sqlite3_finalize(stmt);
	return out->nr;
}

/*
 * Get test files related to the given source file.
 */
int vibes_kg_get_related_tests(struct vibes_db *db, const char *file_path,
			       struct string_list *out)
{
	sqlite3_stmt *stmt;

	stmt = vibes_db_prepare(db,
		"SELECT DISTINCT n2.file_path "
		"FROM kg_nodes n1 "
		"JOIN kg_edges e ON e.target_id = n1.id "
		"JOIN kg_nodes n2 ON n2.id = e.source_id "
		"WHERE n1.file_path = ? AND e.type = 'tests';");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, file_path, -1, SQLITE_STATIC);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *test = (const char *)sqlite3_column_text(stmt, 0);
		if (test)
			string_list_append(out, test);
	}

	sqlite3_finalize(stmt);
	return out->nr;
}

/*
 * Search for symbols by name.
 */
int vibes_kg_search(struct vibes_db *db, const char *symbol_name,
		    struct string_list *out)
{
	sqlite3_stmt *stmt;
	struct strbuf pattern = STRBUF_INIT;

	strbuf_addf(&pattern, "%%%s%%", symbol_name);

	stmt = vibes_db_prepare(db,
		"SELECT type || ':' || name || ' (' || file_path || ':' || "
		"       start_line || ')' "
		"FROM kg_nodes "
		"WHERE name LIKE ? AND type != 'file' "
		"ORDER BY type, name LIMIT 50;");
	if (!stmt) {
		strbuf_release(&pattern);
		return -1;
	}

	sqlite3_bind_text(stmt, 1, pattern.buf, -1, SQLITE_TRANSIENT);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *result = (const char *)sqlite3_column_text(stmt, 0);
		if (result)
			string_list_append(out, result);
	}

	sqlite3_finalize(stmt);
	strbuf_release(&pattern);
	return out->nr;
}

#endif /* VIBES_ENABLED */
