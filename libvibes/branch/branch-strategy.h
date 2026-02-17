#ifndef LIBVIBES_BRANCH_BRANCH_STRATEGY_H
#define LIBVIBES_BRANCH_BRANCH_STRATEGY_H
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "string-list.h"

struct vibes_db;
struct repository;

/*
 * Branch convention types.
 */
enum branch_convention {
	BRANCH_TRUNK_BASED = 0,
	BRANCH_GITHUB_FLOW,
	BRANCH_GITFLOW,
	BRANCH_STACKED,
	BRANCH_CUSTOM,
};

/*
 * Branch topology plan.
 */
struct branch_topology {
	char *main_branch;
	char *feature_branch;
	struct string_list task_branches;
	enum branch_convention convention;
};

/* --- Convention detection (convention-detect.c) --- */

enum branch_convention vibes_detect_convention(struct repository *repo);
const char *branch_convention_str(enum branch_convention c);

/* --- Topology planning (topology-plan.c) --- */

int vibes_plan_topology(struct vibes_db *db, const char *intent_id,
			enum branch_convention convention,
			struct branch_topology *topo);
void vibes_topology_free(struct branch_topology *topo);

/* --- Merge ordering (merge-order.c) --- */

int vibes_merge_order(struct vibes_db *db, const char *intent_id,
		      struct string_list *order);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_BRANCH_BRANCH_STRATEGY_H */
