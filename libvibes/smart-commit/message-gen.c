#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "libvibes/smart-commit/smart-commit.h"
#include "libvibes/ai/ai.h"
#include "libvibes/ai/prompt-templates.h"

/*
 * Commit message generation using AI.
 */

static void build_file_list(const struct vibes_commit_cluster *cluster,
			    struct strbuf *out)
{
	int i;
	for (i = 0; i < cluster->files.nr; i++) {
		if (i > 0)
			strbuf_addch(out, '\n');
		strbuf_addstr(out, cluster->files.items[i].string);
	}
}

int vibes_generate_commit_message(struct vibes_commit_cluster *cluster,
				  const struct vibes_changeset *cs UNUSED,
				  struct repository *repo)
{
	struct vibes_ai_config ai_cfg;
	struct strbuf prompt = STRBUF_INIT;
	struct strbuf response = STRBUF_INIT;
	struct strbuf file_list = STRBUF_INIT;
	int ret;

	if (vibes_ai_config_init(&ai_cfg, repo) < 0)
		return -1;

	build_file_list(cluster, &file_list);

	vibes_prompt_commit_message(&prompt,
				    cluster->type,
				    cluster->scope,
				    file_list.buf,
				    cluster->diff_text.buf);

	ret = vibes_ai_complete(&ai_cfg, prompt.buf, &response,
				VIBES_AI_TASK_COMMIT_MSG);

	if (!ret && response.len > 0) {
		free(cluster->message);
		cluster->message = strbuf_detach(&response, NULL);
	}

	strbuf_release(&prompt);
	strbuf_release(&response);
	strbuf_release(&file_list);
	vibes_ai_config_free(&ai_cfg);

	return ret;
}

#endif /* VIBES_ENABLED */
