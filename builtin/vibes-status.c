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

static const char * const vibes_status_usage[] = {
	"git vibes-status",
	NULL
};

/*
 * Show active intents (not completed or abandoned).
 */
static void show_active_intents(struct vibes_db *db)
{
	sqlite3_stmt *stmt;
	int count = 0;

	stmt = vibes_db_prepare(db,
		"SELECT id, type, status, raw_input FROM intents "
		"WHERE status NOT IN ('completed', 'abandoned') "
		"ORDER BY created_at DESC;");
	if (!stmt)
		return;

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *id = (const char *)sqlite3_column_text(stmt, 0);
		const char *type = (const char *)sqlite3_column_text(stmt, 1);
		const char *status = (const char *)sqlite3_column_text(stmt, 2);
		const char *input = (const char *)sqlite3_column_text(stmt, 3);
		struct strbuf display = STRBUF_INIT;

		if (!count)
			printf("Active intents:\n");

		/* Truncate long input */
		if (input && strlen(input) > 50) {
			strbuf_add(&display, input, 47);
			strbuf_addstr(&display, "...");
		} else if (input) {
			strbuf_addstr(&display, input);
		}

		printf("  %-12s [%-10s] %.8s  %s\n",
		       status, type,
		       id ? id : "?",
		       display.buf);
		strbuf_release(&display);
		count++;
	}
	sqlite3_finalize(stmt);

	if (count)
		printf("\n");
}

/*
 * Show in-progress tasks.
 */
static void show_active_tasks(struct vibes_db *db)
{
	sqlite3_stmt *stmt;
	int count = 0;

	stmt = vibes_db_prepare(db,
		"SELECT t.id, t.title, t.status, t.wave_number, "
		"       t.assigned_agent, i.raw_input "
		"FROM tasks t "
		"LEFT JOIN intents i ON t.intent_id = i.id "
		"WHERE t.status IN ('pending', 'claimed', 'in_progress') "
		"ORDER BY t.wave_number, t.id;");
	if (!stmt)
		return;

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *id = (const char *)sqlite3_column_text(stmt, 0);
		const char *title = (const char *)sqlite3_column_text(stmt, 1);
		const char *status = (const char *)sqlite3_column_text(stmt, 2);
		int wave = sqlite3_column_int(stmt, 3);
		const char *agent = (const char *)sqlite3_column_text(stmt, 4);

		if (!count)
			printf("Active tasks:\n");

		printf("  [wave %d] %-12s %.8s  %s",
		       wave, status,
		       id ? id : "?",
		       title ? title : "(untitled)");
		if (agent)
			printf("  (agent: %.8s)", agent);
		printf("\n");
		count++;
	}
	sqlite3_finalize(stmt);

	if (count)
		printf("\n");
}

/*
 * Show active agents.
 */
static void show_active_agents(struct vibes_db *db)
{
	sqlite3_stmt *stmt;
	int count = 0;

	/* Check if events table has recent agent activity */
	stmt = vibes_db_prepare(db,
		"SELECT DISTINCT agent_id, type, payload "
		"FROM events "
		"WHERE agent_id IS NOT NULL AND type IN ('TASK_CLAIMED', 'PROGRESS') "
		"ORDER BY created_at DESC LIMIT 10;");
	if (!stmt)
		return;

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *agent_id = (const char *)sqlite3_column_text(stmt, 0);
		const char *type = (const char *)sqlite3_column_text(stmt, 1);

		if (!count)
			printf("Recent agent activity:\n");

		printf("  %.8s  %s\n",
		       agent_id ? agent_id : "?",
		       type ? type : "?");
		count++;
	}
	sqlite3_finalize(stmt);

	if (count)
		printf("\n");
}

int cmd_vibes_status(int argc, const char **argv, const char *prefix,
		     struct repository *repo)
{
	struct option options[] = {
		OPT_END()
	};
	struct vibes_db db;
	char *db_path;
	int has_vibes_data = 0;

	argc = parse_options(argc, argv, prefix, options,
			     vibes_status_usage, 0);

	/* First, run normal git status */
	{
		struct child_process cp = CHILD_PROCESS_INIT;
		strvec_pushl(&cp.args, "status", "--short", NULL);
		cp.git_cmd = 1;
		run_command(&cp);
	}

	/* Open vibes database */
	db_path = vibes_db_repo_path(repo->gitdir);
	if (vibes_db_open(&db, db_path) < 0) {
		/* No vibes database — just show git status */
		free(db_path);
		return 0;
	}
	free(db_path);

	printf("\n--- gitvibes ---\n\n");

	/* Show vibes-specific status */
	show_active_intents(&db);
	show_active_tasks(&db);
	show_active_agents(&db);

	/* Summary counts */
	{
		int nr_intents = vibes_db_count_table(&db, "intents");
		int nr_tasks = vibes_db_count_table(&db, "tasks");
		int nr_commits = vibes_db_count_table(&db, "commit_intents");

		if (nr_intents > 0 || nr_tasks > 0) {
			printf("Totals: %d intent(s), %d task(s), %d linked commit(s)\n",
			       nr_intents, nr_tasks, nr_commits);
			has_vibes_data = 1;
		}
	}

	if (!has_vibes_data)
		printf("No gitvibes data. Use 'git vibes \"your request\"' to get started.\n");

	vibes_db_close(&db);
	return 0;
}
#else
int cmd_vibes_status(int argc, const char **argv, const char *prefix,
		     struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
