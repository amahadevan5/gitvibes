#ifndef LIBVIBES_INTENT_INTENT_H
#define LIBVIBES_INTENT_INTENT_H
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "string-list.h"
#include "libvibes/vibes.h"

struct repository;
struct vibes_db;

/*
 * Intent types.
 */
enum intent_type {
	INTENT_FEATURE = 0,
	INTENT_BUGFIX,
	INTENT_REFACTOR,
	INTENT_DOCS,
	INTENT_CHORE,
};

enum intent_status {
	INTENT_CAPTURED = 0,
	INTENT_DECOMPOSED,
	INTENT_IN_PROGRESS,
	INTENT_COMPLETED,
	INTENT_ABANDONED,
};

enum task_status {
	TASK_PENDING = 0,
	TASK_CLAIMED,
	TASK_IN_PROGRESS,
	TASK_COMPLETED,
	TASK_FAILED,
};

/*
 * A vibes intent: a natural language development request
 * parsed into structured form.
 */
struct vibes_intent {
	char id[VIBES_ULID_LEN];
	char *parent_id;
	enum intent_type type;
	enum intent_status status;

	/* Natural language */
	char *raw_input;
	char *parsed_goal;
	struct string_list criteria;
	struct string_list constraints;

	/* Task graph */
	struct vibes_task *tasks;
	int nr_tasks;

	/* Tracking */
	char session_id[VIBES_ULID_LEN];
	int64_t created_at;
	int64_t started_at;
	int64_t completed_at;
};

/*
 * A decomposed task within an intent.
 */
struct vibes_task {
	char id[VIBES_ULID_LEN];
	char *intent_id;
	char *title;
	char *description;
	struct string_list dependencies;
	struct string_list est_files;
	char *assigned_agent;
	char *worktree_path;
	char *branch;
	enum task_status status;
	int wave_number;
	struct vibes_task *next;
};

/* Convert string to intent_type */
enum intent_type intent_type_from_str(const char *s);
const char *intent_type_to_str(enum intent_type t);
const char *intent_status_to_str(enum intent_status s);

/* Intent CRUD */
void vibes_intent_init(struct vibes_intent *intent);
void vibes_intent_free(struct vibes_intent *intent);
int vibes_intent_create(struct vibes_db *db, struct vibes_intent *intent);
int vibes_intent_get(struct vibes_db *db, const char *id,
		     struct vibes_intent *intent);
int vibes_intent_update_status(struct vibes_db *db, const char *id,
			       enum intent_status status);

/* Task CRUD */
void vibes_task_init(struct vibes_task *task);
void vibes_task_free(struct vibes_task *task);
void vibes_task_list_free(struct vibes_task *head);

/* NL intent parser (parser.c) */
int vibes_parse_intent(struct vibes_intent *intent, struct repository *repo);

/* Task decomposer (decomposer.c) */
int vibes_decompose_intent(struct vibes_intent *intent,
			   struct vibes_db *db,
			   struct repository *repo);

/* Full pipeline: parse → decompose → print plan (vibes-pipeline.c) */
int vibes_pipeline_run(const char *raw_input,
		       struct vibes_db *db,
		       struct repository *repo);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_INTENT_INTENT_H */
