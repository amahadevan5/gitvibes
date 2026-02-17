#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <curses.h>
#include <sqlite3.h>
#include "libvibes/tui/tui.h"
#include "libvibes/storage/db.h"

/*
 * Conflict view: file tree with conflict probability colors.
 */
void vibes_conflict_view_render(struct vibes_tui *tui)
{
	WINDOW *w = tui->main_win;
	sqlite3_stmt *stmt;
	int row = 1;

	/* Title */
	vibes_set_color(w, VIBES_COLOR_ACCENT);
	wattron(w, A_BOLD);
	mvwprintw(w, row++, 2, "Conflict Predictions");
	wattroff(w, A_BOLD);
	vibes_unset_color(w, VIBES_COLOR_ACCENT);
	row++;

	/* Find tasks with overlapping estimated files */
	stmt = vibes_db_prepare(tui->db,
		"SELECT a.id, a.title, b.id, b.title, "
		"  a.estimated_files, b.estimated_files "
		"FROM tasks a JOIN tasks b ON a.id < b.id "
		"WHERE a.status IN ('pending','claimed','in_progress') "
		"AND b.status IN ('pending','claimed','in_progress') "
		"LIMIT 20;");
	if (stmt) {
		int has_conflicts = 0;

		wattron(w, A_BOLD);
		mvwprintw(w, row, 2, "%-12s  %-12s  %-20s  %s",
			  "TASK A", "TASK B", "A TITLE", "B TITLE");
		wattroff(w, A_BOLD);
		row++;

		vibes_set_color(w, VIBES_COLOR_MUTED);
		mvwprintw(w, row, 2, "%-12s  %-12s  %-20s  %s",
			  "------------", "------------",
			  "--------------------",
			  "--------------------");
		vibes_unset_color(w, VIBES_COLOR_MUTED);
		row++;

		while (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *id_a =
				(const char *)sqlite3_column_text(stmt, 0);
			const char *title_a =
				(const char *)sqlite3_column_text(stmt, 1);
			const char *id_b =
				(const char *)sqlite3_column_text(stmt, 2);
			const char *title_b =
				(const char *)sqlite3_column_text(stmt, 3);
			const char *files_a =
				(const char *)sqlite3_column_text(stmt, 4);
			const char *files_b =
				(const char *)sqlite3_column_text(stmt, 5);
			int overlap = 0;

			/* Simple overlap check: see if any file appears
			 * in both JSON arrays */
			if (files_a && files_b && strstr(files_a, files_b))
				overlap = 1;

			if (overlap) {
				vibes_set_color(w, VIBES_COLOR_ERROR);
			} else {
				vibes_set_color(w, VIBES_COLOR_SUCCESS);
			}

			mvwprintw(w, row, 2, "%.12s  %.12s  %-20.20s  %s",
				  id_a ? id_a : "?",
				  id_b ? id_b : "?",
				  title_a ? title_a : "?",
				  title_b ? title_b : "?");

			vibes_unset_color(w, VIBES_COLOR_ERROR);
			vibes_unset_color(w, VIBES_COLOR_SUCCESS);

			row++;
			has_conflicts = 1;

			if (row >= tui->term_rows - 3)
				break;
		}
		sqlite3_finalize(stmt);

		if (!has_conflicts) {
			vibes_set_color(w, VIBES_COLOR_SUCCESS);
			mvwprintw(w, row++, 4,
				  "No active task pairs to analyze.");
			vibes_unset_color(w, VIBES_COLOR_SUCCESS);
		}
	}

	/* File lock conflicts */
	row += 2;
	vibes_set_color(w, VIBES_COLOR_ACCENT);
	mvwprintw(w, row++, 2, "Currently Locked Files:");
	vibes_unset_color(w, VIBES_COLOR_ACCENT);

	stmt = vibes_db_prepare(tui->db,
		"SELECT file_path, agent_id FROM file_locks "
		"ORDER BY locked_at DESC LIMIT 15;");
	if (stmt) {
		int has_locks = 0;
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *path =
				(const char *)sqlite3_column_text(stmt, 0);
			const char *agent =
				(const char *)sqlite3_column_text(stmt, 1);

			vibes_set_color(w, VIBES_COLOR_WARNING);
			mvwprintw(w, row++, 4, "%-50s  agent:%.8s",
				  path ? path : "?",
				  agent ? agent : "?");
			vibes_unset_color(w, VIBES_COLOR_WARNING);
			has_locks = 1;

			if (row >= tui->term_rows - 3)
				break;
		}
		sqlite3_finalize(stmt);

		if (!has_locks) {
			vibes_set_color(w, VIBES_COLOR_MUTED);
			mvwprintw(w, row++, 4, "(none)");
			vibes_unset_color(w, VIBES_COLOR_MUTED);
		}
	}
}

#endif /* VIBES_ENABLED */
