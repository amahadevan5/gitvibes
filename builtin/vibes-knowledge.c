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

static const char * const vibes_knowledge_usage[] = {
	"git vibes-knowledge index [--max-files <n>]",
	"git vibes-knowledge query <symbol>",
	"git vibes-knowledge deps <file>",
	"git vibes-knowledge stats",
	NULL
};

/* Forward declaration from indexer.c */
int vibes_kg_stats(struct vibes_db *db, int *nr_nodes, int *nr_edges,
		   int *nr_files);

static int do_index(struct vibes_db *db, struct repository *repo,
		    int max_files)
{
	int indexed;

	printf("Indexing repository with Tree-sitter...\n");
	indexed = vibes_kg_index_repo(db, repo, max_files);
	if (indexed < 0)
		return -1;

	printf("Indexed %d file(s).\n", indexed);

	{
		int nr_nodes = 0, nr_edges = 0, nr_files = 0;
		vibes_kg_stats(db, &nr_nodes, &nr_edges, &nr_files);
		printf("  Nodes: %d (%d files)\n", nr_nodes, nr_files);
		printf("  Edges: %d\n", nr_edges);
	}

	return 0;
}

static int do_query(struct vibes_db *db, const char *symbol)
{
	struct string_list results = STRING_LIST_INIT_DUP;
	int i;

	if (vibes_kg_search(db, symbol, &results) < 0)
		return -1;

	if (!results.nr) {
		printf("No symbols matching '%s'\n", symbol);
	} else {
		printf("Symbols matching '%s':\n", symbol);
		for (i = 0; i < results.nr; i++)
			printf("  %s\n", results.items[i].string);
	}

	string_list_clear(&results, 0);
	return 0;
}

static int do_deps(struct vibes_db *db, const char *file_path)
{
	struct string_list deps = STRING_LIST_INIT_DUP;
	struct string_list dependents = STRING_LIST_INIT_DUP;
	struct string_list tests = STRING_LIST_INIT_DUP;
	int i;

	printf("Dependencies for %s:\n\n", file_path);

	vibes_kg_get_dependencies(db, file_path, &deps);
	if (deps.nr) {
		printf("  Imports/depends on:\n");
		for (i = 0; i < deps.nr; i++)
			printf("    %s\n", deps.items[i].string);
	} else {
		printf("  (no dependencies found)\n");
	}

	vibes_kg_get_dependents(db, file_path, &dependents);
	if (dependents.nr) {
		printf("\n  Depended on by:\n");
		for (i = 0; i < dependents.nr; i++)
			printf("    %s\n", dependents.items[i].string);
	}

	vibes_kg_get_related_tests(db, file_path, &tests);
	if (tests.nr) {
		printf("\n  Related tests:\n");
		for (i = 0; i < tests.nr; i++)
			printf("    %s\n", tests.items[i].string);
	}

	string_list_clear(&deps, 0);
	string_list_clear(&dependents, 0);
	string_list_clear(&tests, 0);
	return 0;
}

static int do_stats(struct vibes_db *db)
{
	int nr_nodes = 0, nr_edges = 0, nr_files = 0;

	vibes_kg_stats(db, &nr_nodes, &nr_edges, &nr_files);

	printf("Knowledge graph statistics:\n");
	printf("  Total nodes: %d\n", nr_nodes);
	printf("  File nodes:  %d\n", nr_files);
	printf("  Symbol nodes: %d\n", nr_nodes - nr_files);
	printf("  Edges:       %d\n", nr_edges);

	/* Show per-type counts */
	{
		sqlite3_stmt *stmt = vibes_db_prepare(db,
			"SELECT type, COUNT(*) FROM kg_nodes "
			"GROUP BY type ORDER BY type;");
		if (stmt) {
			printf("\n  By type:\n");
			while (sqlite3_step(stmt) == SQLITE_ROW) {
				const char *type = (const char *)
					sqlite3_column_text(stmt, 0);
				int count = sqlite3_column_int(stmt, 1);
				printf("    %-12s %d\n",
				       type ? type : "?", count);
			}
			sqlite3_finalize(stmt);
		}
	}

	return 0;
}

int cmd_vibes_knowledge(int argc, const char **argv, const char *prefix,
			struct repository *repo)
{
	int max_files = 0;
	struct option options[] = {
		OPT_INTEGER(0, "max-files", &max_files,
			    "maximum files to index"),
		OPT_END()
	};
	struct vibes_db db;
	char *db_path;
	int ret = 0;

	argc = parse_options(argc, argv, prefix, options,
			     vibes_knowledge_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc < 1)
		usage_with_options(vibes_knowledge_usage, options);

	db_path = vibes_db_repo_path(repo->gitdir);
	if (vibes_db_open(&db, db_path) < 0) {
		free(db_path);
		die("gitvibes: cannot open database. Run 'git vibes-init' first.");
	}
	free(db_path);

	if (!strcmp(argv[0], "index")) {
		ret = do_index(&db, repo, max_files);
	} else if (!strcmp(argv[0], "query")) {
		if (argc < 2)
			die("usage: git vibes-knowledge query <symbol>");
		ret = do_query(&db, argv[1]);
	} else if (!strcmp(argv[0], "deps")) {
		if (argc < 2)
			die("usage: git vibes-knowledge deps <file>");
		ret = do_deps(&db, argv[1]);
	} else if (!strcmp(argv[0], "stats")) {
		ret = do_stats(&db);
	} else {
		die("unknown subcommand: %s", argv[0]);
	}

	vibes_ts_shutdown();
	vibes_db_close(&db);
	return ret < 0 ? 1 : 0;
}
#else
int cmd_vibes_knowledge(int argc, const char **argv, const char *prefix,
			struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
