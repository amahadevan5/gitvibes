#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "strbuf.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/*
 * Common query helpers for gitvibes storage operations.
 */

int vibes_db_insert_intent(struct vibes_db *db,
			   const char *id,
			   const char *type,
			   const char *raw_input,
			   const char *session_id)
{
	sqlite3_stmt *stmt;
	int rc;

	stmt = vibes_db_prepare(db,
		"INSERT INTO intents (id, type, status, raw_input, session_id, created_at)"
		" VALUES (?, ?, 'captured', ?, ?, ?);");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, type, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, raw_input, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4, session_id, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 5, vibes_timestamp_ms());

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE)
		return error("gitvibes: failed to insert intent");
	return 0;
}

int vibes_db_update_intent_status(struct vibes_db *db,
				  const char *id,
				  const char *status)
{
	sqlite3_stmt *stmt;
	int rc;

	stmt = vibes_db_prepare(db,
		"UPDATE intents SET status = ? WHERE id = ?;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, status, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, id, -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE)
		return error("gitvibes: failed to update intent status");
	return 0;
}

int vibes_db_insert_task(struct vibes_db *db,
			 const char *id,
			 const char *intent_id,
			 const char *title,
			 const char *description,
			 int wave_number)
{
	sqlite3_stmt *stmt;
	int rc;

	stmt = vibes_db_prepare(db,
		"INSERT INTO tasks (id, intent_id, title, description, status, wave_number, created_at)"
		" VALUES (?, ?, ?, ?, 'pending', ?, ?);");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, intent_id, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, title, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4, description, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 5, wave_number);
	sqlite3_bind_int64(stmt, 6, vibes_timestamp_ms());

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE)
		return error("gitvibes: failed to insert task");
	return 0;
}

int vibes_db_insert_commit_intent(struct vibes_db *db,
				  const char *commit_oid,
				  const char *intent_id,
				  const char *task_id,
				  const char *message)
{
	sqlite3_stmt *stmt;
	int rc;

	stmt = vibes_db_prepare(db,
		"INSERT INTO commit_intents (commit_oid, intent_id, task_id, message, created_at)"
		" VALUES (?, ?, ?, ?, ?);");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, commit_oid, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, intent_id, -1, SQLITE_STATIC);
	if (task_id)
		sqlite3_bind_text(stmt, 3, task_id, -1, SQLITE_STATIC);
	else
		sqlite3_bind_null(stmt, 3);
	sqlite3_bind_text(stmt, 4, message, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 5, vibes_timestamp_ms());

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE)
		return error("gitvibes: failed to link commit to intent");
	return 0;
}

int vibes_db_insert_event(struct vibes_db *db,
			  const char *id,
			  const char *type,
			  const char *agent_id,
			  const char *intent_id,
			  const char *task_id,
			  const char *payload)
{
	sqlite3_stmt *stmt;
	int rc;

	stmt = vibes_db_prepare(db,
		"INSERT INTO events (id, type, agent_id, intent_id, task_id, payload, created_at)"
		" VALUES (?, ?, ?, ?, ?, ?, ?);");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, type, -1, SQLITE_STATIC);
	if (agent_id)
		sqlite3_bind_text(stmt, 3, agent_id, -1, SQLITE_STATIC);
	else
		sqlite3_bind_null(stmt, 3);
	if (intent_id)
		sqlite3_bind_text(stmt, 4, intent_id, -1, SQLITE_STATIC);
	else
		sqlite3_bind_null(stmt, 4);
	if (task_id)
		sqlite3_bind_text(stmt, 5, task_id, -1, SQLITE_STATIC);
	else
		sqlite3_bind_null(stmt, 5);
	if (payload)
		sqlite3_bind_text(stmt, 6, payload, -1, SQLITE_STATIC);
	else
		sqlite3_bind_null(stmt, 6);
	sqlite3_bind_int64(stmt, 7, vibes_timestamp_ms());

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE)
		return error("gitvibes: failed to insert event");
	return 0;
}

int vibes_db_count_table(struct vibes_db *db, const char *table)
{
	struct strbuf sql = STRBUF_INIT;
	sqlite3_stmt *stmt;
	int count = 0;

	strbuf_addf(&sql, "SELECT COUNT(*) FROM %s;", table);
	stmt = vibes_db_prepare(db, sql.buf);
	strbuf_release(&sql);

	if (!stmt)
		return -1;

	if (sqlite3_step(stmt) == SQLITE_ROW)
		count = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	return count;
}

#endif /* VIBES_ENABLED */
