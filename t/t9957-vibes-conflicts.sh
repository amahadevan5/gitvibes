#!/bin/sh

test_description='gitvibes: conflict prediction (vibes-conflicts)'

. ./test-lib.sh

test_expect_success 'setup repo for conflict tests' '
	git init vibes-conflict-test &&
	cd vibes-conflict-test &&
	git vibes-init &&
	echo "init" >file.txt &&
	git add . &&
	git commit -m "initial commit"
'

test_expect_success 'vibes-conflicts runs with no tasks' '
	git vibes-conflicts 2>&1 | grep -q -i "no active\|prediction\|conflict"
'

test_expect_success 'conflict prediction with overlapping tasks' '
	sqlite3 .git/vibes.db <<-\EOSQL &&
	INSERT INTO intents (id, type, status, raw_input, created_at)
	VALUES ("INT001", "feature", "in_progress",
		"add auth", strftime("%s"));
	INSERT INTO tasks (id, intent_id, title, status, wave_number,
		estimated_files, created_at)
	VALUES ("TASK001", "INT001", "implement auth",
		"in_progress", 0,
		"[""src/auth.c"", ""src/main.c""]",
		strftime("%s"));
	INSERT INTO tasks (id, intent_id, title, status, wave_number,
		estimated_files, created_at)
	VALUES ("TASK002", "INT001", "add tests",
		"in_progress", 0,
		"[""src/auth.c"", ""tests/test_auth.c""]",
		strftime("%s"));
	EOSQL
	git vibes-conflicts 2>&1
'

test_done
