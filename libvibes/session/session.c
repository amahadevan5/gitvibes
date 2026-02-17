#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "strbuf.h"
#include "string-list.h"
#include "libvibes/session/session.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/*
 * Session Management: track coding sessions with context.
 */

int vibes_session_start(struct vibes_db *db, const char *name,
			struct vibes_session *session)
{
	sqlite3_stmt *stmt;
	int ret;

	vibes_ulid_generate(session->id);
	session->name = name ? xstrdup(name) : NULL;
	session->description = NULL;
	session->created_at = vibes_timestamp_ms() / 1000;
	session->ended_at = 0;

	stmt = vibes_db_prepare(db,
		"INSERT INTO sessions (id, name, description, created_at) "
		"VALUES (?, ?, NULL, ?);");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, session->id, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 3, session->created_at);

	ret = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	return (ret == SQLITE_DONE) ? 0 : -1;
}

int vibes_session_end(struct vibes_db *db, const char *session_id)
{
	sqlite3_stmt *stmt;
	int ret;
	int64_t now = vibes_timestamp_ms() / 1000;

	stmt = vibes_db_prepare(db,
		"UPDATE sessions SET ended_at = ? WHERE id = ?;");
	if (!stmt)
		return -1;

	sqlite3_bind_int64(stmt, 1, now);
	sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);

	ret = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	return (ret == SQLITE_DONE) ? 0 : -1;
}

int vibes_session_list(struct vibes_db *db, struct string_list *out)
{
	sqlite3_stmt *stmt;

	stmt = vibes_db_prepare(db,
		"SELECT id || ' ' || COALESCE(name, '(unnamed)') || ' ' || "
		"       CASE WHEN ended_at IS NULL THEN '(active)' "
		"            ELSE '(ended)' END "
		"FROM sessions ORDER BY created_at DESC LIMIT 20;");
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

int vibes_session_get(struct vibes_db *db, const char *id,
		      struct vibes_session *session)
{
	sqlite3_stmt *stmt;

	stmt = vibes_db_prepare(db,
		"SELECT id, name, description, created_at, ended_at "
		"FROM sessions WHERE id = ?;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return error("gitvibes: session '%s' not found", id);
	}

	{
		const char *sid = (const char *)sqlite3_column_text(stmt, 0);
		const char *name = (const char *)sqlite3_column_text(stmt, 1);
		const char *desc = (const char *)sqlite3_column_text(stmt, 2);

		memset(session, 0, sizeof(*session));
		if (sid)
			snprintf(session->id, sizeof(session->id), "%s", sid);
		session->name = name ? xstrdup(name) : NULL;
		session->description = desc ? xstrdup(desc) : NULL;
		session->created_at = sqlite3_column_int64(stmt, 3);
		session->ended_at = sqlite3_column_int64(stmt, 4);
	}

	sqlite3_finalize(stmt);
	return 0;
}

void vibes_session_free(struct vibes_session *session)
{
	free(session->name);
	free(session->description);
	session->name = NULL;
	session->description = NULL;
}

#endif /* VIBES_ENABLED */
