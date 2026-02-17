#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <curses.h>
#include <sqlite3.h>
#include "libvibes/tui/tui.h"
#include "libvibes/storage/db.h"

/*
 * Dashboard view: summary cards + recent activity.
 */
void vibes_dashboard_render(struct vibes_tui *tui)
{
	WINDOW *w = tui->main_win;
	sqlite3_stmt *stmt;
	int row = 1;
	int nr_intents = 0, nr_tasks = 0, nr_events = 0;
	int nr_active = 0, nr_completed = 0;

	/* Title */
	vibes_set_color(w, VIBES_COLOR_ACCENT);
	wattron(w, A_BOLD);
	mvwprintw(w, row++, 2, "Dashboard");
	wattroff(w, A_BOLD);
	vibes_unset_color(w, VIBES_COLOR_ACCENT);
	row++;

	/* Gather stats */
	nr_intents = vibes_db_count_table(tui->db, "intents");
	nr_tasks = vibes_db_count_table(tui->db, "tasks");
	nr_events = vibes_db_count_table(tui->db, "events");

	/* Active intents */
	stmt = vibes_db_prepare(tui->db,
		"SELECT COUNT(*) FROM intents WHERE status = 'in_progress';");
	if (stmt) {
		if (sqlite3_step(stmt) == SQLITE_ROW)
			nr_active = sqlite3_column_int(stmt, 0);
		sqlite3_finalize(stmt);
	}

	/* Completed intents */
	stmt = vibes_db_prepare(tui->db,
		"SELECT COUNT(*) FROM intents WHERE status = 'completed';");
	if (stmt) {
		if (sqlite3_step(stmt) == SQLITE_ROW)
			nr_completed = sqlite3_column_int(stmt, 0);
		sqlite3_finalize(stmt);
	}

	/* Summary cards */
	mvwprintw(w, row, 2, "+--------------+--------------+--------------+");
	row++;
	mvwprintw(w, row, 2, "|");
	vibes_set_color(w, VIBES_COLOR_WARNING);
	wprintw(w, "  Active: %-3d ", nr_active);
	vibes_unset_color(w, VIBES_COLOR_WARNING);
	wprintw(w, "|");
	vibes_set_color(w, VIBES_COLOR_SUCCESS);
	wprintw(w, "  Done: %-5d ", nr_completed);
	vibes_unset_color(w, VIBES_COLOR_SUCCESS);
	wprintw(w, "|");
	wprintw(w, "  Tasks: %-4d ", nr_tasks);
	wprintw(w, "|");
	row++;
	mvwprintw(w, row, 2, "+--------------+--------------+--------------+");
	row += 2;

	/* Totals */
	vibes_set_color(w, VIBES_COLOR_MUTED);
	mvwprintw(w, row++, 2, "Total intents: %d  |  Events: %d",
		  nr_intents, nr_events);
	vibes_unset_color(w, VIBES_COLOR_MUTED);
	row++;

	/* Recent activity */
	vibes_set_color(w, VIBES_COLOR_ACCENT);
	mvwprintw(w, row++, 2, "Recent Activity:");
	vibes_unset_color(w, VIBES_COLOR_ACCENT);

	stmt = vibes_db_prepare(tui->db,
		"SELECT type, agent_id, task_id, created_at FROM events "
		"ORDER BY created_at DESC LIMIT 10;");
	if (stmt) {
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *type =
				(const char *)sqlite3_column_text(stmt, 0);
			const char *agent =
				(const char *)sqlite3_column_text(stmt, 1);
			const char *task =
				(const char *)sqlite3_column_text(stmt, 2);

			mvwprintw(w, row, 4, "%-20s",
				  type ? type : "?");
			if (agent)
				wprintw(w, " agent:%.8s", agent);
			if (task)
				wprintw(w, " task:%.8s", task);
			row++;

			if (row >= tui->term_rows - 3)
				break;
		}
		sqlite3_finalize(stmt);
	}

	if (nr_events == 0) {
		vibes_set_color(w, VIBES_COLOR_MUTED);
		mvwprintw(w, row++, 4,
			  "(no activity yet - run 'git vibes' to start)");
		vibes_unset_color(w, VIBES_COLOR_MUTED);
	}
}

#endif /* VIBES_ENABLED */
