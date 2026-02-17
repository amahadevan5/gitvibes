#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <curses.h>
#include "strbuf.h"
#include "string-list.h"
#include "libvibes/tui/tui.h"
#include "libvibes/storage/db.h"
#include "libvibes/intent/intent.h"
#include "libvibes/ai/ai.h"
#include "libvibes/ai/prompt-templates.h"
#include "libvibes/json-parser.h"

/*
 * Vibe Input: natural language intent input with AI-powered autocomplete.
 *
 * Tab triggers AI completion of partial input. The AI returns 3
 * suggestions which are shown below the input line. Arrow keys
 * select a suggestion, Enter accepts it.
 */

static char input_buf[1024];
static int input_len;
static int input_submitted;

/* Autocomplete state */
#define MAX_SUGGESTIONS 5
static char *suggestions[MAX_SUGGESTIONS];
static int nr_suggestions;
static int selected_suggestion;
static int showing_suggestions;

static void clear_suggestions(void)
{
	int i;
	for (i = 0; i < nr_suggestions; i++) {
		free(suggestions[i]);
		suggestions[i] = NULL;
	}
	nr_suggestions = 0;
	selected_suggestion = -1;
	showing_suggestions = 0;
}

/*
 * Fetch AI completions for the current partial input.
 * Called when user presses Tab.
 */
static void fetch_completions(struct vibes_tui *tui)
{
	struct vibes_ai_config ai_cfg;
	struct strbuf prompt = STRBUF_INIT;
	struct strbuf response = STRBUF_INIT;
	struct vibes_json root;
	int i;

	clear_suggestions();

	if (input_len < 3)
		return; /* Too short to autocomplete */

	input_buf[input_len] = '\0';

	if (vibes_ai_config_init(&ai_cfg, tui->repo) < 0)
		return;

	/* Use lower max_tokens for fast completions */
	ai_cfg.max_tokens = 256;

	vibes_prompt_autocomplete(&prompt, input_buf, NULL);

	if (vibes_ai_complete(&ai_cfg, prompt.buf, &response,
			      VIBES_AI_TASK_AUTOCOMPLETE) < 0)
		goto cleanup;

	/* Parse JSON array of suggestions */
	if (vibes_json_parse_any(response.buf, &root) == 0) {
		if (root.type == JSON_ARRAY) {
			int count = root.nr_elements;
			if (count > MAX_SUGGESTIONS)
				count = MAX_SUGGESTIONS;
			for (i = 0; i < count; i++) {
				if (root.elements[i].type == JSON_STRING &&
				    root.elements[i].str_val) {
					suggestions[nr_suggestions++] =
						xstrdup(root.elements[i].str_val);
				}
			}
		}
		vibes_json_free(&root);
	}

	if (nr_suggestions > 0) {
		showing_suggestions = 1;
		selected_suggestion = 0;
	}

cleanup:
	strbuf_release(&prompt);
	strbuf_release(&response);
	vibes_ai_config_free(&ai_cfg);
}

void vibes_input_render(struct vibes_tui *tui)
{
	WINDOW *w = tui->main_win;
	int row = 1;
	int i;

	/* Title */
	vibes_set_color(w, VIBES_COLOR_ACCENT);
	wattron(w, A_BOLD);
	mvwprintw(w, row++, 2, "Vibe Input");
	wattroff(w, A_BOLD);
	vibes_unset_color(w, VIBES_COLOR_ACCENT);
	row++;

	mvwprintw(w, row++, 2,
		  "Type a natural language intent and press Enter.");
	mvwprintw(w, row++, 2,
		  "Press Tab for AI suggestions, Escape to cancel.");
	row++;

	/* Input prompt */
	vibes_set_color(w, VIBES_COLOR_ACCENT);
	mvwprintw(w, row, 2, "> ");
	vibes_unset_color(w, VIBES_COLOR_ACCENT);

	/* Current input text */
	mvwprintw(w, row, 4, "%.*s", input_len, input_buf);

	/* Position cursor */
	wmove(w, row, 4 + input_len);
	row += 2;

	/* Show suggestions if active */
	if (showing_suggestions && nr_suggestions > 0) {
		vibes_set_color(w, VIBES_COLOR_MUTED);
		mvwprintw(w, row++, 2,
			  "Suggestions (Up/Down to select, Enter to accept):");
		vibes_unset_color(w, VIBES_COLOR_MUTED);

		for (i = 0; i < nr_suggestions; i++) {
			if (i == selected_suggestion) {
				vibes_set_color(w, VIBES_COLOR_SELECTED);
				mvwprintw(w, row, 2, " > ");
			} else {
				mvwprintw(w, row, 2, "   ");
			}
			mvwprintw(w, row, 5, "%s", suggestions[i]);
			if (i == selected_suggestion)
				vibes_unset_color(w, VIBES_COLOR_SELECTED);
			row++;
		}
		row++;
	}

	if (input_submitted) {
		vibes_set_color(w, VIBES_COLOR_SUCCESS);
		mvwprintw(w, row++, 2,
			  "Submitted: %.*s", input_len, input_buf);
		vibes_unset_color(w, VIBES_COLOR_SUCCESS);
		mvwprintw(w, row++, 2,
			  "(Run 'git vibes \"%.*s\"' to execute)",
			  input_len, input_buf);
		input_submitted = 0;
	}

	/* Hints */
	if (!showing_suggestions) {
		row += 2;
		vibes_set_color(w, VIBES_COLOR_MUTED);
		mvwprintw(w, row++, 2, "Examples:");
		mvwprintw(w, row++, 4,
			  "\"add JWT authentication\"");
		mvwprintw(w, row++, 4,
			  "\"fix the login page redirect bug\"");
		mvwprintw(w, row++, 4,
			  "\"refactor database layer to use connection pool\"");
		mvwprintw(w, row++, 4,
			  "\"add unit tests for the payment module\"");
		vibes_unset_color(w, VIBES_COLOR_MUTED);
	}
}

int vibes_input_handle_key(struct vibes_tui *tui, int ch)
{
	switch (ch) {
	case '\n':
	case '\r':
		if (showing_suggestions && selected_suggestion >= 0 &&
		    selected_suggestion < nr_suggestions) {
			/* Accept selected suggestion */
			int slen = (int)strlen(suggestions[selected_suggestion]);
			if (slen < (int)sizeof(input_buf) - 1) {
				memcpy(input_buf,
				       suggestions[selected_suggestion],
				       slen);
				input_len = slen;
				input_buf[input_len] = '\0';
			}
			clear_suggestions();
			return 1;
		}
		/* Submit input */
		if (input_len > 0) {
			input_buf[input_len] = '\0';
			input_submitted = 1;
			clear_suggestions();
		}
		return 1;

	case '\t':
		/* Trigger AI autocomplete */
		fetch_completions(tui);
		return 1;

	case KEY_UP:
		if (showing_suggestions && selected_suggestion > 0) {
			selected_suggestion--;
			return 1;
		}
		return 0;

	case KEY_DOWN:
		if (showing_suggestions &&
		    selected_suggestion < nr_suggestions - 1) {
			selected_suggestion++;
			return 1;
		}
		return 0;

	case 27: /* Escape */
		if (showing_suggestions) {
			clear_suggestions();
			return 1;
		}
		return 0;

	case KEY_BACKSPACE:
	case 127:
	case 8:
		/* Delete last character */
		if (input_len > 0) {
			input_len--;
			input_buf[input_len] = '\0';
			clear_suggestions();
		}
		return 1;

	case KEY_DC:
		/* Clear all */
		input_len = 0;
		input_buf[0] = '\0';
		clear_suggestions();
		return 1;

	default:
		/* Add character */
		if (ch >= 32 && ch < 127 &&
		    input_len < (int)sizeof(input_buf) - 1) {
			input_buf[input_len++] = ch;
			input_buf[input_len] = '\0';
			clear_suggestions();
			return 1;
		}
		return 0;
	}
}

#endif /* VIBES_ENABLED */
