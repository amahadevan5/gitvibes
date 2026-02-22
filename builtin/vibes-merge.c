#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "builtin.h"
#ifdef VIBES_ENABLED
#include "config.h"
#include "repository.h"
#include "parse-options.h"
#include <sqlite3.h>
#include "libvibes/merge/semantic-merge.h"
#include "libvibes/safety/safety.h"
#include "libvibes/storage/db.h"

static const char * const vibes_merge_usage[] = {
	"git vibes-merge <branch>",
	"git vibes-merge --rollback",
	"git vibes-merge --rollback-intent <id>",
	"git vibes-merge --candidates <file>",
	NULL
};

int cmd_vibes_merge(int argc, const char **argv, const char *prefix,
		    struct repository *repo)
{
	int rollback = 0;
	int run_gates = 1;
	const char *rollback_intent_id = NULL;
	const char *candidates_file = NULL;
	struct option options[] = {
		OPT_BOOL(0, "rollback", &rollback,
			 "rollback last vibes operation"),
		OPT_STRING(0, "rollback-intent", &rollback_intent_id, "id",
			   "rollback all commits for an intent"),
		OPT_STRING(0, "candidates", &candidates_file, "file",
			   "generate AI resolution candidates for conflicted file"),
		OPT_BOOL(0, "gates", &run_gates,
			 "run validation gates before merge (default: on)"),
		OPT_END()
	};
	struct vibes_db db;
	char *db_path;
	int ret;

	argc = parse_options(argc, argv, prefix, options,
			     vibes_merge_usage, 0);

	db_path = vibes_db_repo_path(repo->gitdir);
	if (vibes_db_open(&db, db_path) < 0) {
		free(db_path);
		die("gitvibes: cannot open database. Run 'git vibes-init' first.");
	}
	free(db_path);

	if (rollback) {
		printf("Rolling back last vibes operation...\n");
		ret = vibes_rollback_last(&db, repo);
		vibes_db_close(&db);
		return ret < 0 ? 1 : 0;
	}

	if (rollback_intent_id) {
		ret = vibes_rollback_intent(&db, repo, rollback_intent_id);
		vibes_db_close(&db);
		return ret < 0 ? 1 : 0;
	}

	if (candidates_file) {
		struct vibes_conflict conflict;
		struct resolution_candidate candidates[3];
		int nr, j;

		vibes_conflict_init(&conflict);
		conflict.file_path = xstrdup(candidates_file);

		/* Read conflict stages */
		vibes_read_conflict_stage(&conflict.base, 1, candidates_file);
		vibes_read_conflict_stage(&conflict.ours, 2, candidates_file);
		vibes_read_conflict_stage(&conflict.theirs, 3, candidates_file);

		nr = vibes_generate_candidates(&db, &conflict, candidates, 3);
		if (nr > 0) {
			for (j = 0; j < nr; j++) {
				printf("Candidate %d (score: %.1f):\n%s\n---\n",
				       j + 1, candidates[j].score,
				       candidates[j].content.buf);
				vibes_candidate_free(&candidates[j]);
			}
		} else {
			printf("No AI candidates generated "
			       "(check vibes.backend config)\n");
		}
		vibes_conflict_free(&conflict);
		vibes_db_close(&db);
		return 0;
	}

	if (argc < 1) {
		vibes_db_close(&db);
		usage_with_options(vibes_merge_usage, options);
	}

	/* Run validation gates before merge */
	if (run_gates) {
		struct gate_result *results = NULL;
		int nr_results = 0;
		int gate_failed = 0;
		int i;

		printf("Running validation gates...\n");
		vibes_gate_run_all(repo, &db, &results, &nr_results);

		for (i = 0; i < nr_results; i++) {
			printf("  %s: %s\n", results[i].gate_name,
			       results[i].passed ? "PASS" : "FAIL");
			if (!results[i].passed && results[i].output)
				printf("    %s\n", results[i].output);
			if (!results[i].passed)
				gate_failed = 1;
			vibes_gate_result_free(&results[i]);
		}
		free(results);

		if (gate_failed) {
			printf("\nValidation gates failed. "
			       "Use --no-gates to skip.\n");
			vibes_db_close(&db);
			return 1;
		}
		printf("All gates passed.\n\n");
	}

	/* Create safety snapshot before merge */
	vibes_safety_snapshot(&db, repo, "pre-merge");

	/* Run semantic merge */
	printf("Merging branch '%s' with semantic awareness...\n", argv[0]);
	ret = vibes_semantic_merge(repo, argv[0], &db);

	if (ret < 0)
		fprintf(stderr, "gitvibes: merge encountered errors\n");
	else
		printf("Semantic merge completed successfully.\n");

	vibes_db_close(&db);
	return ret < 0 ? 1 : 0;
}
#else
int cmd_vibes_merge(int argc, const char **argv, const char *prefix,
		    struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
