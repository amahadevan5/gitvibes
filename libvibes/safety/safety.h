#ifndef LIBVIBES_SAFETY_SAFETY_H
#define LIBVIBES_SAFETY_SAFETY_H
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "string-list.h"
#include <stdint.h>

struct vibes_db;
struct repository;

/*
 * A validation gate.
 */
struct vibes_gate {
	char *name;
	char *command;
	int required;   /* 1 = must pass, 0 = advisory */
};

/*
 * Gate result.
 */
struct gate_result {
	char *gate_name;
	int passed;
	char *output;
};

/*
 * A safety snapshot for rollback.
 */
struct vibes_snapshot {
	char id[27];
	char *label;
	char *head_ref;
	int64_t created_at;
};

/* --- Validation gates (gates.c) --- */

int vibes_gate_run(const char *name, const char *command,
		   struct gate_result *result);
int vibes_gate_run_all(struct repository *repo, struct vibes_db *db,
		       struct gate_result **results, int *nr_results);
void vibes_gate_result_free(struct gate_result *result);

/* --- Safety snapshots (snapshot.c) --- */

int vibes_safety_snapshot(struct vibes_db *db, struct repository *repo,
			  const char *label);

/* --- Rollback (rollback.c) --- */

int vibes_rollback_last(struct vibes_db *db, struct repository *repo);
int vibes_rollback_intent(struct vibes_db *db, struct repository *repo,
			  const char *intent_id);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_SAFETY_SAFETY_H */
