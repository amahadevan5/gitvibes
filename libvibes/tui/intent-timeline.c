#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <curses.h>
#include <sqlite3.h>
#include "libvibes/tui/tui.h"
#include "libvibes/storage/db.h"

/*
 * Intent timeline: chronological list of intents with status badges.
 */
void vibes_intent_timeline_render(struct vibes_tui *tui)
{
	WINDOW *w = tui->main_win;
	sqlite3_stmt *stmt;
	int row = 1;

	/* Title */
	vibes_set_color(w, VIBES_COLOR_ACCENT);
	wattron(w, A_BOLD);
	mvwprintw(w, row++, 2, "Intent Timeline");
	wattroff(w, A_BOLD);
	vibes_unset_color(w, VIBES_COLOR_ACCENT);
	row++;

	/* Column headers */
	wattron(w, A_BOLD);
	mvwprintw(w, row, 2, "%-12s  %-8s  %-10s  %s",
		  "ID", "TYPE", "STATUS", "INPUT");
	wattroff(w, A_BOLD);
	row++;

	vibes_set_color(w, VIBES_COLOR_MUTED);
	mvwprintw(w, row, 2, "%-12s  %-8s  %-10s  %s",
		  "------------", "--------", "----------",
		  "------------------------------------");
	vibes_unset_color(w, VIBES_COLOR_MUTED);
	row++;

	stmt = vibes_db_prepare(tui->db,
		"SELECT id, type, status, raw_input FROM intents "
		"ORDER BY created_at DESC LIMIT 30;");
	if (stmt) {
		int has_intents = 0;
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *id =
				(const char *)sqlite3_column_text(stmt, 0);
			const char *type =
				(const char *)sqlite3_column_text(stmt, 1);
			const char *status =
				(const char *)sqlite3_column_text(stmt, 2);
			const char *input =
				(const char *)sqlite3_column_text(stmt, 3);

			/* Status badge with color */
			mvwprintw(w, row, 2, "%.12s  ",
				  id ? id : "?");
			mvwprintw(w, row, 16, "%-8s  ",
				  type ? type : "?");

			if (status && !strcmp(status, "completed")) {
				vibes_set_color(w, VIBES_COLOR_SUCCESS);
			} else if (status &&
				   !strcmp(status, "in_progress")) {
				vibes_set_color(w, VIBES_COLOR_WARNING);
			} else if (status &&
				   !strcmp(status, "abandoned")) {
				vibes_set_color(w, VIBES_COLOR_ERROR);
			} else {
				vibes_set_color(w, VIBES_COLOR_MUTED);
			}
			mvwprintw(w, row, 26, "%-10s",
				  status ? status : "?");
			vibes_unset_color(w, VIBES_COLOR_SUCCESS);
			vibes_unset_color(w, VIBES_COLOR_WARNING);
			vibes_unset_color(w, VIBES_COLOR_ERROR);
			vibes_unset_color(w, VIBES_COLOR_MUTED);

			/* Truncate input to fit */
			if (input) {
				int maxlen = tui->term_cols - 40;
				if (maxlen > 0) {
					mvwprintw(w, row, 38, "%.*s",
						  maxlen, input);
				}
			}

			row++;
			has_intents = 1;

			/* Show tasks for this intent */
			if (id) {
				sqlite3_stmt *ts;
				ts = vibes_db_prepare(tui->db,
					"SELECT title, status FROM tasks "
					"WHERE intent_id = ? "
					"ORDER BY wave_number, id "
					"LIMIT 5;");
				if (ts) {
					sqlite3_bind_text(ts, 1, id, -1,
							  SQLITE_STATIC);
					while (sqlite3_step(ts) ==
					       SQLITE_ROW) {
						const char *title =
							(const char *)
							sqlite3_column_text(
								ts, 0);
						const char *tstatus =
							(const char *)
							sqlite3_column_text(
								ts, 1);
						vibes_set_color(w,
							VIBES_COLOR_MUTED);
						mvwprintw(w, row, 6,
							  "  %-10s %s",
							  tstatus ? tstatus
								 : "?",
							  title ? title
							       : "?");
						vibes_unset_color(w,
							VIBES_COLOR_MUTED);
						row++;
						if (row >=
						    tui->term_rows - 3)
							break;
					}
					sqlite3_finalize(ts);
				}
			}

			if (row >= tui->term_rows - 3)
				break;
		}
		sqlite3_finalize(stmt);

		if (!has_intents) {
			vibes_set_color(w, VIBES_COLOR_MUTED);
			mvwprintw(w, row, 4,
				  "(no intents - use 'git vibes \"...\"')");
			vibes_unset_color(w, VIBES_COLOR_MUTED);
		}
	}
}

#endif /* VIBES_ENABLED */
