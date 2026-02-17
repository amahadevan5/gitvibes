#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "builtin.h"
#ifdef VIBES_ENABLED
#include "repository.h"

/*
 * git vibes-decompose is an alias for git vibes-commit.
 * Both analyze staged changes and create atomic commits.
 * The name "decompose" emphasizes retroactive splitting.
 */
int cmd_vibes_commit(int argc, const char **argv, const char *prefix,
		     struct repository *repo);

int cmd_vibes_decompose(int argc, const char **argv, const char *prefix,
			struct repository *repo)
{
	return cmd_vibes_commit(argc, argv, prefix, repo);
}
#else
int cmd_vibes_decompose(int argc, const char **argv, const char *prefix,
			struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
