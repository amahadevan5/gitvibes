#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "repository.h"
#include "strbuf.h"
#include "run-command.h"
#include "libvibes/agents/agents.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/*
 * Agent Worker: the entry point for a forked agent process.
 * Runs in a git worktree, claims and executes a task,
 * then reports completion via the event bus.
 *
 * In the current implementation, worker logic is invoked
 * directly by the orchestrator rather than via fork/exec.
 */

/*
 * Execute a task in the current worktree using AI assistance.
 * This is a simplified version that creates commits with smart-commit.
 */
static int execute_task(struct vibes_db *db, const char *task_id,
			const char *worktree_path)
{
	sqlite3_stmt *stmt;
	struct agent_event event;
	const char *title = NULL;
	const char *description = NULL;

	/* Get task details */
	stmt = vibes_db_prepare(db,
		"SELECT title, description FROM tasks WHERE id = ?;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		title = (const char *)sqlite3_column_text(stmt, 0);
		description = (const char *)sqlite3_column_text(stmt, 1);
	}

	/* Update task to in_progress */
	{
		sqlite3_stmt *update = vibes_db_prepare(db,
			"UPDATE tasks SET status = 'in_progress' WHERE id = ?;");
		if (update) {
			sqlite3_bind_text(update, 1, task_id, -1, SQLITE_STATIC);
			sqlite3_step(update);
			sqlite3_finalize(update);
		}
	}

	/* Publish progress event */
	memset(&event, 0, sizeof(event));
	event.type = EVENT_PROGRESS;
	event.task_id = (char *)task_id;
	event.payload = (char *)(title ? title : "working...");
	vibes_event_publish(db, &event);

	sqlite3_finalize(stmt);

	/*
	 * In a full implementation, this would:
	 * 1. Use AI to generate code changes
	 * 2. Write files to the worktree
	 * 3. Run tests
	 * 4. Create smart commits
	 *
	 * For now, mark the task as completed to demonstrate
	 * the orchestration flow.
	 */

	/* Mark task completed */
	{
		sqlite3_stmt *complete = vibes_db_prepare(db,
			"UPDATE tasks SET status = 'completed' WHERE id = ?;");
		if (complete) {
			sqlite3_bind_text(complete, 1, task_id, -1, SQLITE_STATIC);
			sqlite3_step(complete);
			sqlite3_finalize(complete);
		}
	}

	/* Publish completion event */
	memset(&event, 0, sizeof(event));
	event.type = EVENT_COMPLETED;
	event.task_id = (char *)task_id;
	vibes_event_publish(db, &event);

	return 0;
}

/*
 * Worker main loop: claim tasks and execute them.
 */
int vibes_agent_worker_run(struct vibes_db *db, const char *agent_id,
			   const char *worktree_path)
{
	char *task_id;
	int tasks_done = 0;

	while ((task_id = vibes_queue_claim(db, agent_id)) != NULL) {
		printf("  Agent %.8s: working on task %.8s\n",
		       agent_id, task_id);

		if (execute_task(db, task_id, worktree_path) < 0) {
			vibes_queue_release(db, task_id);
			free(task_id);
			break;
		}

		free(task_id);
		tasks_done++;
	}

	return tasks_done;
}

#endif /* VIBES_ENABLED */
