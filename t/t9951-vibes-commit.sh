#!/bin/sh

test_description='gitvibes: smart commit (vibes-commit)'

. ./test-lib.sh

test_expect_success 'setup test repo with staged changes' '
	git init vibes-commit-test &&
	cd vibes-commit-test &&
	git vibes-init &&

	mkdir -p src tests &&
	cat >src/main.c <<-EOF &&
	#include <stdio.h>
	int main(void) { return 0; }
	EOF
	cat >src/util.c <<-EOF &&
	#include <stdlib.h>
	void *safe_malloc(size_t n) { return malloc(n); }
	EOF
	cat >tests/test_main.c <<-EOF &&
	#include <assert.h>
	void test_main(void) { assert(1); }
	EOF
	cat >README.md <<-EOF &&
	# Test Project
	A test project.
	EOF
	git add . &&
	git commit -m "initial"
'

test_expect_success 'vibes-commit --dry-run shows clusters' '
	cd vibes-commit-test &&
	echo "// updated" >>src/main.c &&
	echo "// updated" >>src/util.c &&
	echo "more docs" >>README.md &&
	git add . &&
	git vibes-commit --dry-run 2>&1 | grep -q -i "cluster\|commit\|file"
'

test_expect_success 'vibes-commit --no-ai works without AI' '
	cd vibes-commit-test &&
	echo "// change 2" >>src/main.c &&
	git add src/main.c &&
	git vibes-commit --no-ai 2>&1
'

test_done
