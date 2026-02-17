#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "repository.h"
#include "strbuf.h"
#include "string-list.h"
#include "run-command.h"
#include "libvibes/agents/agents.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/* Forward declaration from agent-worker.c */
int vibes_agent_worker_run(struct vibes_db *db, const char *agent_id,
			   const char *worktree_path);

/*
 * Orchestrator: coordinates multi-agent execution of an intent's tasks.
 *
 * Flow:
 *   1. Queue wave 0 tasks
 *   2. Spawn agents for each task
 *   3. Wait for wave completion
 *   4. Advance to next wave
 *   5. Repeat until all waves done
 *   6. Merge results
 */

static int count_active_tasks(struct vibes_db *db, const char *intent_id)
{
	sqlite3_stmt *stmt;
	int count = 0;

	stmt = vibes_db_prepare(db,
		"SELECT COUNT(*) FROM tasks "
		"WHERE intent_id = ? "
		"AND status IN ('pending', 'claimed', 'in_progress');");
	if (!stmt)
		return 0;

	sqlite3_bind_text(stmt, 1, intent_id, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_ROW)
		count = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	return count;
}

static int count_wave_tasks(struct vibes_db *db, const char *intent_id,
			    int wave)
{
	sqlite3_stmt *stmt;
	int count = 0;

	stmt = vibes_db_prepare(db,
		"SELECT COUNT(*) FROM tasks "
		"WHERE intent_id = ? AND wave_number = ?;");
	if (!stmt)
		return 0;

	sqlite3_bind_text(stmt, 1, intent_id, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 2, wave);

	if (sqlite3_step(stmt) == SQLITE_ROW)
		count = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	return count;
}

static int get_max_wave(struct vibes_db *db, const char *intent_id)
{
	sqlite3_stmt *stmt;
	int max_wave = 0;

	stmt = vibes_db_prepare(db,
		"SELECT MAX(wave_number) FROM tasks WHERE intent_id = ?;");
	if (!stmt)
		return 0;

	sqlite3_bind_text(stmt, 1, intent_id, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_ROW)
		max_wave = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	return max_wave;
}

int vibes_orchestrator_run(struct vibes_db *db, struct repository *repo,
			   const char *intent_id)
{
	int max_wave = get_max_wave(db, intent_id);
	int wave;
	int total_tasks = 0;
	int completed_tasks = 0;
	struct agent_event event;

	printf("\nOrchestrator starting for intent %.8s\n", intent_id);
	printf("  Total waves: %d\n\n", max_wave + 1);

	/* Update intent status to in_progress */
	vibes_db_update_intent_status(db, intent_id, "in_progress");

	for (wave = 0; wave <= max_wave; wave++) {
		int wave_size = count_wave_tasks(db, intent_id, wave);
		int i;

		if (wave_size == 0)
			continue;

		printf("Wave %d: %d task(s)\n", wave, wave_size);

		/* Queue tasks for this wave */
		vibes_queue_tasks(db, intent_id, wave);

		/*
		 * In a full implementation with fork/exec, we'd spawn
		 * separate processes here. For now, we execute tasks
		 * sequentially within this process.
		 */
		for (i = 0; i < wave_size; i++) {
			struct vibes_agent agent;
			char *task_id;

			memset(&agent, 0, sizeof(agent));
			vibes_ulid_generate(agent.id);

			task_id = vibes_queue_claim(db, agent.id);
			if (!task_id)
				break;

			printf("  [%d/%d] Agent %.8s -> task %.8s\n",
			       i + 1, wave_size, agent.id, task_id);

			/* Execute the task (simplified: in-process) */
			vibes_agent_worker_run(db, agent.id, NULL);

			free(task_id);
			completed_tasks++;
			total_tasks++;
		}

		/* Check wave completion */
		vibes_queue_advance_wave(db, intent_id);
		printf("  Wave %d complete.\n\n", wave);
	}

	/* Mark intent as completed */
	if (count_active_tasks(db, intent_id) == 0) {
		vibes_db_update_intent_status(db, intent_id, "completed");

		memset(&event, 0, sizeof(event));
		event.type = EVENT_COMPLETED;
		event.intent_id = (char *)intent_id;
		event.payload = (char *)"all tasks completed";
		vibes_event_publish(db, &event);

		printf("Intent %.8s completed! %d task(s) executed.\n",
		       intent_id, completed_tasks);
	} else {
		printf("Intent %.8s: %d tasks remain.\n",
		       intent_id, count_active_tasks(db, intent_id));
	}

	return 0;
}

#endif /* VIBES_ENABLED */
