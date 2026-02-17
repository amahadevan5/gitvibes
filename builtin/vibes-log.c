#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "builtin.h"
#ifdef VIBES_ENABLED
#include "config.h"
#include "repository.h"
#include "parse-options.h"
#include "run-command.h"
#include <sqlite3.h>
#include "libvibes/intent/intent.h"
#include "libvibes/storage/db.h"

static const char * const vibes_log_usage[] = {
	"git vibes-log [--intent <id>] [--limit <n>]",
	NULL
};

/*
 * Show commits grouped by intent.
 */
static int show_intent_log(struct vibes_db *db, const char *intent_filter,
			   int limit)
{
	sqlite3_stmt *stmt;
	struct strbuf sql = STRBUF_INIT;
	const char *last_intent_id = NULL;
	struct strbuf last_id_buf = STRBUF_INIT;
	int count = 0;

	strbuf_addstr(&sql,
		"SELECT ci.commit_oid, ci.intent_id, ci.task_id, ci.message, "
		"       i.type, i.raw_input "
		"FROM commit_intents ci "
		"LEFT JOIN intents i ON ci.intent_id = i.id ");

	if (intent_filter)
		strbuf_addf(&sql, "WHERE ci.intent_id = '%s' ", intent_filter);

	strbuf_addstr(&sql, "ORDER BY ci.intent_id, ci.created_at DESC");

	if (limit > 0)
		strbuf_addf(&sql, " LIMIT %d", limit);

	strbuf_addstr(&sql, ";");

	stmt = vibes_db_prepare(db, sql.buf);
	strbuf_release(&sql);
	if (!stmt)
		return -1;

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *oid = (const char *)sqlite3_column_text(stmt, 0);
		const char *intent_id = (const char *)sqlite3_column_text(stmt, 1);
		const char *task_id = (const char *)sqlite3_column_text(stmt, 2);
		const char *message = (const char *)sqlite3_column_text(stmt, 3);
		const char *type = (const char *)sqlite3_column_text(stmt, 4);
		const char *input = (const char *)sqlite3_column_text(stmt, 5);

		/* Print intent header when we encounter a new intent */
		if (!last_intent_id || strcmp(last_intent_id, intent_id ? intent_id : "")) {
			if (count > 0)
				printf("\n");
			printf("Intent: %.8s [%s] %s\n",
			       intent_id ? intent_id : "?",
			       type ? type : "?",
			       input ? input : "");
			strbuf_reset(&last_id_buf);
			if (intent_id)
				strbuf_addstr(&last_id_buf, intent_id);
			last_intent_id = last_id_buf.buf;
		}

		printf("  %.12s %s",
		       oid ? oid : "????????????",
		       message ? message : "(no message)");
		if (task_id)
			printf("  [task:%.8s]", task_id);
		printf("\n");
		count++;
	}

	sqlite3_finalize(stmt);
	strbuf_release(&last_id_buf);

	if (!count) {
		if (intent_filter)
			printf("No commits linked to intent %s\n", intent_filter);
		else
			printf("No intent-linked commits found.\n"
			       "Use 'git vibes-commit' to create intent-aware commits.\n");
	}

	return 0;
}

/*
 * If no intent-linked commits, fall back to showing recent git log.
 */
static int show_fallback_log(int limit)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf limit_str = STRBUF_INIT;

	strbuf_addf(&limit_str, "-%d", limit > 0 ? limit : 20);

	strvec_pushl(&cp.args, "log", "--oneline",
		     limit_str.buf, NULL);
	cp.git_cmd = 1;

	strbuf_release(&limit_str);
	return run_command(&cp);
}

int cmd_vibes_log(int argc, const char **argv, const char *prefix,
		  struct repository *repo)
{
	const char *intent_id = NULL;
	int limit = 0;
	int show_all = 0;
	struct option options[] = {
		OPT_STRING('i', "intent", &intent_id, "id",
			   "show commits for specific intent"),
		OPT_INTEGER('n', "limit", &limit,
			    "maximum commits to show"),
		OPT_BOOL('a', "all", &show_all,
			 "show all commits (git log + intent info)"),
		OPT_END()
	};
	struct vibes_db db;
	char *db_path;
	int ret;

	argc = parse_options(argc, argv, prefix, options,
			     vibes_log_usage, 0);

	/* Open vibes database */
	db_path = vibes_db_repo_path(repo->gitdir);
	if (vibes_db_open(&db, db_path) < 0) {
		/* No vibes DB — show standard git log */
		free(db_path);
		return show_fallback_log(limit);
	}
	free(db_path);

	if (show_all) {
		/* Show regular git log, then intent summary */
		show_fallback_log(limit);
		printf("\n--- Intent-linked commits ---\n\n");
	}

	ret = show_intent_log(&db, intent_id, limit);

	vibes_db_close(&db);
	return ret < 0 ? 1 : 0;
}
#else
int cmd_vibes_log(int argc, const char **argv, const char *prefix,
		  struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
