#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "diff.h"
#include "diffcore.h"
#include "hash.h"
#include "hex.h"
#include "object.h"
#include "object-name.h"
#include "repository.h"
#include "revision.h"
#include "strbuf.h"
#include "run-command.h"
#include "libvibes/smart-commit/smart-commit.h"

/*
 * Diff analyzer: extracts structured information about staged changes
 * using git's internal diff API for metadata and subprocess for text.
 */

static void collect_staged_files(struct diff_queue_struct *q,
				 struct diff_options *options UNUSED,
				 void *data)
{
	struct vibes_changeset *cs = data;
	int i;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		struct vibes_file_change *fc;

		fc = xcalloc(1, sizeof(*fc));
		fc->path = xstrdup(p->two->path);
		fc->status = p->status;
		strbuf_init(&fc->diff_text, 0);

		/* Link into changeset list */
		fc->next = cs->files;
		cs->files = fc;
		cs->nr_files++;
	}
}

/*
 * Get diff text for a specific file via git diff --cached subprocess.
 */
static int get_file_diff(const char *path, struct strbuf *out)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	int ret;

	strvec_pushl(&cp.args, "diff", "--cached", "--no-color",
		     "-U3", "--", path, NULL);
	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.out = -1;

	ret = start_command(&cp);
	if (ret)
		return -1;

	strbuf_read(out, cp.out, 0);
	close(cp.out);

	return finish_command(&cp);
}

/*
 * Count added/deleted lines from diff text (lines starting with +/-)
 * excluding the diff header lines (--- and +++).
 */
static void count_diff_lines(const char *diff_text,
			     int *added, int *deleted)
{
	const char *p = diff_text;
	*added = 0;
	*deleted = 0;

	while (*p) {
		if (p[0] == '+' && p[1] != '+')
			(*added)++;
		else if (p[0] == '-' && p[1] != '-')
			(*deleted)++;

		/* Skip to next line */
		while (*p && *p != '\n')
			p++;
		if (*p == '\n')
			p++;
	}
}

int vibes_analyze_staged(struct vibes_changeset *cs)
{
	struct rev_info rev;
	struct vibes_file_change *fc;
	struct object_id head_oid;

	memset(cs, 0, sizeof(*cs));

	if (repo_read_index(the_repository) < 0)
		return error("gitvibes: failed to read index");

	repo_init_revisions(the_repository, &rev, NULL);
	rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = collect_staged_files;
	rev.diffopt.format_callback_data = cs;

	/* Handle initial commit (no HEAD): diff against empty tree */
	if (repo_get_oid(the_repository, "HEAD", &head_oid)) {
		struct object *obj;
		obj = parse_object(the_repository,
				   the_repository->hash_algo->empty_tree);
		if (!obj)
			return error("gitvibes: failed to find empty tree");
		add_pending_object(&rev, obj, "");
	} else {
		add_head_to_pending(&rev);
	}

	diff_setup_done(&rev.diffopt);
	run_diff_index(&rev, DIFF_INDEX_CACHED);

	if (cs->nr_files == 0) {
		release_revisions(&rev);
		return error("gitvibes: nothing staged to commit");
	}

	/* Collect diff text and line counts per file */
	for (fc = cs->files; fc; fc = fc->next) {
		get_file_diff(fc->path, &fc->diff_text);
		count_diff_lines(fc->diff_text.buf,
				 &fc->lines_added, &fc->lines_deleted);
		cs->total_added += fc->lines_added;
		cs->total_deleted += fc->lines_deleted;
	}

	release_revisions(&rev);
	return 0;
}

void vibes_changeset_free(struct vibes_changeset *cs)
{
	struct vibes_file_change *fc = cs->files;
	while (fc) {
		struct vibes_file_change *next = fc->next;
		free(fc->path);
		strbuf_release(&fc->diff_text);
		free(fc);
		fc = next;
	}
	memset(cs, 0, sizeof(*cs));
}

#endif /* VIBES_ENABLED */
