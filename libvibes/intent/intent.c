#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "libvibes/intent/intent.h"
#include "libvibes/storage/db.h"

enum intent_type intent_type_from_str(const char *s)
{
	if (!s)
		return INTENT_FEATURE;
	if (!strcmp(s, "feature") || !strcmp(s, "feat"))
		return INTENT_FEATURE;
	if (!strcmp(s, "bugfix") || !strcmp(s, "fix"))
		return INTENT_BUGFIX;
	if (!strcmp(s, "refactor"))
		return INTENT_REFACTOR;
	if (!strcmp(s, "docs") || !strcmp(s, "documentation"))
		return INTENT_DOCS;
	if (!strcmp(s, "chore"))
		return INTENT_CHORE;
	return INTENT_FEATURE;
}

const char *intent_type_to_str(enum intent_type t)
{
	switch (t) {
	case INTENT_FEATURE:  return "feature";
	case INTENT_BUGFIX:   return "bugfix";
	case INTENT_REFACTOR: return "refactor";
	case INTENT_DOCS:     return "docs";
	case INTENT_CHORE:    return "chore";
	default:              return "feature";
	}
}

const char *intent_status_to_str(enum intent_status s)
{
	switch (s) {
	case INTENT_CAPTURED:    return "captured";
	case INTENT_DECOMPOSED:  return "decomposed";
	case INTENT_IN_PROGRESS: return "in_progress";
	case INTENT_COMPLETED:   return "completed";
	case INTENT_ABANDONED:   return "abandoned";
	default:                 return "captured";
	}
}

void vibes_intent_init(struct vibes_intent *intent)
{
	memset(intent, 0, sizeof(*intent));
	string_list_init_dup(&intent->criteria);
	string_list_init_dup(&intent->constraints);
}

void vibes_intent_free(struct vibes_intent *intent)
{
	free(intent->parent_id);
	free(intent->raw_input);
	free(intent->parsed_goal);
	string_list_clear(&intent->criteria, 0);
	string_list_clear(&intent->constraints, 0);
	vibes_task_list_free(intent->tasks);
	memset(intent, 0, sizeof(*intent));
}

void vibes_task_init(struct vibes_task *task)
{
	memset(task, 0, sizeof(*task));
	string_list_init_dup(&task->dependencies);
	string_list_init_dup(&task->est_files);
}

void vibes_task_free(struct vibes_task *task)
{
	free(task->intent_id);
	free(task->title);
	free(task->description);
	free(task->assigned_agent);
	free(task->worktree_path);
	free(task->branch);
	string_list_clear(&task->dependencies, 0);
	string_list_clear(&task->est_files, 0);
}

void vibes_task_list_free(struct vibes_task *head)
{
	while (head) {
		struct vibes_task *next = head->next;
		vibes_task_free(head);
		free(head);
		head = next;
	}
}

int vibes_intent_create(struct vibes_db *db, struct vibes_intent *intent)
{
	/* Generate ULID if not set */
	if (!intent->id[0])
		vibes_ulid_generate(intent->id);

	intent->status = INTENT_CAPTURED;
	intent->created_at = vibes_timestamp_ms();

	return vibes_db_insert_intent(db, intent->id,
				      intent_type_to_str(intent->type),
				      intent->raw_input,
				      intent->session_id[0] ? intent->session_id : NULL);
}

int vibes_intent_get(struct vibes_db *db, const char *id,
		     struct vibes_intent *intent)
{
	sqlite3_stmt *stmt;
	int rc;

	vibes_intent_init(intent);

	stmt = vibes_db_prepare(db,
		"SELECT id, type, status, raw_input, parsed_goal, "
		"created_at, started_at, completed_at "
		"FROM intents WHERE id = ?;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);
	rc = sqlite3_step(stmt);

	if (rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return error("gitvibes: intent '%s' not found", id);
	}

	strlcpy(intent->id, (const char *)sqlite3_column_text(stmt, 0),
		VIBES_ULID_LEN);
	intent->type = intent_type_from_str(
		(const char *)sqlite3_column_text(stmt, 1));
	/* status ignored for now — always captured */
	intent->raw_input = xstrdup(
		(const char *)sqlite3_column_text(stmt, 3));
	if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
		intent->parsed_goal = xstrdup(
			(const char *)sqlite3_column_text(stmt, 4));

	intent->created_at = sqlite3_column_int64(stmt, 5);
	intent->started_at = sqlite3_column_int64(stmt, 6);
	intent->completed_at = sqlite3_column_int64(stmt, 7);

	sqlite3_finalize(stmt);
	return 0;
}

int vibes_intent_update_status(struct vibes_db *db, const char *id,
			       enum intent_status status)
{
	return vibes_db_update_intent_status(db, id,
					     intent_status_to_str(status));
}

#endif /* VIBES_ENABLED */
