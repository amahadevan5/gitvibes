#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "strbuf.h"
#include "libvibes/session/session.h"
#include "libvibes/storage/db.h"

/*
 * Context Restore: reload session context and display a summary
 * of what was in progress when the session was last active.
 */

int vibes_snapshot_restore(struct vibes_db *db, const char *session_id)
{
	sqlite3_stmt *stmt;
	const char *context_json = NULL;
	const char *name = NULL;

	stmt = vibes_db_prepare(db,
		"SELECT name, context_json FROM sessions WHERE id = ?;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return error("gitvibes: session '%s' not found", session_id);
	}

	name = (const char *)sqlite3_column_text(stmt, 0);
	context_json = (const char *)sqlite3_column_text(stmt, 1);

	printf("Restoring session: %s\n", name ? name : "(unnamed)");

	if (context_json && *context_json) {
		printf("  Context: %s\n", context_json);
	}

	sqlite3_finalize(stmt);

	/* Show active intents from this session's time */
	{
		sqlite3_stmt *q = vibes_db_prepare(db,
			"SELECT id, type, status, raw_input FROM intents "
			"WHERE status NOT IN ('completed', 'abandoned') "
			"ORDER BY created_at DESC LIMIT 5;");
		if (q) {
			int count = 0;
			while (sqlite3_step(q) == SQLITE_ROW) {
				const char *id = (const char *)
					sqlite3_column_text(q, 0);
				const char *type = (const char *)
					sqlite3_column_text(q, 1);
				const char *status = (const char *)
					sqlite3_column_text(q, 2);
				const char *input = (const char *)
					sqlite3_column_text(q, 3);
				if (!count)
					printf("\n  Active intents:\n");
				printf("    %.8s [%s] %s - %s\n",
				       id ? id : "?",
				       type ? type : "?",
				       status ? status : "?",
				       input ? input : "");
				count++;
			}
			sqlite3_finalize(q);

			if (!count)
				printf("\n  No active intents.\n");
		}
	}

	/* Show pending tasks */
	{
		sqlite3_stmt *q = vibes_db_prepare(db,
			"SELECT id, title, status FROM tasks "
			"WHERE status IN ('pending', 'claimed', 'in_progress') "
			"ORDER BY wave_number LIMIT 10;");
		if (q) {
			int count = 0;
			while (sqlite3_step(q) == SQLITE_ROW) {
				const char *id = (const char *)
					sqlite3_column_text(q, 0);
				const char *title = (const char *)
					sqlite3_column_text(q, 1);
				const char *status = (const char *)
					sqlite3_column_text(q, 2);
				if (!count)
					printf("\n  Pending tasks:\n");
				printf("    %.8s [%s] %s\n",
				       id ? id : "?",
				       status ? status : "?",
				       title ? title : "(untitled)");
				count++;
			}
			sqlite3_finalize(q);
		}
	}

	return 0;
}

#endif /* VIBES_ENABLED */
