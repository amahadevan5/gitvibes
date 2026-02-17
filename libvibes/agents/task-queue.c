#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "strbuf.h"
#include "string-list.h"
#include "libvibes/agents/agents.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/*
 * Task Queue: claim-based task distribution for agents.
 * Tasks are claimed atomically via SQLite transactions.
 */

int vibes_queue_get_current_wave(struct vibes_db *db, const char *intent_id)
{
	sqlite3_stmt *stmt;
	int wave = -1;

	stmt = vibes_db_prepare(db,
		"SELECT MIN(wave_number) FROM tasks "
		"WHERE intent_id = ? AND status NOT IN ('completed', 'failed');");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, intent_id, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_ROW && !sqlite3_column_type(stmt, 0))
		wave = sqlite3_column_int(stmt, 0);
	else if (sqlite3_step(stmt) == SQLITE_ROW)
		wave = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	return wave;
}

int vibes_queue_tasks(struct vibes_db *db, const char *intent_id, int wave)
{
	sqlite3_stmt *stmt;
	int ret;

	stmt = vibes_db_prepare(db,
		"UPDATE tasks SET status = 'pending' "
		"WHERE intent_id = ? AND wave_number = ? "
		"AND status NOT IN ('completed', 'failed', 'claimed', 'in_progress');");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, intent_id, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 2, wave);

	ret = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	return (ret == SQLITE_DONE) ? 0 : -1;
}

char *vibes_queue_claim(struct vibes_db *db, const char *agent_id)
{
	sqlite3_stmt *stmt;
	char *task_id = NULL;

	vibes_db_begin(db);

	stmt = vibes_db_prepare(db,
		"SELECT id FROM tasks "
		"WHERE status = 'pending' AND assigned_agent IS NULL "
		"ORDER BY wave_number, id LIMIT 1;");
	if (!stmt) {
		vibes_db_rollback(db);
		return NULL;
	}

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *id = (const char *)sqlite3_column_text(stmt, 0);
		if (id)
			task_id = xstrdup(id);
	}
	sqlite3_finalize(stmt);

	if (!task_id) {
		vibes_db_rollback(db);
		return NULL;
	}

	stmt = vibes_db_prepare(db,
		"UPDATE tasks SET status = 'claimed', assigned_agent = ? "
		"WHERE id = ? AND status = 'pending';");
	if (!stmt) {
		free(task_id);
		vibes_db_rollback(db);
		return NULL;
	}

	sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, task_id, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		free(task_id);
		vibes_db_rollback(db);
		return NULL;
	}
	sqlite3_finalize(stmt);

	vibes_db_commit(db);
	return task_id;
}

int vibes_queue_release(struct vibes_db *db, const char *task_id)
{
	sqlite3_stmt *stmt;
	int ret;

	stmt = vibes_db_prepare(db,
		"UPDATE tasks SET status = 'pending', assigned_agent = NULL "
		"WHERE id = ?;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);

	ret = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	return (ret == SQLITE_DONE) ? 0 : -1;
}

int vibes_queue_advance_wave(struct vibes_db *db, const char *intent_id)
{
	sqlite3_stmt *stmt;
	int current_wave, pending_count = 0;

	current_wave = vibes_queue_get_current_wave(db, intent_id);
	if (current_wave < 0)
		return 0; /* All done */

	stmt = vibes_db_prepare(db,
		"SELECT COUNT(*) FROM tasks "
		"WHERE intent_id = ? AND wave_number = ? "
		"AND status NOT IN ('completed');");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, intent_id, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 2, current_wave);

	if (sqlite3_step(stmt) == SQLITE_ROW)
		pending_count = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);

	if (pending_count > 0)
		return 0;

	return vibes_queue_tasks(db, intent_id, current_wave + 1);
}

#endif /* VIBES_ENABLED */
