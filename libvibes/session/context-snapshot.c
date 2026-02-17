#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "repository.h"
#include "strbuf.h"
#include "run-command.h"
#include "libvibes/session/session.h"
#include "libvibes/storage/db.h"

/*
 * Context Snapshot: capture current working state for session persistence.
 * Stores: active intents, task statuses, HEAD ref, branch list,
 * uncommitted changes summary.
 */

static void capture_head(struct strbuf *out)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf head = STRBUF_INIT;

	strvec_pushl(&cp.args, "rev-parse", "HEAD", NULL);
	cp.git_cmd = 1;
	cp.no_stdin = 1;

	if (capture_command(&cp, &head, 0) == 0) {
		strbuf_trim(&head);
		strbuf_addf(out, "\"head\":\"%s\"", head.buf);
	}
	strbuf_release(&head);
}

static void capture_branch(struct strbuf *out)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf branch = STRBUF_INIT;

	strvec_pushl(&cp.args, "rev-parse", "--abbrev-ref", "HEAD", NULL);
	cp.git_cmd = 1;
	cp.no_stdin = 1;

	if (capture_command(&cp, &branch, 0) == 0) {
		strbuf_trim(&branch);
		strbuf_addf(out, ",\"branch\":\"%s\"", branch.buf);
	}
	strbuf_release(&branch);
}

static void capture_status_summary(struct strbuf *out)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf status = STRBUF_INIT;
	int modified = 0, staged = 0, untracked = 0;
	const char *p;

	strvec_pushl(&cp.args, "status", "--porcelain", NULL);
	cp.git_cmd = 1;
	cp.no_stdin = 1;

	if (capture_command(&cp, &status, 0) < 0) {
		strbuf_release(&status);
		return;
	}

	p = status.buf;
	while (*p) {
		if (p[0] == 'M' || p[1] == 'M')
			modified++;
		else if (p[0] == 'A' || p[0] == 'D')
			staged++;
		else if (p[0] == '?')
			untracked++;
		p = strchr(p, '\n');
		if (!p) break;
		p++;
	}

	strbuf_addf(out, ",\"modified\":%d,\"staged\":%d,\"untracked\":%d",
		    modified, staged, untracked);
	strbuf_release(&status);
}

int vibes_snapshot_create(struct vibes_db *db, struct repository *repo,
			  const char *session_id)
{
	struct strbuf context = STRBUF_INIT;
	sqlite3_stmt *stmt;
	int ret;

	strbuf_addstr(&context, "{");
	capture_head(&context);
	capture_branch(&context);
	capture_status_summary(&context);

	/* Capture active intent count */
	{
		int nr_intents = 0;
		sqlite3_stmt *q = vibes_db_prepare(db,
			"SELECT COUNT(*) FROM intents "
			"WHERE status NOT IN ('completed', 'abandoned');");
		if (q) {
			if (sqlite3_step(q) == SQLITE_ROW)
				nr_intents = sqlite3_column_int(q, 0);
			sqlite3_finalize(q);
		}
		strbuf_addf(&context, ",\"active_intents\":%d", nr_intents);
	}

	strbuf_addstr(&context, "}");

	/* Store in sessions table */
	stmt = vibes_db_prepare(db,
		"UPDATE sessions SET context_json = ? WHERE id = ?;");
	if (!stmt) {
		strbuf_release(&context);
		return -1;
	}

	sqlite3_bind_text(stmt, 1, context.buf, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_STATIC);

	ret = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	strbuf_release(&context);

	return (ret == SQLITE_DONE) ? 0 : -1;
}

#endif /* VIBES_ENABLED */
