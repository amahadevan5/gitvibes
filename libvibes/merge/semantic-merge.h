#ifndef LIBVIBES_MERGE_SEMANTIC_MERGE_H
#define LIBVIBES_MERGE_SEMANTIC_MERGE_H
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "string-list.h"

struct vibes_db;
struct repository;

/*
 * Conflict classification.
 */
enum conflict_type {
	CONFLICT_TEXTUAL = 0,   /* Same lines changed differently */
	CONFLICT_SEMANTIC,      /* Different symbols but behavioral overlap */
	CONFLICT_SAFE_AUTO,     /* Can be auto-resolved (import ordering, etc.) */
};

/*
 * A merge conflict with classification.
 */
struct vibes_conflict {
	char *file_path;
	enum conflict_type type;
	struct strbuf ours;     /* Our version */
	struct strbuf theirs;   /* Their version */
	struct strbuf base;     /* Common ancestor */
	struct strbuf resolved; /* Resolved content (if auto-resolved) */
	int auto_resolved;
};

/*
 * A resolution candidate for a conflict.
 */
struct resolution_candidate {
	struct strbuf content;
	double score;           /* 0.0 - 1.0 confidence */
	char *reasoning;
};

/* --- Conflict classification (conflict-classify.c) --- */

enum conflict_type vibes_classify_conflict(const char *ours,
					   const char *theirs,
					   const char *base,
					   const char *file_path);

/* --- Auto-resolution (auto-resolve.c) --- */

int vibes_auto_resolve(struct vibes_conflict *conflict);

/* --- Semantic merge (semantic-merge.c) --- */

int vibes_semantic_merge(struct repository *repo, const char *branch,
			 struct vibes_db *db);

/* --- LLM candidate generation (candidate-gen.c) --- */

int vibes_generate_candidates(struct vibes_db *db,
			      struct vibes_conflict *conflict,
			      struct resolution_candidate *candidates,
			      int max_candidates);
void vibes_candidate_free(struct resolution_candidate *candidate);

/* Read a conflict stage (1=base, 2=ours, 3=theirs) via git show */
int vibes_read_conflict_stage(struct strbuf *out, int stage, const char *path);

/* Utility */
void vibes_conflict_init(struct vibes_conflict *c);
void vibes_conflict_free(struct vibes_conflict *c);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_MERGE_SEMANTIC_MERGE_H */
