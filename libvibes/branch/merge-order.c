#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "strbuf.h"
#include "string-list.h"
#include "libvibes/branch/branch-strategy.h"
#include "libvibes/storage/db.h"

/*
 * Merge Order: determine optimal merge sequence for task branches.
 * Order: lowest wave first, then by dependency depth.
 */

int vibes_merge_order(struct vibes_db *db, const char *intent_id,
		      struct string_list *order)
{
	sqlite3_stmt *stmt;

	stmt = vibes_db_prepare(db,
		"SELECT 'vibes/task-' || SUBSTR(id, 1, 8) "
		"FROM tasks "
		"WHERE intent_id = ? AND status = 'completed' "
		"ORDER BY wave_number, id;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, intent_id, -1, SQLITE_STATIC);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *branch = (const char *)sqlite3_column_text(stmt, 0);
		if (branch)
			string_list_append(order, branch);
	}

	sqlite3_finalize(stmt);
	return order->nr;
}

#endif /* VIBES_ENABLED */
