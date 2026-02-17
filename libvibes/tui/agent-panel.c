#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <curses.h>
#include <sqlite3.h>
#include "libvibes/tui/tui.h"
#include "libvibes/storage/db.h"

/*
 * Agent panel: per-agent rows with status, task, progress.
 */
void vibes_agent_panel_render(struct vibes_tui *tui)
{
	WINDOW *w = tui->main_win;
	sqlite3_stmt *stmt;
	int row = 1;
	int nr_agents = 0;

	/* Title */
	vibes_set_color(w, VIBES_COLOR_ACCENT);
	wattron(w, A_BOLD);
	mvwprintw(w, row++, 2, "Agents");
	wattroff(w, A_BOLD);
	vibes_unset_color(w, VIBES_COLOR_ACCENT);
	row++;

	/* Column headers */
	wattron(w, A_BOLD);
	mvwprintw(w, row, 2, "%-10s  %-15s  %-26s  %s",
		  "STATUS", "TYPE", "AGENT", "TASK");
	wattroff(w, A_BOLD);
	row++;

	vibes_set_color(w, VIBES_COLOR_MUTED);
	mvwprintw(w, row, 2, "%-10s  %-15s  %-26s  %s",
		  "----------", "---------------",
		  "--------------------------",
		  "--------------------");
	vibes_unset_color(w, VIBES_COLOR_MUTED);
	row++;

	/* Query recent agent events for status */
	stmt = vibes_db_prepare(tui->db,
		"SELECT DISTINCT agent_id, type, task_id FROM events "
		"WHERE agent_id IS NOT NULL "
		"ORDER BY created_at DESC LIMIT 20;");
	if (stmt) {
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *agent_id =
				(const char *)sqlite3_column_text(stmt, 0);
			const char *event_type =
				(const char *)sqlite3_column_text(stmt, 1);
			const char *task_id =
				(const char *)sqlite3_column_text(stmt, 2);

			/* Color-code by event type */
			if (event_type && strstr(event_type, "COMPLETED")) {
				vibes_set_color(w, VIBES_COLOR_SUCCESS);
				mvwprintw(w, row, 2, "%-10s", "done");
			} else if (event_type &&
				   strstr(event_type, "FAILED")) {
				vibes_set_color(w, VIBES_COLOR_ERROR);
				mvwprintw(w, row, 2, "%-10s", "failed");
			} else if (event_type &&
				   strstr(event_type, "PROGRESS")) {
				vibes_set_color(w, VIBES_COLOR_WARNING);
				mvwprintw(w, row, 2, "%-10s", "working");
			} else {
				vibes_set_color(w, VIBES_COLOR_MUTED);
				mvwprintw(w, row, 2, "%-10s", "idle");
			}
			vibes_unset_color(w, VIBES_COLOR_MUTED);
			vibes_unset_color(w, VIBES_COLOR_SUCCESS);
			vibes_unset_color(w, VIBES_COLOR_WARNING);
			vibes_unset_color(w, VIBES_COLOR_ERROR);

			mvwprintw(w, row, 14, "%-15s",
				  event_type ? event_type : "?");
			mvwprintw(w, row, 31, "%.26s",
				  agent_id ? agent_id : "?");
			if (task_id)
				mvwprintw(w, row, 59, "%.8s", task_id);

			row++;
			nr_agents++;

			if (row >= tui->term_rows - 3)
				break;
		}
		sqlite3_finalize(stmt);
	}

	if (nr_agents == 0) {
		vibes_set_color(w, VIBES_COLOR_MUTED);
		mvwprintw(w, row++, 4,
			  "(no agents running - use 'git vibes-agents run')");
		vibes_unset_color(w, VIBES_COLOR_MUTED);
	}

	/* Active file locks */
	row += 2;
	vibes_set_color(w, VIBES_COLOR_ACCENT);
	mvwprintw(w, row++, 2, "File Locks:");
	vibes_unset_color(w, VIBES_COLOR_ACCENT);

	stmt = vibes_db_prepare(tui->db,
		"SELECT file_path, agent_id FROM file_locks "
		"ORDER BY locked_at DESC LIMIT 10;");
	if (stmt) {
		int has_locks = 0;
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *path =
				(const char *)sqlite3_column_text(stmt, 0);
			const char *agent =
				(const char *)sqlite3_column_text(stmt, 1);
			mvwprintw(w, row++, 4, "%-40s  locked by %.8s",
				  path ? path : "?",
				  agent ? agent : "?");
			has_locks = 1;
			if (row >= tui->term_rows - 3)
				break;
		}
		sqlite3_finalize(stmt);

		if (!has_locks) {
			vibes_set_color(w, VIBES_COLOR_MUTED);
			mvwprintw(w, row++, 4, "(no active locks)");
			vibes_unset_color(w, VIBES_COLOR_MUTED);
		}
	}
}

#endif /* VIBES_ENABLED */
