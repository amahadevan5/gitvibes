#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "builtin.h"
#ifdef VIBES_ENABLED
#include "config.h"
#include "repository.h"
#include "parse-options.h"
#include <sqlite3.h>
#include "libvibes/session/session.h"
#include "libvibes/storage/db.h"

static const char * const vibes_session_usage[] = {
	"git vibes-session start <name>",
	"git vibes-session end",
	"git vibes-session resume [<id>]",
	"git vibes-session list",
	"git vibes-session show <id>",
	NULL
};

static int do_start(struct vibes_db *db, struct repository *repo,
		    const char *name)
{
	struct vibes_session session;

	memset(&session, 0, sizeof(session));
	if (vibes_session_start(db, name, &session) < 0) {
		fprintf(stderr, "gitvibes: failed to start session\n");
		return 1;
	}

	printf("Session started: %s\n", session.id);
	printf("  Name: %s\n", session.name ? session.name : "(unnamed)");

	/* Create initial context snapshot */
	vibes_snapshot_create(db, repo, session.id);
	printf("  Context snapshot saved.\n");

	vibes_session_free(&session);
	return 0;
}

static int do_end(struct vibes_db *db, struct repository *repo)
{
	struct string_list sessions = STRING_LIST_INIT_DUP;
	const char *latest_id;

	/* Find the most recent active session */
	vibes_session_list(db, &sessions);
	if (!sessions.nr) {
		printf("No active sessions.\n");
		string_list_clear(&sessions, 0);
		return 0;
	}

	/* The first entry should be most recent (we'll use it) */
	latest_id = sessions.items[0].string;
	/* Extract just the ID (first 26 chars of the line) */
	{
		char id_buf[27];
		snprintf(id_buf, sizeof(id_buf), "%.26s", latest_id);

		/* Create final context snapshot */
		vibes_snapshot_create(db, repo, id_buf);

		if (vibes_session_end(db, id_buf) < 0) {
			fprintf(stderr,
				"gitvibes: failed to end session\n");
			string_list_clear(&sessions, 0);
			return 1;
		}

		printf("Session %.8s ended.\n", id_buf);
		printf("  Context snapshot saved.\n");
	}

	string_list_clear(&sessions, 0);
	return 0;
}

static int do_resume(struct vibes_db *db, const char *session_id)
{
	struct string_list sessions = STRING_LIST_INIT_DUP;
	const char *id_to_resume;

	if (session_id) {
		id_to_resume = session_id;
	} else {
		/* Resume most recent session */
		vibes_session_list(db, &sessions);
		if (!sessions.nr) {
			printf("No sessions to resume.\n");
			string_list_clear(&sessions, 0);
			return 0;
		}
		/* Extract ID from first list entry */
		id_to_resume = sessions.items[0].string;
	}

	printf("Restoring session context...\n\n");
	vibes_snapshot_restore(db, id_to_resume);

	string_list_clear(&sessions, 0);
	return 0;
}

static int do_list(struct vibes_db *db)
{
	struct string_list sessions = STRING_LIST_INIT_DUP;
	int i;

	vibes_session_list(db, &sessions);

	if (!sessions.nr) {
		printf("No sessions found.\n");
	} else {
		printf("Sessions:\n\n");
		for (i = 0; i < sessions.nr; i++)
			printf("  %s\n", sessions.items[i].string);
	}

	string_list_clear(&sessions, 0);
	return 0;
}

static int do_show(struct vibes_db *db, const char *session_id)
{
	struct vibes_session session;

	memset(&session, 0, sizeof(session));
	if (vibes_session_get(db, session_id, &session) < 0) {
		fprintf(stderr, "gitvibes: session not found: %s\n",
			session_id);
		return 1;
	}

	printf("Session: %s\n", session.id);
	printf("  Name:    %s\n", session.name ? session.name : "(unnamed)");
	printf("  Created: %lld\n", (long long)session.created_at);
	if (session.ended_at)
		printf("  Ended:   %lld\n", (long long)session.ended_at);
	else
		printf("  Status:  active\n");

	printf("\nContext:\n");
	vibes_snapshot_restore(db, session_id);

	vibes_session_free(&session);
	return 0;
}

int cmd_vibes_session(int argc, const char **argv, const char *prefix,
		      struct repository *repo)
{
	struct option options[] = {
		OPT_END()
	};
	struct vibes_db db;
	char *db_path;
	int ret = 0;

	argc = parse_options(argc, argv, prefix, options,
			     vibes_session_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc < 1)
		usage_with_options(vibes_session_usage, options);

	db_path = vibes_db_repo_path(repo->gitdir);
	if (vibes_db_open(&db, db_path) < 0) {
		free(db_path);
		die("gitvibes: cannot open database. Run 'git vibes-init' first.");
	}
	free(db_path);

	if (!strcmp(argv[0], "start")) {
		const char *name = argc > 1 ? argv[1] : NULL;
		ret = do_start(&db, repo, name);
	} else if (!strcmp(argv[0], "end")) {
		ret = do_end(&db, repo);
	} else if (!strcmp(argv[0], "resume")) {
		const char *id = argc > 1 ? argv[1] : NULL;
		ret = do_resume(&db, id);
	} else if (!strcmp(argv[0], "list")) {
		ret = do_list(&db);
	} else if (!strcmp(argv[0], "show")) {
		if (argc < 2)
			die("usage: git vibes-session show <id>");
		ret = do_show(&db, argv[1]);
	} else {
		die("unknown subcommand: %s", argv[0]);
	}

	vibes_db_close(&db);
	return ret;
}
#else
int cmd_vibes_session(int argc, const char **argv, const char *prefix,
		      struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
