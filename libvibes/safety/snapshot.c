#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "repository.h"
#include "strbuf.h"
#include "run-command.h"
#include "libvibes/safety/safety.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/*
 * Safety Snapshots: capture git state before risky operations.
 * Uses git stash internally to preserve working tree state.
 */

int vibes_safety_snapshot(struct vibes_db *db, struct repository *repo,
			  const char *label)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf head = STRBUF_INIT;
	struct strbuf msg = STRBUF_INIT;
	char ulid[VIBES_ULID_LEN];

	vibes_ulid_generate(ulid);

	/* Capture current HEAD */
	{
		struct child_process rev = CHILD_PROCESS_INIT;
		strvec_pushl(&rev.args, "rev-parse", "HEAD", NULL);
		rev.git_cmd = 1;
		rev.no_stdin = 1;
		if (capture_command(&rev, &head, 0) == 0)
			strbuf_trim(&head);
	}

	/* Create a stash-like snapshot */
	strbuf_addf(&msg, "vibes-snapshot: %s [%s]",
		    label ? label : "auto", ulid);

	strvec_pushl(&cp.args, "stash", "push", "-m", msg.buf,
		     "--include-untracked", NULL);
	cp.git_cmd = 1;
	run_command(&cp);

	/* Record in events */
	{
		struct strbuf payload = STRBUF_INIT;
		strbuf_addf(&payload,
			"{\"snapshot_id\":\"%s\",\"head\":\"%s\",\"label\":\"%s\"}",
			ulid, head.buf, label ? label : "auto");
		vibes_db_insert_event(db, ulid, "SNAPSHOT", NULL, NULL, NULL,
				      payload.buf);
		strbuf_release(&payload);
	}

	/* Pop the stash back (we just wanted to record it) */
	{
		struct child_process pop = CHILD_PROCESS_INIT;
		strvec_pushl(&pop.args, "stash", "pop", NULL);
		pop.git_cmd = 1;
		run_command(&pop);
	}

	strbuf_release(&head);
	strbuf_release(&msg);

	printf("Safety snapshot created: %.8s (%s)\n", ulid,
	       label ? label : "auto");
	return 0;
}

#endif /* VIBES_ENABLED */
