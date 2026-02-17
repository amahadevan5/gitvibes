#!/bin/sh

test_description='gitvibes performance benchmarks

Measures performance of core gitvibes operations:
  - Database initialization and status queries
  - Tree-sitter knowledge graph indexing at various scales
  - Diff analysis and change clustering (no AI)
  - Conflict prediction

Targets:
  vibes-commit (20 files, no AI): < 5s
  vibes-knowledge index (500 files): < 30s
  vibes-conflicts prediction: < 2s
  vibes-init: < 500ms
'
. ./perf-lib.sh

test_perf_fresh_repo

# Guard: skip if not built with VIBES_ENABLED
test_expect_success 'check vibes support' '
	git vibes-init 2>/dev/null ||
	{
		skip_all="git not built with VIBES_ENABLED" &&
		test_done
	}
'

test_expect_success 'setup: generate 500 C files' '
	for i in $(test_seq 1 500)
	do
		dir="src/mod$(( (i - 1) / 50 ))" &&
		mkdir -p "$dir" &&
		printf "#include <stdio.h>\n#include <string.h>\n\nstruct data_%d {\n    int value;\n    char name[64];\n};\n\nvoid function_%d(int arg)\n{\n    printf(\"hello from function %%d\\n\", arg);\n}\n\nint helper_%d(const char *str)\n{\n    return str ? (int)strlen(str) : 0;\n}\n\nstatic int internal_%d(struct data_%d *d)\n{\n    return d->value + 1;\n}\n" "$i" "$i" "$i" "$i" "$i" >"$dir/file${i}.c" ||
		return 1
	done
'

test_expect_success 'setup: generate 100 Python files' '
	for i in $(test_seq 1 100)
	do
		dir="lib/pkg$(( (i - 1) / 20 ))" &&
		mkdir -p "$dir" &&
		printf "class Service%d:\n    def __init__(self, config):\n        self.config = config\n\n    def process(self, data):\n        return data\n\n    def validate(self, item):\n        return item is not None\n\ndef helper_%d(x):\n    return x * 2\n\ndef transform_%d(items):\n    return [helper_%d(i) for i in items]\n" "$i" "$i" "$i" "$i" >"$dir/module${i}.py" ||
		return 1
	done
'

test_expect_success 'setup: commit all files' '
	git add -A &&
	git commit -q -m "initial: 600 test files" &&
	git vibes-init
'

# -- Database benchmarks --

test_perf 'vibes-init (cold start)' '
	rm -f .git/vibes.db &&
	git vibes-init
'

test_perf 'vibes-status (warm DB)' '
	git vibes-status >/dev/null
'

# -- Knowledge graph benchmarks --

test_perf 'vibes-knowledge index (50 files)' '
	git vibes-knowledge index --max-files 50
'

test_perf 'vibes-knowledge index (200 files)' '
	git vibes-knowledge index --max-files 200
'

test_perf 'vibes-knowledge index (500 files)' '
	git vibes-knowledge index --max-files 500
'

test_perf 'vibes-knowledge stats' '
	git vibes-knowledge stats >/dev/null
'

test_perf 'vibes-knowledge query (symbol lookup)' '
	git vibes-knowledge query function >/dev/null
'

# -- Smart commit benchmarks (no AI) --

test_expect_success 'setup: stage 20 file changes' '
	for i in $(test_seq 1 20)
	do
		echo "/* perf test modification */" >>"src/mod0/file${i}.c" ||
		return 1
	done &&
	git add src/mod0/
'

test_perf 'vibes-commit --dry-run --no-ai (20 files)' '
	git vibes-commit --dry-run --no-ai
'

# Restage for broader test
test_expect_success 'setup: stage 50 file changes' '
	git checkout -q -- . &&
	for i in $(test_seq 1 100)
	do
		echo "/* broader perf test */" >>"src/mod$(( (i - 1) / 50 ))/file${i}.c" ||
		return 1
	done &&
	git add src/
'

test_perf 'vibes-commit --dry-run --no-ai (100 files)' '
	git vibes-commit --dry-run --no-ai
'

# -- Conflict prediction benchmarks --

test_expect_success 'setup: create tasks for conflict prediction' '
	sqlite3 .git/vibes.db "
		INSERT OR IGNORE INTO intents (id, raw_input, parsed_goal, type, status, created_at)
		VALUES (\"01INTENT_PERF0001\", \"perf test\", \"performance testing\", \"chore\", \"active\", datetime(\"now\"));
		INSERT OR IGNORE INTO tasks (id, intent_id, title, description, status, wave, estimated_files, created_at)
		VALUES
		(\"01TASK_A_PERF0001\", \"01INTENT_PERF0001\", \"task A\", \"modify auth\", \"in_progress\", 0, \"src/mod0/file1.c,src/mod0/file2.c,src/mod0/file3.c\", datetime(\"now\")),
		(\"01TASK_B_PERF0002\", \"01INTENT_PERF0001\", \"task B\", \"modify api\", \"in_progress\", 0, \"src/mod0/file2.c,src/mod0/file4.c,src/mod1/file51.c\", datetime(\"now\")),
		(\"01TASK_C_PERF0003\", \"01INTENT_PERF0001\", \"task C\", \"modify db\", \"pending\", 1, \"src/mod0/file3.c,src/mod1/file52.c,lib/pkg0/module1.py\", datetime(\"now\")),
		(\"01TASK_D_PERF0004\", \"01INTENT_PERF0001\", \"task D\", \"modify ui\", \"pending\", 1, \"src/mod2/file101.c,src/mod3/file151.c\", datetime(\"now\"));
	"
'

test_perf 'vibes-conflicts (4 tasks)' '
	git vibes-conflicts
'

test_perf 'vibes-conflicts --impact (3 files)' '
	git vibes-conflicts --impact src/mod0/file1.c src/mod0/file2.c src/mod0/file3.c
'

# -- Size measurements --

test_size 'knowledge graph nodes' '
	sqlite3 .git/vibes.db "SELECT COUNT(*) FROM kg_nodes;"
'

test_size 'knowledge graph edges' '
	sqlite3 .git/vibes.db "SELECT COUNT(*) FROM kg_edges;"
'

test_size 'vibes.db file size (bytes)' '
	wc -c <.git/vibes.db
'

test_done
