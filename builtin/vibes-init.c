#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "builtin.h"
#ifdef VIBES_ENABLED
#include "config.h"
#include "repository.h"
#include "parse-options.h"
#include "strbuf.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

static const char * const vibes_init_usage[] = {
	"git vibes-init [--force]",
	NULL
};

int cmd_vibes_init(int argc, const char **argv, const char *prefix,
		   struct repository *repo)
{
	int force = 0;
	struct option options[] = {
		OPT_BOOL('f', "force", &force, "reinitialize even if already set up"),
		OPT_END()
	};
	struct vibes_db db;
	char *db_path;
	const char *git_dir;
	struct stat st;

	argc = parse_options(argc, argv, prefix, options, vibes_init_usage, 0);

	/* Verify we're in a git repository */
	git_dir = repo->gitdir;
	if (!git_dir)
		die("gitvibes: not a git repository");

	/* Check if already initialized */
	db_path = vibes_db_repo_path(git_dir);
	if (!force && !stat(db_path, &st)) {
		fprintf(stderr, "gitvibes: already initialized (%s exists)\n"
				"Use --force to reinitialize.\n", db_path);
		free(db_path);
		return 0;
	}

	/* Create database with schema */
	if (vibes_db_open(&db, db_path) < 0) {
		free(db_path);
		return 1;
	}
	vibes_db_close(&db);

	/* Set default config values if not already set */
	{
		const char *val = NULL;
		if (repo_config_get_string_tmp(repo, "vibes.backend", &val))
			repo_config_set(repo, "vibes.backend", "hybrid");
		if (repo_config_get_string_tmp(repo, "vibes.route-commit-msg", &val))
			repo_config_set(repo, "vibes.route-commit-msg", "cli");
		if (repo_config_get_string_tmp(repo, "vibes.route-intent-parse", &val))
			repo_config_set(repo, "vibes.route-intent-parse", "cli");
	}

	printf("gitvibes initialized.\n");
	printf("  Database: %s\n", db_path);
	printf("  Backend:  hybrid (Claude CLI + API fallback)\n");
	printf("\nQuick start:\n");
	printf("  git vibes-commit        Smart atomic commits from staged changes\n");
	printf("  git vibes \"add auth\"    Plan a feature from natural language\n");
	printf("  git vibes-status        Show active intents and tasks\n");
	printf("\nConfiguration:\n");
	printf("  git config vibes.backend cli|api|hybrid\n");
	printf("  git config vibes.claude-api-key <key>     # for API backend\n");

	free(db_path);
	return 0;
}
#else
int cmd_vibes_init(int argc, const char **argv, const char *prefix,
		   struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
