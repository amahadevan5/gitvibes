#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "repository.h"
#include "strbuf.h"
#include "run-command.h"
#include "libvibes/safety/safety.h"
#include "libvibes/storage/db.h"

/*
 * Validation Gates: run configurable checks before merge/commit.
 */

int vibes_gate_run(const char *name, const char *command,
		   struct gate_result *result)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf output = STRBUF_INIT;
	int ret;

	result->gate_name = xstrdup(name);

	strvec_pushl(&cp.args, "sh", "-c", command, NULL);
	cp.no_stdin = 1;

	ret = capture_command(&cp, &output, 0);
	result->passed = (ret == 0);
	result->output = strbuf_detach(&output, NULL);

	return ret;
}

int vibes_gate_run_all(struct repository *repo, struct vibes_db *db,
		       struct gate_result **results, int *nr_results)
{
	int count = 0;
	int failed = 0;
	struct gate_result *res;

	/* Allocate space for built-in gates */
	res = xcalloc(3, sizeof(*res));

	/* Gate 1: Check for uncommitted changes */
	{
		struct child_process cp = CHILD_PROCESS_INIT;
		struct strbuf out = STRBUF_INIT;

		strvec_pushl(&cp.args, "diff", "--cached", "--quiet", NULL);
		cp.git_cmd = 1;
		cp.no_stdin = 1;

		res[count].gate_name = xstrdup("staged-changes");
		res[count].passed = (run_command(&cp) == 0);
		res[count].output = xstrdup(
			res[count].passed ? "No staged changes" :
			"Has staged changes ready to commit");
		strbuf_release(&out);
		if (!res[count].passed)
			failed++;
		count++;
	}

	/* Gate 2: Check for conflict markers */
	{
		struct child_process cp = CHILD_PROCESS_INIT;
		struct strbuf out = STRBUF_INIT;
		int has_markers;

		strvec_pushl(&cp.args, "diff", "--check", NULL);
		cp.git_cmd = 1;
		cp.no_stdin = 1;

		has_markers = capture_command(&cp, &out, 0);
		res[count].gate_name = xstrdup("conflict-markers");
		res[count].passed = (has_markers == 0);
		res[count].output = out.len ? strbuf_detach(&out, NULL) :
			xstrdup("No conflict markers");
		if (!res[count].passed)
			failed++;
		count++;
	}

	*results = res;
	*nr_results = count;

	return failed;
}

void vibes_gate_result_free(struct gate_result *result)
{
	free(result->gate_name);
	free(result->output);
}

#endif /* VIBES_ENABLED */
