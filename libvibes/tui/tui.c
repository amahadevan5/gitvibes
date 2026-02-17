#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <curses.h>
#include <signal.h>
#include "libvibes/tui/tui.h"
#include "libvibes/storage/db.h"

static struct vibes_tui *g_tui;

static void handle_resize(int sig)
{
	(void)sig;
	if (g_tui) {
		endwin();
		refresh();
		getmaxyx(stdscr, g_tui->term_rows, g_tui->term_cols);
		vibes_tui_refresh(g_tui);
	}
}

static void draw_header(struct vibes_tui *tui)
{
	static const char *view_names[] = {
		"Dashboard", "Agents", "Intents", "Branches",
		"Conflicts", "Input"
	};
	int i;

	werase(tui->header_win);
	vibes_set_color(tui->header_win, VIBES_COLOR_HEADER);

	/* Fill header background */
	for (i = 0; i < tui->term_cols; i++)
		waddch(tui->header_win, ' ');

	wmove(tui->header_win, 0, 1);
	wattron(tui->header_win, A_BOLD);
	wprintw(tui->header_win, "gitvibes");
	wattroff(tui->header_win, A_BOLD);
	wprintw(tui->header_win, " | ");

	/* View tabs */
	for (i = 0; i < VIBES_VIEW_INPUT; i++) {
		if (i == (int)tui->current_view) {
			wattron(tui->header_win, A_REVERSE);
			wprintw(tui->header_win, " %d:%s ",
				i + 1, view_names[i]);
			wattroff(tui->header_win, A_REVERSE);
		} else {
			wprintw(tui->header_win, " %d:%s ",
				i + 1, view_names[i]);
		}
	}

	vibes_unset_color(tui->header_win, VIBES_COLOR_HEADER);
	wrefresh(tui->header_win);
}

static void draw_status_bar(struct vibes_tui *tui)
{
	int y = tui->term_rows - 1;

	werase(tui->status_win);
	vibes_set_color(tui->status_win, VIBES_COLOR_STATUS);

	/* Fill status bar */
	{
		int i;
		for (i = 0; i < tui->term_cols; i++)
			waddch(tui->status_win, ' ');
	}

	wmove(tui->status_win, 0, 1);
	wprintw(tui->status_win,
		"q:Quit  1-5:View  Tab:Next  /:Vibe  ?:Help");

	(void)y;
	vibes_unset_color(tui->status_win, VIBES_COLOR_STATUS);
	wrefresh(tui->status_win);
}

static void render_current_view(struct vibes_tui *tui)
{
	werase(tui->main_win);

	switch (tui->current_view) {
	case VIBES_VIEW_DASHBOARD:
		vibes_dashboard_render(tui);
		break;
	case VIBES_VIEW_AGENTS:
		vibes_agent_panel_render(tui);
		break;
	case VIBES_VIEW_INTENTS:
		vibes_intent_timeline_render(tui);
		break;
	case VIBES_VIEW_BRANCHES:
		vibes_branch_view_render(tui);
		break;
	case VIBES_VIEW_CONFLICTS:
		vibes_conflict_view_render(tui);
		break;
	case VIBES_VIEW_INPUT:
		vibes_input_render(tui);
		break;
	default:
		break;
	}

	wrefresh(tui->main_win);
}

int vibes_tui_init(struct vibes_tui *tui, struct vibes_db *db,
		   struct repository *repo)
{
	memset(tui, 0, sizeof(*tui));
	tui->db = db;
	tui->repo = repo;
	tui->running = 1;
	tui->current_view = VIBES_VIEW_DASHBOARD;

	/* Initialize ncurses */
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	timeout(1000); /* 1s refresh interval */

	vibes_colors_init();

	getmaxyx(stdscr, tui->term_rows, tui->term_cols);

	/* Create windows: header (1 row), main (middle), status (1 row) */
	tui->header_win = newwin(1, tui->term_cols, 0, 0);
	tui->main_win = newwin(tui->term_rows - 2, tui->term_cols, 1, 0);
	tui->status_win = newwin(1, tui->term_cols, tui->term_rows - 1, 0);

	/* Handle terminal resize */
	g_tui = tui;
	signal(SIGWINCH, handle_resize);

	return 0;
}

void vibes_tui_refresh(struct vibes_tui *tui)
{
	/* Recreate windows on resize */
	if (tui->header_win) delwin(tui->header_win);
	if (tui->main_win) delwin(tui->main_win);
	if (tui->status_win) delwin(tui->status_win);

	tui->header_win = newwin(1, tui->term_cols, 0, 0);
	tui->main_win = newwin(tui->term_rows - 2, tui->term_cols, 1, 0);
	tui->status_win = newwin(1, tui->term_cols, tui->term_rows - 1, 0);

	draw_header(tui);
	render_current_view(tui);
	draw_status_bar(tui);
}

int vibes_tui_run(struct vibes_tui *tui)
{
	int ch;

	vibes_tui_refresh(tui);

	while (tui->running) {
		ch = getch();

		if (ch == ERR) {
			/* Timeout — refresh data */
			render_current_view(tui);
			continue;
		}

		switch (ch) {
		case 'q':
		case 'Q':
			tui->running = 0;
			break;

		case '1':
			tui->current_view = VIBES_VIEW_DASHBOARD;
			draw_header(tui);
			render_current_view(tui);
			break;
		case '2':
			tui->current_view = VIBES_VIEW_AGENTS;
			draw_header(tui);
			render_current_view(tui);
			break;
		case '3':
			tui->current_view = VIBES_VIEW_INTENTS;
			draw_header(tui);
			render_current_view(tui);
			break;
		case '4':
			tui->current_view = VIBES_VIEW_BRANCHES;
			draw_header(tui);
			render_current_view(tui);
			break;
		case '5':
			tui->current_view = VIBES_VIEW_CONFLICTS;
			draw_header(tui);
			render_current_view(tui);
			break;

		case '\t': /* Tab — next view */
			tui->current_view =
				(tui->current_view + 1) % VIBES_VIEW_INPUT;
			draw_header(tui);
			render_current_view(tui);
			break;

		case '/':
		case 'v':
			tui->current_view = VIBES_VIEW_INPUT;
			curs_set(1);
			draw_header(tui);
			render_current_view(tui);
			break;

		case 27: /* Escape — back to dashboard */
			if (tui->current_view == VIBES_VIEW_INPUT)
				curs_set(0);
			tui->current_view = VIBES_VIEW_DASHBOARD;
			draw_header(tui);
			render_current_view(tui);
			break;

		default:
			if (tui->current_view == VIBES_VIEW_INPUT) {
				if (vibes_input_handle_key(tui, ch))
					render_current_view(tui);
			}
			break;
		}
	}

	return 0;
}

void vibes_tui_shutdown(struct vibes_tui *tui)
{
	g_tui = NULL;
	signal(SIGWINCH, SIG_DFL);

	if (tui->header_win) delwin(tui->header_win);
	if (tui->main_win) delwin(tui->main_win);
	if (tui->status_win) delwin(tui->status_win);

	endwin();
}

#endif /* VIBES_ENABLED */
