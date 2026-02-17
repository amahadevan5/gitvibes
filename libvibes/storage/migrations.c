#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "libvibes/storage/db.h"

/*
 * Migration framework for gitvibes schema evolution.
 *
 * Each migration function takes the database from version N-1 to N.
 * Migrations run inside a transaction so they are atomic.
 */

/*
 * Example migration template for future use:
 *
 * static int migrate_v1_to_v2(struct vibes_db *db)
 * {
 *     return vibes_db_exec(db,
 *         "ALTER TABLE intents ADD COLUMN priority TEXT DEFAULT 'normal';");
 * }
 */

int vibes_db_run_migrations(struct vibes_db *db)
{
	int current = vibes_db_get_version(db);
	int target = VIBES_SCHEMA_VERSION;

	if (current >= target)
		return 0;

	if (vibes_db_begin(db) < 0)
		return -1;

	/*
	 * Migration dispatch:
	 * if (current < 2 && migrate_v1_to_v2(db) < 0) goto fail;
	 * if (current < 3 && migrate_v2_to_v3(db) < 0) goto fail;
	 */

	/* Update stored version */
	{
		sqlite3_stmt *stmt = vibes_db_prepare(db,
			"UPDATE schema_version SET version = ?;");
		if (!stmt)
			goto fail;
		sqlite3_bind_int(stmt, 1, target);
		if (sqlite3_step(stmt) != SQLITE_DONE) {
			sqlite3_finalize(stmt);
			goto fail;
		}
		sqlite3_finalize(stmt);
	}

	if (vibes_db_commit(db) < 0)
		goto fail;

	db->schema_version = target;
	return 0;

fail:
	vibes_db_rollback(db);
	return error("gitvibes: migration from v%d to v%d failed", current, target);
}

#endif /* VIBES_ENABLED */
