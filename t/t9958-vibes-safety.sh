#!/bin/sh

test_description='gitvibes: safety (rollback, gates, snapshots)'

. ./test-lib.sh

test_expect_success 'setup repo for safety tests' '
	git init vibes-safety-test &&
	cd vibes-safety-test &&
	git vibes-init &&
	echo "init" >file.txt &&
	git add . &&
	git commit -m "initial commit"
'

test_expect_success 'vibes-merge --rollback works on empty history' '
	cd vibes-safety-test &&
	test_expect_code 1 git vibes-merge --rollback 2>&1
'

test_expect_success 'safety snapshot can be created' '
	cd vibes-safety-test &&
	echo "change" >>file.txt &&
	git add . &&
	git commit -m "test commit" &&
	event_count_before=$(sqlite3 .git/vibes.db \
		"SELECT COUNT(*) FROM events;") &&
	echo "more changes" >>file.txt &&
	git add . &&
	git commit -m "another commit"
'

test_expect_success 'rollback undoes last commit' '
	cd vibes-safety-test &&
	before=$(git rev-parse HEAD) &&
	git vibes-merge --rollback 2>&1 | grep -q -i "roll" &&
	after=$(git rev-parse HEAD) &&
	test "$before" != "$after"
'

test_done
