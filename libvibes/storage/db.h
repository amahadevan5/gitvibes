#ifndef LIBVIBES_STORAGE_DB_H
#define LIBVIBES_STORAGE_DB_H

#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include <stdint.h>

/* Database handle */
struct vibes_db {
	sqlite3 *sqlite;
	char *db_path;
	int schema_version;
};

/* Current schema version */
#define VIBES_SCHEMA_VERSION 1

/*
 * Open the vibes database at the given path.
 * Creates the database and schema if it doesn't exist.
 * Sets WAL mode and busy timeout for concurrent access.
 * Returns 0 on success, -1 on error.
 */
int vibes_db_open(struct vibes_db *db, const char *path);

/*
 * Close the vibes database and free resources.
 */
void vibes_db_close(struct vibes_db *db);

/*
 * Initialize the database schema (create tables if not exist).
 * Called automatically by vibes_db_open().
 * Returns 0 on success, -1 on error.
 */
int vibes_db_init_schema(struct vibes_db *db);

/*
 * Execute a simple SQL statement (no results).
 * Returns 0 on success, -1 on error.
 */
int vibes_db_exec(struct vibes_db *db, const char *sql);

/*
 * Prepare a SQL statement.
 * Caller must finalize with sqlite3_finalize().
 * Returns NULL on error.
 */
sqlite3_stmt *vibes_db_prepare(struct vibes_db *db, const char *sql);

/*
 * Begin/commit/rollback transactions.
 */
int vibes_db_begin(struct vibes_db *db);
int vibes_db_commit(struct vibes_db *db);
int vibes_db_rollback(struct vibes_db *db);

/*
 * Get the database path for a repository.
 * Returns allocated string: "<git_dir>/vibes.db"
 * Caller must free().
 */
char *vibes_db_repo_path(const char *git_dir);

/*
 * Run schema migrations if needed.
 * Returns 0 on success, -1 on error.
 */
int vibes_db_migrate(struct vibes_db *db);

/*
 * Get current schema version from database.
 */
int vibes_db_get_version(struct vibes_db *db);

/*
 * Run all pending schema migrations.
 * Called from migrations.c
 */
int vibes_db_run_migrations(struct vibes_db *db);

/* --- Query helpers (queries.c) --- */

int vibes_db_insert_intent(struct vibes_db *db,
			   const char *id,
			   const char *type,
			   const char *raw_input,
			   const char *session_id);

int vibes_db_update_intent_status(struct vibes_db *db,
				  const char *id,
				  const char *status);

int vibes_db_insert_task(struct vibes_db *db,
			 const char *id,
			 const char *intent_id,
			 const char *title,
			 const char *description,
			 int wave_number);

int vibes_db_insert_commit_intent(struct vibes_db *db,
				  const char *commit_oid,
				  const char *intent_id,
				  const char *task_id,
				  const char *message);

int vibes_db_insert_event(struct vibes_db *db,
			  const char *id,
			  const char *type,
			  const char *agent_id,
			  const char *intent_id,
			  const char *task_id,
			  const char *payload);

int vibes_db_count_table(struct vibes_db *db, const char *table);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_STORAGE_DB_H */
