#!/bin/sh

test_description='gitvibes: session management (vibes-session)'

. ./test-lib.sh

test_expect_success 'setup repo for session tests' '
	git init vibes-session-test &&
	cd vibes-session-test &&
	git vibes-init &&
	echo "init" >file.txt &&
	git add . &&
	git commit -m "initial commit"
'

test_expect_success 'vibes-session list shows empty initially' '
	git vibes-session list 2>&1 | grep -q -i "no sessions"
'

test_expect_success 'vibes-session start creates session' '
	git vibes-session start "test session" 2>&1 | grep -q -i "session started" &&
	count=$(sqlite3 .git/vibes.db "SELECT COUNT(*) FROM sessions;") &&
	test "$count" = "1"
'

test_expect_success 'vibes-session list shows created session' '
	git vibes-session list 2>&1 | grep -q "test session"
'

test_expect_success 'vibes-session end finalizes session' '
	git vibes-session end 2>&1 | grep -q -i "ended"
'

test_expect_success 'vibes-session resume restores context' '
	git vibes-session resume 2>&1 | grep -q -i "restor"
'

test_done
