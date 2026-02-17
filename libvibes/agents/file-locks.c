#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "strbuf.h"
#include "libvibes/agents/agents.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/*
 * File Locks: prevent concurrent agents from modifying the same file.
 * Locks are stored in SQLite with TTL for auto-expiration.
 */

int vibes_lock_file(struct vibes_db *db, const char *path,
		    const char *agent_id, int ttl_seconds)
{
	sqlite3_stmt *stmt;
	int ret;
	int64_t now = vibes_timestamp_ms() / 1000;

	/* First expire stale locks */
	vibes_expire_locks(db);

	/* Check if already locked by someone else */
	stmt = vibes_db_prepare(db,
		"SELECT agent_id FROM file_locks "
		"WHERE file_path = ? AND agent_id != ?;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, agent_id, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return error("gitvibes: file '%s' is locked by another agent",
			     path);
	}
	sqlite3_finalize(stmt);

	/* Insert or update lock */
	stmt = vibes_db_prepare(db,
		"INSERT OR REPLACE INTO file_locks "
		"(file_path, agent_id, locked_at, expires_at) "
		"VALUES (?, ?, ?, ?);");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, agent_id, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 3, now);
	sqlite3_bind_int64(stmt, 4, now + ttl_seconds);

	ret = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	return (ret == SQLITE_DONE) ? 0 : -1;
}

int vibes_unlock_file(struct vibes_db *db, const char *path,
		      const char *agent_id)
{
	sqlite3_stmt *stmt;
	int ret;

	stmt = vibes_db_prepare(db,
		"DELETE FROM file_locks "
		"WHERE file_path = ? AND agent_id = ?;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, agent_id, -1, SQLITE_STATIC);

	ret = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	return (ret == SQLITE_DONE) ? 0 : -1;
}

const char *vibes_check_lock(struct vibes_db *db, const char *path)
{
	sqlite3_stmt *stmt;
	static char holder[27];

	vibes_expire_locks(db);

	stmt = vibes_db_prepare(db,
		"SELECT agent_id FROM file_locks WHERE file_path = ?;");
	if (!stmt)
		return NULL;

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *id = (const char *)sqlite3_column_text(stmt, 0);
		if (id) {
			snprintf(holder, sizeof(holder), "%s", id);
			sqlite3_finalize(stmt);
			return holder;
		}
	}

	sqlite3_finalize(stmt);
	return NULL;
}

int vibes_expire_locks(struct vibes_db *db)
{
	struct strbuf sql = STRBUF_INIT;
	int64_t now = vibes_timestamp_ms() / 1000;

	strbuf_addf(&sql,
		"DELETE FROM file_locks WHERE expires_at < %lld;",
		(long long)now);

	vibes_db_exec(db, sql.buf);
	strbuf_release(&sql);
	return 0;
}

#endif /* VIBES_ENABLED */
