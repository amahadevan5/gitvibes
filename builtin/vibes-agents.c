#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "builtin.h"
#ifdef VIBES_ENABLED
#include "config.h"
#include "repository.h"
#include "parse-options.h"
#include <sqlite3.h>
#include "libvibes/agents/agents.h"
#include "libvibes/storage/db.h"

static const char * const vibes_agents_usage[] = {
	"git vibes-agents list",
	"git vibes-agents status",
	"git vibes-agents run <intent-id>",
	"git vibes-agents logs [--type <type>] [--limit <n>]",
	NULL
};

static int do_list(struct vibes_db *db)
{
	struct string_list agents = STRING_LIST_INIT_DUP;
	int i;

	vibes_agent_list(db, &agents);

	if (!agents.nr) {
		printf("No agent activity found.\n");
	} else {
		printf("%-26s  %-15s  %s\n", "AGENT", "EVENT", "TASK");
		printf("%-26s  %-15s  %s\n",
		       "--------------------------",
		       "---------------",
		       "--------------------");
		for (i = 0; i < agents.nr; i++)
			printf("  %s\n", agents.items[i].string);
	}

	string_list_clear(&agents, 0);
	return 0;
}

static int do_status(struct vibes_db *db)
{
	int nr_events, nr_locks;
	sqlite3_stmt *stmt;

	printf("Agent System Status:\n\n");

	/* Event count */
	nr_events = vibes_db_count_table(db, "events");
	printf("  Total events: %d\n", nr_events);

	/* Active locks */
	nr_locks = vibes_db_count_table(db, "file_locks");
	printf("  Active locks: %d\n", nr_locks);

	/* Active tasks */
	stmt = vibes_db_prepare(db,
		"SELECT status, COUNT(*) FROM tasks GROUP BY status;");
	if (stmt) {
		printf("\n  Task status:\n");
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *status =
				(const char *)sqlite3_column_text(stmt, 0);
			int count = sqlite3_column_int(stmt, 1);
			printf("    %-12s %d\n",
			       status ? status : "?", count);
		}
		sqlite3_finalize(stmt);
	}

	/* Recent events */
	{
		struct string_list recent = STRING_LIST_INIT_DUP;
		vibes_event_get_recent(db, NULL, 5, &recent);
		if (recent.nr) {
			int i;
			printf("\n  Recent events:\n");
			for (i = 0; i < recent.nr; i++)
				printf("    %s\n", recent.items[i].string);
		}
		string_list_clear(&recent, 0);
	}

	return 0;
}

static int do_run(struct vibes_db *db, struct repository *repo,
		  const char *intent_id)
{
	return vibes_orchestrator_run(db, repo, intent_id);
}

static int do_logs(struct vibes_db *db, const char *type_filter, int limit)
{
	struct string_list events = STRING_LIST_INIT_DUP;
	int i;

	vibes_event_get_recent(db, type_filter,
			       limit > 0 ? limit : 20, &events);

	if (!events.nr) {
		printf("No events found.\n");
	} else {
		printf("Recent events:\n");
		for (i = 0; i < events.nr; i++)
			printf("  %s\n", events.items[i].string);
	}

	string_list_clear(&events, 0);
	return 0;
}

int cmd_vibes_agents(int argc, const char **argv, const char *prefix,
		     struct repository *repo)
{
	const char *type_filter = NULL;
	int limit = 0;
	struct option options[] = {
		OPT_STRING(0, "type", &type_filter, "type",
			   "filter events by type"),
		OPT_INTEGER('n', "limit", &limit,
			    "maximum events to show"),
		OPT_END()
	};
	struct vibes_db db;
	char *db_path;
	int ret = 0;

	argc = parse_options(argc, argv, prefix, options,
			     vibes_agents_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc < 1)
		usage_with_options(vibes_agents_usage, options);

	db_path = vibes_db_repo_path(repo->gitdir);
	if (vibes_db_open(&db, db_path) < 0) {
		free(db_path);
		die("gitvibes: cannot open database. Run 'git vibes-init' first.");
	}
	free(db_path);

	if (!strcmp(argv[0], "list")) {
		ret = do_list(&db);
	} else if (!strcmp(argv[0], "status")) {
		ret = do_status(&db);
	} else if (!strcmp(argv[0], "run")) {
		if (argc < 2)
			die("usage: git vibes-agents run <intent-id>");
		ret = do_run(&db, repo, argv[1]);
	} else if (!strcmp(argv[0], "logs")) {
		ret = do_logs(&db, type_filter, limit);
	} else {
		die("unknown subcommand: %s", argv[0]);
	}

	vibes_db_close(&db);
	return ret < 0 ? 1 : 0;
}
#else
int cmd_vibes_agents(int argc, const char **argv, const char *prefix,
		     struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
