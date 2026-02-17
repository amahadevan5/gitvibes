#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sqlite3.h>
#include <unistd.h>
#include "strbuf.h"
#include "libvibes/webui/webui.h"
#include "libvibes/storage/db.h"

static void send_json(int fd, int status, const char *json, int len)
{
	char header[512];
	int hlen;

	hlen = snprintf(header, sizeof(header),
			"HTTP/1.1 %d OK\r\n"
			"Content-Type: application/json\r\n"
			"Content-Length: %d\r\n"
			"Connection: close\r\n"
			"Access-Control-Allow-Origin: *\r\n"
			"\r\n",
			status, len);
	write(fd, header, hlen);
	write(fd, json, len);
}

static void send_error(int fd, int status, const char *msg)
{
	struct strbuf json = STRBUF_INIT;
	strbuf_addf(&json, "{\"error\":\"%s\"}", msg);
	send_json(fd, status, json.buf, json.len);
	strbuf_release(&json);
}

static void escape_json_str(struct strbuf *out, const char *s)
{
	if (!s) {
		strbuf_addstr(out, "null");
		return;
	}
	strbuf_addch(out, '"');
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
	strbuf_addch(out, '"');
}

static void handle_intents(struct vibes_webui *ui, int fd)
{
	struct strbuf json = STRBUF_INIT;
	sqlite3_stmt *stmt;

	strbuf_addch(&json, '[');

	stmt = vibes_db_prepare(ui->db,
		"SELECT id, type, status, raw_input, created_at "
		"FROM intents ORDER BY created_at DESC LIMIT 50;");
	if (stmt) {
		int first = 1;
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			if (!first) strbuf_addch(&json, ',');
			first = 0;

			strbuf_addstr(&json, "{\"id\":");
			escape_json_str(&json,
				(const char *)sqlite3_column_text(stmt, 0));
			strbuf_addstr(&json, ",\"type\":");
			escape_json_str(&json,
				(const char *)sqlite3_column_text(stmt, 1));
			strbuf_addstr(&json, ",\"status\":");
			escape_json_str(&json,
				(const char *)sqlite3_column_text(stmt, 2));
			strbuf_addstr(&json, ",\"raw_input\":");
			escape_json_str(&json,
				(const char *)sqlite3_column_text(stmt, 3));
			strbuf_addf(&json, ",\"created_at\":%lld}",
				(long long)sqlite3_column_int64(stmt, 4));
		}
		sqlite3_finalize(stmt);
	}

	strbuf_addch(&json, ']');
	send_json(fd, 200, json.buf, json.len);
	strbuf_release(&json);
}

static void handle_tasks(struct vibes_webui *ui, int fd)
{
	struct strbuf json = STRBUF_INIT;
	sqlite3_stmt *stmt;

	strbuf_addch(&json, '[');

	stmt = vibes_db_prepare(ui->db,
		"SELECT id, intent_id, title, status, wave_number "
		"FROM tasks ORDER BY created_at DESC LIMIT 50;");
	if (stmt) {
		int first = 1;
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			if (!first) strbuf_addch(&json, ',');
			first = 0;

			strbuf_addstr(&json, "{\"id\":");
			escape_json_str(&json,
				(const char *)sqlite3_column_text(stmt, 0));
			strbuf_addstr(&json, ",\"intent_id\":");
			escape_json_str(&json,
				(const char *)sqlite3_column_text(stmt, 1));
			strbuf_addstr(&json, ",\"title\":");
			escape_json_str(&json,
				(const char *)sqlite3_column_text(stmt, 2));
			strbuf_addstr(&json, ",\"status\":");
			escape_json_str(&json,
				(const char *)sqlite3_column_text(stmt, 3));
			strbuf_addf(&json, ",\"wave\":%d}",
				sqlite3_column_int(stmt, 4));
		}
		sqlite3_finalize(stmt);
	}

	strbuf_addch(&json, ']');
	send_json(fd, 200, json.buf, json.len);
	strbuf_release(&json);
}

static void handle_events(struct vibes_webui *ui, int fd)
{
	struct strbuf json = STRBUF_INIT;
	sqlite3_stmt *stmt;

	strbuf_addch(&json, '[');

	stmt = vibes_db_prepare(ui->db,
		"SELECT id, type, agent_id, task_id, created_at "
		"FROM events ORDER BY created_at DESC LIMIT 50;");
	if (stmt) {
		int first = 1;
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			if (!first) strbuf_addch(&json, ',');
			first = 0;

			strbuf_addstr(&json, "{\"id\":");
			escape_json_str(&json,
				(const char *)sqlite3_column_text(stmt, 0));
			strbuf_addstr(&json, ",\"type\":");
			escape_json_str(&json,
				(const char *)sqlite3_column_text(stmt, 1));
			strbuf_addstr(&json, ",\"agent_id\":");
			escape_json_str(&json,
				(const char *)sqlite3_column_text(stmt, 2));
			strbuf_addstr(&json, ",\"task_id\":");
			escape_json_str(&json,
				(const char *)sqlite3_column_text(stmt, 3));
			strbuf_addf(&json, ",\"created_at\":%lld}",
				(long long)sqlite3_column_int64(stmt, 4));
		}
		sqlite3_finalize(stmt);
	}

	strbuf_addch(&json, ']');
	send_json(fd, 200, json.buf, json.len);
	strbuf_release(&json);
}

static void handle_knowledge_stats(struct vibes_webui *ui, int fd)
{
	struct strbuf json = STRBUF_INIT;
	int nodes = vibes_db_count_table(ui->db, "kg_nodes");
	int edges = vibes_db_count_table(ui->db, "kg_edges");

	strbuf_addf(&json,
		    "{\"nodes\":%d,\"edges\":%d}",
		    nodes, edges);

	send_json(fd, 200, json.buf, json.len);
	strbuf_release(&json);
}

int vibes_api_handle(struct vibes_webui *ui, int fd,
		     const char *method, const char *path)
{
	(void)method; /* All GET for now */

	if (!strcmp(path, "/api/intents")) {
		handle_intents(ui, fd);
	} else if (!strcmp(path, "/api/tasks")) {
		handle_tasks(ui, fd);
	} else if (!strcmp(path, "/api/events")) {
		handle_events(ui, fd);
	} else if (!strcmp(path, "/api/knowledge/stats")) {
		handle_knowledge_stats(ui, fd);
	} else {
		send_error(fd, 404, "Not found");
	}

	return 0;
}

#endif /* VIBES_ENABLED */
