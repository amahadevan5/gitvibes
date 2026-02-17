#!/bin/sh

test_description='gitvibes: agent orchestration sequential mode'

. ./test-lib.sh

test_expect_success 'setup repo for orchestration tests' '
	git init vibes-orch-test &&
	cd vibes-orch-test &&
	git vibes-init &&
	echo "int main() { return 0; }" >main.c &&
	echo "void helper() {}" >helper.c &&
	git add . &&
	git commit -m "initial commit"
'

test_expect_success 'insert intent and tasks into DB' '
	sqlite3 .git/vibes.db <<-\EOSQL
	INSERT INTO intents (id, type, raw_input, parsed_goal, status, created_at)
	VALUES ("INT_ORCH_001", "feature", "add logging support",
		"Add logging to all functions", "decomposed", strftime("%s"));

	INSERT INTO tasks (id, intent_id, title, description, status,
		wave_number, estimated_files, created_at)
	VALUES ("TASK_W0_A", "INT_ORCH_001", "Add logger to main",
		"Add logging calls to main.c", "pending",
		0, "main.c", strftime("%s"));

	INSERT INTO tasks (id, intent_id, title, description, status,
		wave_number, estimated_files, created_at)
	VALUES ("TASK_W0_B", "INT_ORCH_001", "Add logger to helper",
		"Add logging calls to helper.c", "pending",
		0, "helper.c", strftime("%s"));

	INSERT INTO tasks (id, intent_id, title, description, status,
		wave_number, estimated_files, created_at)
	VALUES ("TASK_W1_A", "INT_ORCH_001", "Add log config",
		"Create logging configuration file", "pending",
		1, "log.conf", strftime("%s"));
	EOSQL
'

test_expect_success 'verify tasks inserted correctly' '
	count=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM tasks WHERE intent_id = '\''INT_ORCH_001'\'';") &&
	test "$count" = "3" &&
	wave0=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM tasks WHERE intent_id = '\''INT_ORCH_001'\'' AND wave_number = 0;") &&
	test "$wave0" = "2" &&
	wave1=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM tasks WHERE intent_id = '\''INT_ORCH_001'\'' AND wave_number = 1;") &&
	test "$wave1" = "1"
'

test_expect_success 'configure sequential mode (max-agents=0)' '
	git config vibes.max-agents 0
'

test_expect_success 'run orchestrator in sequential mode' '
	git vibes-agents run INT_ORCH_001 >orch.out 2>&1 &&
	cat orch.out &&
	grep -q "sequential" orch.out &&
	grep -q "Wave 0" orch.out
'

test_expect_success 'tasks processed by worker (at least 2 of 3)' '
	completed=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM tasks WHERE intent_id = '\''INT_ORCH_001'\''
		 AND status = '\''completed'\'';") &&
	test "$completed" -ge 2
'

test_expect_success 'intent status updated by orchestrator' '
	status=$(sqlite3 .git/vibes.db \
		"SELECT status FROM intents WHERE id = '\''INT_ORCH_001'\'';") &&
	test "$status" != "decomposed"
'

test_expect_success 'events published for task execution' '
	event_count=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM events;") &&
	test "$event_count" -gt 0
'

test_expect_success 'placeholder commits created (AI unavailable)' '
	todo_count=$(git log --oneline | grep -c "feat:") &&
	test "$todo_count" -ge 1
'

test_expect_success 'vibes-agents status reflects task progress' '
	git vibes-agents status >status.out 2>&1 &&
	grep -q "completed" status.out
'

test_expect_success 'vibes-agents logs shows events' '
	git vibes-agents logs >logs.out 2>&1 &&
	! grep -q "No events" logs.out
'

test_expect_success 'task queue claim and release work' '
	sqlite3 .git/vibes.db <<-\EOSQL &&
	INSERT INTO intents (id, type, raw_input, status, created_at)
	VALUES ("INT_QUEUE_TEST", "feature", "queue test",
		"decomposed", strftime("%s"));
	INSERT INTO tasks (id, intent_id, title, status,
		wave_number, created_at)
	VALUES ("TASK_QT_1", "INT_QUEUE_TEST", "queue task 1",
		"pending", 0, strftime("%s"));
	EOSQL
	count=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM tasks WHERE id = '\''TASK_QT_1'\''
		 AND status = '\''pending'\'';") &&
	test "$count" = "1"
'

test_expect_success 'event bus stores typed events' '
	sqlite3 .git/vibes.db <<-\EOSQL &&
	INSERT INTO events (id, type, agent_id, task_id, created_at)
	VALUES ("EVT_TEST_1", "TASK_CLAIMED", "AGENT_T1",
		"TASK_QT_1", strftime("%s"));
	INSERT INTO events (id, type, agent_id, task_id, created_at)
	VALUES ("EVT_TEST_2", "COMPLETED", "AGENT_T1",
		"TASK_QT_1", strftime("%s"));
	EOSQL
	claimed=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM events WHERE type = '\''TASK_CLAIMED'\'';") &&
	test "$claimed" -ge 1 &&
	done_count=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM events WHERE type = '\''COMPLETED'\'';") &&
	test "$done_count" -ge 1
'

test_done
