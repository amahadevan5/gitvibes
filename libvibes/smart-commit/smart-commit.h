#ifndef LIBVIBES_SMART_COMMIT_H
#define LIBVIBES_SMART_COMMIT_H
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "string-list.h"

struct repository;

/*
 * A single file change in the staging area.
 */
struct vibes_file_change {
	char *path;
	char status;               /* 'M', 'A', 'D', 'R', 'C' */
	struct strbuf diff_text;   /* unified diff text */
	int lines_added;
	int lines_deleted;
	struct vibes_file_change *next;
};

/*
 * A set of staged changes, extracted by vibes_analyze_staged().
 */
struct vibes_changeset {
	struct vibes_file_change *files; /* linked list */
	int nr_files;
	int total_added;
	int total_deleted;
};

/*
 * A cluster of related files that should be committed together.
 */
struct vibes_commit_cluster {
	char *type;                /* Conventional Commit type: feat, fix, etc. */
	char *scope;               /* Scope: module or directory name */
	struct string_list files;  /* file paths in this cluster */
	struct strbuf diff_text;   /* combined diff for all files */
	char *message;             /* generated commit message (NULL until generated) */
	struct vibes_commit_cluster *next;
};

/*
 * Analyze staged changes using git's diff API.
 * Fills in cs with file metadata and diff text.
 * Returns 0 on success, -1 on error (including "nothing staged").
 */
int vibes_analyze_staged(struct vibes_changeset *cs);

/*
 * Free a changeset and all its file changes.
 */
void vibes_changeset_free(struct vibes_changeset *cs);

/*
 * Group related file changes into logical commit clusters.
 * Returns a linked list of clusters (caller must free with vibes_clusters_free).
 */
struct vibes_commit_cluster *vibes_cluster_changes(const struct vibes_changeset *cs);

/*
 * Free a linked list of clusters.
 */
void vibes_clusters_free(struct vibes_commit_cluster *head);

/*
 * Generate a commit message for a cluster using AI.
 * Populates cluster->message. Returns 0 on success.
 */
int vibes_generate_commit_message(struct vibes_commit_cluster *cluster,
				  const struct vibes_changeset *cs,
				  struct repository *repo);

/*
 * Count the number of clusters in a linked list.
 */
static inline int vibes_cluster_count(const struct vibes_commit_cluster *head)
{
	int n = 0;
	for (; head; head = head->next)
		n++;
	return n;
}

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_SMART_COMMIT_H */
