#!/bin/sh

test_description='gitvibes: pipeline dry-run without AI'

. ./test-lib.sh

test_expect_success 'setup repo for pipeline tests' '
	git init vibes-pipeline-test &&
	cd vibes-pipeline-test &&
	git vibes-init &&
	echo "int main() { return 0; }" >main.c &&
	echo "void login() {}" >auth.c &&
	git add . &&
	git commit -m "initial commit"
'

test_expect_success 'git vibes creates intent (dry-run)' '
	test_might_fail git vibes "add user login" >vibes.out 2>&1 &&
	cat vibes.out
'

test_expect_success 'intent exists in database' '
	count=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM intents;") &&
	test "$count" -ge 1
'

test_expect_success 'intent has correct raw_input' '
	raw=$(sqlite3 .git/vibes.db \
		"SELECT raw_input FROM intents ORDER BY created_at DESC LIMIT 1;") &&
	echo "$raw" | grep -q "add user login"
'

test_expect_success 'git vibes --list shows intent' '
	git vibes --list >list.out 2>&1 &&
	cat list.out &&
	grep -q -i "login\|intent\|captured" list.out
'

test_expect_success 'git vibes --show displays intent details' '
	intent_id=$(sqlite3 .git/vibes.db \
		"SELECT id FROM intents ORDER BY created_at DESC LIMIT 1;") &&
	git vibes --show "$intent_id" >show.out 2>&1 &&
	cat show.out &&
	grep -q "$intent_id" show.out
'

test_expect_success 'git vibes --abandon marks intent abandoned' '
	intent_id=$(sqlite3 .git/vibes.db \
		"SELECT id FROM intents WHERE status != '\''abandoned'\'' ORDER BY created_at DESC LIMIT 1;") &&
	git vibes --abandon "$intent_id" &&
	status=$(sqlite3 .git/vibes.db \
		"SELECT status FROM intents WHERE id = '\''${intent_id}'\'';") &&
	test "$status" = "abandoned"
'

test_expect_success 'create second intent for further testing' '
	test_might_fail git vibes "refactor authentication module" >vibes2.out 2>&1 &&
	count=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM intents;") &&
	test "$count" -ge 2
'

test_expect_success 'vibes-status shows system overview' '
	git vibes-status >vstatus.out 2>&1 &&
	cat vstatus.out
'

test_expect_success 'vibes-log shows history' '
	git vibes-log >vlog.out 2>&1 &&
	cat vlog.out
'

test_expect_success 'git vibes -h shows --execute option' '
	test_expect_code 129 git vibes -h >help.out 2>&1 &&
	grep -q "execute" help.out
'

test_done
