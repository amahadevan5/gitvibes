#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "builtin.h"
#ifdef VIBES_ENABLED
#include "config.h"
#include "repository.h"
#include "parse-options.h"
#include "strbuf.h"
#include "string-list.h"
#include "libvibes/intent/intent.h"
#include "libvibes/storage/db.h"

static const char * const vibes_usage[] = {
	"git vibes \"<natural language request>\"",
	"git vibes --list [--status <status>]",
	"git vibes --show <id>",
	"git vibes --abandon <id>",
	NULL
};

/*
 * List all intents, optionally filtered by status.
 */
static int list_intents(struct vibes_db *db, const char *status_filter)
{
	sqlite3_stmt *stmt;
	struct strbuf sql = STRBUF_INIT;
	int count = 0;

	strbuf_addstr(&sql,
		"SELECT id, type, status, raw_input, created_at "
		"FROM intents");
	if (status_filter)
		strbuf_addf(&sql, " WHERE status = '%s'", status_filter);
	strbuf_addstr(&sql, " ORDER BY created_at DESC;");

	stmt = vibes_db_prepare(db, sql.buf);
	strbuf_release(&sql);
	if (!stmt)
		return -1;

	printf("%-26s  %-10s  %-12s  %s\n",
	       "ID", "TYPE", "STATUS", "INPUT");
	printf("%-26s  %-10s  %-12s  %s\n",
	       "--------------------------", "----------",
	       "------------", "--------------------");

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *id = (const char *)sqlite3_column_text(stmt, 0);
		const char *type = (const char *)sqlite3_column_text(stmt, 1);
		const char *status = (const char *)sqlite3_column_text(stmt, 2);
		const char *input = (const char *)sqlite3_column_text(stmt, 3);
		struct strbuf truncated = STRBUF_INIT;

		/* Truncate input for display */
		if (input && strlen(input) > 40) {
			strbuf_add(&truncated, input, 37);
			strbuf_addstr(&truncated, "...");
		} else if (input) {
			strbuf_addstr(&truncated, input);
		}

		printf("%-26s  %-10s  %-12s  %s\n",
		       id, type, status, truncated.buf);
		strbuf_release(&truncated);
		count++;
	}

	sqlite3_finalize(stmt);

	if (!count)
		printf("(no intents found)\n");
	else
		printf("\n%d intent(s)\n", count);

	return 0;
}

/*
 * Show detailed info for a single intent.
 */
static int show_intent(struct vibes_db *db, const char *id)
{
	struct vibes_intent intent;
	sqlite3_stmt *stmt;
	int ret;

	ret = vibes_intent_get(db, id, &intent);
	if (ret < 0)
		return ret;

	printf("Intent: %s\n", intent.id);
	printf("  Type:     %s\n", intent_type_to_str(intent.type));
	printf("  Status:   %s\n", intent_status_to_str(intent.status));
	printf("  Input:    %s\n", intent.raw_input ? intent.raw_input : "(none)");
	if (intent.parsed_goal)
		printf("  Goal:     %s\n", intent.parsed_goal);
	printf("  Created:  %lld\n", (long long)intent.created_at);
	if (intent.started_at)
		printf("  Started:  %lld\n", (long long)intent.started_at);
	if (intent.completed_at)
		printf("  Completed: %lld\n", (long long)intent.completed_at);

	/* Show tasks for this intent */
	stmt = vibes_db_prepare(db,
		"SELECT id, title, status, wave_number "
		"FROM tasks WHERE intent_id = ? "
		"ORDER BY wave_number, id;");
	if (stmt) {
		int current_wave = -1;
		int nr_tasks = 0;

		sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);
		printf("\n  Tasks:\n");

		while (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *tid = (const char *)sqlite3_column_text(stmt, 0);
			const char *title = (const char *)sqlite3_column_text(stmt, 1);
			const char *status = (const char *)sqlite3_column_text(stmt, 2);
			int wave = sqlite3_column_int(stmt, 3);

			if (wave != current_wave) {
				current_wave = wave;
				printf("    Wave %d:\n", wave);
			}
			printf("      [%s] %s (%s)\n",
			       tid ? tid : "?",
			       title ? title : "(untitled)",
			       status ? status : "?");
			nr_tasks++;
		}
		sqlite3_finalize(stmt);

		if (!nr_tasks)
			printf("    (no tasks)\n");
	}

	/* Show linked commits */
	stmt = vibes_db_prepare(db,
		"SELECT commit_oid, message FROM commit_intents "
		"WHERE intent_id = ? ORDER BY created_at;");
	if (stmt) {
		int nr_commits = 0;

		sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);

		while (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *oid = (const char *)sqlite3_column_text(stmt, 0);
			const char *msg = (const char *)sqlite3_column_text(stmt, 1);
			if (!nr_commits)
				printf("\n  Commits:\n");
			printf("    %.12s %s\n",
			       oid ? oid : "?",
			       msg ? msg : "");
			nr_commits++;
		}
		sqlite3_finalize(stmt);
	}

	vibes_intent_free(&intent);
	return 0;
}

/*
 * Abandon an intent.
 */
static int abandon_intent(struct vibes_db *db, const char *id)
{
	int ret = vibes_intent_update_status(db, id, INTENT_ABANDONED);
	if (ret < 0)
		return ret;
	printf("Intent %s abandoned.\n", id);
	return 0;
}

int cmd_vibes(int argc, const char **argv, const char *prefix,
	      struct repository *repo)
{
	int list = 0;
	const char *show_id = NULL;
	const char *abandon_id = NULL;
	const char *status_filter = NULL;
	struct option options[] = {
		OPT_BOOL('l', "list", &list, "list all intents"),
		OPT_STRING('s', "show", &show_id, "id", "show intent details"),
		OPT_STRING(0, "abandon", &abandon_id, "id", "abandon an intent"),
		OPT_STRING(0, "status", &status_filter, "status",
			   "filter by status (with --list)"),
		OPT_END()
	};
	struct vibes_db db;
	char *db_path;
	int ret;

	argc = parse_options(argc, argv, prefix, options, vibes_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	/* Open database */
	db_path = vibes_db_repo_path(repo->gitdir);
	if (vibes_db_open(&db, db_path) < 0) {
		free(db_path);
		die("gitvibes: cannot open database. Run 'git vibes-init' first.");
	}
	free(db_path);

	if (list) {
		ret = list_intents(&db, status_filter);
	} else if (show_id) {
		ret = show_intent(&db, show_id);
	} else if (abandon_id) {
		ret = abandon_intent(&db, abandon_id);
	} else if (argc > 0) {
		/* Remaining args are the natural language request */
		struct strbuf input = STRBUF_INIT;
		int i;

		for (i = 0; i < argc; i++) {
			if (i > 0)
				strbuf_addch(&input, ' ');
			strbuf_addstr(&input, argv[i]);
		}

		ret = vibes_pipeline_run(input.buf, &db, repo);
		strbuf_release(&input);
	} else {
		usage_with_options(vibes_usage, options);
		ret = 1;
	}

	vibes_db_close(&db);
	return ret < 0 ? 1 : ret;
}
#else
int cmd_vibes(int argc, const char **argv, const char *prefix,
	      struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
