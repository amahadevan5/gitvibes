#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "strbuf.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/*
 * Full database schema for gitvibes.
 * Embedded as a C string constant - no external file dependency.
 */
static const char *VIBES_SCHEMA_SQL =
	/* Schema version tracking */
	"CREATE TABLE IF NOT EXISTS schema_version ("
	"    version INTEGER NOT NULL"
	");"

	/* Intents */
	"CREATE TABLE IF NOT EXISTS intents ("
	"    id TEXT PRIMARY KEY,"
	"    parent_id TEXT REFERENCES intents(id),"
	"    type TEXT NOT NULL CHECK(type IN ('feature','bugfix','refactor','docs','chore')),"
	"    status TEXT NOT NULL CHECK(status IN ('captured','decomposed','in_progress','completed','abandoned')),"
	"    raw_input TEXT NOT NULL,"
	"    parsed_goal TEXT,"
	"    criteria TEXT,"
	"    constraints TEXT,"
	"    session_id TEXT,"
	"    priority TEXT NOT NULL DEFAULT 'normal',"
	"    created_at INTEGER NOT NULL,"
	"    started_at INTEGER,"
	"    completed_at INTEGER"
	");"

	/* Tasks (decomposed from intents) */
	"CREATE TABLE IF NOT EXISTS tasks ("
	"    id TEXT PRIMARY KEY,"
	"    intent_id TEXT NOT NULL REFERENCES intents(id),"
	"    title TEXT NOT NULL,"
	"    description TEXT,"
	"    dependencies TEXT,"
	"    estimated_files TEXT,"
	"    assigned_agent TEXT,"
	"    worktree_path TEXT,"
	"    branch TEXT,"
	"    status TEXT NOT NULL CHECK(status IN ('pending','claimed','in_progress','completed','failed')),"
	"    wave_number INTEGER NOT NULL DEFAULT 0,"
	"    retry_count INTEGER NOT NULL DEFAULT 0,"
	"    created_at INTEGER NOT NULL"
	");"

	/* Knowledge graph nodes */
	"CREATE TABLE IF NOT EXISTS kg_nodes ("
	"    id INTEGER PRIMARY KEY AUTOINCREMENT,"
	"    type TEXT NOT NULL,"
	"    name TEXT NOT NULL,"
	"    file_path TEXT NOT NULL,"
	"    start_line INTEGER,"
	"    end_line INTEGER,"
	"    language TEXT,"
	"    last_indexed INTEGER NOT NULL"
	");"
	"CREATE INDEX IF NOT EXISTS idx_kg_nodes_file ON kg_nodes(file_path);"
	"CREATE INDEX IF NOT EXISTS idx_kg_nodes_name ON kg_nodes(name);"

	/* Knowledge graph edges */
	"CREATE TABLE IF NOT EXISTS kg_edges ("
	"    source_id INTEGER REFERENCES kg_nodes(id) ON DELETE CASCADE,"
	"    target_id INTEGER REFERENCES kg_nodes(id) ON DELETE CASCADE,"
	"    type TEXT NOT NULL,"
	"    PRIMARY KEY (source_id, target_id, type)"
	");"

	/* Agent events (event bus) */
	"CREATE TABLE IF NOT EXISTS events ("
	"    id TEXT PRIMARY KEY,"
	"    type TEXT NOT NULL,"
	"    agent_id TEXT,"
	"    intent_id TEXT,"
	"    task_id TEXT,"
	"    payload TEXT,"
	"    created_at INTEGER NOT NULL"
	");"
	"CREATE INDEX IF NOT EXISTS idx_events_type ON events(type);"
	"CREATE INDEX IF NOT EXISTS idx_events_agent ON events(agent_id);"
	"CREATE INDEX IF NOT EXISTS idx_events_created ON events(created_at);"

	/* File locks */
	"CREATE TABLE IF NOT EXISTS file_locks ("
	"    file_path TEXT PRIMARY KEY,"
	"    agent_id TEXT NOT NULL,"
	"    locked_at INTEGER NOT NULL,"
	"    expires_at INTEGER NOT NULL"
	");"

	/* Sessions */
	"CREATE TABLE IF NOT EXISTS sessions ("
	"    id TEXT PRIMARY KEY,"
	"    name TEXT,"
	"    description TEXT,"
	"    context_json TEXT,"
	"    created_at INTEGER NOT NULL,"
	"    ended_at INTEGER"
	");"

	/* Commit-intent linkage */
	"CREATE TABLE IF NOT EXISTS commit_intents ("
	"    commit_oid TEXT NOT NULL,"
	"    intent_id TEXT NOT NULL REFERENCES intents(id),"
	"    task_id TEXT REFERENCES tasks(id),"
	"    message TEXT,"
	"    created_at INTEGER NOT NULL,"
	"    PRIMARY KEY (commit_oid, intent_id)"
	");"
;

int vibes_db_open(struct vibes_db *db, const char *path)
{
	int rc;

	memset(db, 0, sizeof(*db));
	db->db_path = xstrdup(path);

	rc = sqlite3_open(path, &db->sqlite);
	if (rc != SQLITE_OK) {
		error("gitvibes: cannot open database '%s': %s",
		      path, sqlite3_errmsg(db->sqlite));
		sqlite3_close(db->sqlite);
		db->sqlite = NULL;
		free(db->db_path);
		db->db_path = NULL;
		return -1;
	}

	/* WAL mode for concurrent agent access */
	sqlite3_exec(db->sqlite, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);

	/* 5 second busy timeout for concurrent access */
	sqlite3_busy_timeout(db->sqlite, 5000);

	/* Enable foreign keys */
	sqlite3_exec(db->sqlite, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);

	/* Initialize schema */
	if (vibes_db_init_schema(db) < 0) {
		vibes_db_close(db);
		return -1;
	}

	/* Run any pending migrations */
	if (vibes_db_run_migrations(db) < 0) {
		vibes_db_close(db);
		return -1;
	}

	return 0;
}

void vibes_db_close(struct vibes_db *db)
{
	if (db->sqlite) {
		sqlite3_close(db->sqlite);
		db->sqlite = NULL;
	}
	free(db->db_path);
	db->db_path = NULL;
	db->schema_version = 0;
}

int vibes_db_init_schema(struct vibes_db *db)
{
	char *errmsg = NULL;
	int rc;

	rc = sqlite3_exec(db->sqlite, VIBES_SCHEMA_SQL, NULL, NULL, &errmsg);
	if (rc != SQLITE_OK) {
		error("gitvibes: schema initialization failed: %s", errmsg);
		sqlite3_free(errmsg);
		return -1;
	}

	/* Set initial schema version if not present */
	if (vibes_db_get_version(db) == 0) {
		sqlite3_stmt *stmt;
		stmt = vibes_db_prepare(db,
			"INSERT INTO schema_version (version) VALUES (?);");
		if (!stmt)
			return -1;
		sqlite3_bind_int(stmt, 1, VIBES_SCHEMA_VERSION);
		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		if (rc != SQLITE_DONE) {
			error("gitvibes: failed to set schema version");
			return -1;
		}
	}

	db->schema_version = VIBES_SCHEMA_VERSION;
	return 0;
}

int vibes_db_exec(struct vibes_db *db, const char *sql)
{
	char *errmsg = NULL;
	int rc;

	if (!db->sqlite)
		return error("gitvibes: database not open");

	rc = sqlite3_exec(db->sqlite, sql, NULL, NULL, &errmsg);
	if (rc != SQLITE_OK) {
		error("gitvibes: SQL error: %s", errmsg);
		sqlite3_free(errmsg);
		return -1;
	}
	return 0;
}

sqlite3_stmt *vibes_db_prepare(struct vibes_db *db, const char *sql)
{
	sqlite3_stmt *stmt = NULL;
	int rc;

	if (!db->sqlite) {
		error("gitvibes: database not open");
		return NULL;
	}

	rc = sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		error("gitvibes: prepare failed: %s",
		      sqlite3_errmsg(db->sqlite));
		return NULL;
	}
	return stmt;
}

int vibes_db_begin(struct vibes_db *db)
{
	return vibes_db_exec(db, "BEGIN IMMEDIATE;");
}

int vibes_db_commit(struct vibes_db *db)
{
	return vibes_db_exec(db, "COMMIT;");
}

int vibes_db_rollback(struct vibes_db *db)
{
	return vibes_db_exec(db, "ROLLBACK;");
}

char *vibes_db_repo_path(const char *git_dir)
{
	struct strbuf path = STRBUF_INIT;
	strbuf_addf(&path, "%s/vibes.db", git_dir);
	return strbuf_detach(&path, NULL);
}

int vibes_db_get_version(struct vibes_db *db)
{
	sqlite3_stmt *stmt;
	int version = 0;
	int rc;

	stmt = vibes_db_prepare(db,
		"SELECT version FROM schema_version LIMIT 1;");
	if (!stmt)
		return 0; /* table may not exist yet */

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW)
		version = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	return version;
}

int vibes_db_migrate(struct vibes_db *db)
{
	return vibes_db_run_migrations(db);
}

#endif /* VIBES_ENABLED */
