#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "run-command.h"
#include "libvibes/intent/intent.h"
#include "libvibes/json-parser.h"
#include "libvibes/ai/ai.h"
#include "libvibes/ai/prompt-templates.h"

/*
 * NL Intent Parser: parses natural language into a structured intent
 * using the AI backend.
 *
 * Input: intent->raw_input (natural language string)
 * Output: fills intent->type, parsed_goal, criteria, constraints
 */

int vibes_parse_intent(struct vibes_intent *intent, struct repository *repo)
{
	struct vibes_ai_config ai_cfg;
	struct strbuf prompt = STRBUF_INIT;
	struct strbuf response = STRBUF_INIT;
	struct vibes_json root;
	const char *val;
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
	if (vibes_json_parse_any(response.buf, &root) == 0) {
		val = vibes_json_get_string(&root, "type");
		if (val)
			intent->type = intent_type_from_str(val);

		val = vibes_json_get_string(&root, "parsed_goal");
		if (val) {
			free(intent->parsed_goal);
			intent->parsed_goal = xstrdup(val);
		}

		vibes_json_free(&root);
	}

	ret = 0;

cleanup:
	strbuf_release(&prompt);
	strbuf_release(&response);
	vibes_ai_config_free(&ai_cfg);
	return ret;
}

#endif /* VIBES_ENABLED */
