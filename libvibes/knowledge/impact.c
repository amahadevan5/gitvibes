#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "strbuf.h"
#include "string-list.h"
#include "libvibes/knowledge/knowledge.h"
#include "libvibes/storage/db.h"

/*
 * Impact Analysis: given a set of file paths, determine what other files
 * are affected, which tests should be run, and the overall risk level.
 */

/*
 * Get transitive dependents up to a given depth.
 */
static void collect_transitive(struct vibes_db *db,
			       const struct string_list *seeds,
			       struct string_list *out, int max_depth)
{
	struct string_list frontier = STRING_LIST_INIT_DUP;
	struct string_list visited = STRING_LIST_INIT_DUP;
	int depth;

	/* Initialize frontier with seeds */
	for (int i = 0; i < seeds->nr; i++) {
		string_list_append(&frontier, seeds->items[i].string);
		string_list_append(&visited, seeds->items[i].string);
	}

	for (depth = 0; depth < max_depth && frontier.nr > 0; depth++) {
		struct string_list next_frontier = STRING_LIST_INIT_DUP;

		for (int i = 0; i < frontier.nr; i++) {
			struct string_list deps = STRING_LIST_INIT_DUP;
			vibes_kg_get_dependents(db, frontier.items[i].string,
						&deps);
			for (int j = 0; j < deps.nr; j++) {
				if (!string_list_has_string(&visited,
							   deps.items[j].string)) {
					string_list_append(&visited,
							   deps.items[j].string);
					string_list_append(&next_frontier,
							   deps.items[j].string);
					string_list_append(out,
							   deps.items[j].string);
				}
			}
			string_list_clear(&deps, 0);
		}

		string_list_clear(&frontier, 0);
		frontier = next_frontier;
	}

	string_list_clear(&frontier, 0);
	string_list_clear(&visited, 0);
}

int vibes_impact_analyze(struct vibes_db *db,
			 const struct string_list *file_paths,
			 struct impact_analysis *result)
{
	struct strbuf reasoning = STRBUF_INIT;

	memset(result, 0, sizeof(*result));
	string_list_init_dup(&result->direct);
	string_list_init_dup(&result->transitive);
	string_list_init_dup(&result->tests);

	/* Get direct dependents for each file */
	for (int i = 0; i < file_paths->nr; i++) {
		vibes_kg_get_dependents(db, file_paths->items[i].string,
					&result->direct);
	}
	string_list_sort(&result->direct);
	string_list_remove_duplicates(&result->direct, 0);

	/* Get transitive dependents (depth 3) */
	collect_transitive(db, file_paths, &result->transitive, 3);
	string_list_sort(&result->transitive);
	string_list_remove_duplicates(&result->transitive, 0);

	/* Find related tests */
	for (int i = 0; i < file_paths->nr; i++) {
		vibes_kg_get_related_tests(db, file_paths->items[i].string,
					   &result->tests);
	}
	for (int i = 0; i < result->direct.nr; i++) {
		vibes_kg_get_related_tests(db, result->direct.items[i].string,
					   &result->tests);
	}
	string_list_sort(&result->tests);
	string_list_remove_duplicates(&result->tests, 0);

	/* Calculate risk level */
	{
		int total_affected = result->direct.nr + result->transitive.nr;
		if (total_affected == 0)
			result->risk = RISK_LOW;
		else if (total_affected <= 3)
			result->risk = RISK_LOW;
		else if (total_affected <= 10)
			result->risk = RISK_MEDIUM;
		else if (total_affected <= 25)
			result->risk = RISK_HIGH;
		else
			result->risk = RISK_CRITICAL;

		/* Bump risk if few tests cover the change */
		if (result->tests.nr == 0 && total_affected > 0 &&
		    result->risk < RISK_HIGH)
			result->risk++;
	}

	/* Generate reasoning */
	strbuf_addf(&reasoning, "%d file(s) changed, %d direct dependent(s), "
		    "%d transitive, %d test file(s)",
		    file_paths->nr, result->direct.nr,
		    result->transitive.nr, result->tests.nr);
	if (result->tests.nr == 0)
		strbuf_addstr(&reasoning, ". WARNING: no test coverage found");
	result->reasoning = strbuf_detach(&reasoning, NULL);

	return 0;
}

void vibes_impact_free(struct impact_analysis *result)
{
	string_list_clear(&result->direct, 0);
	string_list_clear(&result->transitive, 0);
	string_list_clear(&result->tests, 0);
	free(result->reasoning);
	result->reasoning = NULL;
}

#endif /* VIBES_ENABLED */
