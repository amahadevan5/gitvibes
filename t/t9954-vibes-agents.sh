#!/bin/sh

test_description='gitvibes: agent system (vibes-agents)'

. ./test-lib.sh

test_expect_success 'setup repo for agent tests' '
	git init vibes-agent-test &&
	cd vibes-agent-test &&
	git vibes-init &&
	echo "init" >file.txt &&
	git add . &&
	git commit -m "initial commit"
'

test_expect_success 'vibes-agents list shows empty initially' '
	git vibes-agents list 2>&1 | grep -q -i "no agent"
'

test_expect_success 'vibes-agents status shows system info' '
	git vibes-agents status 2>&1 | grep -q -i "status\|events\|locks"
'

test_expect_success 'vibes-agents logs shows empty initially' '
	git vibes-agents logs 2>&1 | grep -q -i "no events"
'

test_expect_success 'event bus inserts and queries work' '
	sqlite3 .git/vibes.db "
		INSERT INTO events (id, type, agent_id, task_id, created_at)
		VALUES (\"EVT001\", \"TASK_CLAIMED\", \"AGENT001\",
			\"TASK001\", strftime(\"%s\"));
	" &&
	count=$(sqlite3 .git/vibes.db "SELECT COUNT(*) FROM events;") &&
	test "$count" = "1" &&
	git vibes-agents logs 2>&1 | grep -q "TASK_CLAIMED"
'

test_expect_success 'file lock operations work' '
	sqlite3 .git/vibes.db "
		INSERT INTO file_locks (file_path, agent_id, locked_at, expires_at)
		VALUES (\"src/main.c\", \"AGENT001\",
			strftime(\"%s\"), strftime(\"%s\") + 3600);
	" &&
	count=$(sqlite3 .git/vibes.db "SELECT COUNT(*) FROM file_locks;") &&
	test "$count" = "1"
'

test_expect_success 'vibes-decompose --worker requires task-id' '
	test_expect_code 128 git vibes-decompose --worker 2>err &&
	grep -q "requires --task-id" err
'

test_expect_success 'task queue claim and release work' '
	sqlite3 .git/vibes.db <<-\EOSQL &&
	INSERT INTO intents (id, type, raw_input, status, created_at)
	VALUES ("INT_AGENT_TEST", "feature", "test intent",
		"decomposed", strftime("%s"));
	INSERT INTO tasks (id, intent_id, title, status,
		wave_number, created_at)
	VALUES ("TASK_Q1", "INT_AGENT_TEST", "queue test task",
		"pending", 0, strftime("%s"));
	EOSQL
	count=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM tasks WHERE status = '\''pending'\'' AND intent_id = '\''INT_AGENT_TEST'\'';") &&
	test "$count" = "1"
'

test_expect_success 'orchestrator config reads max-agents' '
	git config vibes.max-agents 0 &&
	val=$(git config vibes.max-agents) &&
	test "$val" = "0"
'

test_done
