# gitvibes - AI-Native Git Fork

## Build

```bash
# Standard build (gitvibes disabled)
make -j$(sysctl -n hw.ncpu)

# Build with gitvibes enabled
make VIBES_ENABLED=1 NO_GETTEXT=1 -j$(sysctl -n hw.ncpu)

# Run git from build directory
./git vibes-init
./git vibes-commit --dry-run
./git vibes-dashboard
```

## Project Structure

- `builtin/vibes*.c` - Git builtin commands (vibes, vibes-commit, vibes-init, vibes-status, vibes-log, vibes-branch, vibes-merge, vibes-agents, vibes-conflicts, vibes-session, vibes-knowledge, vibes-dashboard, vibes-decompose)
- `libvibes/` - Core library
  - `ai/` - AI backends (Claude API + CLI)
  - `intent/` - Intent tracking, NL parsing, task decomposition
  - `smart-commit/` - Diff analysis, change clustering, message generation
  - `knowledge/` - Tree-sitter AST parsing, knowledge graph, impact analysis
  - `agents/` - Multi-agent orchestration, event bus, IPC, file locks
  - `merge/` - Semantic merge, conflict classification, auto-resolution
  - `branch/` - Branch convention detection, topology planning
  - `session/` - Session management, context snapshots
  - `safety/` - Validation gates, rollback, snapshots
  - `storage/` - SQLite layer (db, schema, migrations, queries)
  - `tui/` - ncurses terminal UI
  - `webui/` - Embedded HTTP server + REST API
- `deps/` - Dependencies (sqlite3, tree-sitter, tree-sitter grammars)
- `t/t995[0-8]-vibes-*.sh` - gitvibes test suite

## Conventions

- All gitvibes code is guarded by `#ifdef VIBES_ENABLED`
- Builtin commands use `#define USE_THE_REPOSITORY_VARIABLE` before includes
- Function signature: `int cmd_vibes_*(int argc, const char **argv, const char *prefix, struct repository *repo)`
- Git config API: `repo_config_get_string_tmp(repo, "vibes.key", &val)`
- Database: SQLite3 with WAL mode at `.git/vibes.db`
- IDs: ULID format (26 chars, base32, time-ordered)
- C style: tabs for indentation, K&R braces, git coding style
- No external JSON library - hand-written minimal JSON parsing

## Testing

```bash
# Run gitvibes tests
cd t && sh t9950-vibes-init.sh -v

# Run all vibes tests
for t in t/t995[0-8]-vibes-*.sh; do sh "$t" -v; done
```

## Dependencies

- SQLite3 amalgamation (in deps/sqlite3/)
- Tree-sitter + language grammars (in deps/tree-sitter*/)
- libcurl (system, for Claude API)
- ncurses (system, for TUI)
