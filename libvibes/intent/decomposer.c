#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "repository.h"
#include "strbuf.h"
#include "string-list.h"
#include "run-command.h"
#include "libvibes/intent/intent.h"
#include "libvibes/json-parser.h"
#include "libvibes/storage/db.h"
#include "libvibes/ai/ai.h"
#include "libvibes/ai/prompt-templates.h"

/*
 * Task Decomposer: breaks a parsed intent into a task DAG
 * with dependencies and wave numbers.
 *
 * Uses the AI backend to decompose the intent, then parses
 * the JSON response into vibes_task structs and stores them
 * in the database.
 */

/*
 * Build a simple file listing from the repository for context.
 * Lists tracked files up to a maximum count.
 */
static void get_repo_file_list(struct strbuf *out, int max_files)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf files = STRBUF_INIT;
	const char *p;
	int count = 0;

	strvec_pushl(&cp.args, "ls-files", NULL);
	cp.git_cmd = 1;
	cp.no_stdin = 1;

	if (capture_command(&cp, &files, 0) < 0) {
		strbuf_addstr(out, "(unable to list files)\n");
		return;
	}

	p = files.buf;
	while (*p && count < max_files) {
		const char *eol = strchr(p, '\n');
		if (!eol)
			eol = p + strlen(p);
		strbuf_add(out, p, eol - p);
		strbuf_addch(out, '\n');
		count++;
		p = *eol ? eol + 1 : eol;
	}

	if (*p)
		strbuf_addf(out, "... (%d more files)\n",
			    (int)(strchr(p, '\0') - p)); /* approximate */

	strbuf_release(&files);
}

/*
 * Build criteria/constraints as a single string for the prompt.
 */
static void string_list_to_str(const struct string_list *list, struct strbuf *out)
{
	int i;
	for (i = 0; i < list->nr; i++) {
		if (i > 0)
			strbuf_addstr(out, ", ");
		strbuf_addstr(out, list->items[i].string);
	}
	if (!list->nr)
		strbuf_addstr(out, "(none)");
}

/*
 * Parse the AI response (JSON array of task objects) into vibes_task
 * structs and link them into the intent's task list.
 */
static int parse_tasks_from_json(const char *response,
				 struct vibes_intent *intent,
				 struct vibes_db *db)
{
	struct vibes_json root;
	struct vibes_task *tail = NULL;
	int nr_tasks = 0;
	int i;

	if (vibes_json_parse_any(response, &root) < 0)
		return error("gitvibes: AI response contains no valid JSON");

	if (root.type != JSON_ARRAY) {
		vibes_json_free(&root);
		return error("gitvibes: AI response is not a JSON array");
	}

	for (i = 0; i < root.nr_elements; i++) {
		const struct vibes_json *obj = &root.elements[i];
		const struct vibes_json *arr;
		struct vibes_task *task;
		const char *title;
		const char *desc;

		if (obj->type != JSON_OBJECT)
			continue;

		task = xcalloc(1, sizeof(*task));
		vibes_task_init(task);

		vibes_ulid_generate(task->id);
		task->intent_id = xstrdup(intent->id);

		title = vibes_json_get_string(obj, "title");
		task->title = title ? xstrdup(title) : NULL;

		desc = vibes_json_get_string(obj, "description");
		task->description = desc ? xstrdup(desc) : NULL;

		task->wave_number = vibes_json_get_int(obj, "wave", 0);

		arr = vibes_json_get_array(obj, "dependencies");
		if (arr)
			vibes_json_array_to_strings(arr, &task->dependencies);

		arr = vibes_json_get_array(obj, "estimated_files");
		if (arr)
			vibes_json_array_to_strings(arr, &task->est_files);

		task->status = TASK_PENDING;

		/* Insert into database */
		if (db) {
			vibes_db_insert_task(db, task->id, task->intent_id,
					    task->title ? task->title : "untitled",
					    task->description,
					    task->wave_number);
		}

		/* Append to linked list */
		if (!intent->tasks)
			intent->tasks = task;
		else
			tail->next = task;
		tail = task;
		nr_tasks++;
	}

	vibes_json_free(&root);
	intent->nr_tasks = nr_tasks;
	return 0;
}

int vibes_decompose_intent(struct vibes_intent *intent,
			   struct vibes_db *db,
			   struct repository *repo)
{
	struct vibes_ai_config ai_cfg;
	struct strbuf prompt = STRBUF_INIT;
	struct strbuf response = STRBUF_INIT;
	struct strbuf files = STRBUF_INIT;
	struct strbuf criteria_str = STRBUF_INIT;
	struct strbuf constraints_str = STRBUF_INIT;
	int ret;

	if (!intent->parsed_goal && !intent->raw_input)
		return error("gitvibes: intent has no goal to decompose");

	if (vibes_ai_config_init(&ai_cfg, repo) < 0)
		return -1;

	/* Build context strings */
	get_repo_file_list(&files, 100);
	string_list_to_str(&intent->criteria, &criteria_str);
	string_list_to_str(&intent->constraints, &constraints_str);

	/* Build prompt */
	vibes_prompt_decompose_intent(&prompt,
		intent->parsed_goal ? intent->parsed_goal : intent->raw_input,
		intent_type_to_str(intent->type),
		criteria_str.buf,
		constraints_str.buf,
		files.buf);

	ret = vibes_ai_complete(&ai_cfg, prompt.buf, &response,
				VIBES_AI_TASK_DECOMPOSE);
	if (ret < 0)
		goto cleanup;

	/* Parse JSON response into task structs */
	ret = parse_tasks_from_json(response.buf, intent, db);
	if (ret < 0)
		goto cleanup;

	/* Update intent status to DECOMPOSED */
	intent->status = INTENT_DECOMPOSED;
	if (db)
		vibes_intent_update_status(db, intent->id, INTENT_DECOMPOSED);

	ret = 0;

cleanup:
	strbuf_release(&prompt);
	strbuf_release(&response);
	strbuf_release(&files);
	strbuf_release(&criteria_str);
	strbuf_release(&constraints_str);
	vibes_ai_config_free(&ai_cfg);
	return ret;
}

#endif /* VIBES_ENABLED */
