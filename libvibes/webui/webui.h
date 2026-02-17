#ifndef LIBVIBES_WEBUI_WEBUI_H
#define LIBVIBES_WEBUI_WEBUI_H
#ifdef VIBES_ENABLED

#include <stdint.h>

struct vibes_db;
struct repository;

#define VIBES_MAX_WS_CLIENTS 32

/*
 * Web UI server state.
 */
struct vibes_webui {
	int server_fd;
	int port;
	int running;
	struct vibes_db *db;
	struct repository *repo;

	/* WebSocket clients */
	int ws_clients[VIBES_MAX_WS_CLIENTS];
	int nr_ws_clients;
	int64_t last_event_ts;
};

/* --- HTTP server (server.c) --- */

int vibes_webui_start(struct vibes_webui *ui, struct vibes_db *db,
		      struct repository *repo, int port);
int vibes_webui_run(struct vibes_webui *ui);
void vibes_webui_stop(struct vibes_webui *ui);

/* --- API endpoints (api.c) --- */

int vibes_api_handle(struct vibes_webui *ui, int client_fd,
		     const char *method, const char *path);

/* --- WebSocket (websocket.c) --- */

int vibes_ws_is_upgrade(const char *request);
int vibes_ws_handshake(int fd, const char *request);
int vibes_ws_send_text(int fd, const char *text, int len);
int vibes_ws_recv_frame(int fd, struct strbuf *payload);
void vibes_ws_send_close(int fd);
void vibes_ws_broadcast(struct vibes_webui *ui, const char *message, int len);
void vibes_ws_process_clients(struct vibes_webui *ui);
void vibes_ws_push_events(struct vibes_webui *ui);
int vibes_ws_add_client(struct vibes_webui *ui, int fd);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_WEBUI_WEBUI_H */
