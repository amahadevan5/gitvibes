#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include <unistd.h>
#include "repository.h"
#include "strbuf.h"
#include "run-command.h"
#include "libvibes/agents/agents.h"
#include "libvibes/ai/ai.h"
#include "libvibes/ai/prompt-templates.h"
#include "libvibes/storage/db.h"
#include "libvibes/vibes.h"

/*
 * Agent Worker: executes a task in a git worktree using AI assistance.
 *
 * The worker:
 *   1. Reads task details from the database
 *   2. Sends a code generation prompt to the AI backend
 *   3. Parses the response for file changes
 *   4. Writes files to the worktree
 *   5. Stages and commits using the smart-commit engine
 *   6. Reports progress via IPC and event bus
 */

/*
 * Send a heartbeat message via IPC socket.
 */
static void send_heartbeat(int ipc_fd, const char *agent_id,
			    const char *status)
{
	struct strbuf msg = STRBUF_INIT;

	if (ipc_fd < 0)
		return;

	strbuf_addf(&msg, "{\"type\":\"heartbeat\",\"agent\":\"%.8s\","
		    "\"status\":\"%s\"}", agent_id, status);
	vibes_ipc_send(ipc_fd, msg.buf);
	strbuf_release(&msg);
}

/*
 * Send a progress message via IPC socket.
 */
static void send_progress(int ipc_fd, const char *agent_id,
			   const char *message)
{
	struct strbuf msg = STRBUF_INIT;

	if (ipc_fd < 0)
		return;

	strbuf_addf(&msg, "{\"type\":\"progress\",\"agent\":\"%.8s\","
		    "\"message\":\"%s\"}", agent_id, message);
	vibes_ipc_send(ipc_fd, msg.buf);
	strbuf_release(&msg);
}

/*
 * Send completion status via IPC socket.
 */
static void send_done(int ipc_fd, const char *agent_id, int success)
{
	struct strbuf msg = STRBUF_INIT;

	if (ipc_fd < 0)
		return;

	strbuf_addf(&msg, "{\"type\":\"done\",\"agent\":\"%.8s\","
		    "\"success\":%s}", agent_id,
		    success ? "true" : "false");
	vibes_ipc_send(ipc_fd, msg.buf);
	strbuf_release(&msg);
}

/*
 * Build a code generation prompt from task details.
 */
static void build_codegen_prompt(struct strbuf *out, const char *title,
				  const char *description,
				  const char *estimated_files)
{
	strbuf_addstr(out,
		"You are an AI coding agent working on a task in a git repository.\n"
		"Generate the code changes needed to complete this task.\n\n"
		"Task: ");
	strbuf_addstr(out, title ? title : "(no title)");
	strbuf_addstr(out, "\n\n");

	if (description && *description) {
		strbuf_addstr(out, "Description: ");
		strbuf_addstr(out, description);
		strbuf_addstr(out, "\n\n");
	}

	if (estimated_files && *estimated_files) {
		strbuf_addstr(out, "Files to modify: ");
		strbuf_addstr(out, estimated_files);
		strbuf_addstr(out, "\n\n");
	}

	strbuf_addstr(out,
		"Output your changes as a series of file blocks:\n"
		"--- FILE: <path> ---\n"
		"<file contents>\n"
		"--- END FILE ---\n\n"
		"Only output the file blocks, nothing else.\n");
}

/*
 * Parse AI response for file blocks and write them to the worktree.
 * Returns number of files written.
 */
static int apply_codegen_response(const char *response,
				   const char *worktree_path)
{
	const char *p = response;
	int files_written = 0;

	while ((p = strstr(p, "--- FILE: ")) != NULL) {
		const char *path_start, *path_end;
		const char *content_start, *content_end;
		struct strbuf filepath = STRBUF_INIT;
		struct strbuf full_path = STRBUF_INIT;
		struct strbuf dir_path = STRBUF_INIT;
		FILE *f;

		p += 10; /* skip "--- FILE: " */

		/* Parse filename */
		path_start = p;
		path_end = strstr(p, " ---");
		if (!path_end)
			path_end = strchr(p, '\n');
		if (!path_end)
			break;

		strbuf_add(&filepath, path_start, path_end - path_start);

		/* Skip to content */
		p = strchr(path_end, '\n');
		if (!p) {
			strbuf_release(&filepath);
			break;
		}
		p++;
		content_start = p;

		/* Find end marker */
		content_end = strstr(p, "--- END FILE ---");
		if (!content_end) {
			strbuf_release(&filepath);
			break;
		}

		/* Construct full path */
		strbuf_addf(&full_path, "%s/%s", worktree_path,
			    filepath.buf);

		/* Create parent directories */
		{
			const char *last_slash = strrchr(full_path.buf, '/');
			if (last_slash) {
				strbuf_add(&dir_path, full_path.buf,
					   last_slash - full_path.buf);
				mkdir(dir_path.buf, 0755);
			}
		}

		/* Write file */
		f = fopen(full_path.buf, "w");
		if (f) {
			/* Trim trailing whitespace before end marker */
			while (content_end > content_start &&
			       (content_end[-1] == '\n' ||
				content_end[-1] == '\r'))
				content_end--;
			/* Keep one newline */
			if (content_end < strstr(p, "--- END FILE ---"))
				content_end++;

			fwrite(content_start, 1, content_end - content_start, f);
			fclose(f);
			files_written++;
		}

		strbuf_release(&filepath);
		strbuf_release(&full_path);
		strbuf_release(&dir_path);

		/* Move past end marker */
		p = strstr(content_start, "--- END FILE ---");
		if (p)
			p += 16;
	}

	return files_written;
}

/*
 * Stage and commit all changes in the worktree.
 */
static int commit_changes(const char *worktree_path, const char *task_title,
			   const char *task_id)
{
	struct child_process add = CHILD_PROCESS_INIT;
	struct child_process commit = CHILD_PROCESS_INIT;
	struct strbuf msg = STRBUF_INIT;

	/* git add -A */
	strvec_pushl(&add.args, "add", "-A", NULL);
	add.git_cmd = 1;
	add.dir = worktree_path;
	if (run_command(&add))
		return -1;

	/* git commit */
	strbuf_addf(&msg, "feat: %s\n\nTask: %.8s\nGenerated by gitvibes agent",
		    task_title ? task_title : "agent task", task_id);

	strvec_pushl(&commit.args, "commit", "-m", msg.buf,
		     "--allow-empty", NULL);
	commit.git_cmd = 1;
	commit.dir = worktree_path;
	run_command(&commit);

	strbuf_release(&msg);
	return 0;
}

/*
 * Execute a task in the current worktree using AI assistance.
 */
static int execute_task(struct vibes_db *db, const char *task_id,
			const char *agent_id, const char *worktree_path,
			int ipc_fd)
{
	sqlite3_stmt *stmt;
	struct agent_event event;
	const char *title = NULL;
	const char *description = NULL;
	const char *estimated_files = NULL;
	struct strbuf prompt = STRBUF_INIT;
	struct strbuf response = STRBUF_INIT;
	struct vibes_ai_config ai_cfg;
	int ret = -1;
	int files_written;

	send_heartbeat(ipc_fd, agent_id, "starting");

	/* Get task details */
	stmt = vibes_db_prepare(db,
		"SELECT title, description, estimated_files "
		"FROM tasks WHERE id = ?;");
	if (!stmt)
		return -1;

	sqlite3_bind_text(stmt, 1, task_id, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		title = (const char *)sqlite3_column_text(stmt, 0);
		description = (const char *)sqlite3_column_text(stmt, 1);
		estimated_files = (const char *)sqlite3_column_text(stmt, 2);
	} else {
		sqlite3_finalize(stmt);
		return error("gitvibes: task %s not found", task_id);
	}

	/* Update task to in_progress */
	{
		sqlite3_stmt *update = vibes_db_prepare(db,
			"UPDATE tasks SET status = 'in_progress', "
			"assigned_agent = ? WHERE id = ?;");
		if (update) {
			sqlite3_bind_text(update, 1, agent_id, -1,
					  SQLITE_STATIC);
			sqlite3_bind_text(update, 2, task_id, -1,
					  SQLITE_STATIC);
			sqlite3_step(update);
			sqlite3_finalize(update);
		}
	}

	/* Publish progress event */
	memset(&event, 0, sizeof(event));
	event.type = EVENT_PROGRESS;
	event.agent_id = (char *)agent_id;
	event.task_id = (char *)task_id;
	event.payload = (char *)(title ? title : "working...");
	vibes_event_publish(db, &event);

	send_progress(ipc_fd, agent_id, "generating code");

	/* Build code generation prompt */
	build_codegen_prompt(&prompt, title, description, estimated_files);

	/* Initialize AI config from the repo config in the worktree */
	memset(&ai_cfg, 0, sizeof(ai_cfg));
	ai_cfg.backend = VIBES_AI_BACKEND_CLI;
	ai_cfg.cli_path = xstrdup("claude");

	/* Call AI to generate code */
	ret = vibes_ai_complete(&ai_cfg, prompt.buf, &response,
				VIBES_AI_TASK_GENERAL);

	if (ret < 0 || !response.len) {
		/*
		 * AI unavailable — create a scaffold commit with
		 * placeholder files based on estimated_files.
		 */
		send_progress(ipc_fd, agent_id,
			      "AI unavailable, creating placeholder");
		fprintf(stderr, "gitvibes: AI backend unavailable, "
			"creating placeholder for task %.8s\n", task_id);

		/* Create placeholder files */
		if (estimated_files) {
			struct strbuf placeholder = STRBUF_INIT;

			strbuf_addf(&placeholder,
				"/* TODO: %s */\n/* Task: %.8s */\n",
				title ? title : "implement task", task_id);

			/* Simple placeholder: write a TODO comment */
			{
				struct strbuf path = STRBUF_INIT;
				FILE *f;

				strbuf_addf(&path, "%s/TODO-%.8s.md",
					    worktree_path, task_id);
				f = fopen(path.buf, "w");
				if (f) {
					fprintf(f, "# Task: %s\n\n"
						"Files to modify:\n%s\n",
						title ? title : "(no title)",
						estimated_files);
					fclose(f);
				}
				strbuf_release(&path);
			}
			strbuf_release(&placeholder);
		}

		commit_changes(worktree_path, title, task_id);
		ret = 0;
	} else {
		/* Apply AI-generated code */
		send_progress(ipc_fd, agent_id, "applying changes");

		files_written = apply_codegen_response(response.buf,
						       worktree_path);

		if (files_written > 0) {
			commit_changes(worktree_path, title, task_id);
			fprintf(stderr, "gitvibes: agent %.8s wrote %d file(s)\n",
				agent_id, files_written);
		} else {
			fprintf(stderr, "gitvibes: agent %.8s: no files "
				"extracted from AI response\n", agent_id);
		}

		ret = 0;
	}

	sqlite3_finalize(stmt);
	vibes_ai_config_free(&ai_cfg);

	/* Mark task completed */
	{
		sqlite3_stmt *complete = vibes_db_prepare(db,
			"UPDATE tasks SET status = 'completed' WHERE id = ?;");
		if (complete) {
			sqlite3_bind_text(complete, 1, task_id, -1,
					  SQLITE_STATIC);
			sqlite3_step(complete);
			sqlite3_finalize(complete);
		}
	}

	/* Publish completion event */
	memset(&event, 0, sizeof(event));
	event.type = EVENT_COMPLETED;
	event.agent_id = (char *)agent_id;
	event.task_id = (char *)task_id;
	vibes_event_publish(db, &event);

	send_done(ipc_fd, agent_id, 1);

	strbuf_release(&prompt);
	strbuf_release(&response);

	return ret;
}

/*
 * Worker main entry point.
 * Called when this process is exec'd by the agent runner.
 *
 * argv: --task-id <id> --agent-id <id> --db-path <path> --ipc-fd <fd>
 */
int vibes_agent_worker_main(const char *task_id, const char *agent_id,
			     const char *db_path, int ipc_fd)
{
	struct vibes_db db;
	char cwd[PATH_MAX];
	int ret;

	if (!getcwd(cwd, sizeof(cwd)))
		return error("gitvibes: worker cannot get cwd");

	/* Open database */
	if (vibes_db_open(&db, db_path) < 0)
		return error("gitvibes: worker cannot open database");

	/* Publish heartbeat */
	{
		struct agent_event hb;
		memset(&hb, 0, sizeof(hb));
		hb.type = EVENT_HEARTBEAT;
		hb.agent_id = (char *)agent_id;
		hb.task_id = (char *)task_id;
		vibes_event_publish(&db, &hb);
	}

	/* Execute the task */
	ret = execute_task(&db, task_id, agent_id, cwd, ipc_fd);

	vibes_db_close(&db);
	vibes_ipc_close(ipc_fd);

	return ret;
}

/*
 * Legacy in-process worker (for orchestrator fallback mode).
 */
int vibes_agent_worker_run(struct vibes_db *db, const char *agent_id,
			   const char *worktree_path)
{
	char *task_id;
	int tasks_done = 0;

	while ((task_id = vibes_queue_claim(db, agent_id)) != NULL) {
		printf("  Agent %.8s: working on task %.8s\n",
		       agent_id, task_id);

		if (execute_task(db, task_id, agent_id,
				 worktree_path ? worktree_path : ".", -1) < 0) {
			vibes_queue_release(db, task_id);
			free(task_id);
			break;
		}

		free(task_id);
		tasks_done++;
	}

	return tasks_done;
}

#endif /* VIBES_ENABLED */
