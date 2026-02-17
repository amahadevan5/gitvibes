#include "git-compat-util.h"
#ifdef VIBES_ENABLED

/*
 * Retroactive commit decomposition.
 * This is essentially vibes-commit applied to already-staged changes,
 * splitting them into atomic commits. The core logic is shared with
 * the clusterer and message generator - this file just provides
 * the vibes-decompose entry point wrapper.
 *
 * For now this is a thin wrapper; the real work is done by
 * vibes_analyze_staged() + vibes_cluster_changes() + vibes_generate_commit_message()
 * which are called from builtin/vibes-decompose.c
 */

#endif /* VIBES_ENABLED */
