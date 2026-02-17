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
 *
 * The runner creates a worktree, sets up a Unix domain socket pair,
 * then fork/exec's a child process that runs the agent worker.
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

/*
 * Spawn an agent process in its own worktree.
 *
 * The child process is a `git vibes-decompose --worker` invocation
 * that reads task details from the database and uses AI to generate code.
 * Communication happens via Unix domain sockets for progress/heartbeat.
 */
int vibes_agent_spawn(struct vibes_db *db, struct repository *repo,
		      const char *task_id, struct vibes_agent *agent)
{
	struct strbuf worktree_path = STRBUF_INIT;
	struct strbuf branch_name = STRBUF_INIT;
	struct strbuf db_path = STRBUF_INIT;
	struct agent_event event;
	int sv[2]; /* socket pair */
	pid_t pid;

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
			    branch_name.buf)) {
		strbuf_release(&branch_name);
		return error("gitvibes: failed to create worktree for agent");
	}
	strbuf_release(&branch_name);

	/* Set up Unix domain socket pair for IPC */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
		remove_worktree(agent->worktree_path);
		return error_errno("gitvibes: failed to create socket pair");
	}

	/* Get database path for child process */
	strbuf_addf(&db_path, "%s/vibes.db", repo->gitdir);

	/* Fork the worker process */
	pid = fork();
	if (pid < 0) {
		close(sv[0]);
		close(sv[1]);
		remove_worktree(agent->worktree_path);
		strbuf_release(&db_path);
		return error_errno("gitvibes: fork failed");
	}

	if (pid == 0) {
		/* --- Child process --- */
		close(sv[0]); /* close parent's end */

		/*
		 * Exec the worker. We use the current git binary
		 * with a special internal command that runs the worker loop.
		 *
		 * The worker receives:
		 *   - task_id via argv
		 *   - db_path via argv
		 *   - worktree_path via argv
		 *   - agent_id via argv
		 *   - socket fd via argv (for IPC)
		 */
		{
			struct strbuf fd_str = STRBUF_INIT;
			const char *git_exec = getenv("GIT_EXEC_PATH");
			struct strbuf exec_path = STRBUF_INIT;

			strbuf_addf(&fd_str, "%d", sv[1]);

			if (git_exec)
				strbuf_addstr(&exec_path, git_exec);
			else
				strbuf_addstr(&exec_path, "git");

			/* Change to the worktree directory */
			if (chdir(agent->worktree_path) < 0) {
				fprintf(stderr, "gitvibes: worker chdir failed\n");
				_exit(1);
			}

			execlp("git", "git", "vibes-decompose",
			       "--worker",
			       "--task-id", task_id,
			       "--agent-id", agent->id,
			       "--db-path", db_path.buf,
			       "--ipc-fd", fd_str.buf,
			       NULL);

			/* If exec fails, exit */
			fprintf(stderr, "gitvibes: exec failed\n");
			_exit(127);
		}
	}

	/* --- Parent process --- */
	close(sv[1]); /* close child's end */

	agent->pid = pid;
	agent->socket_fd = sv[0];
	agent->last_heartbeat = vibes_timestamp_ms();

	strbuf_release(&db_path);

	/* Publish spawn event */
	memset(&event, 0, sizeof(event));
	event.type = EVENT_TASK_CLAIMED;
	event.agent_id = agent->id;
	event.task_id = agent->task_id;
	vibes_event_publish(db, &event);

	printf("  Agent %.8s spawned (pid=%d) in %s\n",
	       agent->id, pid, agent->worktree_path);

	return 0;
}

/*
 * Non-blocking check if agent process has exited.
 * Returns: 0 if still running, exit status if exited, -1 on error.
 */
int vibes_agent_wait(pid_t pid)
{
	int status;
	pid_t ret;

	ret = waitpid(pid, &status, WNOHANG);
	if (ret == 0)
		return 0; /* still running */
	if (ret < 0)
		return -1; /* error */

	if (WIFEXITED(status))
		return WEXITSTATUS(status);

	/* killed by signal */
	return -1;
}

/*
 * Kill an agent process with graceful shutdown.
 */
int vibes_agent_kill(pid_t pid)
{
	if (pid > 0 && pid != getpid()) {
		kill(pid, SIGTERM);
		usleep(500000); /* 500ms grace period */

		/* Check if it actually died */
		if (waitpid(pid, NULL, WNOHANG) == 0) {
			kill(pid, SIGKILL);
			waitpid(pid, NULL, 0);
		}
	}
	return 0;
}

/*
 * Clean up after an agent: remove worktree, release locks, release tasks.
 */
int vibes_agent_cleanup(struct vibes_db *db, const char *agent_id)
{
	char worktree[256];

	/* Remove worktree */
	snprintf(worktree, sizeof(worktree), "/tmp/gitvibes-%.8s", agent_id);
	remove_worktree(worktree);

	/* Release any file locks held by this agent */
	{
		sqlite3_stmt *stmt = vibes_db_prepare(db,
			"DELETE FROM file_locks WHERE agent_id = ?;");
		if (stmt) {
			sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_STATIC);
			sqlite3_step(stmt);
			sqlite3_finalize(stmt);
		}
	}

	/* Release any claimed tasks back to pending */
	{
		sqlite3_stmt *stmt = vibes_db_prepare(db,
			"UPDATE tasks SET status = 'pending', "
			"assigned_agent = NULL "
			"WHERE assigned_agent = ? "
			"AND status IN ('claimed', 'in_progress');");
		if (stmt) {
			sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_STATIC);
			sqlite3_step(stmt);
			sqlite3_finalize(stmt);
		}
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
