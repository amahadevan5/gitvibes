#!/bin/sh

test_description='gitvibes: intent system (vibes command, vibes-status, vibes-log)'

. ./test-lib.sh

test_expect_success 'setup repo for intent tests' '
	git init vibes-intent-test &&
	cd vibes-intent-test &&
	git vibes-init &&
	echo "init" >file.txt &&
	git add . &&
	git commit -m "initial commit"
'

test_expect_success 'git vibes --list shows no intents initially' '
	git vibes --list 2>&1 | grep -q -i "no intents\|0"
'

test_expect_success 'git vibes-status runs without error' '
	git vibes-status
'

test_expect_success 'git vibes-log runs without error' '
	git vibes-log
'

test_expect_success 'intent database operations work' '
	sqlite3 .git/vibes.db "
		INSERT INTO intents (id, type, status, raw_input, created_at)
		VALUES (\"TEST001\", \"feature\", \"captured\",
			\"add JWT authentication\", strftime(\"%s\"));
	" &&
	count=$(sqlite3 .git/vibes.db "SELECT COUNT(*) FROM intents;") &&
	test "$count" = "1" &&
	git vibes --list 2>&1 | grep -q "TEST001\|JWT"
'

test_expect_success 'git vibes --show displays intent details' '
	git vibes --show TEST001 2>&1 | grep -q "TEST001\|JWT"
'

test_expect_success 'git vibes --abandon marks intent abandoned' '
	git vibes --abandon TEST001 &&
	status=$(sqlite3 .git/vibes.db \
		"SELECT status FROM intents WHERE id=\"TEST001\";") &&
	test "$status" = "abandoned"
'

test_expect_success 'git vibes -h shows --execute option' '
	test_expect_code 129 git vibes -h >help.out 2>&1 &&
	grep -q "execute" help.out
'

test_done
