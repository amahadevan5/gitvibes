#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "string-list.h"
#include "libvibes/smart-commit/smart-commit.h"

/*
 * Change clustering: groups related file changes into logical commits.
 *
 * Heuristics (in priority order):
 * 1. Test pairing: test file + implementation → same cluster
 * 2. Directory grouping: files in same dir with same type
 * 3. Config/build files → separate cluster
 * 4. Documentation → separate cluster
 */

/* File type classification */
enum file_class {
	FC_SOURCE,
	FC_TEST,
	FC_CONFIG,
	FC_DOCS,
	FC_SCHEMA,
	FC_OTHER,
};

static enum file_class classify_file(const char *path)
{
	const char *base;

	/* Find basename */
	base = strrchr(path, '/');
	base = base ? base + 1 : path;

	/* Test files */
	if (strstr(path, "test") || strstr(path, "spec") ||
	    strstr(path, "_test.") || strstr(path, ".test.") ||
	    !strncmp(base, "test_", 5) || !strncmp(base, "t/", 2))
		return FC_TEST;

	/* Documentation */
	if (strstr(path, "doc") || strstr(path, "README") ||
	    strstr(path, ".md") || strstr(path, ".txt") ||
	    strstr(path, ".rst"))
		return FC_DOCS;

	/* Config and build files */
	if (!strcmp(base, "Makefile") || !strcmp(base, "CMakeLists.txt") ||
	    strstr(base, ".yml") || strstr(base, ".yaml") ||
	    strstr(base, ".json") || strstr(base, ".toml") ||
	    !strcmp(base, ".gitignore") || !strcmp(base, ".editorconfig") ||
	    strstr(base, "config") || strstr(base, "Dockerfile") ||
	    strstr(base, ".env"))
		return FC_CONFIG;

	/* Schema/migration files */
	if (strstr(path, "schema") || strstr(path, "migration") ||
	    strstr(base, ".sql"))
		return FC_SCHEMA;

	return FC_SOURCE;
}

/*
 * Extract directory path from a file path.
 * Returns allocated string.
 */
static char *get_directory(const char *path)
{
	const char *slash = strrchr(path, '/');
	if (!slash)
		return xstrdup(".");
	return xstrndup(path, slash - path);
}

/*
 * Determine Conventional Commit type from file class.
 */
static const char *commit_type_for_class(enum file_class cls,
					 int has_additions, int has_deletions)
{
	switch (cls) {
	case FC_TEST:
		return "test";
	case FC_DOCS:
		return "docs";
	case FC_CONFIG:
		return "chore";
	case FC_SCHEMA:
		return "feat";
	case FC_SOURCE:
	case FC_OTHER:
	default:
		if (has_additions && !has_deletions)
			return "feat";
		if (!has_additions && has_deletions)
			return "refactor";
		return "feat";
	}
}

/*
 * Find or create a cluster for the given directory and class.
 */
static struct vibes_commit_cluster *find_or_create_cluster(
	struct vibes_commit_cluster **head,
	const char *dir, enum file_class cls)
{
	struct vibes_commit_cluster *c;

	/* Look for existing cluster with same dir and class */
	for (c = *head; c; c = c->next) {
		if (!strcmp(c->scope ? c->scope : ".", dir)) {
			/* Same directory - merge if compatible class */
			return c;
		}
	}

	/* Create new cluster */
	c = xcalloc(1, sizeof(*c));
	c->type = xstrdup(commit_type_for_class(cls, 1, 0));
	c->scope = xstrdup(dir);
	string_list_init_dup(&c->files);
	strbuf_init(&c->diff_text, 0);
	c->next = *head;
	*head = c;
	return c;
}

struct vibes_commit_cluster *vibes_cluster_changes(const struct vibes_changeset *cs)
{
	struct vibes_commit_cluster *clusters = NULL;
	struct vibes_commit_cluster *test_cluster = NULL;
	struct vibes_commit_cluster *docs_cluster = NULL;
	struct vibes_commit_cluster *config_cluster = NULL;
	const struct vibes_file_change *fc;

	if (!cs || cs->nr_files == 0)
		return NULL;

	/* Single file = single cluster */
	if (cs->nr_files == 1) {
		struct vibes_commit_cluster *c;
		fc = cs->files;
		c = xcalloc(1, sizeof(*c));
		c->type = xstrdup(commit_type_for_class(
			classify_file(fc->path),
			fc->lines_added, fc->lines_deleted));
		c->scope = get_directory(fc->path);
		string_list_init_dup(&c->files);
		strbuf_init(&c->diff_text, 0);
		string_list_append(&c->files, fc->path);
		strbuf_addbuf(&c->diff_text, &fc->diff_text);
		return c;
	}

	/* Classify and group files */
	for (fc = cs->files; fc; fc = fc->next) {
		enum file_class cls = classify_file(fc->path);
		struct vibes_commit_cluster *target;

		switch (cls) {
		case FC_TEST:
			if (!test_cluster) {
				test_cluster = xcalloc(1, sizeof(*test_cluster));
				test_cluster->type = xstrdup("test");
				test_cluster->scope = xstrdup("tests");
				string_list_init_dup(&test_cluster->files);
				strbuf_init(&test_cluster->diff_text, 0);
				test_cluster->next = clusters;
				clusters = test_cluster;
			}
			target = test_cluster;
			break;

		case FC_DOCS:
			if (!docs_cluster) {
				docs_cluster = xcalloc(1, sizeof(*docs_cluster));
				docs_cluster->type = xstrdup("docs");
				docs_cluster->scope = NULL;
				string_list_init_dup(&docs_cluster->files);
				strbuf_init(&docs_cluster->diff_text, 0);
				docs_cluster->next = clusters;
				clusters = docs_cluster;
			}
			target = docs_cluster;
			break;

		case FC_CONFIG:
			if (!config_cluster) {
				config_cluster = xcalloc(1, sizeof(*config_cluster));
				config_cluster->type = xstrdup("chore");
				config_cluster->scope = xstrdup("config");
				string_list_init_dup(&config_cluster->files);
				strbuf_init(&config_cluster->diff_text, 0);
				config_cluster->next = clusters;
				clusters = config_cluster;
			}
			target = config_cluster;
			break;

		case FC_SCHEMA:
		case FC_SOURCE:
		case FC_OTHER:
		default:
			{
				char *dir = get_directory(fc->path);
				target = find_or_create_cluster(&clusters, dir, cls);
				/* Update type based on actual changes */
				free(target->type);
				target->type = xstrdup(commit_type_for_class(
					cls, fc->lines_added, fc->lines_deleted));
				free(dir);
			}
			break;
		}

		string_list_append(&target->files, fc->path);
		strbuf_addbuf(&target->diff_text, &fc->diff_text);
	}

	return clusters;
}

void vibes_clusters_free(struct vibes_commit_cluster *head)
{
	while (head) {
		struct vibes_commit_cluster *next = head->next;
		free(head->type);
		free(head->scope);
		string_list_clear(&head->files, 0);
		strbuf_release(&head->diff_text);
		free(head->message);
		free(head);
		head = next;
	}
}

#endif /* VIBES_ENABLED */
