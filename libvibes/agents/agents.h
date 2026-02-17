#ifndef LIBVIBES_AGENTS_AGENTS_H
#define LIBVIBES_AGENTS_AGENTS_H
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "string-list.h"
#include <stdint.h>
#include <sys/types.h>

struct vibes_db;
struct repository;

/*
 * Agent types.
 */
enum agent_type {
	AGENT_ORCHESTRATOR = 0,
	AGENT_WORKER,
	AGENT_GIT_STRATEGIST,
	AGENT_CONFLICT_RESOLVER,
};

/*
 * Agent status.
 */
enum agent_status {
	AGENT_IDLE = 0,
	AGENT_WORKING,
	AGENT_COMPLETED,
	AGENT_FAILED,
};

/*
 * Event types for the event bus.
 */
enum event_type {
	EVENT_TASK_CLAIMED = 0,
	EVENT_PROGRESS,
	EVENT_COMPLETED,
	EVENT_FAILED,
	EVENT_FILE_LOCKED,
	EVENT_FILE_UNLOCKED,
	EVENT_MERGE_REQUESTED,
	EVENT_HUMAN_REQUIRED,
	EVENT_HEARTBEAT,
};

/*
 * An agent process.
 */
struct vibes_agent {
	char id[27];
	enum agent_type type;
	enum agent_status status;
	pid_t pid;
	char *task_id;
	char *worktree_path;
	int64_t started_at;
	int64_t last_heartbeat;
	int socket_fd;
};

/*
 * An event on the event bus.
 */
struct agent_event {
	char id[27];
	enum event_type type;
	char *agent_id;
	char *intent_id;
	char *task_id;
	char *payload;
	int64_t created_at;
};

/* --- Event bus (event-bus.c) --- */

int vibes_event_publish(struct vibes_db *db, const struct agent_event *event);
int vibes_event_get_recent(struct vibes_db *db, const char *type_filter,
			   int limit, struct string_list *out);

/* --- File locks (file-locks.c) --- */

int vibes_lock_file(struct vibes_db *db, const char *path,
		    const char *agent_id, int ttl_seconds);
int vibes_unlock_file(struct vibes_db *db, const char *path,
		      const char *agent_id);
const char *vibes_check_lock(struct vibes_db *db, const char *path);
int vibes_expire_locks(struct vibes_db *db);

/* --- Task queue (task-queue.c) --- */

int vibes_queue_tasks(struct vibes_db *db, const char *intent_id, int wave);
char *vibes_queue_claim(struct vibes_db *db, const char *agent_id);
int vibes_queue_release(struct vibes_db *db, const char *task_id);
int vibes_queue_advance_wave(struct vibes_db *db, const char *intent_id);
int vibes_queue_get_current_wave(struct vibes_db *db, const char *intent_id);

/* --- Agent runner (agent-runner.c) --- */

int vibes_agent_spawn(struct vibes_db *db, struct repository *repo,
		      const char *task_id, struct vibes_agent *agent);
int vibes_agent_wait(pid_t pid);
int vibes_agent_kill(pid_t pid);
int vibes_agent_cleanup(struct vibes_db *db, const char *agent_id);
int vibes_agent_list(struct vibes_db *db, struct string_list *out);

/* --- Orchestrator (orchestrator.c) --- */

int vibes_orchestrator_run(struct vibes_db *db, struct repository *repo,
			   const char *intent_id);

/* --- IPC (ipc.c) --- */

int vibes_ipc_server_start(const char *socket_path);
int vibes_ipc_client_connect(const char *socket_path);
int vibes_ipc_send(int fd, const char *message);
int vibes_ipc_recv(int fd, struct strbuf *buffer);
void vibes_ipc_close(int fd);

/* Utility */
const char *agent_type_str(enum agent_type t);
const char *agent_status_str(enum agent_status s);
const char *event_type_str(enum event_type t);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_AGENTS_AGENTS_H */
