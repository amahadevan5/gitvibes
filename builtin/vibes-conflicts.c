#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "builtin.h"
#ifdef VIBES_ENABLED
#include "config.h"
#include "repository.h"
#include "parse-options.h"
#include <sqlite3.h>
#include "libvibes/knowledge/knowledge.h"
#include "libvibes/storage/db.h"

static const char * const vibes_conflicts_usage[] = {
	"git vibes-conflicts [--task-a <id> --task-b <id>]",
	"git vibes-conflicts --impact <file>...",
	NULL
};

static int show_all_conflicts(struct vibes_db *db)
{
	sqlite3_stmt *stmt;
	struct string_list task_ids = STRING_LIST_INIT_DUP;
	int found_conflict = 0;
	int i, j;

	/* Get all active tasks */
	stmt = vibes_db_prepare(db,
		"SELECT id, title FROM tasks "
		"WHERE status IN ('pending', 'claimed', 'in_progress') "
		"ORDER BY id;");
	if (!stmt)
		return -1;

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *id = (const char *)sqlite3_column_text(stmt, 0);
		if (id)
			string_list_append(&task_ids, id);
	}
	sqlite3_finalize(stmt);

	if (task_ids.nr < 2) {
		printf("Need at least 2 active tasks to predict conflicts.\n");
		string_list_clear(&task_ids, 0);
		return 0;
	}

	printf("Conflict predictions for %d active tasks:\n\n", task_ids.nr);

	/* Check each pair */
	for (i = 0; i < task_ids.nr; i++) {
		for (j = i + 1; j < task_ids.nr; j++) {
			struct conflict_prediction pred;

			vibes_predict_conflicts(db,
				task_ids.items[i].string,
				task_ids.items[j].string, &pred);

			if (pred.probability > 0.05) {
				printf("  %.8s <-> %.8s: %.0f%% risk",
				       task_ids.items[i].string,
				       task_ids.items[j].string,
				       pred.probability * 100);
				if (pred.shared_files.nr > 0) {
					printf(" (%d shared file(s))",
					       pred.shared_files.nr);
				}
				printf("\n    %s\n\n",
				       pred.recommendation ?
				       pred.recommendation : "");
				found_conflict = 1;
			}

			vibes_conflict_prediction_free(&pred);
		}
	}

	if (!found_conflict)
		printf("  No significant conflict risk detected.\n");

	string_list_clear(&task_ids, 0);
	return 0;
}

static int show_between(struct vibes_db *db, const char *task_a,
			const char *task_b)
{
	struct conflict_prediction pred;

	vibes_predict_conflicts(db, task_a, task_b, &pred);

	printf("Conflict prediction: %.8s <-> %.8s\n\n", task_a, task_b);
	printf("  Risk:     %.0f%%\n", pred.probability * 100);
	printf("  Level:    %s\n",
	       pred.probability < 0.1 ? "LOW" :
	       pred.probability < 0.4 ? "MEDIUM" :
	       pred.probability < 0.7 ? "HIGH" : "CRITICAL");

	if (pred.shared_files.nr > 0) {
		int i;
		printf("  Shared files:\n");
		for (i = 0; i < pred.shared_files.nr; i++)
			printf("    %s\n", pred.shared_files.items[i].string);
	}

	printf("  Recommendation: %s\n",
	       pred.recommendation ? pred.recommendation : "(none)");

	vibes_conflict_prediction_free(&pred);
	return 0;
}

static int show_impact(struct vibes_db *db, int file_argc,
		       const char **file_argv)
{
	struct string_list files = STRING_LIST_INIT_DUP;
	struct impact_analysis analysis;
	int i;

	for (i = 0; i < file_argc; i++)
		string_list_append(&files, file_argv[i]);

	if (vibes_impact_analyze(db, &files, &analysis) < 0) {
		string_list_clear(&files, 0);
		return -1;
	}

	printf("Impact analysis for %d file(s):\n\n", files.nr);
	printf("  Risk level: %s\n", risk_level_str(analysis.risk));
	printf("  %s\n", analysis.reasoning ? analysis.reasoning : "");

	if (analysis.direct.nr) {
		printf("\n  Direct dependents (%d):\n", analysis.direct.nr);
		for (i = 0; i < analysis.direct.nr; i++)
			printf("    %s\n", analysis.direct.items[i].string);
	}

	if (analysis.transitive.nr) {
		printf("\n  Transitive dependents (%d):\n",
		       analysis.transitive.nr);
		for (i = 0; i < analysis.transitive.nr; i++)
			printf("    %s\n",
			       analysis.transitive.items[i].string);
	}

	if (analysis.tests.nr) {
		printf("\n  Tests to run (%d):\n", analysis.tests.nr);
		for (i = 0; i < analysis.tests.nr; i++)
			printf("    %s\n", analysis.tests.items[i].string);
	}

	vibes_impact_free(&analysis);
	string_list_clear(&files, 0);
	return 0;
}

int cmd_vibes_conflicts(int argc, const char **argv, const char *prefix,
			struct repository *repo)
{
	const char *between_a = NULL;
	const char *between_b = NULL;
	int impact_mode = 0;
	struct option options[] = {
		OPT_STRING(0, "task-a", &between_a, "id",
			   "first task for comparison"),
		OPT_STRING(0, "task-b", &between_b, "id",
			   "second task for comparison"),
		OPT_BOOL(0, "impact", &impact_mode,
			 "run impact analysis on files"),
		OPT_END()
	};
	struct vibes_db db;
	char *db_path;
	int ret;

	argc = parse_options(argc, argv, prefix, options,
			     vibes_conflicts_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	db_path = vibes_db_repo_path(repo->gitdir);
	if (vibes_db_open(&db, db_path) < 0) {
		free(db_path);
		die("gitvibes: cannot open database. Run 'git vibes-init' first.");
	}
	free(db_path);

	if (between_a && between_b) {
		ret = show_between(&db, between_a, between_b);
	} else if (impact_mode && argc > 0) {
		ret = show_impact(&db, argc, argv);
	} else {
		ret = show_all_conflicts(&db);
	}

	vibes_db_close(&db);
	return ret < 0 ? 1 : 0;
}
#else
int cmd_vibes_conflicts(int argc, const char **argv, const char *prefix,
			struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
