#!/bin/sh

test_description='gitvibes: schema migration framework'

. ./test-lib.sh

test_expect_success 'setup: create repo and init vibes' '
	git init migration-test &&
	cd migration-test &&
	git vibes-init
'

test_expect_success 'fresh install has schema version 2' '
	version=$(sqlite3 .git/vibes.db "SELECT version FROM schema_version;") &&
	test "$version" = "2"
'

test_expect_success 'fresh schema includes priority column on intents' '
	sqlite3 .git/vibes.db ".schema intents" >schema.out &&
	grep -q "priority" schema.out
'

test_expect_success 'fresh schema includes retry_count column on tasks' '
	sqlite3 .git/vibes.db ".schema tasks" >schema.out &&
	grep -q "retry_count" schema.out
'

test_expect_success 'migration from v1 to v2 adds columns' '
	git init migration-v1 &&
	cd migration-v1 &&
	git vibes-init &&

	# Simulate a v1 database by rebuilding both tables without v2 columns
	sqlite3 .git/vibes.db "
		UPDATE schema_version SET version = 1;

		/* Strip intents back to v1 (no priority) */
		CREATE TABLE intents_backup AS SELECT
			id, parent_id, type, status, raw_input,
			parsed_goal, criteria, constraints,
			session_id, created_at, started_at, completed_at
		FROM intents;
		DROP TABLE intents;
		CREATE TABLE intents (
			id TEXT PRIMARY KEY,
			parent_id TEXT REFERENCES intents(id),
			type TEXT NOT NULL CHECK(type IN
				(\"feature\",\"bugfix\",\"refactor\",\"docs\",\"chore\")),
			status TEXT NOT NULL CHECK(status IN
				(\"captured\",\"decomposed\",\"in_progress\",
				 \"completed\",\"abandoned\")),
			raw_input TEXT NOT NULL,
			parsed_goal TEXT,
			criteria TEXT,
			constraints TEXT,
			session_id TEXT,
			created_at INTEGER NOT NULL,
			started_at INTEGER,
			completed_at INTEGER
		);
		INSERT INTO intents SELECT * FROM intents_backup;
		DROP TABLE intents_backup;

		/* Strip tasks back to v1 (no retry_count) */
		CREATE TABLE tasks_backup AS SELECT
			id, intent_id, title, description,
			dependencies, estimated_files,
			assigned_agent, worktree_path, branch,
			status, wave_number, created_at
		FROM tasks;
		DROP TABLE tasks;
		CREATE TABLE tasks (
			id TEXT PRIMARY KEY,
			intent_id TEXT NOT NULL REFERENCES intents(id),
			title TEXT NOT NULL,
			description TEXT,
			dependencies TEXT,
			estimated_files TEXT,
			assigned_agent TEXT,
			worktree_path TEXT,
			branch TEXT,
			status TEXT NOT NULL CHECK(status IN
				(\"pending\",\"claimed\",\"in_progress\",
				 \"completed\",\"failed\")),
			wave_number INTEGER NOT NULL DEFAULT 0,
			created_at INTEGER NOT NULL
		);
		INSERT INTO tasks SELECT * FROM tasks_backup;
		DROP TABLE tasks_backup;
	" &&

	# Verify version is 1
	version=$(sqlite3 .git/vibes.db "SELECT version FROM schema_version;") &&
	test "$version" = "1" &&

	# Any command that opens the DB will trigger migration.
	git vibes-status &&

	# Verify migration bumped to v2
	version=$(sqlite3 .git/vibes.db "SELECT version FROM schema_version;") &&
	test "$version" = "2" &&

	# Verify new columns exist
	sqlite3 .git/vibes.db ".schema intents" >schema.out &&
	grep -q "priority" schema.out &&
	sqlite3 .git/vibes.db ".schema tasks" >schema.out &&
	grep -q "retry_count" schema.out
'

test_expect_success 'repeated open does not re-run migrations' '
	git vibes-status &&
	version=$(sqlite3 .git/vibes.db "SELECT version FROM schema_version;") &&
	test "$version" = "2"
'

test_done
