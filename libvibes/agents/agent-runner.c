#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include <signal.h>
#include <sys/wait.h>
#include "repository.h"
#include "strbuf.h"
#include "string-list.h"
#include "run-command.h"
#include "libvibes/agents/agents.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/*
 * Agent Runner: spawns agent processes in isolated git worktrees.
 * Each agent gets its own worktree and works independently.
 */

/*
 * Create a git worktree for an agent.
 */
static int create_worktree(struct repository *repo, const char *path,
			   const char *branch)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	strvec_pushl(&cp.args, "worktree", "add", path, "-b", branch, NULL);
	cp.git_cmd = 1;

	return run_command(&cp);
}

/*
 * Remove a git worktree.
 */
static int remove_worktree(const char *path)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	strvec_pushl(&cp.args, "worktree", "remove", "--force", path, NULL);
	cp.git_cmd = 1;

	return run_command(&cp);
}

int vibes_agent_spawn(struct vibes_db *db, struct repository *repo,
		      const char *task_id, struct vibes_agent *agent)
{
	struct strbuf worktree_path = STRBUF_INIT;
	struct strbuf branch_name = STRBUF_INIT;
	struct agent_event event;

	vibes_ulid_generate(agent->id);
	agent->type = AGENT_WORKER;
	agent->status = AGENT_WORKING;
	agent->task_id = xstrdup(task_id);
	agent->started_at = vibes_timestamp_ms();

	/* Create worktree path */
	strbuf_addf(&worktree_path, "/tmp/gitvibes-%.8s", agent->id);
	agent->worktree_path = strbuf_detach(&worktree_path, NULL);

	/* Create branch name */
	strbuf_addf(&branch_name, "vibes/agent-%.8s", agent->id);

	/* Create worktree */
	if (create_worktree(repo, agent->worktree_path,
			    branch_name.buf) < 0) {
		strbuf_release(&branch_name);
		return error("gitvibes: failed to create worktree for agent");
	}
	strbuf_release(&branch_name);

	/* Publish spawn event */
	memset(&event, 0, sizeof(event));
	event.type = EVENT_TASK_CLAIMED;
	event.agent_id = agent->id;
	event.task_id = agent->task_id;
	vibes_event_publish(db, &event);

	/*
	 * In a full implementation, we'd fork() here and exec the agent
	 * worker process. For now, we record the agent in the DB and
	 * the orchestrator handles execution.
	 */
	agent->pid = getpid(); /* placeholder */

	printf("  Agent %.8s spawned in %s\n",
	       agent->id, agent->worktree_path);

	return 0;
}

int vibes_agent_wait(pid_t pid)
{
	int status;
	if (waitpid(pid, &status, WNOHANG) > 0)
		return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	return 0;
}

int vibes_agent_kill(pid_t pid)
{
	if (pid > 0 && pid != getpid()) {
		kill(pid, SIGTERM);
		usleep(100000); /* 100ms grace period */
		kill(pid, SIGKILL);
	}
	return 0;
}

int vibes_agent_cleanup(struct vibes_db *db, const char *agent_id)
{
	sqlite3_stmt *stmt;
	char worktree[256];
	int found = 0;

	/* Get worktree path from events */
	stmt = vibes_db_prepare(db,
		"SELECT payload FROM events "
		"WHERE agent_id = ? AND type = 'TASK_CLAIMED' "
		"ORDER BY created_at DESC LIMIT 1;");
	if (stmt) {
		sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_STATIC);
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			/* The worktree path is stored in the spawn event */
			found = 1;
		}
		sqlite3_finalize(stmt);
	}

	/* Clean up worktree */
	snprintf(worktree, sizeof(worktree), "/tmp/gitvibes-%.8s", agent_id);
	remove_worktree(worktree);

	/* Release any file locks held by this agent */
	{
		struct strbuf sql = STRBUF_INIT;
		strbuf_addf(&sql, "DELETE FROM file_locks WHERE agent_id = '%s';",
			    agent_id);
		vibes_db_exec(db, sql.buf);
		strbuf_release(&sql);
	}

	/* Release any claimed tasks */
	{
		struct strbuf sql = STRBUF_INIT;
		strbuf_addf(&sql,
			"UPDATE tasks SET status = 'pending', assigned_agent = NULL "
			"WHERE assigned_agent = '%s' AND status IN ('claimed', 'in_progress');",
			agent_id);
		vibes_db_exec(db, sql.buf);
		strbuf_release(&sql);
	}

	return 0;
}

int vibes_agent_list(struct vibes_db *db, struct string_list *out)
{
	sqlite3_stmt *stmt;

	stmt = vibes_db_prepare(db,
		"SELECT DISTINCT agent_id || ' ' || type || ' ' || "
		"       COALESCE(task_id, '?') "
		"FROM events "
		"WHERE agent_id IS NOT NULL "
		"AND type IN ('TASK_CLAIMED', 'PROGRESS', 'HEARTBEAT') "
		"GROUP BY agent_id "
		"ORDER BY MAX(created_at) DESC LIMIT 20;");
	if (!stmt)
		return -1;

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *row = (const char *)sqlite3_column_text(stmt, 0);
		if (row)
			string_list_append(out, row);
	}

	sqlite3_finalize(stmt);
	return out->nr;
}

#endif /* VIBES_ENABLED */
