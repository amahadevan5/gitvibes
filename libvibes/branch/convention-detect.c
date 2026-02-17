#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "repository.h"
#include "strbuf.h"
#include "run-command.h"
#include "libvibes/branch/branch-strategy.h"

/*
 * Convention Detection: analyze existing branch names and merge
 * patterns to determine the repo's branching convention.
 */

const char *branch_convention_str(enum branch_convention c)
{
	switch (c) {
	case BRANCH_TRUNK_BASED: return "trunk-based";
	case BRANCH_GITHUB_FLOW: return "github-flow";
	case BRANCH_GITFLOW: return "gitflow";
	case BRANCH_STACKED: return "stacked";
	case BRANCH_CUSTOM: return "custom";
	}
	return "unknown";
}

enum branch_convention vibes_detect_convention(struct repository *repo)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf output = STRBUF_INIT;
	int has_develop = 0, has_release = 0, has_feature = 0;
	int has_main = 0, has_master = 0;
	int branch_count = 0;
	const char *p;

	strvec_pushl(&cp.args, "branch", "--list", NULL);
	cp.git_cmd = 1;
	cp.no_stdin = 1;

	if (capture_command(&cp, &output, 0) < 0) {
		strbuf_release(&output);
		return BRANCH_GITHUB_FLOW; /* safe default */
	}

	p = output.buf;
	while (*p) {
		const char *eol = strchr(p, '\n');
		if (!eol)
			eol = p + strlen(p);

		/* Skip the "* " prefix */
		while (p < eol && (*p == ' ' || *p == '*'))
			p++;

		if (eol > p) {
			if (!strncmp(p, "develop", 7))
				has_develop = 1;
			else if (!strncmp(p, "release/", 8))
				has_release = 1;
			else if (!strncmp(p, "feature/", 8))
				has_feature = 1;
			else if (!strncmp(p, "main", 4))
				has_main = 1;
			else if (!strncmp(p, "master", 6))
				has_master = 1;
			branch_count++;
		}
		p = *eol ? eol + 1 : eol;
	}
	strbuf_release(&output);

	/* Gitflow: has develop and release branches */
	if (has_develop && (has_release || has_feature))
		return BRANCH_GITFLOW;

	/* Trunk-based: only 1-2 branches total */
	if (branch_count <= 2)
		return BRANCH_TRUNK_BASED;

	/* Default to github-flow */
	return BRANCH_GITHUB_FLOW;
}

#endif /* VIBES_ENABLED */
