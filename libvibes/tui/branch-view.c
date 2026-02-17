#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <curses.h>
#include "run-command.h"
#include "strbuf.h"
#include "libvibes/tui/tui.h"
#include "libvibes/storage/db.h"

/*
 * Helper: find substring in bounded buffer.
 */
static const char *vibes_strnstr(const char *haystack, const char *needle,
				 int len)
{
	int nlen = strlen(needle);
	int i;

	if (nlen > len)
		return NULL;

	for (i = 0; i <= len - nlen; i++) {
		if (memcmp(haystack + i, needle, nlen) == 0)
			return haystack + i;
	}
	return NULL;
}

/*
 * Branch view: ASCII art branch topology with intent coloring.
 */
void vibes_branch_view_render(struct vibes_tui *tui)
{
	WINDOW *w = tui->main_win;
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf out = STRBUF_INIT;
	int row = 1;

	/* Title */
	vibes_set_color(w, VIBES_COLOR_ACCENT);
	wattron(w, A_BOLD);
	mvwprintw(w, row++, 2, "Branch Topology");
	wattroff(w, A_BOLD);
	vibes_unset_color(w, VIBES_COLOR_ACCENT);
	row++;

	/* Use git log --graph for ASCII branch visualization */
	strvec_pushl(&cp.args, "log", "--graph", "--oneline",
		     "--decorate", "--all", "-20", NULL);
	cp.git_cmd = 1;
	cp.out = -1;

	if (start_command(&cp) == 0) {
		strbuf_read(&out, cp.out, 4096);
		close(cp.out);
		finish_command(&cp);
	}

	if (out.len > 0) {
		const char *p = out.buf;
		const char *end = out.buf + out.len;

		while (p < end && row < tui->term_rows - 3) {
			const char *nl = memchr(p, '\n', end - p);
			int linelen = nl ? (int)(nl - p) : (int)(end - p);
			int maxlen = tui->term_cols - 4;

			if (linelen > maxlen)
				linelen = maxlen;

			/* Color vibes branches differently */
			if (vibes_strnstr(p, "vibes/", linelen)) {
				vibes_set_color(w, VIBES_COLOR_SUCCESS);
				mvwprintw(w, row, 2, "%.*s", linelen, p);
				vibes_unset_color(w, VIBES_COLOR_SUCCESS);
			} else {
				mvwprintw(w, row, 2, "%.*s", linelen, p);
			}

			row++;
			p = nl ? nl + 1 : end;
		}
	} else {
		vibes_set_color(w, VIBES_COLOR_MUTED);
		mvwprintw(w, row++, 4,
			  "(no commits yet or not in a git repo)");
		vibes_unset_color(w, VIBES_COLOR_MUTED);
	}

	strbuf_release(&out);

	(void)tui->db;
}

#endif /* VIBES_ENABLED */
