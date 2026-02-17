#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include "repository.h"
#include "strbuf.h"
#include "string-list.h"
#include "run-command.h"
#include "libvibes/intent/intent.h"
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
 * Minimal JSON array iterator for task objects.
 * Returns pointer to next '{' in a JSON array, or NULL.
 */
static const char *json_next_object(const char *p)
{
	while (*p && *p != '{')
		p++;
	return *p ? p : NULL;
}

/*
 * Find the matching closing brace, handling nesting.
 */
static const char *json_object_end(const char *start)
{
	const char *p = start;
	int depth = 0;
	int in_string = 0;

	for (; *p; p++) {
		if (*p == '"' && (p == start || *(p - 1) != '\\'))
			in_string = !in_string;
		if (in_string)
			continue;
		if (*p == '{')
			depth++;
		else if (*p == '}') {
			depth--;
			if (depth == 0)
				return p;
		}
	}
	return NULL;
}

/*
 * Extract a string value from a JSON object.
 * Looks for "key":"value" and returns an allocated copy.
 */
static char *json_get_string(const char *obj, const char *end, const char *key)
{
	struct strbuf search = STRBUF_INIT;
	const char *p, *start, *val_end;
	char *result;

	strbuf_addf(&search, "\"%s\"", key);
	p = obj;
	while (p < end) {
		p = strstr(p, search.buf);
		if (!p || p >= end) {
			strbuf_release(&search);
			return NULL;
		}
		/* Make sure this key is within our object bounds */
		break;
	}
	strbuf_release(&search);

	/* Skip past key and colon */
	p += strlen(key) + 2;
	while (p < end && (*p == ':' || *p == ' ' || *p == '\t' || *p == '\n'))
		p++;

	if (p >= end || *p != '"')
		return NULL;
	p++; /* skip opening quote */
	start = p;

	/* Find closing quote (handle escaped quotes) */
	while (p < end && !(*p == '"' && *(p - 1) != '\\'))
		p++;
	val_end = p;

	result = xstrndup(start, val_end - start);
	return result;
}

/*
 * Extract an integer value from a JSON object.
 */
static int json_get_int(const char *obj, const char *end, const char *key,
			int default_val)
{
	struct strbuf search = STRBUF_INIT;
	const char *p;

	strbuf_addf(&search, "\"%s\"", key);
	p = strstr(obj, search.buf);
	strbuf_release(&search);

	if (!p || p >= end)
		return default_val;

	p += strlen(key) + 2;
	while (p < end && (*p == ':' || *p == ' ' || *p == '\t'))
		p++;

	return atoi(p);
}

/*
 * Extract a JSON array of strings for "dependencies" or "estimated_files".
 * Fills a string_list with the values.
 */
static void json_get_string_array(const char *obj, const char *end,
				  const char *key, struct string_list *list)
{
	struct strbuf search = STRBUF_INIT;
	const char *p;

	strbuf_addf(&search, "\"%s\"", key);
	p = strstr(obj, search.buf);
	strbuf_release(&search);

	if (!p || p >= end)
		return;

	/* Find the opening bracket */
	p += strlen(key) + 2;
	while (p < end && *p != '[')
		p++;
	if (p >= end)
		return;
	p++; /* skip '[' */

	/* Extract each string element */
	while (p < end && *p != ']') {
		while (p < end && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t'))
			p++;
		if (*p == '"') {
			const char *start, *val_end;
			p++; /* skip opening quote */
			start = p;
			while (p < end && !(*p == '"' && *(p - 1) != '\\'))
				p++;
			val_end = p;
			if (p < end) {
				char *val = xstrndup(start, val_end - start);
				string_list_append(list, val);
				free(val);
				p++; /* skip closing quote */
			}
		} else {
			p++;
		}
	}
}

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
	const char *p;
	struct vibes_task *tail = NULL;
	int nr_tasks = 0;
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

	/* Parse the JSON array response into tasks */
	p = response.buf;

	/* Find the start of the JSON array */
	while (*p && *p != '[')
		p++;
	if (!*p) {
		ret = error("gitvibes: AI response contains no task array");
		goto cleanup;
	}

	/* Parse each task object */
	while ((p = json_next_object(p)) != NULL) {
		const char *obj_end = json_object_end(p);
		struct vibes_task *task;

		if (!obj_end)
			break;

		task = xcalloc(1, sizeof(*task));
		vibes_task_init(task);

		/* Generate task ID */
		vibes_ulid_generate(task->id);
		task->intent_id = xstrdup(intent->id);

		/* Extract fields from JSON */
		task->title = json_get_string(p, obj_end, "title");
		task->description = json_get_string(p, obj_end, "description");
		task->wave_number = json_get_int(p, obj_end, "wave", 0);

		json_get_string_array(p, obj_end, "dependencies",
				      &task->dependencies);
		json_get_string_array(p, obj_end, "estimated_files",
				      &task->est_files);

		task->status = TASK_PENDING;

		/* Insert into database */
		if (db) {
			vibes_db_insert_task(db, task->id, task->intent_id,
					    task->title ? task->title : "untitled",
					    task->description,
					    task->wave_number);
		}

		/* Append to linked list */
		if (!intent->tasks) {
			intent->tasks = task;
		} else {
			tail->next = task;
		}
		tail = task;
		nr_tasks++;

		p = obj_end + 1;
	}

	intent->nr_tasks = nr_tasks;

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
