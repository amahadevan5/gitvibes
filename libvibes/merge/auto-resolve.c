#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "string-list.h"
#include "libvibes/merge/semantic-merge.h"

/*
 * Auto-resolve: handle SAFE_AUTO conflicts automatically.
 * Handles import deduplication, sorted includes, adjacent additions.
 */

/*
 * Merge two sets of import lines by combining and deduplicating.
 */
static int merge_imports(struct vibes_conflict *conflict)
{
	struct string_list lines = STRING_LIST_INIT_DUP;
	const char *p;
	struct strbuf line = STRBUF_INIT;

	/* Collect lines from 'ours' */
	p = conflict->ours.buf;
	while (*p) {
		const char *eol = strchr(p, '\n');
		if (!eol)
			eol = p + strlen(p);
		strbuf_reset(&line);
		strbuf_add(&line, p, eol - p);
		if (line.len > 0 && !string_list_has_string(&lines, line.buf))
			string_list_append(&lines, line.buf);
		p = *eol ? eol + 1 : eol;
	}

	/* Collect lines from 'theirs' */
	p = conflict->theirs.buf;
	while (*p) {
		const char *eol = strchr(p, '\n');
		if (!eol)
			eol = p + strlen(p);
		strbuf_reset(&line);
		strbuf_add(&line, p, eol - p);
		if (line.len > 0 && !string_list_has_string(&lines, line.buf))
			string_list_append(&lines, line.buf);
		p = *eol ? eol + 1 : eol;
	}

	/* Sort and output */
	string_list_sort(&lines);
	{
		int i;
		for (i = 0; i < lines.nr; i++) {
			strbuf_addstr(&conflict->resolved, lines.items[i].string);
			strbuf_addch(&conflict->resolved, '\n');
		}
	}

	strbuf_release(&line);
	string_list_clear(&lines, 0);
	conflict->auto_resolved = 1;
	return 0;
}

/*
 * Merge adjacent additions by concatenating both sides.
 */
static int merge_adjacent(struct vibes_conflict *conflict)
{
	/* Include base content first, then both additions */
	if (conflict->base.len)
		strbuf_addbuf(&conflict->resolved, &conflict->base);
	strbuf_addbuf(&conflict->resolved, &conflict->ours);
	strbuf_addbuf(&conflict->resolved, &conflict->theirs);
	conflict->auto_resolved = 1;
	return 0;
}

int vibes_auto_resolve(struct vibes_conflict *conflict)
{
	if (conflict->type != CONFLICT_SAFE_AUTO)
		return -1;

	strbuf_reset(&conflict->resolved);

	/* Try import merging */
	if (conflict->ours.len > 0 && conflict->theirs.len > 0) {
		const char *o = conflict->ours.buf;
		const char *t = conflict->theirs.buf;

		if ((starts_with(o, "#include") || starts_with(o, "import ") ||
		     starts_with(o, "from ") || starts_with(o, "use ")) &&
		    (starts_with(t, "#include") || starts_with(t, "import ") ||
		     starts_with(t, "from ") || starts_with(t, "use ")))
			return merge_imports(conflict);
	}

	/* Fall back to adjacent merge */
	return merge_adjacent(conflict);
}

#endif /* VIBES_ENABLED */
