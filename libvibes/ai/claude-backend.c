#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <curl/curl.h>
#include "libvibes/ai/ai.h"

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
 * Extract the first "text" value from a Claude API JSON response.
 * Minimal hand-written parser — finds "text":"..." in the response.
 */
static int extract_text(const char *json, struct strbuf *out)
{
	const char *p, *key = "\"text\":\"";

	p = strstr(json, key);
	if (!p)
		return -1;
	p += strlen(key);

	while (*p && *p != '"') {
		if (*p == '\\' && *(p + 1)) {
			p++;
			switch (*p) {
			case '"':  strbuf_addch(out, '"'); break;
			case '\\': strbuf_addch(out, '\\'); break;
			case 'n':  strbuf_addch(out, '\n'); break;
			case 'r':  strbuf_addch(out, '\r'); break;
			case 't':  strbuf_addch(out, '\t'); break;
			case '/':  strbuf_addch(out, '/'); break;
			default:   strbuf_addch(out, *p);
			}
		} else {
			strbuf_addch(out, *p);
		}
		p++;
	}
	return 0;
}

/*
 * Extract "message" field from an error response.
 */
static void extract_error(const char *json, struct strbuf *out)
{
	const char *p, *key = "\"message\":\"";

	p = strstr(json, key);
	if (!p) {
		strbuf_addstr(out, "unknown API error");
		return;
	}
	p += strlen(key);

	while (*p && *p != '"') {
		if (*p == '\\' && *(p + 1)) {
			p++;
			strbuf_addch(out, *p);
		} else {
			strbuf_addch(out, *p);
		}
		p++;
	}
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
		curl_easy_cleanup(curl);
		curl_slist_free_all(hdrs);
		hdrs = NULL;

		if (res != CURLE_OK) {
			error("gitvibes: curl error: %s",
			      curl_easy_strerror(res));
			goto out;
		}

		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

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
