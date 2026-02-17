#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <curses.h>
#include "libvibes/tui/tui.h"

/*
 * Initialize color pairs for the TUI.
 * Dark theme inspired by Claude Code.
 */
void vibes_colors_init(void)
{
	if (!has_colors())
		return;

	start_color();
	use_default_colors();

	/* VIBES_COLOR_HEADER: white on dark blue */
	init_pair(VIBES_COLOR_HEADER, COLOR_WHITE, COLOR_BLUE);

	/* VIBES_COLOR_STATUS: black on white */
	init_pair(VIBES_COLOR_STATUS, COLOR_BLACK, COLOR_WHITE);

	/* VIBES_COLOR_SUCCESS: green */
	init_pair(VIBES_COLOR_SUCCESS, COLOR_GREEN, -1);

	/* VIBES_COLOR_WARNING: yellow */
	init_pair(VIBES_COLOR_WARNING, COLOR_YELLOW, -1);

	/* VIBES_COLOR_ERROR: red */
	init_pair(VIBES_COLOR_ERROR, COLOR_RED, -1);

	/* VIBES_COLOR_ACCENT: cyan */
	init_pair(VIBES_COLOR_ACCENT, COLOR_CYAN, -1);

	/* VIBES_COLOR_MUTED: default dimmed (we'll use A_DIM) */
	init_pair(VIBES_COLOR_MUTED, COLOR_WHITE, -1);

	/* VIBES_COLOR_SELECTED: black on cyan */
	init_pair(VIBES_COLOR_SELECTED, COLOR_BLACK, COLOR_CYAN);

	/* VIBES_COLOR_PANEL_BORDER: cyan */
	init_pair(VIBES_COLOR_PANEL_BORDER, COLOR_CYAN, -1);
}

void vibes_set_color(WINDOW *win, enum vibes_color color)
{
	if (!has_colors())
		return;

	wattron(win, COLOR_PAIR(color));
	if (color == VIBES_COLOR_MUTED)
		wattron(win, A_DIM);
	if (color == VIBES_COLOR_HEADER || color == VIBES_COLOR_STATUS)
		wattron(win, A_BOLD);
}

void vibes_unset_color(WINDOW *win, enum vibes_color color)
{
	if (!has_colors())
		return;

	wattroff(win, COLOR_PAIR(color));
	if (color == VIBES_COLOR_MUTED)
		wattroff(win, A_DIM);
	if (color == VIBES_COLOR_HEADER || color == VIBES_COLOR_STATUS)
		wattroff(win, A_BOLD);
}

#endif /* VIBES_ENABLED */
