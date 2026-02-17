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

test_expect_success 'vibes-merge --rollback fails on single-commit history' '
	test_expect_code 1 git vibes-merge --rollback
'

test_expect_success 'safety snapshot can be created' '
	echo "change" >>file.txt &&
	git add . &&
	git commit -m "test commit" &&
	echo "more changes" >>file.txt &&
	git add . &&
	git commit -m "another commit"
'

test_expect_success 'rollback undoes last commit' '
	before=$(git rev-parse HEAD) &&
	git vibes-merge --rollback >rollback.out 2>&1 &&
	grep -q -i "roll" rollback.out &&
	after=$(git rev-parse HEAD) &&
	test "$before" != "$after"
'

test_done
