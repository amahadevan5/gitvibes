#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "builtin.h"
#ifdef VIBES_ENABLED
#include "config.h"
#include "repository.h"
#include "parse-options.h"
#include <sqlite3.h>
#include "libvibes/tui/tui.h"
#include "libvibes/webui/webui.h"
#include "libvibes/storage/db.h"

static const char * const vibes_dashboard_usage[] = {
	"git vibes-dashboard",
	"git vibes-dashboard --web [--port <port>]",
	NULL
};

int cmd_vibes_dashboard(int argc, const char **argv, const char *prefix,
			struct repository *repo)
{
	int web_mode = 0;
	int port = 3737;
	struct option options[] = {
		OPT_BOOL(0, "web", &web_mode,
			 "launch web UI instead of TUI"),
		OPT_INTEGER('p', "port", &port,
			    "web server port (default: 3737)"),
		OPT_END()
	};
	struct vibes_db db;
	char *db_path;

	argc = parse_options(argc, argv, prefix, options,
			     vibes_dashboard_usage, 0);

	db_path = vibes_db_repo_path(repo->gitdir);
	if (vibes_db_open(&db, db_path) < 0) {
		free(db_path);
		die("gitvibes: cannot open database. Run 'git vibes-init' first.");
	}
	free(db_path);

	if (web_mode) {
		struct vibes_webui webui;

		if (vibes_webui_start(&webui, &db, repo, port) < 0) {
			vibes_db_close(&db);
			return 1;
		}
		vibes_webui_run(&webui);
		vibes_webui_stop(&webui);
		vibes_db_close(&db);
		return 0;
	}

	/* Launch ncurses TUI */
	{
		struct vibes_tui tui;

		vibes_tui_init(&tui, &db, repo);
		vibes_tui_run(&tui);
		vibes_tui_shutdown(&tui);
	}

	vibes_db_close(&db);
	return 0;
}
#else
int cmd_vibes_dashboard(int argc, const char **argv, const char *prefix,
			struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
