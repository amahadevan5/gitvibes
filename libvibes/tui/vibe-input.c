#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <curses.h>
#include "strbuf.h"
#include "libvibes/tui/tui.h"
#include "libvibes/storage/db.h"
#include "libvibes/intent/intent.h"

static char input_buf[1024];
static int input_len;
static int input_submitted;

void vibes_input_render(struct vibes_tui *tui)
{
	WINDOW *w = tui->main_win;
	int row = 1;

	/* Title */
	vibes_set_color(w, VIBES_COLOR_ACCENT);
	wattron(w, A_BOLD);
	mvwprintw(w, row++, 2, "Vibe Input");
	wattroff(w, A_BOLD);
	vibes_unset_color(w, VIBES_COLOR_ACCENT);
	row++;

	mvwprintw(w, row++, 2,
		  "Type a natural language intent and press Enter.");
	mvwprintw(w, row++, 2,
		  "Press Escape to cancel.");
	row++;

	/* Input prompt */
	vibes_set_color(w, VIBES_COLOR_ACCENT);
	mvwprintw(w, row, 2, "> ");
	vibes_unset_color(w, VIBES_COLOR_ACCENT);

	/* Current input text */
	mvwprintw(w, row, 4, "%.*s", input_len, input_buf);

	/* Position cursor */
	wmove(w, row, 4 + input_len);

	row += 2;

	if (input_submitted) {
		vibes_set_color(w, VIBES_COLOR_SUCCESS);
		mvwprintw(w, row++, 2,
			  "Submitted: %.*s", input_len, input_buf);
		vibes_unset_color(w, VIBES_COLOR_SUCCESS);
		mvwprintw(w, row++, 2,
			  "(Run 'git vibes \"%.*s\"' to execute)",
			  input_len, input_buf);
		input_submitted = 0;
	}

	/* History hint */
	row += 2;
	vibes_set_color(w, VIBES_COLOR_MUTED);
	mvwprintw(w, row++, 2, "Examples:");
	mvwprintw(w, row++, 4, "\"add JWT authentication\"");
	mvwprintw(w, row++, 4, "\"fix the login page redirect bug\"");
	mvwprintw(w, row++, 4, "\"refactor database layer to use connection pool\"");
	mvwprintw(w, row++, 4, "\"add unit tests for the payment module\"");
	vibes_unset_color(w, VIBES_COLOR_MUTED);
}

int vibes_input_handle_key(struct vibes_tui *tui, int ch)
{
	(void)tui;

	switch (ch) {
	case '\n':
	case '\r':
		/* Submit input */
		if (input_len > 0) {
			input_buf[input_len] = '\0';
			input_submitted = 1;
		}
		return 1;

	case KEY_BACKSPACE:
	case 127:
	case 8:
		/* Delete last character */
		if (input_len > 0) {
			input_len--;
			input_buf[input_len] = '\0';
		}
		return 1;

	case KEY_DC:
		/* Clear all */
		input_len = 0;
		input_buf[0] = '\0';
		return 1;

	default:
		/* Add character */
		if (ch >= 32 && ch < 127 &&
		    input_len < (int)sizeof(input_buf) - 1) {
			input_buf[input_len++] = ch;
			input_buf[input_len] = '\0';
			return 1;
		}
		return 0;
	}
}

#endif /* VIBES_ENABLED */
