#!/bin/sh

test_description='gitvibes: vibes-init and storage layer'

. ./test-lib.sh

test_expect_success 'vibes-init creates database' '
	git init vibes-test &&
	cd vibes-test &&
	git vibes-init &&
	test_path_is_file .git/vibes.db
'

test_expect_success 'vibes-init is idempotent' '
	cd vibes-test &&
	git vibes-init &&
	test_path_is_file .git/vibes.db
'

test_expect_success 'vibes-init creates schema tables' '
	cd vibes-test &&
	sqlite3 .git/vibes.db ".tables" >tables.out &&
	grep -q "intents" tables.out &&
	grep -q "tasks" tables.out &&
	grep -q "events" tables.out &&
	grep -q "sessions" tables.out &&
	grep -q "kg_nodes" tables.out &&
	grep -q "kg_edges" tables.out &&
	grep -q "file_locks" tables.out &&
	grep -q "commit_intents" tables.out
'

test_expect_success 'vibes-init sets WAL mode' '
	cd vibes-test &&
	mode=$(sqlite3 .git/vibes.db "PRAGMA journal_mode;") &&
	test "$mode" = "wal"
'

test_done
