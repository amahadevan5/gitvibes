#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "repository.h"
#include "strbuf.h"
#include "run-command.h"
#include "libvibes/merge/semantic-merge.h"
#include "libvibes/storage/db.h"

/*
 * Semantic Merge: wraps git's merge with conflict classification
 * and auto-resolution for safe patterns.
 */

/*
 * Run git merge and capture conflicts.
 */
static int run_merge(const char *branch)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	strvec_pushl(&cp.args, "merge", "--no-commit", branch, NULL);
	cp.git_cmd = 1;

	return run_command(&cp);
}

/*
 * Get list of conflicted files.
 */
static int get_conflicted_files(struct string_list *files)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf output = STRBUF_INIT;
	const char *p;

	strvec_pushl(&cp.args, "diff", "--name-only", "--diff-filter=U", NULL);
	cp.git_cmd = 1;
	cp.no_stdin = 1;

	if (capture_command(&cp, &output, 0) < 0) {
		strbuf_release(&output);
		return -1;
	}

	p = output.buf;
	while (*p) {
		const char *eol = strchr(p, '\n');
		if (!eol)
			eol = p + strlen(p);
		if (eol > p) {
			char *file = xstrndup(p, eol - p);
			string_list_append(files, file);
			free(file);
		}
		p = *eol ? eol + 1 : eol;
	}

	strbuf_release(&output);
	return files->nr;
}

/*
 * Read a conflict stage from the index via `git show :<stage>:<path>`.
 * stage 1 = base, 2 = ours, 3 = theirs.
 */
int vibes_read_conflict_stage(struct strbuf *out, int stage, const char *path)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf spec = STRBUF_INIT;
	int ret;

	strbuf_addf(&spec, ":%d:%s", stage, path);
	strvec_pushl(&cp.args, "show", spec.buf, NULL);
	cp.git_cmd = 1;
	cp.no_stdin = 1;

	ret = capture_command(&cp, out, 0);
	strbuf_release(&spec);
	return ret;
}

/*
 * Mark a file as resolved.
 */
static int mark_resolved(const char *file_path)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	strvec_pushl(&cp.args, "add", file_path, NULL);
	cp.git_cmd = 1;

	return run_command(&cp);
}

int vibes_semantic_merge(struct repository *repo, const char *branch,
			 struct vibes_db *db)
{
	struct string_list conflicts = STRING_LIST_INIT_DUP;
	int merge_ret;
	int auto_resolved = 0;
	int remaining = 0;
	int i;

	printf("Attempting merge of '%s'...\n", branch);

	merge_ret = run_merge(branch);

	if (merge_ret == 0) {
		printf("Merge completed cleanly.\n");
		return 0;
	}

	/* Get conflicted files */
	get_conflicted_files(&conflicts);

	if (!conflicts.nr) {
		printf("Merge failed but no conflicts detected.\n");
		string_list_clear(&conflicts, 0);
		return -1;
	}

	printf("Found %d conflict(s). Analyzing...\n\n", conflicts.nr);

	for (i = 0; i < conflicts.nr; i++) {
		const char *path = conflicts.items[i].string;
		struct vibes_conflict conflict;
		enum conflict_type ctype;

		vibes_conflict_init(&conflict);
		conflict.file_path = xstrdup(path);

		/* Read conflict stages from the index */
		vibes_read_conflict_stage(&conflict.base, 1, path);
		vibes_read_conflict_stage(&conflict.ours, 2, path);
		vibes_read_conflict_stage(&conflict.theirs, 3, path);

		/* Classify the conflict */
		ctype = vibes_classify_conflict(conflict.ours.buf,
						conflict.theirs.buf,
						conflict.base.buf, path);
		conflict.type = ctype;

		printf("  %s: %s", path,
		       ctype == CONFLICT_SAFE_AUTO ? "auto-resolvable" :
		       ctype == CONFLICT_SEMANTIC ? "semantic" :
		       "textual");

		if (ctype == CONFLICT_SAFE_AUTO) {
			if (vibes_auto_resolve(&conflict) == 0) {
				/* Write resolved content back to working tree */
				FILE *f = fopen(path, "w");
				if (f) {
					fwrite(conflict.resolved.buf, 1,
					       conflict.resolved.len, f);
					fclose(f);
					mark_resolved(path);
				}
				printf(" -> resolved\n");
				auto_resolved++;
			} else {
				printf(" -> resolution failed\n");
				remaining++;
			}
		} else {
			printf(" -> manual resolution needed\n");
			remaining++;
		}

		vibes_conflict_free(&conflict);
	}

	printf("\nMerge summary:\n");
	printf("  Auto-resolved: %d\n", auto_resolved);
	printf("  Remaining:     %d\n", remaining);

	if (remaining > 0)
		printf("\nUse 'git vibes-merge --candidates <file>' for AI "
		       "resolution suggestions.\n");

	string_list_clear(&conflicts, 0);
	return remaining > 0 ? 1 : 0;
}

#endif /* VIBES_ENABLED */
