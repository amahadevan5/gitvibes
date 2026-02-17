#!/bin/sh

test_description='gitvibes: knowledge graph (vibes-knowledge)'

. ./test-lib.sh

test_expect_success 'setup repo with source files' '
	git init vibes-kg-test &&
	cd vibes-kg-test &&
	git vibes-init &&

	mkdir -p src tests &&
	cat >src/main.c <<-"EOF" &&
	#include <stdio.h>
	#include "util.h"

	int main(int argc, char **argv) {
		printf("hello\n");
		return 0;
	}
	EOF
	cat >src/util.h <<-"EOF" &&
	#ifndef UTIL_H
	#define UTIL_H
	void helper(void);
	#endif
	EOF
	cat >src/util.c <<-"EOF" &&
	#include "util.h"

	void helper(void) {
	}
	EOF
	cat >tests/test_util.c <<-"EOF" &&
	#include <assert.h>
	#include "../src/util.h"

	void test_helper(void) {
		assert(1);
	}
	EOF
	git add . &&
	git commit -m "initial source"
'

test_expect_success 'vibes-knowledge stats works on empty graph' '
	git vibes-knowledge stats 2>&1 | grep -q "node\|edge\|0"
'

test_expect_success 'vibes-knowledge index builds graph' '
	git vibes-knowledge index &&
	nodes=$(sqlite3 .git/vibes.db "SELECT COUNT(*) FROM kg_nodes;") &&
	test "$nodes" -gt 0
'

test_expect_success 'vibes-knowledge query finds symbols' '
	git vibes-knowledge query main 2>&1 | grep -q "main"
'

test_expect_success 'vibes-knowledge deps shows dependencies' '
	git vibes-knowledge deps src/main.c
'

test_done
