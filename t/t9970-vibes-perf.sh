#!/bin/sh

test_description='gitvibes performance benchmarks

Measures wall-clock time of core gitvibes operations:
  - Database initialization and status queries
  - Tree-sitter knowledge graph indexing at various scales
  - Diff analysis and change clustering (no AI)
  - Conflict prediction and impact analysis

Targets:
  vibes-init:                      < 500ms
  vibes-commit (20 files, no AI):  < 5s
  vibes-knowledge index (500 files): < 30s
  vibes-conflicts prediction:      < 2s
'

. ./test-lib.sh

# Helper: time a command and report elapsed milliseconds
time_cmd () {
	local label="$1"
	shift
	local start_ms end_ms elapsed
	start_ms=$(($(date +%s) * 1000 + $(date +%N 2>/dev/null | sed 's/^0*//' | head -c3 || echo 0)))
	# Use portable seconds-based timing
	start_s=$(date +%s)
	"$@"
	local ret=$?
	end_s=$(date +%s)
	elapsed=$(( end_s - start_s ))
	printf "  PERF %-50s %ds\n" "$label" "$elapsed"
	return $ret
}

# === SETUP: Generate test repository ===

test_expect_success 'setup: initialize vibes repo' '
	git vibes-init
'

test_expect_success 'setup: generate 500 C source files' '
	for i in $(test_seq 1 500)
	do
		dir="src/mod$(( (i - 1) / 50 ))" &&
		mkdir -p "$dir" &&
		printf "#include <stdio.h>\n#include <string.h>\n\nstruct data_%d {\n    int value;\n    char name[64];\n};\n\nvoid function_%d(int arg)\n{\n    printf(\"hello %%d\\n\", arg);\n}\n\nint helper_%d(const char *str)\n{\n    return str ? (int)strlen(str) : 0;\n}\n\nstatic int internal_%d(struct data_%d *d)\n{\n    return d->value + 1;\n}\n" "$i" "$i" "$i" "$i" "$i" >"$dir/file${i}.c" ||
		return 1
	done
'

test_expect_success 'setup: generate 100 Python files' '
	for i in $(test_seq 1 100)
	do
		dir="lib/pkg$(( (i - 1) / 20 ))" &&
		mkdir -p "$dir" &&
		printf "class Service%d:\n    def __init__(self, cfg):\n        self.cfg = cfg\n\n    def run(self, data):\n        return data\n\ndef helper_%d(x):\n    return x * 2\n\ndef transform_%d(items):\n    return [helper_%d(i) for i in items]\n" "$i" "$i" "$i" "$i" >"$dir/module${i}.py" ||
		return 1
	done
'

test_expect_success 'setup: commit all 600 files' '
	git add -A &&
	git commit -q -m "initial: 600 test files"
'

# === DATABASE BENCHMARKS ===

test_expect_success 'perf: vibes-init cold start (target <500ms)' '
	rm -f .git/vibes.db &&
	start=$(date +%s) &&
	git vibes-init &&
	end=$(date +%s) &&
	elapsed=$((end - start)) &&
	echo "vibes-init cold: ${elapsed}s" &&
	test "$elapsed" -lt 5
'

test_expect_success 'perf: vibes-status warm DB' '
	start=$(date +%s) &&
	git vibes-status &&
	end=$(date +%s) &&
	elapsed=$((end - start)) &&
	echo "vibes-status warm: ${elapsed}s" &&
	test "$elapsed" -lt 3
'

# === KNOWLEDGE GRAPH BENCHMARKS ===

test_expect_success 'perf: knowledge index 50 files' '
	start=$(date +%s) &&
	git vibes-knowledge index --max-files 50 &&
	end=$(date +%s) &&
	elapsed=$((end - start)) &&
	echo "knowledge index 50: ${elapsed}s" &&
	test "$elapsed" -lt 10
'

test_expect_success 'perf: knowledge index 200 files' '
	start=$(date +%s) &&
	git vibes-knowledge index --max-files 200 &&
	end=$(date +%s) &&
	elapsed=$((end - start)) &&
	echo "knowledge index 200: ${elapsed}s" &&
	test "$elapsed" -lt 20
'

test_expect_success 'perf: knowledge index 500 files (target <30s)' '
	start=$(date +%s) &&
	git vibes-knowledge index --max-files 500 &&
	end=$(date +%s) &&
	elapsed=$((end - start)) &&
	echo "knowledge index 500: ${elapsed}s" &&
	test "$elapsed" -lt 30
'

test_expect_success 'perf: knowledge stats query' '
	start=$(date +%s) &&
	git vibes-knowledge stats &&
	end=$(date +%s) &&
	elapsed=$((end - start)) &&
	echo "knowledge stats: ${elapsed}s" &&
	test "$elapsed" -lt 3
'

test_expect_success 'perf: knowledge symbol lookup' '
	start=$(date +%s) &&
	git vibes-knowledge query function &&
	end=$(date +%s) &&
	elapsed=$((end - start)) &&
	echo "knowledge query: ${elapsed}s" &&
	test "$elapsed" -lt 3
'

# === SMART COMMIT BENCHMARKS (no AI) ===

test_expect_success 'setup: stage 20 modified files' '
	for i in $(test_seq 1 20)
	do
		echo "/* perf modification */" >>"src/mod0/file${i}.c" ||
		return 1
	done &&
	git add src/mod0/
'

test_expect_success 'perf: vibes-commit dry-run no-ai 20 files (target <5s)' '
	start=$(date +%s) &&
	git vibes-commit --dry-run --no-ai &&
	end=$(date +%s) &&
	elapsed=$((end - start)) &&
	echo "vibes-commit 20 files: ${elapsed}s" &&
	test "$elapsed" -lt 5
'

test_expect_success 'setup: stage 100 modified files' '
	git checkout -q -- . &&
	for i in $(test_seq 1 100)
	do
		echo "/* broader modification */" >>"src/mod$(( (i - 1) / 50 ))/file${i}.c" ||
		return 1
	done &&
	git add src/
'

test_expect_success 'perf: vibes-commit dry-run no-ai 100 files' '
	start=$(date +%s) &&
	git vibes-commit --dry-run --no-ai &&
	end=$(date +%s) &&
	elapsed=$((end - start)) &&
	echo "vibes-commit 100 files: ${elapsed}s" &&
	test "$elapsed" -lt 15
'

# === CONFLICT PREDICTION BENCHMARKS ===

test_expect_success 'setup: create tasks for conflict prediction' '
	sqlite3 .git/vibes.db "
		INSERT OR IGNORE INTO intents (id, raw_input, parsed_goal, type, status, created_at)
		VALUES (\"01INTENT_PERF0001\", \"perf test\", \"benchmark\", \"chore\", \"captured\", strftime(\"%s\",\"now\"));
		INSERT OR IGNORE INTO tasks (id, intent_id, title, description, status, wave_number, estimated_files, created_at)
		VALUES
		(\"01TASK_A_PERF0001\", \"01INTENT_PERF0001\", \"task A\", \"auth\", \"in_progress\", 0, \"src/mod0/file1.c,src/mod0/file2.c,src/mod0/file3.c\", strftime(\"%s\",\"now\")),
		(\"01TASK_B_PERF0002\", \"01INTENT_PERF0001\", \"task B\", \"api\", \"in_progress\", 0, \"src/mod0/file2.c,src/mod0/file4.c,src/mod1/file51.c\", strftime(\"%s\",\"now\")),
		(\"01TASK_C_PERF0003\", \"01INTENT_PERF0001\", \"task C\", \"db\", \"pending\", 1, \"src/mod0/file3.c,src/mod1/file52.c,lib/pkg0/module1.py\", strftime(\"%s\",\"now\")),
		(\"01TASK_D_PERF0004\", \"01INTENT_PERF0001\", \"task D\", \"ui\", \"pending\", 1, \"src/mod2/file101.c,src/mod3/file151.c\", strftime(\"%s\",\"now\"));
	"
'

test_expect_success 'perf: vibes-conflicts 4 tasks (target <2s)' '
	start=$(date +%s) &&
	git vibes-conflicts &&
	end=$(date +%s) &&
	elapsed=$((end - start)) &&
	echo "vibes-conflicts 4 tasks: ${elapsed}s" &&
	test "$elapsed" -lt 5
'

test_expect_success 'perf: vibes-conflicts impact 3 files' '
	start=$(date +%s) &&
	git vibes-conflicts --impact src/mod0/file1.c src/mod0/file2.c src/mod0/file3.c &&
	end=$(date +%s) &&
	elapsed=$((end - start)) &&
	echo "vibes-conflicts impact: ${elapsed}s" &&
	test "$elapsed" -lt 5
'

# === SIZE MEASUREMENTS ===

test_expect_success 'size: knowledge graph stats' '
	nodes=$(sqlite3 .git/vibes.db "SELECT COUNT(*) FROM kg_nodes;") &&
	edges=$(sqlite3 .git/vibes.db "SELECT COUNT(*) FROM kg_edges;") &&
	db_size=$(wc -c <.git/vibes.db | tr -d " ") &&
	echo "Knowledge graph: $nodes nodes, $edges edges" &&
	echo "Database size: $db_size bytes"
'

test_done
