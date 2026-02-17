#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "repository.h"
#include "strbuf.h"
#include "string-list.h"
#include "libvibes/branch/branch-strategy.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/*
 * Topology Planning: create branch topology from a task DAG.
 */

int vibes_plan_topology(struct vibes_db *db, const char *intent_id,
			enum branch_convention convention,
			struct branch_topology *topo)
{
	sqlite3_stmt *stmt;
	struct strbuf main_branch = STRBUF_INIT;
	struct strbuf feature_branch = STRBUF_INIT;

	memset(topo, 0, sizeof(*topo));
	string_list_init_dup(&topo->task_branches);
	topo->convention = convention;

	/* Determine main branch name */
	if (convention == BRANCH_GITFLOW)
		strbuf_addstr(&main_branch, "develop");
	else
		strbuf_addstr(&main_branch, "main");
	topo->main_branch = strbuf_detach(&main_branch, NULL);

	/* Feature branch from intent */
	strbuf_addf(&feature_branch, "vibes/intent-%.8s", intent_id);
	topo->feature_branch = strbuf_detach(&feature_branch, NULL);

	/* Create per-task branches */
	stmt = vibes_db_prepare(db,
		"SELECT id, title, wave_number FROM tasks "
		"WHERE intent_id = ? ORDER BY wave_number, id;");
	if (stmt) {
		sqlite3_bind_text(stmt, 1, intent_id, -1, SQLITE_STATIC);

		while (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *task_id =
				(const char *)sqlite3_column_text(stmt, 0);
			struct strbuf branch = STRBUF_INIT;

			strbuf_addf(&branch, "vibes/task-%.8s",
				    task_id ? task_id : "unknown");
			string_list_append(&topo->task_branches, branch.buf);
			strbuf_release(&branch);
		}
		sqlite3_finalize(stmt);
	}

	return 0;
}

void vibes_topology_free(struct branch_topology *topo)
{
	free(topo->main_branch);
	free(topo->feature_branch);
	string_list_clear(&topo->task_branches, 0);
}

#endif /* VIBES_ENABLED */
