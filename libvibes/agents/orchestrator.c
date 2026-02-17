#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include <signal.h>
#include <sys/wait.h>
#include <poll.h>
#include "repository.h"
#include "strbuf.h"
#include "string-list.h"
#include "config.h"
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
 * In fork/exec mode, the orchestrator:
 *   1. For each wave, spawns N agent processes in parallel
 *   2. Uses poll() on IPC sockets for multiplexed progress monitoring
 *   3. Uses waitpid(WNOHANG) to detect finished agents
 *   4. Monitors heartbeats and detects dead agents
 *   5. Waits for all wave-N agents before starting wave-N+1
 *   6. Handles failures: logs error, cleans up, continues with remaining
 *
 * Falls back to sequential in-process execution if max-agents is 0
 * or if the --sequential flag is set.
 */

#define MAX_AGENTS 8
#define HEARTBEAT_TIMEOUT_MS 60000  /* 60 seconds */
#define POLL_INTERVAL_MS     1000   /* 1 second */

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

/*
 * Get task IDs for a given wave.
 */
static int get_wave_task_ids(struct vibes_db *db, const char *intent_id,
			     int wave, struct string_list *task_ids)
{
	sqlite3_stmt *stmt;

	stmt = vibes_db_prepare(db,
		"SELECT id FROM tasks "
		"WHERE intent_id = ? AND wave_number = ? "
		"AND status = 'pending' "
		"ORDER BY created_at;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, intent_id, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 2, wave);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *id = (const char *)sqlite3_column_text(stmt, 0);
		if (id)
			string_list_append(task_ids, id);
	}

	sqlite3_finalize(stmt);
	return task_ids->nr;
}

/*
 * Process IPC messages from an agent.
 */
static void process_ipc_message(struct vibes_agent *agent,
				const char *message)
{
	/* Simple JSON parsing for progress messages */
	if (strstr(message, "\"type\":\"heartbeat\"")) {
		agent->last_heartbeat = vibes_timestamp_ms();
	} else if (strstr(message, "\"type\":\"progress\"")) {
		const char *msg_start = strstr(message, "\"message\":\"");
		if (msg_start) {
			const char *msg_end;
			msg_start += 11;
			msg_end = strchr(msg_start, '"');
			if (msg_end) {
				struct strbuf status_msg = STRBUF_INIT;
				strbuf_add(&status_msg, msg_start,
					   msg_end - msg_start);
				printf("  Agent %.8s: %s\n",
				       agent->id, status_msg.buf);
				strbuf_release(&status_msg);
			}
		}
	} else if (strstr(message, "\"type\":\"done\"")) {
		if (strstr(message, "\"success\":true"))
			agent->status = AGENT_COMPLETED;
		else
			agent->status = AGENT_FAILED;
	}
}

/*
 * Monitor running agents: poll IPC sockets and check process status.
 * Returns number of agents still running.
 */
static int monitor_agents(struct vibes_agent *agents, int nr_agents,
			   struct vibes_db *db)
{
	struct pollfd *pfds;
	int running = 0;
	int i;
	int64_t now = vibes_timestamp_ms();

	pfds = xcalloc(nr_agents, sizeof(struct pollfd));

	/* Set up poll descriptors */
	for (i = 0; i < nr_agents; i++) {
		if (agents[i].status == AGENT_WORKING) {
			pfds[i].fd = agents[i].socket_fd;
			pfds[i].events = POLLIN;
		} else {
			pfds[i].fd = -1;
		}
	}

	/* Poll for IPC messages */
	if (poll(pfds, nr_agents, POLL_INTERVAL_MS) > 0) {
		for (i = 0; i < nr_agents; i++) {
			if (pfds[i].revents & POLLIN) {
				struct strbuf buf = STRBUF_INIT;
				if (vibes_ipc_recv(agents[i].socket_fd,
						   &buf) == 0) {
					process_ipc_message(&agents[i],
							    buf.buf);
				}
				strbuf_release(&buf);
			}
			if (pfds[i].revents & (POLLHUP | POLLERR)) {
				/* Socket closed — agent likely exited */
				if (agents[i].status == AGENT_WORKING)
					agents[i].status = AGENT_COMPLETED;
			}
		}
	}

	/* Check process status and heartbeats */
	for (i = 0; i < nr_agents; i++) {
		if (agents[i].status != AGENT_WORKING)
			continue;

		/* Check if process exited */
		{
			int exit_status = vibes_agent_wait(agents[i].pid);
			if (exit_status > 0) {
				/* Exited with error */
				agents[i].status = AGENT_FAILED;
				printf("  Agent %.8s failed "
				       "(exit=%d)\n",
				       agents[i].id, exit_status);
				continue;
			} else if (exit_status < 0) {
				/* Killed by signal or error */
				agents[i].status = AGENT_FAILED;
				printf("  Agent %.8s killed\n",
				       agents[i].id);
				continue;
			}
			/* exit_status == 0: still running */
		}

		/* Check heartbeat timeout */
		if (now - agents[i].last_heartbeat > HEARTBEAT_TIMEOUT_MS) {
			printf("  Agent %.8s timed out (no heartbeat)\n",
			       agents[i].id);
			vibes_agent_kill(agents[i].pid);
			agents[i].status = AGENT_FAILED;
			continue;
		}

		running++;
	}

	free(pfds);
	return running;
}

/*
 * Run a wave of agents in parallel using fork/exec.
 */
static int run_wave_parallel(struct vibes_db *db, struct repository *repo,
			      const char *intent_id, int wave,
			      struct string_list *task_ids)
{
	int nr_tasks = task_ids->nr;
	struct vibes_agent *agents;
	int i;
	int completed = 0;
	int failed = 0;

	agents = xcalloc(nr_tasks, sizeof(struct vibes_agent));

	/* Spawn all agents for this wave */
	for (i = 0; i < nr_tasks; i++) {
		const char *task_id = task_ids->items[i].string;

		if (vibes_agent_spawn(db, repo, task_id, &agents[i]) < 0) {
			printf("  Failed to spawn agent for task %.8s\n",
			       task_id);
			agents[i].status = AGENT_FAILED;
			failed++;
			continue;
		}
	}

	/* Monitor until all agents complete */
	while (monitor_agents(agents, nr_tasks, db) > 0) {
		/* Loop continues polling */
	}

	/* Tally results and clean up */
	for (i = 0; i < nr_tasks; i++) {
		if (agents[i].status == AGENT_COMPLETED)
			completed++;
		else if (agents[i].status == AGENT_FAILED)
			failed++;

		/* Clean up socket */
		vibes_ipc_close(agents[i].socket_fd);

		/* Clean up agent resources */
		vibes_agent_cleanup(db, agents[i].id);
		free(agents[i].task_id);
		free(agents[i].worktree_path);
	}

	free(agents);

	printf("  Wave %d: %d completed, %d failed\n",
	       wave, completed, failed);

	return failed > 0 ? -1 : 0;
}

/*
 * Run a wave sequentially in-process (fallback mode).
 */
static int run_wave_sequential(struct vibes_db *db, struct repository *repo,
				const char *intent_id, int wave,
				struct string_list *task_ids)
{
	int i;

	/* Queue tasks for this wave */
	vibes_queue_tasks(db, intent_id, wave);

	for (i = 0; i < task_ids->nr; i++) {
		struct vibes_agent agent;
		char *task_id;

		memset(&agent, 0, sizeof(agent));
		vibes_ulid_generate(agent.id);

		task_id = vibes_queue_claim(db, agent.id);
		if (!task_id)
			break;

		printf("  [%d/%d] Agent %.8s -> task %.8s\n",
		       i + 1, (int)task_ids->nr, agent.id, task_id);

		vibes_agent_worker_run(db, agent.id, NULL);

		free(task_id);
	}

	vibes_queue_advance_wave(db, intent_id);
	return 0;
}

int vibes_orchestrator_run(struct vibes_db *db, struct repository *repo,
			   const char *intent_id)
{
	int max_wave = get_max_wave(db, intent_id);
	int wave;
	int total_completed = 0;
	int total_failed = 0;
	int use_parallel = 1;
	struct agent_event event;
	const char *val = NULL;

	/* Check config for max-agents (0 = sequential mode) */
	if (!repo_config_get_string_tmp(repo, "vibes.max-agents", &val) && val) {
		int max_agents = atoi(val);
		if (max_agents == 0)
			use_parallel = 0;
	}

	printf("\nOrchestrator starting for intent %.8s\n", intent_id);
	printf("  Mode: %s\n", use_parallel ? "parallel (fork/exec)" :
					       "sequential (in-process)");
	printf("  Total waves: %d\n\n", max_wave + 1);

	/* Update intent status to in_progress */
	vibes_db_update_intent_status(db, intent_id, "in_progress");

	for (wave = 0; wave <= max_wave; wave++) {
		int wave_size = count_wave_tasks(db, intent_id, wave);
		struct string_list task_ids = STRING_LIST_INIT_DUP;
		int ret;

		if (wave_size == 0)
			continue;

		printf("Wave %d: %d task(s)\n", wave, wave_size);

		get_wave_task_ids(db, intent_id, wave, &task_ids);

		if (use_parallel && task_ids.nr > 0)
			ret = run_wave_parallel(db, repo, intent_id,
						wave, &task_ids);
		else
			ret = run_wave_sequential(db, repo, intent_id,
						   wave, &task_ids);

		if (ret < 0)
			total_failed += task_ids.nr;
		else
			total_completed += task_ids.nr;

		string_list_clear(&task_ids, 0);

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

		printf("Intent %.8s completed! %d task(s) executed",
		       intent_id, total_completed);
		if (total_failed > 0)
			printf(", %d failed", total_failed);
		printf(".\n");
	} else {
		printf("Intent %.8s: %d tasks remain.\n",
		       intent_id, count_active_tasks(db, intent_id));
	}

	return total_failed > 0 ? -1 : 0;
}

#endif /* VIBES_ENABLED */
