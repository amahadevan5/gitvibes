#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "builtin.h"
#ifdef VIBES_ENABLED
#include "config.h"
#include "repository.h"
#include "parse-options.h"
#include "strbuf.h"
#include "run-command.h"
#include "string-list.h"
#include "libvibes/smart-commit/smart-commit.h"
#include "libvibes/ai/ai.h"

static const char * const vibes_commit_usage[] = {
	"git vibes-commit [--dry-run] [--no-ai] [--max-clusters <n>]",
	NULL
};

/*
 * Unstage all files in the index (prepare for per-cluster staging).
 * Handles initial commit case where HEAD doesn't exist.
 */
static int unstage_all(void)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	/* Try "git reset HEAD" first (works when HEAD exists) */
	strvec_pushl(&cp.args, "reset", "--quiet", "HEAD", NULL);
	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.no_stdout = 1;
	cp.no_stderr = 1;

	if (run_command(&cp) == 0)
		return 0;

	/* Initial commit: use "git rm --cached -r ." */
	{
		struct child_process rm = CHILD_PROCESS_INIT;
		strvec_pushl(&rm.args, "rm", "--cached", "-r", "--quiet", ".", NULL);
		rm.git_cmd = 1;
		rm.no_stdin = 1;
		rm.no_stdout = 1;
		rm.no_stderr = 1;
		return run_command(&rm);
	}
}

/*
 * Stage specific files and create a commit.
 * Assumes all files are already unstaged.
 */
static int do_commit(struct string_list *files, const char *message)
{
	struct child_process add = CHILD_PROCESS_INIT;
	struct child_process cp = CHILD_PROCESS_INIT;
	int i, ret;

	/* Stage just these files */
	strvec_push(&add.args, "add");
	strvec_push(&add.args, "--");
	for (i = 0; i < files->nr; i++)
		strvec_push(&add.args, files->items[i].string);
	add.git_cmd = 1;
	add.no_stdin = 1;
	ret = run_command(&add);
	if (ret)
		return error("gitvibes: failed to stage files for commit");

	/* Commit */
	strvec_push(&cp.args, "commit");
	strvec_push(&cp.args, "-m");
	strvec_push(&cp.args, message);
	strvec_push(&cp.args, "--no-verify");
	cp.git_cmd = 1;
	cp.no_stdin = 1;
	ret = run_command(&cp);

	if (ret)
		return error("gitvibes: commit failed");
	return 0;
}

/*
 * Fallback: generate a simple commit message without AI.
 */
static char *generate_fallback_message(const struct vibes_commit_cluster *c)
{
	struct strbuf msg = STRBUF_INIT;

	strbuf_addf(&msg, "%s", c->type ? c->type : "chore");
	if (c->scope && strcmp(c->scope, "."))
		strbuf_addf(&msg, "(%s)", c->scope);
	strbuf_addstr(&msg, ": update ");
	if (c->files.nr == 1)
		strbuf_addstr(&msg, c->files.items[0].string);
	else
		strbuf_addf(&msg, "%d files", c->files.nr);

	return strbuf_detach(&msg, NULL);
}

int cmd_vibes_commit(int argc, const char **argv, const char *prefix,
		     struct repository *repo)
{
	int dry_run = 0;
	int no_ai = 0;
	int max_clusters = 0;
	struct option options[] = {
		OPT_BOOL('n', "dry-run", &dry_run, "show what would be committed"),
		OPT_BOOL(0, "no-ai", &no_ai, "skip AI message generation"),
		OPT_INTEGER(0, "max-clusters", &max_clusters,
			    "maximum number of commit clusters"),
		OPT_END()
	};
	struct vibes_changeset cs;
	struct vibes_commit_cluster *clusters, *c;
	int nr_clusters = 0;
	int nr_committed = 0;

	argc = parse_options(argc, argv, prefix, options,
			     vibes_commit_usage, 0);

	/* 1. Analyze staged changes */
	if (vibes_analyze_staged(&cs) < 0)
		return 1;

	printf("Analyzed %d staged file(s) (+%d/-%d lines)\n",
	       cs.nr_files, cs.total_added, cs.total_deleted);

	/* 2. Cluster changes */
	clusters = vibes_cluster_changes(&cs);
	if (!clusters) {
		vibes_changeset_free(&cs);
		die("gitvibes: failed to cluster changes");
	}

	nr_clusters = vibes_cluster_count(clusters);
	if (max_clusters > 0 && nr_clusters > max_clusters) {
		/* TODO: merge smallest clusters */
		printf("Note: %d clusters (max %d requested, using all)\n",
		       nr_clusters, max_clusters);
	}

	printf("Grouped into %d commit cluster(s):\n\n", nr_clusters);

	/* 3. Generate messages and show plan */
	for (c = clusters; c; c = c->next) {
		int i;

		/* Generate commit message */
		if (!no_ai) {
			if (vibes_generate_commit_message(c, &cs, repo) < 0) {
				/* Fall back to non-AI message */
				c->message = generate_fallback_message(c);
			}
		} else {
			c->message = generate_fallback_message(c);
		}

		printf("  [%s] %s\n", c->type ? c->type : "?",
		       c->message ? c->message : "(no message)");
		for (i = 0; i < c->files.nr; i++)
			printf("    %s\n", c->files.items[i].string);
		printf("\n");
	}

	if (dry_run) {
		printf("(dry run — no commits created)\n");
		vibes_clusters_free(clusters);
		vibes_changeset_free(&cs);
		return 0;
	}

	/* 4. Unstage everything, then create commits per cluster */
	if (unstage_all() < 0) {
		error("gitvibes: failed to unstage files");
		vibes_clusters_free(clusters);
		vibes_changeset_free(&cs);
		return 1;
	}

	printf("Creating commits...\n");
	for (c = clusters; c; c = c->next) {
		if (!c->message || !c->files.nr)
			continue;

		if (do_commit(&c->files, c->message) < 0) {
			error("gitvibes: failed to create commit for cluster");
			break;
		}
		nr_committed++;
	}

	printf("\n%d atomic commit(s) created from %d file(s).\n",
	       nr_committed, cs.nr_files);

	vibes_clusters_free(clusters);
	vibes_changeset_free(&cs);
	return nr_committed > 0 ? 0 : 1;
}
#else
int cmd_vibes_commit(int argc, const char **argv, const char *prefix,
		     struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
