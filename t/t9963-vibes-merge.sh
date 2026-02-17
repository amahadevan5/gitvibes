#!/bin/sh

test_description='gitvibes: merge operations'

. ./test-lib.sh

test_expect_success 'setup repo for merge tests' '
	git init vibes-merge-test &&
	cd vibes-merge-test &&
	git vibes-init &&
	echo "int main() { return 0; }" >main.c &&
	echo "void helper() {}" >helper.c &&
	git add . &&
	git commit -m "initial commit"
'

test_expect_success 'create agent branch with changes' '
	git checkout -b vibes/agent-test1 &&
	echo "/* added by agent */" >>main.c &&
	echo "void new_func() {}" >>helper.c &&
	git add . &&
	git commit -m "feat: add logging (agent work)" &&
	git checkout master
'

test_expect_success 'record pre-merge HEAD' '
	echo $(git rev-parse HEAD) >pre_merge_head
'

test_expect_success 'vibes-merge merges agent branch' '
	test_might_fail git vibes-merge --no-gates vibes/agent-test1 &&
	grep -q "added by agent" main.c &&
	grep -q "new_func" helper.c
'

test_expect_success 'HEAD advanced after merge' '
	pre=$(cat pre_merge_head) &&
	post=$(git rev-parse HEAD) &&
	test "$pre" != "$post"
'

test_expect_success 'vibes-merge --rollback changes HEAD' '
	before=$(git rev-parse HEAD) &&
	git vibes-merge --rollback >rollback.out 2>&1 &&
	grep -q -i "roll" rollback.out &&
	after=$(git rev-parse HEAD) &&
	test "$before" != "$after"
'

test_expect_success 'vibes-merge -h shows usage' '
	test_expect_code 129 git vibes-merge -h >help.out 2>&1 &&
	grep -q "rollback" help.out
'

test_expect_success 'vibes-merge without args shows usage' '
	test_expect_code 129 git vibes-merge >noargs.out 2>&1
'

test_expect_success 'vibes-merge --gates flag is documented' '
	test_expect_code 129 git vibes-merge -h >help2.out 2>&1 &&
	grep -q "gates" help2.out
'

test_done
