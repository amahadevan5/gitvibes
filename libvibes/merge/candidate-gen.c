#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include "repository.h"
#include "strbuf.h"
#include "libvibes/merge/semantic-merge.h"
#include "libvibes/ai/ai.h"
#include "libvibes/storage/db.h"

/*
 * LLM Conflict Resolution: generate candidate resolutions
 * for merge conflicts using the AI backend.
 */

int vibes_generate_candidates(struct vibes_db *db,
			      struct vibes_conflict *conflict,
			      struct resolution_candidate *candidates,
			      int max_candidates)
{
	struct vibes_ai_config ai_cfg;
	struct strbuf prompt = STRBUF_INIT;
	struct strbuf response = STRBUF_INIT;
	int count = 0;

	if (vibes_ai_config_init(&ai_cfg, the_repository) < 0)
		return -1;

	/* Build prompt with both versions */
	strbuf_addstr(&prompt,
		"You are resolving a git merge conflict. Generate the best "
		"resolution that preserves the intent of both changes.\n\n");

	if (conflict->file_path)
		strbuf_addf(&prompt, "File: %s\n\n", conflict->file_path);

	strbuf_addstr(&prompt, "<<<<<<< OURS\n");
	strbuf_addbuf(&prompt, &conflict->ours);
	strbuf_addstr(&prompt, "\n=======\n");
	strbuf_addbuf(&prompt, &conflict->theirs);
	strbuf_addstr(&prompt, "\n>>>>>>> THEIRS\n\n");

	if (conflict->base.len) {
		strbuf_addstr(&prompt, "Base version:\n");
		strbuf_addbuf(&prompt, &conflict->base);
		strbuf_addstr(&prompt, "\n\n");
	}

	strbuf_addstr(&prompt,
		"Provide the resolved content only, no explanation. "
		"The resolution should compile and be correct.");

	if (vibes_ai_complete(&ai_cfg, prompt.buf, &response,
			      VIBES_AI_TASK_CONFLICT_RESOLVE) == 0 &&
	    response.len > 0) {
		strbuf_init(&candidates[0].content, 0);
		strbuf_addbuf(&candidates[0].content, &response);
		candidates[0].score = 0.7;
		candidates[0].reasoning = xstrdup("AI-generated resolution");
		count = 1;
	}

	strbuf_release(&prompt);
	strbuf_release(&response);
	vibes_ai_config_free(&ai_cfg);
	return count;
}

void vibes_candidate_free(struct resolution_candidate *candidate)
{
	strbuf_release(&candidate->content);
	free(candidate->reasoning);
	candidate->reasoning = NULL;
}

#endif /* VIBES_ENABLED */
