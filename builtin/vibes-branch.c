#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "builtin.h"
#ifdef VIBES_ENABLED
#include "config.h"
#include "repository.h"
#include "parse-options.h"
#include <sqlite3.h>
#include "libvibes/branch/branch-strategy.h"
#include "libvibes/storage/db.h"

static const char * const vibes_branch_usage[] = {
	"git vibes-branch [--intent <id>]",
	"git vibes-branch detect",
	"git vibes-branch topology <intent-id>",
	"git vibes-branch merge-order <intent-id>",
	NULL
};

static int do_detect(struct repository *repo)
{
	enum branch_convention c;

	c = vibes_detect_convention(repo);
	printf("Detected branch convention: %s\n",
	       branch_convention_str(c));
	return 0;
}

static int do_topology(struct vibes_db *db, struct repository *repo,
		       const char *intent_id)
{
	struct branch_topology topo;
	enum branch_convention conv;
	int i, ret;

	memset(&topo, 0, sizeof(topo));
	string_list_init_dup(&topo.task_branches);

	conv = vibes_detect_convention(repo);
	ret = vibes_plan_topology(db, intent_id, conv, &topo);
	if (ret < 0) {
		fprintf(stderr, "gitvibes: failed to plan topology\n");
		return 1;
	}

	printf("Branch Topology for intent %.8s:\n\n", intent_id);
	printf("  Convention: %s\n", branch_convention_str(topo.convention));
	printf("  Main:       %s\n", topo.main_branch ? topo.main_branch : "?");
	printf("  Feature:    %s\n",
	       topo.feature_branch ? topo.feature_branch : "?");

	if (topo.task_branches.nr) {
		printf("\n  Task branches:\n");
		for (i = 0; i < topo.task_branches.nr; i++)
			printf("    %s\n", topo.task_branches.items[i].string);
	}

	vibes_topology_free(&topo);
	return 0;
}

static int do_merge_order(struct vibes_db *db, const char *intent_id)
{
	struct string_list order = STRING_LIST_INIT_DUP;
	int i;

	if (vibes_merge_order(db, intent_id, &order) < 0) {
		fprintf(stderr, "gitvibes: failed to compute merge order\n");
		string_list_clear(&order, 0);
		return 1;
	}

	if (!order.nr) {
		printf("No completed tasks for intent %.8s\n", intent_id);
	} else {
		printf("Merge order for intent %.8s:\n\n", intent_id);
		for (i = 0; i < order.nr; i++)
			printf("  %d. %s\n", i + 1,
			       order.items[i].string);
	}

	string_list_clear(&order, 0);
	return 0;
}

int cmd_vibes_branch(int argc, const char **argv, const char *prefix,
		     struct repository *repo)
{
	const char *intent_id = NULL;
	struct option options[] = {
		OPT_STRING('i', "intent", &intent_id, "id",
			   "intent ID for branch planning"),
		OPT_END()
	};
	struct vibes_db db;
	char *db_path;
	int ret = 0;

	argc = parse_options(argc, argv, prefix, options,
			     vibes_branch_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc < 1) {
		/* Default: detect convention */
		return do_detect(repo);
	}

	if (!strcmp(argv[0], "detect"))
		return do_detect(repo);

	/* Commands below require DB */
	db_path = vibes_db_repo_path(repo->gitdir);
	if (vibes_db_open(&db, db_path) < 0) {
		free(db_path);
		die("gitvibes: cannot open database. Run 'git vibes-init' first.");
	}
	free(db_path);

	if (!strcmp(argv[0], "topology")) {
		if (argc < 2)
			die("usage: git vibes-branch topology <intent-id>");
		ret = do_topology(&db, repo, argv[1]);
	} else if (!strcmp(argv[0], "merge-order")) {
		if (argc < 2)
			die("usage: git vibes-branch merge-order <intent-id>");
		ret = do_merge_order(&db, argv[1]);
	} else {
		die("unknown subcommand: %s", argv[0]);
	}

	vibes_db_close(&db);
	return ret;
}
#else
int cmd_vibes_branch(int argc, const char **argv, const char *prefix,
		     struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
