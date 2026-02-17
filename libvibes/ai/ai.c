#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "libvibes/ai/ai.h"
#include "config.h"
#include "repository.h"

/*
 * AI dispatcher for gitvibes.
 *
 * Reads [vibes] config to determine which backend to use for each task type.
 */

static enum vibes_ai_backend parse_backend(const char *s)
{
	if (!s)
		return VIBES_AI_BACKEND_HYBRID;
	if (!strcmp(s, "cli") || !strcmp(s, "local"))
		return VIBES_AI_BACKEND_CLI;
	if (!strcmp(s, "api") || !strcmp(s, "cloud") || !strcmp(s, "claude"))
		return VIBES_AI_BACKEND_API;
	return VIBES_AI_BACKEND_HYBRID;
}

static void read_route(struct repository *r, const char *key,
		       enum vibes_ai_backend *dest)
{
	const char *val = NULL;
	if (!repo_config_get_string_tmp(r, key, &val))
		*dest = parse_backend(val);
}

int vibes_ai_config_init(struct vibes_ai_config *cfg, struct repository *repo)
{
	const char *val = NULL;

	memset(cfg, 0, sizeof(*cfg));
	cfg->backend = VIBES_AI_BACKEND_HYBRID;
	cfg->max_tokens = 4096;

	/* Default routes */
	cfg->route_commit_msg = VIBES_AI_BACKEND_CLI;
	cfg->route_intent_parse = VIBES_AI_BACKEND_API;
	cfg->route_decompose = VIBES_AI_BACKEND_CLI;
	cfg->route_code_analysis = VIBES_AI_BACKEND_CLI;
	cfg->route_conflict_resolve = VIBES_AI_BACKEND_API;

	/* Read main backend */
	if (!repo_config_get_string_tmp(repo, "vibes.backend", &val))
		cfg->backend = parse_backend(val);

	/* Claude API settings */
	val = NULL;
	if (!repo_config_get_string_tmp(repo, "vibes.claude-api-key", &val) && val)
		cfg->api_key = xstrdup(val);

	val = NULL;
	if (!repo_config_get_string_tmp(repo, "vibes.claude-model", &val) && val)
		cfg->api_model = xstrdup(val);
	else
		cfg->api_model = xstrdup("claude-sonnet-4-20250514");

	/* CLI settings */
	val = NULL;
	if (!repo_config_get_string_tmp(repo, "vibes.claude-cli-path", &val) && val)
		cfg->cli_path = xstrdup(val);
	else
		cfg->cli_path = xstrdup("claude");

	/* Max tokens */
	val = NULL;
	if (!repo_config_get_string_tmp(repo, "vibes.max-tokens", &val) && val)
		cfg->max_tokens = atoi(val);

	/* Per-task routing */
	read_route(repo, "vibes.route-commit-msg", &cfg->route_commit_msg);
	read_route(repo, "vibes.route-intent-parse", &cfg->route_intent_parse);
	read_route(repo, "vibes.route-decompose", &cfg->route_decompose);
	read_route(repo, "vibes.route-code-analysis", &cfg->route_code_analysis);
	read_route(repo, "vibes.route-conflict-resolve", &cfg->route_conflict_resolve);

	return 0;
}

void vibes_ai_config_free(struct vibes_ai_config *cfg)
{
	free(cfg->api_key);
	free(cfg->api_model);
	free(cfg->cli_path);
	memset(cfg, 0, sizeof(*cfg));
}

static enum vibes_ai_backend get_route(const struct vibes_ai_config *cfg,
				       enum vibes_ai_task task)
{
	if (cfg->backend != VIBES_AI_BACKEND_HYBRID)
		return cfg->backend;

	switch (task) {
	case VIBES_AI_TASK_COMMIT_MSG:
		return cfg->route_commit_msg;
	case VIBES_AI_TASK_INTENT_PARSE:
		return cfg->route_intent_parse;
	case VIBES_AI_TASK_DECOMPOSE:
		return cfg->route_decompose;
	case VIBES_AI_TASK_CODE_ANALYSIS:
		return cfg->route_code_analysis;
	case VIBES_AI_TASK_CONFLICT_RESOLVE:
		return cfg->route_conflict_resolve;
	case VIBES_AI_TASK_GENERAL:
	default:
		return VIBES_AI_BACKEND_CLI;
	}
}

int vibes_ai_complete(const struct vibes_ai_config *cfg,
		      const char *prompt,
		      struct strbuf *response,
		      enum vibes_ai_task task)
{
	enum vibes_ai_backend primary = get_route(cfg, task);
	enum vibes_ai_backend fallback;
	int ret;

	if (primary == VIBES_AI_BACKEND_CLI) {
		ret = vibes_ai_complete_cli(cfg, prompt, response);
		fallback = VIBES_AI_BACKEND_API;
	} else {
		ret = vibes_ai_complete_api(cfg, prompt, response);
		fallback = VIBES_AI_BACKEND_CLI;
	}

	if (ret == 0)
		return 0;

	/* Primary failed — try fallback in hybrid mode */
	if (cfg->backend == VIBES_AI_BACKEND_HYBRID) {
		strbuf_reset(response);
		if (fallback == VIBES_AI_BACKEND_CLI)
			ret = vibes_ai_complete_cli(cfg, prompt, response);
		else
			ret = vibes_ai_complete_api(cfg, prompt, response);
	}

	return ret;
}

#endif /* VIBES_ENABLED */
