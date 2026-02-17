#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <curl/curl.h>
#include "libvibes/ai/ai.h"
#include "libvibes/json-parser.h"

/*
 * Claude API backend for gitvibes.
 *
 * Sends HTTP POST to the Anthropic Messages API via libcurl.
 * Parses the JSON response to extract the text content.
 */

#define CLAUDE_API_URL "https://api.anthropic.com/v1/messages"
#define CLAUDE_API_VERSION "2023-06-01"

/* curl write callback: append data to strbuf */
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct strbuf *buf = userdata;
	size_t total = size * nmemb;
	strbuf_add(buf, ptr, total);
	return total;
}

/*
 * Escape a string for JSON embedding.
 */
static void json_escape(struct strbuf *out, const char *s)
{
	for (; *s; s++) {
		switch (*s) {
		case '"':  strbuf_addstr(out, "\\\""); break;
		case '\\': strbuf_addstr(out, "\\\\"); break;
		case '\n': strbuf_addstr(out, "\\n"); break;
		case '\r': strbuf_addstr(out, "\\r"); break;
		case '\t': strbuf_addstr(out, "\\t"); break;
		default:
			if ((unsigned char)*s < 0x20)
				strbuf_addf(out, "\\u%04x", (unsigned char)*s);
			else
				strbuf_addch(out, *s);
		}
	}
}

/*
 * Extract text content from a Claude API JSON response.
 *
 * Claude API returns:
 *   {"content":[{"type":"text","text":"..."}], ...}
 *
 * We walk the parsed JSON tree to find content[0].text.
 */
static int extract_text(const char *json_str, struct strbuf *out)
{
	struct vibes_json root;
	const struct vibes_json *content;
	const char *text;

	if (vibes_json_parse(json_str, &root) < 0)
		return -1;

	content = vibes_json_get_array(&root, "content");
	if (!content || content->nr_elements == 0) {
		vibes_json_free(&root);
		return -1;
	}

	text = vibes_json_get_string(&content->elements[0], "text");
	if (!text) {
		vibes_json_free(&root);
		return -1;
	}

	strbuf_addstr(out, text);
	vibes_json_free(&root);
	return 0;
}

/*
 * Extract error message from a Claude API error response.
 *
 * Error format: {"error":{"type":"...","message":"..."}}
 */
static void extract_error(const char *json_str, struct strbuf *out)
{
	struct vibes_json root;
	const struct vibes_json *err_obj;
	const char *msg;

	if (vibes_json_parse(json_str, &root) < 0) {
		strbuf_addstr(out, "unknown API error");
		return;
	}

	err_obj = vibes_json_get_object(&root, "error");
	if (err_obj) {
		msg = vibes_json_get_string(err_obj, "message");
		if (msg)
			strbuf_addstr(out, msg);
		else
			strbuf_addstr(out, "unknown API error");
	} else {
		/* Fallback: try top-level "message" */
		msg = vibes_json_get_string(&root, "message");
		if (msg)
			strbuf_addstr(out, msg);
		else
			strbuf_addstr(out, "unknown API error");
	}

	vibes_json_free(&root);
}

int vibes_ai_complete_api(const struct vibes_ai_config *cfg,
			  const char *prompt,
			  struct strbuf *response)
{
	CURL *curl;
	CURLcode res;
	struct strbuf body = STRBUF_INIT;
	struct strbuf resp_buf = STRBUF_INIT;
	struct curl_slist *hdrs = NULL;
	struct strbuf auth = STRBUF_INIT;
	long http_code;
	int ret = -1;
	int attempt;

	if (!cfg->api_key || !*cfg->api_key) {
		error("gitvibes: vibes.claude-api-key not set. "
		      "Run: git config vibes.claude-api-key <key>");
		return -1;
	}

	/* Build JSON request body */
	strbuf_addstr(&body, "{\"model\":\"");
	json_escape(&body, cfg->api_model ? cfg->api_model : "claude-sonnet-4-20250514");
	strbuf_addf(&body, "\",\"max_tokens\":%d,", cfg->max_tokens > 0 ? cfg->max_tokens : 4096);
	strbuf_addstr(&body, "\"messages\":[{\"role\":\"user\",\"content\":\"");
	json_escape(&body, prompt);
	strbuf_addstr(&body, "\"}]}");

	/* Retry loop for rate limiting */
	for (attempt = 0; attempt < 3; attempt++) {
		strbuf_reset(&resp_buf);

		curl = curl_easy_init();
		if (!curl) {
			error("gitvibes: curl_easy_init failed");
			goto out;
		}

		/* Headers */
		strbuf_reset(&auth);
		strbuf_addf(&auth, "x-api-key: %s", cfg->api_key);
		hdrs = curl_slist_append(NULL, auth.buf);
		hdrs = curl_slist_append(hdrs, "content-type: application/json");
		hdrs = curl_slist_append(hdrs, "anthropic-version: " CLAUDE_API_VERSION);

		curl_easy_setopt(curl, CURLOPT_URL, CLAUDE_API_URL);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.buf);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_buf);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

		res = curl_easy_perform(curl);

		if (res != CURLE_OK) {
			error("gitvibes: curl error: %s",
			      curl_easy_strerror(res));
			curl_easy_cleanup(curl);
			curl_slist_free_all(hdrs);
			hdrs = NULL;
			goto out;
		}

		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		curl_easy_cleanup(curl);
		curl_slist_free_all(hdrs);
		hdrs = NULL;

		if (http_code == 429) {
			sleep(1 << (attempt + 1));
			continue;
		}
		break;
	}

	if (http_code != 200) {
		struct strbuf errmsg = STRBUF_INIT;
		extract_error(resp_buf.buf, &errmsg);
		error("gitvibes: Claude API error (HTTP %ld): %s",
		      http_code, errmsg.buf);
		strbuf_release(&errmsg);
		goto out;
	}

	/* Parse response */
	if (extract_text(resp_buf.buf, response) < 0) {
		error("gitvibes: failed to parse Claude API response");
		goto out;
	}

	ret = 0;

out:
	strbuf_release(&body);
	strbuf_release(&resp_buf);
	strbuf_release(&auth);
	return ret;
}

#endif /* VIBES_ENABLED */
