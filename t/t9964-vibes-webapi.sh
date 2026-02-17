#!/bin/sh

test_description='gitvibes: web API smoke tests'

. ./test-lib.sh

test_expect_success 'setup repo for web API tests' '
	git init vibes-webapi-test &&
	cd vibes-webapi-test &&
	git vibes-init &&
	echo "init" >file.txt &&
	git add . &&
	git commit -m "initial commit"
'

test_expect_success 'insert test data for API queries' '
	sqlite3 .git/vibes.db <<-\EOSQL
	INSERT INTO intents (id, type, raw_input, status, created_at)
	VALUES ("INT_WEB_001", "feature", "web api test intent",
		"captured", strftime("%s"));

	INSERT INTO tasks (id, intent_id, title, status,
		wave_number, created_at)
	VALUES ("TASK_WEB_001", "INT_WEB_001", "web api test task",
		"pending", 0, strftime("%s"));

	INSERT INTO events (id, type, agent_id, task_id, created_at)
	VALUES ("EVT_WEB_001", "TASK_CLAIMED", "AGENT_W1",
		"TASK_WEB_001", strftime("%s"));
	EOSQL
'

test_expect_success 'vibes-dashboard --web returns not-yet-available' '
	test_expect_code 1 git vibes-dashboard --web >web.out 2>&1 &&
	grep -q "not yet available" web.out
'

test_expect_success 'vibes-dashboard -h shows web option' '
	test_expect_code 129 git vibes-dashboard -h >help.out 2>&1 &&
	grep -q "web" help.out
'

test_expect_success 'vibes-dashboard -h shows port option' '
	grep -q "port" help.out
'

test_expect_success 'vibes-dashboard --web --port accepts custom port' '
	test_expect_code 1 git vibes-dashboard --web --port 4242 >port.out 2>&1 &&
	grep -q "not yet available" port.out
'

test_expect_success 'database has test data for future API' '
	intent_count=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM intents;") &&
	test "$intent_count" -ge 1 &&
	event_count=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM events;") &&
	test "$event_count" -ge 1
'

test_done
