#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "repository.h"
#include "strbuf.h"
#include "string-list.h"
#include "run-command.h"
#include "libvibes/intent/intent.h"
#include "libvibes/agents/agents.h"
#include "libvibes/merge/semantic-merge.h"
#include "libvibes/safety/safety.h"
#include "libvibes/storage/db.h"

/*
 * Vibes Pipeline: full pipeline from raw NL input to task plan.
 *
 * Steps:
 *   1. Create intent record in DB
 *   2. Parse NL → structured intent (via AI)
 *   3. Decompose intent → task DAG (via AI)
 *   4. Print plan summary
 */

static void print_intent_summary(const struct vibes_intent *intent)
{
	int i;

	printf("\nIntent: %s\n", intent->id);
	printf("  Type:   %s\n", intent_type_to_str(intent->type));
	printf("  Status: %s\n", intent_status_to_str(intent->status));
	printf("  Input:  %s\n", intent->raw_input ? intent->raw_input : "(none)");
	if (intent->parsed_goal)
		printf("  Goal:   %s\n", intent->parsed_goal);

	if (intent->criteria.nr) {
		printf("  Criteria:\n");
		for (i = 0; i < intent->criteria.nr; i++)
			printf("    - %s\n", intent->criteria.items[i].string);
	}
	if (intent->constraints.nr) {
		printf("  Constraints:\n");
		for (i = 0; i < intent->constraints.nr; i++)
			printf("    - %s\n", intent->constraints.items[i].string);
	}
}

static void print_task_plan(const struct vibes_intent *intent)
{
	struct vibes_task *task;
	int current_wave = -1;
	int i;

	if (!intent->tasks) {
		printf("\n  (no tasks decomposed)\n");
		return;
	}

	printf("\nTask Plan (%d tasks):\n", intent->nr_tasks);

	for (task = intent->tasks; task; task = task->next) {
		if (task->wave_number != current_wave) {
			current_wave = task->wave_number;
			printf("\n  Wave %d%s:\n", current_wave,
			       current_wave == 0 ? " (parallel)" : "");
		}
		printf("    [%s] %s\n", task->id, task->title ? task->title : "(untitled)");
		if (task->description)
			printf("          %s\n", task->description);
		if (task->est_files.nr) {
			printf("          Files: ");
			for (i = 0; i < task->est_files.nr; i++) {
				if (i > 0)
					printf(", ");
				printf("%s", task->est_files.items[i].string);
			}
			printf("\n");
		}
		if (task->dependencies.nr) {
			printf("          Deps: ");
			for (i = 0; i < task->dependencies.nr; i++) {
				if (i > 0)
					printf(", ");
				printf("%s", task->dependencies.items[i].string);
			}
			printf("\n");
		}
	}
}

int vibes_pipeline_run(const char *raw_input,
		       struct vibes_db *db,
		       struct repository *repo)
{
	struct vibes_intent intent;
	int ret;

	vibes_intent_init(&intent);
	intent.raw_input = xstrdup(raw_input);

	/* Step 1: Create intent in DB */
	printf("Creating intent...\n");
	ret = vibes_intent_create(db, &intent);
	if (ret < 0) {
		error("gitvibes: failed to create intent");
		goto done;
	}
	printf("  Intent %s created\n", intent.id);

	/* Step 2: Parse NL → structured intent */
	printf("Parsing intent with AI...\n");
	ret = vibes_parse_intent(&intent, repo);
	if (ret < 0) {
		/* Non-fatal: we can still proceed with raw input */
		warning("gitvibes: AI parsing failed, using raw input as goal");
		intent.parsed_goal = xstrdup(raw_input);
		intent.type = INTENT_FEATURE;
	}

	print_intent_summary(&intent);

	/* Step 3: Decompose → task DAG */
	printf("\nDecomposing into tasks...\n");
	ret = vibes_decompose_intent(&intent, db, repo);
	if (ret < 0) {
		error("gitvibes: failed to decompose intent");
		goto done;
	}

	print_task_plan(&intent);
	printf("\nPipeline complete. Use 'git vibes --show %s' for details.\n",
	       intent.id);
	printf("Run with --execute to start agents.\n");

	ret = 0;

done:
	vibes_intent_free(&intent);
	return ret;
}

/*
 * Merge agent branches back to the current branch.
 * Each agent works in a worktree on a vibes/agent-XXXX branch;
 * after the orchestrator finishes, merge each branch sequentially.
 */
static int merge_agent_branches(struct vibes_db *db, struct repository *repo,
				const char *intent_id)
{
	sqlite3_stmt *stmt;
	int merged = 0;
	int failed = 0;

	stmt = vibes_db_prepare(db,
		"SELECT DISTINCT assigned_agent FROM tasks "
		"WHERE intent_id = ? AND status = 'completed' "
		"AND assigned_agent IS NOT NULL;");
	if (!stmt)
		return 0;

	sqlite3_bind_text(stmt, 1, intent_id, -1, SQLITE_STATIC);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *agent_id;
		struct strbuf branch = STRBUF_INIT;

		agent_id = (const char *)sqlite3_column_text(stmt, 0);
		if (!agent_id)
			continue;

		strbuf_addf(&branch, "vibes/agent-%.8s", agent_id);

		printf("  Merging branch %s...\n", branch.buf);

		if (vibes_semantic_merge(repo, branch.buf, db) < 0) {
			fprintf(stderr, "gitvibes: failed to merge %s\n",
				branch.buf);
			failed++;
		} else {
			merged++;
		}

		strbuf_release(&branch);
	}

	sqlite3_finalize(stmt);

	printf("  Merged %d branch(es)", merged);
	if (failed)
		printf(", %d failed", failed);
	printf(".\n");

	return failed > 0 ? -1 : 0;
}

/*
 * Full pipeline with execution: parse → decompose → orchestrate → merge.
 *
 * This is the end-to-end flow triggered by `git vibes "..." --execute`.
 *
 * Steps:
 *   1. Create intent record in DB
 *   2. Parse NL → structured intent (via AI)
 *   3. Decompose intent → task DAG (via AI)
 *   4. Create safety snapshot
 *   5. Run orchestrator (spawn agents, execute tasks)
 *   6. Run validation gates
 *   7. Merge agent branches back to current branch
 *   8. Report results
 */
int vibes_pipeline_execute(const char *raw_input,
			   struct vibes_db *db,
			   struct repository *repo)
{
	struct vibes_intent intent;
	int ret;

	vibes_intent_init(&intent);
	intent.raw_input = xstrdup(raw_input);

	/* Step 1: Create intent */
	printf("Creating intent...\n");
	ret = vibes_intent_create(db, &intent);
	if (ret < 0) {
		error("gitvibes: failed to create intent");
		goto done;
	}
	printf("  Intent %s created\n", intent.id);

	/* Step 2: Parse NL → structured intent */
	printf("Parsing intent with AI...\n");
	ret = vibes_parse_intent(&intent, repo);
	if (ret < 0) {
		warning("gitvibes: AI parsing failed, using raw input as goal");
		intent.parsed_goal = xstrdup(raw_input);
		intent.type = INTENT_FEATURE;
	}

	print_intent_summary(&intent);

	/* Step 3: Decompose → task DAG */
	printf("\nDecomposing into tasks...\n");
	ret = vibes_decompose_intent(&intent, db, repo);
	if (ret < 0) {
		error("gitvibes: failed to decompose intent");
		goto done;
	}

	print_task_plan(&intent);

	if (!intent.nr_tasks) {
		printf("\nNo tasks to execute.\n");
		ret = 0;
		goto done;
	}

	/* Step 4: Create safety snapshot before execution */
	printf("\nCreating safety snapshot...\n");
	vibes_safety_snapshot(db, repo, "pre-execute");

	/* Step 5: Run orchestrator */
	printf("\nStarting agent orchestration...\n");
	ret = vibes_orchestrator_run(db, repo, intent.id);

	if (ret < 0)
		warning("gitvibes: orchestrator reported failures");

	/* Step 6: Run validation gates */
	{
		struct gate_result *results = NULL;
		int nr_results = 0;
		int gate_failed = 0;
		int i;

		printf("\nRunning validation gates...\n");
		vibes_gate_run_all(repo, db, &results, &nr_results);

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

		if (gate_failed)
			warning("gitvibes: validation gates failed; "
				"review before integrating");
	}

	/* Step 7: Merge agent branches */
	printf("\nMerging agent work...\n");
	ret = merge_agent_branches(db, repo, intent.id);

	/* Step 8: Report */
	if (ret == 0) {
		vibes_db_update_intent_status(db, intent.id, "completed");
		printf("\nIntent %s completed successfully.\n", intent.id);
		printf("Use 'git vibes --show %s' for details.\n", intent.id);
		printf("Use 'git vibes-merge --rollback' to undo.\n");
	} else {
		printf("\nIntent %s finished with errors.\n", intent.id);
		printf("Use 'git vibes --show %s' to review.\n", intent.id);
	}

done:
	vibes_intent_free(&intent);
	return ret;
}

#endif /* VIBES_ENABLED */
