#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include "strbuf.h"
#include "hash.h"
#include "libvibes/webui/webui.h"
#include "libvibes/storage/db.h"

/*
 * WebSocket support for the gitvibes web dashboard.
 *
 * Implements RFC 6455 WebSocket handshake and framing.
 * The server broadcasts new events to all connected WebSocket clients
 * so the dashboard updates in real-time without polling.
 */

#define WS_MAGIC_GUID "258EAFA5-E914-47DA-95CA-5AB53ABA665B"
#define WS_OP_TEXT   0x1
#define WS_OP_CLOSE  0x8
#define WS_OP_PING   0x9
#define WS_OP_PONG   0xA

/*
 * Base64 encode binary data. Returns allocated string.
 */
static char *base64_encode(const unsigned char *data, int len)
{
	static const char tbl[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
		"0123456789+/";
	int out_len = 4 * ((len + 2) / 3);
	char *out = xmalloc(out_len + 1);
	int i, j;

	for (i = 0, j = 0; i < len; i += 3, j += 4) {
		unsigned int v = (unsigned int)data[i] << 16;
		if (i + 1 < len) v |= (unsigned int)data[i + 1] << 8;
		if (i + 2 < len) v |= (unsigned int)data[i + 2];

		out[j]     = tbl[(v >> 18) & 0x3f];
		out[j + 1] = tbl[(v >> 12) & 0x3f];
		out[j + 2] = (i + 1 < len) ? tbl[(v >> 6) & 0x3f] : '=';
		out[j + 3] = (i + 2 < len) ? tbl[v & 0x3f] : '=';
	}
	out[j] = '\0';
	return out;
}

/*
 * Compute the Sec-WebSocket-Accept value from the client key.
 *
 * accept = base64(SHA1(client_key + magic_guid))
 */
static char *compute_accept_key(const char *client_key)
{
	git_SHA_CTX ctx;
	unsigned char hash[20];

	git_SHA1_Init(&ctx);
	git_SHA1_Update(&ctx, client_key, strlen(client_key));
	git_SHA1_Update(&ctx, WS_MAGIC_GUID, strlen(WS_MAGIC_GUID));
	git_SHA1_Final(hash, &ctx);

	return base64_encode(hash, 20);
}

/*
 * Parse the Sec-WebSocket-Key from HTTP headers.
 */
static int parse_ws_key(const char *request, struct strbuf *key)
{
	const char *p = strcasestr(request, "Sec-WebSocket-Key:");
	const char *end;

	if (!p)
		return -1;

	p += 18; /* strlen("Sec-WebSocket-Key:") */
	while (*p == ' ') p++;

	end = strstr(p, "\r\n");
	if (!end)
		end = p + strlen(p);

	strbuf_add(key, p, end - p);
	return 0;
}

/*
 * Check if an HTTP request is a WebSocket upgrade.
 */
int vibes_ws_is_upgrade(const char *request)
{
	return !!strcasestr(request, "Upgrade: websocket");
}

/*
 * Perform the WebSocket handshake.
 * Returns 0 on success, -1 on failure.
 * On success, the fd is now a WebSocket connection.
 */
int vibes_ws_handshake(int fd, const char *request)
{
	struct strbuf key = STRBUF_INIT;
	char *accept_key;
	struct strbuf resp = STRBUF_INIT;
	int ret = -1;

	if (parse_ws_key(request, &key) < 0)
		goto out;

	accept_key = compute_accept_key(key.buf);

	strbuf_addstr(&resp,
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: ");
	strbuf_addstr(&resp, accept_key);
	strbuf_addstr(&resp, "\r\n\r\n");

	free(accept_key);

	if (write(fd, resp.buf, resp.len) == (ssize_t)resp.len)
		ret = 0;

out:
	strbuf_release(&key);
	strbuf_release(&resp);
	return ret;
}

/*
 * Send a WebSocket text frame.
 */
int vibes_ws_send_text(int fd, const char *text, int len)
{
	unsigned char header[10];
	int hlen = 0;

	/* FIN + TEXT opcode */
	header[0] = 0x81;

	if (len < 126) {
		header[1] = (unsigned char)len;
		hlen = 2;
	} else if (len < 65536) {
		header[1] = 126;
		header[2] = (len >> 8) & 0xff;
		header[3] = len & 0xff;
		hlen = 4;
	} else {
		header[1] = 127;
		memset(&header[2], 0, 4); /* high 32 bits = 0 */
		header[6] = (len >> 24) & 0xff;
		header[7] = (len >> 16) & 0xff;
		header[8] = (len >> 8) & 0xff;
		header[9] = len & 0xff;
		hlen = 10;
	}

	if (write(fd, header, hlen) != hlen)
		return -1;
	if (write(fd, text, len) != len)
		return -1;

	return 0;
}

/*
 * Read a WebSocket frame from a client.
 * Returns the opcode, fills buffer with payload.
 * Returns -1 on error or connection close.
 */
int vibes_ws_recv_frame(int fd, struct strbuf *payload)
{
	unsigned char header[2];
	unsigned char mask_key[4];
	uint64_t payload_len;
	int masked;
	int opcode;
	ssize_t n;
	char *buf;
	uint64_t i;

	n = read(fd, header, 2);
	if (n != 2)
		return -1;

	opcode = header[0] & 0x0f;
	masked = (header[1] & 0x80) != 0;
	payload_len = header[1] & 0x7f;

	if (payload_len == 126) {
		unsigned char ext[2];
		if (read(fd, ext, 2) != 2)
			return -1;
		payload_len = ((uint64_t)ext[0] << 8) | ext[1];
	} else if (payload_len == 127) {
		unsigned char ext[8];
		if (read(fd, ext, 8) != 8)
			return -1;
		payload_len = 0;
		for (i = 0; i < 8; i++)
			payload_len = (payload_len << 8) | ext[i];
	}

	/* Sanity check */
	if (payload_len > 1024 * 1024)
		return -1;

	if (masked) {
		if (read(fd, mask_key, 4) != 4)
			return -1;
	}

	if (payload_len > 0) {
		buf = xmalloc((size_t)payload_len);
		n = read(fd, buf, (size_t)payload_len);
		if (n != (ssize_t)payload_len) {
			free(buf);
			return -1;
		}

		if (masked) {
			for (i = 0; i < payload_len; i++)
				buf[i] ^= mask_key[i % 4];
		}

		strbuf_add(payload, buf, (size_t)payload_len);
		free(buf);
	}

	return opcode;
}

/*
 * Send a WebSocket close frame.
 */
void vibes_ws_send_close(int fd)
{
	unsigned char frame[2] = { 0x88, 0x00 }; /* FIN + CLOSE, 0 len */
	(void)write(fd, frame, 2);
}

/*
 * Send a WebSocket pong frame.
 */
static void ws_send_pong(int fd, const char *data, int len)
{
	unsigned char header[2];
	header[0] = 0x8A; /* FIN + PONG */
	header[1] = (unsigned char)(len & 0x7f);
	(void)write(fd, header, 2);
	if (len > 0)
		(void)write(fd, data, len);
}

/*
 * Broadcast a message to all connected WebSocket clients.
 * Removes dead clients from the array.
 */
void vibes_ws_broadcast(struct vibes_webui *ui, const char *message, int len)
{
	int i, j;

	for (i = 0, j = 0; i < ui->nr_ws_clients; i++) {
		if (vibes_ws_send_text(ui->ws_clients[i], message, len) < 0) {
			/* Client disconnected, close fd */
			close(ui->ws_clients[i]);
		} else {
			/* Keep client in array */
			ui->ws_clients[j++] = ui->ws_clients[i];
		}
	}
	ui->nr_ws_clients = j;
}

/*
 * Process incoming WebSocket messages from connected clients.
 * Handles ping/pong, close, and discards text messages.
 */
void vibes_ws_process_clients(struct vibes_webui *ui)
{
	struct pollfd *pfds;
	int i, j;

	if (ui->nr_ws_clients == 0)
		return;

	pfds = xcalloc(ui->nr_ws_clients, sizeof(struct pollfd));
	for (i = 0; i < ui->nr_ws_clients; i++) {
		pfds[i].fd = ui->ws_clients[i];
		pfds[i].events = POLLIN;
	}

	if (poll(pfds, ui->nr_ws_clients, 0) > 0) {
		for (i = 0; i < ui->nr_ws_clients; i++) {
			if (!(pfds[i].revents & (POLLIN | POLLHUP | POLLERR)))
				continue;

			if (pfds[i].revents & (POLLHUP | POLLERR)) {
				close(ui->ws_clients[i]);
				ui->ws_clients[i] = -1;
				continue;
			}

			{
				struct strbuf payload = STRBUF_INIT;
				int op = vibes_ws_recv_frame(
					ui->ws_clients[i], &payload);

				if (op == WS_OP_PING) {
					ws_send_pong(ui->ws_clients[i],
						     payload.buf,
						     payload.len);
				} else if (op == WS_OP_CLOSE || op < 0) {
					vibes_ws_send_close(ui->ws_clients[i]);
					close(ui->ws_clients[i]);
					ui->ws_clients[i] = -1;
				}
				/* text frames are ignored (server push only) */
				strbuf_release(&payload);
			}
		}
	}

	/* Compact client array */
	for (i = 0, j = 0; i < ui->nr_ws_clients; i++) {
		if (ui->ws_clients[i] >= 0)
			ui->ws_clients[j++] = ui->ws_clients[i];
	}
	ui->nr_ws_clients = j;

	free(pfds);
}

/*
 * Check for new events and broadcast to WebSocket clients.
 */
void vibes_ws_push_events(struct vibes_webui *ui)
{
	sqlite3_stmt *stmt;
	struct strbuf json = STRBUF_INIT;

	if (ui->nr_ws_clients == 0)
		return;

	stmt = vibes_db_prepare(ui->db,
		"SELECT id, type, agent_id, task_id, payload "
		"FROM events WHERE created_at > ? "
		"ORDER BY created_at ASC LIMIT 20;");
	if (!stmt)
		return;

	sqlite3_bind_int64(stmt, 1, ui->last_event_ts);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *type = (const char *)sqlite3_column_text(stmt, 1);
		const char *agent = (const char *)sqlite3_column_text(stmt, 2);
		const char *task = (const char *)sqlite3_column_text(stmt, 3);
		const char *payload = (const char *)sqlite3_column_text(stmt, 4);

		strbuf_reset(&json);
		strbuf_addstr(&json, "{\"type\":\"event\",\"data\":{");
		strbuf_addf(&json, "\"type\":\"%s\"", type ? type : "");
		if (agent)
			strbuf_addf(&json, ",\"agent_id\":\"%s\"", agent);
		if (task)
			strbuf_addf(&json, ",\"task_id\":\"%s\"", task);
		if (payload)
			strbuf_addf(&json, ",\"payload\":\"%s\"", payload);
		strbuf_addstr(&json, "}}");

		vibes_ws_broadcast(ui, json.buf, json.len);

		/* Track the latest event timestamp */
		ui->last_event_ts = sqlite3_column_int64(stmt, 0) ?
			time(NULL) : ui->last_event_ts;
	}

	/* Update timestamp to now so we don't re-send */
	ui->last_event_ts = time(NULL);

	sqlite3_finalize(stmt);
	strbuf_release(&json);
}

/*
 * Add a WebSocket client fd to the tracked list.
 * Returns 0 on success, -1 if full.
 */
int vibes_ws_add_client(struct vibes_webui *ui, int fd)
{
	if (ui->nr_ws_clients >= VIBES_MAX_WS_CLIENTS) {
		close(fd);
		return -1;
	}
	ui->ws_clients[ui->nr_ws_clients++] = fd;
	return 0;
}

#endif /* VIBES_ENABLED */
