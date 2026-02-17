#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "libvibes/ai/prompt-templates.h"

const char *VIBES_PROMPT_COMMIT_MSG =
	"You are a git commit message expert. Write a commit message following "
	"Conventional Commits.\n"
	"Format: type(scope): description\\n\\nbody\n"
	"Types: feat, fix, refactor, test, docs, chore, style, perf, ci, build\n"
	"Rules: max 72 char subject, imperative mood, no period, body explains WHY.\n\n"
	"Files changed:\n%s\n\nDiff:\n%s\n\n"
	"Write ONLY the commit message. No explanation, no markdown fences.";

const char *VIBES_PROMPT_CLUSTER_CHANGES =
	"Group these changed files into logical commit clusters.\n\n"
	"Changed files:\n%s\n\n"
	"Respond with ONLY a JSON array:\n"
	"[{\"type\":\"feat\",\"scope\":\"module\","
	"\"files\":[\"path/file1\"],\"reason\":\"why grouped\"}]";

const char *VIBES_PROMPT_INTENT_PARSE =
	"Parse this development request into a structured intent.\n\n"
	"Request: \"%s\"\n\nRepository context:\n%s\n\n"
	"Respond with ONLY JSON:\n"
	"{\"type\":\"feature|bugfix|refactor|docs|chore\","
	"\"parsed_goal\":\"structured goal\","
	"\"criteria\":[\"criterion\"],\"constraints\":[\"constraint\"]}";

const char *VIBES_PROMPT_DECOMPOSE =
	"Decompose this intent into implementable tasks.\n\n"
	"Goal: %s\nType: %s\nCriteria: %s\nConstraints: %s\n\n"
	"Repository files:\n%s\n\n"
	"Respond with ONLY a JSON array:\n"
	"[{\"title\":\"task\",\"description\":\"what to do\","
	"\"dependencies\":[],\"estimated_files\":[\"file.c\"],\"wave\":0}]";

const char *VIBES_PROMPT_CONFLICT_RESOLVE =
	"Resolve this merge conflict.\n\n"
	"File: %s\n\nBase:\n%s\n\nOurs:\n%s\n\nTheirs:\n%s\n\n"
	"Output ONLY the resolved code. No explanation, no fences.";

/*
 * Truncate diff text if too long. Keep first and last N lines.
 */
static void truncate_diff(struct strbuf *out, const char *diff, int max_lines)
{
	const char *p = diff;
	int lines = 0;
	int total_lines = 0;

	/* Count total lines */
	for (p = diff; *p; p++)
		if (*p == '\n')
			total_lines++;

	if (total_lines <= max_lines) {
		strbuf_addstr(out, diff);
		return;
	}

	/* Take first half and last half of max_lines */
	int first = max_lines / 2;
	int last = max_lines - first;
	p = diff;

	/* Add first N lines */
	for (lines = 0; lines < first && *p; p++) {
		strbuf_addch(out, *p);
		if (*p == '\n')
			lines++;
	}

	strbuf_addf(out, "\n... (%d lines omitted) ...\n\n",
		    total_lines - max_lines);

	/* Find start of last N lines */
	int skip = total_lines - last;
	p = diff;
	for (lines = 0; lines < skip && *p; p++)
		if (*p == '\n')
			lines++;

	strbuf_addstr(out, p);
}

void vibes_prompt_commit_message(struct strbuf *out,
				 const char *type,
				 const char *scope,
				 const char *file_list,
				 const char *diff_text)
{
	struct strbuf truncated = STRBUF_INIT;

	strbuf_addstr(out,
		"You are a git commit message expert. Write a commit message "
		"following Conventional Commits.\n");
	strbuf_addf(out,
		"Commit type hint: %s\n", type ? type : "unknown");
	if (scope)
		strbuf_addf(out, "Scope hint: %s\n", scope);
	strbuf_addstr(out,
		"Format: type(scope): description\n\n"
		"Optional body after blank line explaining WHY.\n"
		"Max 72 char subject, imperative mood, no period.\n\n"
		"Files changed:\n");
	strbuf_addstr(out, file_list);
	strbuf_addstr(out, "\n\nDiff:\n");

	truncate_diff(&truncated, diff_text, 200);
	strbuf_addbuf(out, &truncated);
	strbuf_release(&truncated);

	strbuf_addstr(out,
		"\n\nWrite ONLY the commit message. "
		"No explanation, no markdown fences.");
}

void vibes_prompt_intent_parse(struct strbuf *out,
			       const char *raw_input,
			       const char *file_list,
			       const char *recent_commits)
{
	strbuf_addstr(out,
		"You parse development requests into structured intents. "
		"Respond with ONLY JSON.\n\n");
	strbuf_addf(out, "Request: \"%s\"\n\n", raw_input);
	if (file_list)
		strbuf_addf(out, "Repository files:\n%s\n\n", file_list);
	if (recent_commits)
		strbuf_addf(out, "Recent commits:\n%s\n\n", recent_commits);
	strbuf_addstr(out,
		"JSON format:\n"
		"{\"type\":\"feature|bugfix|refactor|docs|chore\","
		"\"parsed_goal\":\"structured goal\","
		"\"criteria\":[\"criterion\"],"
		"\"constraints\":[\"constraint\"]}");
}

void vibes_prompt_decompose_intent(struct strbuf *out,
				   const char *goal,
				   const char *type,
				   const char *criteria,
				   const char *constraints,
				   const char *files)
{
	strbuf_addf(out, VIBES_PROMPT_DECOMPOSE,
		    goal, type, criteria, constraints, files);
}

#endif /* VIBES_ENABLED */
