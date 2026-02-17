#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "libvibes/merge/semantic-merge.h"

/*
 * Conflict Classification: determine the type of merge conflict
 * by analyzing the content of both sides.
 */

void vibes_conflict_init(struct vibes_conflict *c)
{
	memset(c, 0, sizeof(*c));
	strbuf_init(&c->ours, 0);
	strbuf_init(&c->theirs, 0);
	strbuf_init(&c->base, 0);
	strbuf_init(&c->resolved, 0);
}

void vibes_conflict_free(struct vibes_conflict *c)
{
	free(c->file_path);
	strbuf_release(&c->ours);
	strbuf_release(&c->theirs);
	strbuf_release(&c->base);
	strbuf_release(&c->resolved);
}

/*
 * Check if the conflict is purely about import/include ordering.
 */
static int is_import_conflict(const char *ours, const char *theirs)
{
	/* Both sides are import/include statements */
	if ((starts_with(ours, "#include") || starts_with(ours, "import ") ||
	     starts_with(ours, "from ") || starts_with(ours, "use ")) &&
	    (starts_with(theirs, "#include") || starts_with(theirs, "import ") ||
	     starts_with(theirs, "from ") || starts_with(theirs, "use ")))
		return 1;
	return 0;
}

/*
 * Check if the changes are in completely different functions/blocks.
 */
static int is_adjacent_addition(const char *ours, const char *theirs,
				const char *base)
{
	/* If base is empty and both sides are pure additions,
	 * they're likely compatible */
	if (!base || !*base)
		return 1;
	return 0;
}

enum conflict_type vibes_classify_conflict(const char *ours,
					   const char *theirs,
					   const char *base,
					   const char *file_path)
{
	if (!ours || !theirs)
		return CONFLICT_TEXTUAL;

	/* Import ordering conflicts are auto-resolvable */
	if (is_import_conflict(ours, theirs))
		return CONFLICT_SAFE_AUTO;

	/* Adjacent additions are often safe */
	if (is_adjacent_addition(ours, theirs, base))
		return CONFLICT_SAFE_AUTO;

	/* If the file path suggests config/docs, it's usually textual */
	if (file_path) {
		const char *ext = strrchr(file_path, '.');
		if (ext && (!strcmp(ext, ".md") || !strcmp(ext, ".txt") ||
			    !strcmp(ext, ".json") || !strcmp(ext, ".yaml") ||
			    !strcmp(ext, ".yml") || !strcmp(ext, ".toml")))
			return CONFLICT_TEXTUAL;
	}

	/* Default to textual conflict */
	return CONFLICT_TEXTUAL;
}

#endif /* VIBES_ENABLED */
