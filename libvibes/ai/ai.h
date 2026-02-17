#ifndef LIBVIBES_AI_AI_H
#define LIBVIBES_AI_AI_H
#ifdef VIBES_ENABLED

#include "strbuf.h"

/*
 * AI backend types for the vibes AI dispatcher.
 */
enum vibes_ai_backend {
	VIBES_AI_BACKEND_CLI,    /* Claude Code CLI subprocess */
	VIBES_AI_BACKEND_API,    /* Claude API via libcurl */
	VIBES_AI_BACKEND_HYBRID, /* Route by task type */
};

/*
 * AI task types for hybrid routing.
 */
enum vibes_ai_task {
	VIBES_AI_TASK_COMMIT_MSG,
	VIBES_AI_TASK_INTENT_PARSE,
	VIBES_AI_TASK_DECOMPOSE,
	VIBES_AI_TASK_CODE_ANALYSIS,
	VIBES_AI_TASK_CONFLICT_RESOLVE,
	VIBES_AI_TASK_GENERAL,
};

/*
 * AI configuration, read from git config [vibes] section.
 */
struct vibes_ai_config {
	enum vibes_ai_backend backend;

	/* Claude API settings */
	char *api_key;
	char *api_model;

	/* Claude CLI settings */
	char *cli_path;

	/* Hybrid routing: per-task backend */
	enum vibes_ai_backend route_commit_msg;
	enum vibes_ai_backend route_intent_parse;
	enum vibes_ai_backend route_decompose;
	enum vibes_ai_backend route_code_analysis;
	enum vibes_ai_backend route_conflict_resolve;

	int max_tokens;
};

struct repository;

/*
 * Initialize AI configuration from git config.
 * Returns 0 on success, -1 on error.
 */
int vibes_ai_config_init(struct vibes_ai_config *cfg,
			 struct repository *repo);

/*
 * Free AI configuration resources.
 */
void vibes_ai_config_free(struct vibes_ai_config *cfg);

/*
 * Send a prompt to the AI and get a response.
 *
 * The dispatcher selects the backend based on the task type
 * and the configured routing rules.
 *
 * prompt: the prompt string to send
 * response: output buffer, will be grown as needed
 * task: the type of AI task (for routing)
 *
 * Returns 0 on success, -1 on error.
 */
int vibes_ai_complete(const struct vibes_ai_config *cfg,
		      const char *prompt,
		      struct strbuf *response,
		      enum vibes_ai_task task);

/*
 * Backend-specific completion functions.
 */
int vibes_ai_complete_cli(const struct vibes_ai_config *cfg,
			  const char *prompt,
			  struct strbuf *response);

int vibes_ai_complete_api(const struct vibes_ai_config *cfg,
			  const char *prompt,
			  struct strbuf *response);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_AI_AI_H */
