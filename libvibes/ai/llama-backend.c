#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "libvibes/ai/ai.h"
#include "run-command.h"

/*
 * Claude CLI backend for gitvibes.
 *
 * Uses the Claude Code CLI as a subprocess:
 *   claude -p "prompt" --output-format text
 *
 * This gives production-quality responses using the user's existing
 * Claude Code installation. The file is named llama-backend.c for
 * historical reasons (originally planned for llama.cpp).
 */

/*
 * Check if the claude CLI is available on PATH.
 * Caches the result after first check.
 */
static int cli_available(const char *cli_path)
{
	static int checked;
	static int available;

	if (!checked) {
		struct child_process cp = CHILD_PROCESS_INIT;
		struct strbuf out = STRBUF_INIT;

		strvec_push(&cp.args, cli_path);
		strvec_push(&cp.args, "--version");
		cp.no_stdin = 1;
		cp.no_stderr = 1;

		available = !capture_command(&cp, &out, 256);
		strbuf_release(&out);
		checked = 1;
	}

	return available;
}

int vibes_ai_complete_cli(const struct vibes_ai_config *cfg,
			  const char *prompt,
			  struct strbuf *response)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf out = STRBUF_INIT;
	const char *cli_path;
	int ret;

	cli_path = (cfg->cli_path && *cfg->cli_path) ? cfg->cli_path : "claude";

	if (!cli_available(cli_path)) {
		error("gitvibes: '%s' not found. Install Claude Code or "
		      "set vibes.backend = api", cli_path);
		return -1;
	}

	strvec_push(&cp.args, cli_path);
	strvec_push(&cp.args, "-p");
	strvec_push(&cp.args, prompt);
	strvec_push(&cp.args, "--output-format");
	strvec_push(&cp.args, "text");
	strvec_push(&cp.args, "--max-turns");
	strvec_push(&cp.args, "1");

	cp.no_stdin = 1;
	cp.no_stderr = 1;

	ret = capture_command(&cp, &out, 0);
	if (ret) {
		error("gitvibes: claude CLI exited with code %d", ret);
		strbuf_release(&out);
		return -1;
	}

	strbuf_trim_trailing_newline(&out);
	strbuf_addbuf(response, &out);
	strbuf_release(&out);
	return 0;
}

#endif /* VIBES_ENABLED */
