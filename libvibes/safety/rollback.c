#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "repository.h"
#include "strbuf.h"
#include "run-command.h"
#include "libvibes/safety/safety.h"
#include "libvibes/storage/db.h"

/*
 * Rollback Engine: undo vibes operations at various granularities.
 */

/*
 * Rollback the last commit.
 */
int vibes_rollback_last(struct vibes_db *db, struct repository *repo)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	/* First create a safety snapshot */
	vibes_safety_snapshot(db, repo, "pre-rollback");

	/* Soft reset to undo last commit */
	strvec_pushl(&cp.args, "reset", "--soft", "HEAD~1", NULL);
	cp.git_cmd = 1;

	if (run_command(&cp))
		return error("gitvibes: failed to rollback last commit");

	printf("Rolled back last commit. Changes are now staged.\n");
	return 0;
}

/*
 * Rollback all commits linked to an intent.
 */
int vibes_rollback_intent(struct vibes_db *db, struct repository *repo,
			  const char *intent_id)
{
	sqlite3_stmt *stmt;
	int nr_commits = 0;

	/* First create a safety snapshot */
	vibes_safety_snapshot(db, repo, "pre-intent-rollback");

	/* Find all commits linked to this intent */
	stmt = vibes_db_prepare(db,
		"SELECT commit_oid FROM commit_intents "
		"WHERE intent_id = ? ORDER BY created_at DESC;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, intent_id, -1, SQLITE_STATIC);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *oid = (const char *)sqlite3_column_text(stmt, 0);
		if (oid) {
			printf("  Reverting commit %.12s\n", oid);

			{
				struct child_process cp = CHILD_PROCESS_INIT;
				strvec_pushl(&cp.args, "revert",
					     "--no-commit", oid, NULL);
				cp.git_cmd = 1;
				if (run_command(&cp) < 0) {
					printf("  Warning: revert of %.12s "
					       "had conflicts\n", oid);
				}
			}
			nr_commits++;
		}
	}
	sqlite3_finalize(stmt);

	if (nr_commits > 0) {
		/* Create a single rollback commit */
		struct child_process cp = CHILD_PROCESS_INIT;
		struct strbuf msg = STRBUF_INIT;

		strbuf_addf(&msg, "revert: rollback intent %.8s (%d commits)",
			    intent_id, nr_commits);

		strvec_pushl(&cp.args, "commit", "-m", msg.buf, NULL);
		cp.git_cmd = 1;
		run_command(&cp);
		strbuf_release(&msg);

		/* Update intent status */
		vibes_db_update_intent_status(db, intent_id, "abandoned");

		printf("Rolled back %d commit(s) for intent %.8s\n",
		       nr_commits, intent_id);
	} else {
		printf("No commits found for intent %.8s\n", intent_id);
	}

	return 0;
}

#endif /* VIBES_ENABLED */
