#ifndef LIBVIBES_AI_PROMPT_TEMPLATES_H
#define LIBVIBES_AI_PROMPT_TEMPLATES_H
#ifdef VIBES_ENABLED

#include "strbuf.h"

/*
 * Prompt templates for gitvibes AI operations.
 */

/* Generate commit message from diff: args(file_list, diff_text) */
extern const char *VIBES_PROMPT_COMMIT_MSG;

/* Cluster changed files into logical groups: args(file_list) */
extern const char *VIBES_PROMPT_CLUSTER_CHANGES;

/* Parse NL input into structured intent: args(raw_input, repo_context) */
extern const char *VIBES_PROMPT_INTENT_PARSE;

/* Decompose intent into task DAG: args(goal, type, criteria, constraints, files) */
extern const char *VIBES_PROMPT_DECOMPOSE;

/* Resolve merge conflict: args(file, base, ours, theirs) */
extern const char *VIBES_PROMPT_CONFLICT_RESOLVE;

/* Autocomplete a partial intent: args(partial_input, file_list) */
extern const char *VIBES_PROMPT_AUTOCOMPLETE;

/*
 * Build a full commit message prompt from components.
 * Writes the formatted prompt to `out`.
 */
void vibes_prompt_commit_message(struct strbuf *out,
				 const char *type,
				 const char *scope,
				 const char *file_list,
				 const char *diff_text);

/*
 * Build an intent parsing prompt.
 * file_list and recent_commits may be NULL.
 */
void vibes_prompt_intent_parse(struct strbuf *out,
			       const char *raw_input,
			       const char *file_list,
			       const char *recent_commits);

/*
 * Build a task decomposition prompt.
 */
void vibes_prompt_decompose_intent(struct strbuf *out,
				   const char *goal,
				   const char *type,
				   const char *criteria,
				   const char *constraints,
				   const char *files);

/*
 * Build an autocomplete prompt for partial input.
 * file_list may be NULL.
 */
void vibes_prompt_autocomplete(struct strbuf *out,
			       const char *partial_input,
			       const char *file_list);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_AI_PROMPT_TEMPLATES_H */
