#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "strbuf.h"
#include "string-list.h"
#include "libvibes/knowledge/knowledge.h"
#include "libvibes/json-parser.h"
#include "libvibes/storage/db.h"

/*
 * Conflict Prediction: predict merge conflicts between two tasks
 * by analyzing their estimated file sets against the knowledge graph.
 */

/*
 * Get estimated files for a task from the database.
 */
static int get_task_files(struct vibes_db *db, const char *task_id,
			  struct string_list *files)
{
	sqlite3_stmt *stmt;

	stmt = vibes_db_prepare(db,
		"SELECT estimated_files FROM tasks WHERE id = ?;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *json = (const char *)sqlite3_column_text(stmt, 0);
		if (json) {
			struct vibes_json arr;
			if (vibes_json_parse(json, &arr) == 0) {
				vibes_json_array_to_strings(&arr, files);
				vibes_json_free(&arr);
			}
		}
	}

	sqlite3_finalize(stmt);
	return files->nr;
}

int vibes_predict_conflicts(struct vibes_db *db,
			    const char *task_a_id,
			    const char *task_b_id,
			    struct conflict_prediction *result)
{
	struct string_list files_a = STRING_LIST_INIT_DUP;
	struct string_list files_b = STRING_LIST_INIT_DUP;
	struct strbuf reason = STRBUF_INIT;
	int shared_count = 0;
	int i;

	memset(result, 0, sizeof(*result));
	string_list_init_dup(&result->shared_files);

	get_task_files(db, task_a_id, &files_a);
	get_task_files(db, task_b_id, &files_b);

	/* Find shared files */
	string_list_sort(&files_a);
	string_list_sort(&files_b);

	for (i = 0; i < files_a.nr; i++) {
		if (string_list_has_string(&files_b,
					   files_a.items[i].string)) {
			string_list_append(&result->shared_files,
					   files_a.items[i].string);
			shared_count++;
		}
	}

	/* Also check transitive dependencies */
	{
		struct string_list deps_a = STRING_LIST_INIT_DUP;
		struct string_list deps_b = STRING_LIST_INIT_DUP;

		for (i = 0; i < files_a.nr; i++)
			vibes_kg_get_dependents(db, files_a.items[i].string,
						&deps_a);
		for (i = 0; i < files_b.nr; i++)
			vibes_kg_get_dependents(db, files_b.items[i].string,
						&deps_b);

		string_list_sort(&deps_a);
		string_list_sort(&deps_b);

		for (i = 0; i < deps_a.nr; i++) {
			if (string_list_has_string(&deps_b,
						   deps_a.items[i].string) &&
			    !string_list_has_string(&result->shared_files,
						   deps_a.items[i].string)) {
				shared_count++;
			}
		}

		string_list_clear(&deps_a, 0);
		string_list_clear(&deps_b, 0);
	}

	/* Calculate probability */
	{
		int total_files = files_a.nr + files_b.nr;
		if (total_files == 0)
			result->probability = 0.0;
		else
			result->probability = (double)shared_count * 2.0 /
					      (double)total_files;
		if (result->probability > 1.0)
			result->probability = 1.0;
	}

	/* Generate recommendation */
	if (result->probability < 0.1) {
		strbuf_addstr(&reason, "Safe to run in parallel");
	} else if (result->probability < 0.4) {
		strbuf_addstr(&reason, "Low risk: consider file locking on "
			      "shared files");
	} else if (result->probability < 0.7) {
		strbuf_addstr(&reason, "Medium risk: serialize tasks or use "
			      "careful merge strategy");
	} else {
		strbuf_addstr(&reason, "High risk: strongly recommend "
			      "serializing these tasks");
	}
	result->recommendation = strbuf_detach(&reason, NULL);

	string_list_clear(&files_a, 0);
	string_list_clear(&files_b, 0);

	return 0;
}

void vibes_conflict_prediction_free(struct conflict_prediction *result)
{
	string_list_clear(&result->shared_files, 0);
	free(result->recommendation);
	result->recommendation = NULL;
}

#endif /* VIBES_ENABLED */
