#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "repository.h"
#include "strbuf.h"
#include "string-list.h"
#include "libvibes/intent/intent.h"
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

	ret = 0;

done:
	vibes_intent_free(&intent);
	return ret;
}

#endif /* VIBES_ENABLED */
