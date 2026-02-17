#include "git-compat-util.h"
#ifdef VIBES_ENABLED

/*
 * Symbol extraction is integrated directly into ts-parser.c
 * via vibes_ts_extract_symbols(). This file provides additional
 * indexing utilities.
 *
 * The main indexing logic (vibes_kg_index_repo, vibes_kg_index_file)
 * is in graph.c which orchestrates ts-parser.c extraction.
 */

#include <sqlite3.h>
#include "strbuf.h"
#include "string-list.h"
#include "libvibes/knowledge/knowledge.h"
#include "libvibes/storage/db.h"

/*
 * Detect if a file is a test file based on its path.
 */
int vibes_is_test_file(const char *path)
{
	if (!path)
		return 0;
	if (strstr(path, "test") || strstr(path, "spec") ||
	    strstr(path, "Test") || strstr(path, "Spec"))
		return 1;
	/* Python test files */
	if (strstr(path, "test_") || strstr(path, "_test."))
		return 1;
	/* Go test files */
	if (strstr(path, "_test.go"))
		return 1;
	return 0;
}

/*
 * Get statistics about the knowledge graph.
 */
int vibes_kg_stats(struct vibes_db *db, int *nr_nodes, int *nr_edges,
		   int *nr_files)
{
	sqlite3_stmt *stmt;

	if (nr_nodes) {
		*nr_nodes = vibes_db_count_table(db, "kg_nodes");
	}

	if (nr_edges) {
		*nr_edges = vibes_db_count_table(db, "kg_edges");
	}

	if (nr_files) {
		stmt = vibes_db_prepare(db,
			"SELECT COUNT(*) FROM kg_nodes WHERE type = 'file';");
		if (stmt) {
			if (sqlite3_step(stmt) == SQLITE_ROW)
				*nr_files = sqlite3_column_int(stmt, 0);
			sqlite3_finalize(stmt);
		}
	}

	return 0;
}

#endif /* VIBES_ENABLED */
