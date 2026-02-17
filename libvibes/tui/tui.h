#ifndef LIBVIBES_TUI_TUI_H
#define LIBVIBES_TUI_TUI_H
#ifdef VIBES_ENABLED

#include <curses.h>
#include "string-list.h"

struct vibes_db;
struct repository;

/*
 * Color pair IDs for the TUI.
 */
enum vibes_color {
	VIBES_COLOR_DEFAULT = 0,
	VIBES_COLOR_HEADER,       /* Header bar */
	VIBES_COLOR_STATUS,       /* Status bar */
	VIBES_COLOR_SUCCESS,      /* Green - completed, passed */
	VIBES_COLOR_WARNING,      /* Yellow - in progress, caution */
	VIBES_COLOR_ERROR,        /* Red - failed, blocked */
	VIBES_COLOR_ACCENT,       /* Cyan - highlights, links */
	VIBES_COLOR_MUTED,        /* Dim gray - secondary text */
	VIBES_COLOR_SELECTED,     /* Selected row/item */
	VIBES_COLOR_PANEL_BORDER, /* Panel borders */
};

/*
 * TUI panel/view identifiers.
 */
enum vibes_view {
	VIBES_VIEW_DASHBOARD = 0,
	VIBES_VIEW_AGENTS,
	VIBES_VIEW_INTENTS,
	VIBES_VIEW_BRANCHES,
	VIBES_VIEW_CONFLICTS,
	VIBES_VIEW_INPUT,
	VIBES_VIEW_MAX
};

/*
 * TUI application state.
 */
struct vibes_tui {
	WINDOW *header_win;
	WINDOW *main_win;
	WINDOW *status_win;
	int term_rows, term_cols;
	enum vibes_view current_view;
	int running;
	struct vibes_db *db;
	struct repository *repo;
};

/* --- Core TUI (tui.c) --- */

int vibes_tui_init(struct vibes_tui *tui, struct vibes_db *db,
		   struct repository *repo);
int vibes_tui_run(struct vibes_tui *tui);
void vibes_tui_shutdown(struct vibes_tui *tui);
void vibes_tui_refresh(struct vibes_tui *tui);

/* --- Colors (colors.c) --- */

void vibes_colors_init(void);
void vibes_set_color(WINDOW *win, enum vibes_color color);
void vibes_unset_color(WINDOW *win, enum vibes_color color);

/* --- Dashboard view (dashboard-view.c) --- */

void vibes_dashboard_render(struct vibes_tui *tui);

/* --- Agent panel (agent-panel.c) --- */

void vibes_agent_panel_render(struct vibes_tui *tui);

/* --- Branch view (branch-view.c) --- */

void vibes_branch_view_render(struct vibes_tui *tui);

/* --- Conflict view (conflict-view.c) --- */

void vibes_conflict_view_render(struct vibes_tui *tui);

/* --- Intent timeline (intent-timeline.c) --- */

void vibes_intent_timeline_render(struct vibes_tui *tui);

/* --- Vibe input (vibe-input.c) --- */

void vibes_input_render(struct vibes_tui *tui);
int vibes_input_handle_key(struct vibes_tui *tui, int ch);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_TUI_TUI_H */
