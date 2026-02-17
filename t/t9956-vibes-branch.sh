#!/bin/sh

test_description='gitvibes: branch strategy (vibes-branch)'

. ./test-lib.sh

test_expect_success 'setup repo for branch tests' '
	git init vibes-branch-test &&
	cd vibes-branch-test &&
	git vibes-init &&
	echo "init" >file.txt &&
	git add . &&
	git commit -m "initial commit"
'

test_expect_success 'vibes-branch detect detects convention' '
	cd vibes-branch-test &&
	git vibes-branch detect 2>&1 | grep -q -i "convention"
'

test_expect_success 'trunk-based detection for simple repo' '
	cd vibes-branch-test &&
	git vibes-branch detect 2>&1 | grep -q -i "trunk\|github"
'

test_expect_success 'vibes-branch with no args shows convention' '
	cd vibes-branch-test &&
	git vibes-branch 2>&1 | grep -q -i "convention"
'

test_done
