#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "run-command.h"
#include "libvibes/intent/intent.h"
#include "libvibes/ai/ai.h"
#include "libvibes/ai/prompt-templates.h"

/*
 * NL Intent Parser: parses natural language into a structured intent
 * using the AI backend.
 *
 * Input: intent->raw_input (natural language string)
 * Output: fills intent->type, parsed_goal, criteria, constraints
 */

/*
 * Minimal JSON value extractor.
 * Finds "key":"value" and returns allocated copy of value.
 * Returns NULL if not found.
 */
static char *json_extract_string(const char *json, const char *key)
{
	struct strbuf search = STRBUF_INIT;
	const char *p, *start, *end;
	char *result;

	strbuf_addf(&search, "\"%s\"", key);
	p = strstr(json, search.buf);
	strbuf_release(&search);

	if (!p)
		return NULL;

	/* Skip to the colon and value */
	p += strlen(key) + 2;
	while (*p && (*p == ':' || *p == ' ' || *p == '\t' || *p == '\n'))
		p++;

	if (*p != '"')
		return NULL;
	p++; /* skip opening quote */
	start = p;

	/* Find closing quote */
	while (*p && !(*p == '"' && *(p - 1) != '\\'))
		p++;
	end = p;

	result = xstrndup(start, end - start);
	return result;
}

int vibes_parse_intent(struct vibes_intent *intent, struct repository *repo)
{
	struct vibes_ai_config ai_cfg;
	struct strbuf prompt = STRBUF_INIT;
	struct strbuf response = STRBUF_INIT;
	char *val;
	int ret;

	if (!intent->raw_input)
		return error("gitvibes: no input to parse");

	if (vibes_ai_config_init(&ai_cfg, repo) < 0)
		return -1;

	vibes_prompt_intent_parse(&prompt, intent->raw_input, NULL, NULL);

	ret = vibes_ai_complete(&ai_cfg, prompt.buf, &response,
				VIBES_AI_TASK_INTENT_PARSE);
	if (ret < 0)
		goto cleanup;

	/* Parse the JSON response */
	val = json_extract_string(response.buf, "type");
	if (val) {
		intent->type = intent_type_from_str(val);
		free(val);
	}

	val = json_extract_string(response.buf, "parsed_goal");
	if (val) {
		free(intent->parsed_goal);
		intent->parsed_goal = val;
	}

	ret = 0;

cleanup:
	strbuf_release(&prompt);
	strbuf_release(&response);
	vibes_ai_config_free(&ai_cfg);
	return ret;
}

#endif /* VIBES_ENABLED */
