#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "strbuf.h"
#include "string-list.h"
#include "libvibes/agents/agents.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/*
 * Event Bus: SQLite-backed event bus for agent coordination.
 * Uses WAL mode for concurrent write access from multiple agents.
 */

const char *event_type_str(enum event_type t)
{
	switch (t) {
	case EVENT_TASK_CLAIMED: return "TASK_CLAIMED";
	case EVENT_PROGRESS: return "PROGRESS";
	case EVENT_COMPLETED: return "COMPLETED";
	case EVENT_FAILED: return "FAILED";
	case EVENT_FILE_LOCKED: return "FILE_LOCKED";
	case EVENT_FILE_UNLOCKED: return "FILE_UNLOCKED";
	case EVENT_MERGE_REQUESTED: return "MERGE_REQUESTED";
	case EVENT_HUMAN_REQUIRED: return "HUMAN_REQUIRED";
	case EVENT_HEARTBEAT: return "HEARTBEAT";
	}
	return "UNKNOWN";
}

const char *agent_type_str(enum agent_type t)
{
	switch (t) {
	case AGENT_ORCHESTRATOR: return "orchestrator";
	case AGENT_WORKER: return "worker";
	case AGENT_GIT_STRATEGIST: return "git_strategist";
	case AGENT_CONFLICT_RESOLVER: return "conflict_resolver";
	}
	return "unknown";
}

const char *agent_status_str(enum agent_status s)
{
	switch (s) {
	case AGENT_IDLE: return "idle";
	case AGENT_WORKING: return "working";
	case AGENT_COMPLETED: return "completed";
	case AGENT_FAILED: return "failed";
	}
	return "unknown";
}

int vibes_event_publish(struct vibes_db *db, const struct agent_event *event)
{
	char ulid[VIBES_ULID_LEN];

	vibes_ulid_generate(ulid);

	return vibes_db_insert_event(db, ulid,
				     event_type_str(event->type),
				     event->agent_id,
				     event->intent_id,
				     event->task_id,
				     event->payload);
}

int vibes_event_get_recent(struct vibes_db *db, const char *type_filter,
			   int limit, struct string_list *out)
{
	sqlite3_stmt *stmt;
	struct strbuf sql = STRBUF_INIT;

	strbuf_addstr(&sql,
		"SELECT type || ' ' || COALESCE(agent_id, '?') || ' ' || "
		"       COALESCE(task_id, '') || ' ' || COALESCE(payload, '') "
		"FROM events ");

	if (type_filter)
		strbuf_addf(&sql, "WHERE type = '%s' ", type_filter);

	strbuf_addf(&sql, "ORDER BY created_at DESC LIMIT %d;",
		    limit > 0 ? limit : 20);

	stmt = vibes_db_prepare(db, sql.buf);
	strbuf_release(&sql);
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
