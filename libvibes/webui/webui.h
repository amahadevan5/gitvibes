#ifndef LIBVIBES_WEBUI_WEBUI_H
#define LIBVIBES_WEBUI_WEBUI_H
#ifdef VIBES_ENABLED

struct vibes_db;
struct repository;

/*
 * Web UI server state.
 */
struct vibes_webui {
	int server_fd;
	int port;
	int running;
	struct vibes_db *db;
	struct repository *repo;
};

/* --- HTTP server (server.c) --- */

int vibes_webui_start(struct vibes_webui *ui, struct vibes_db *db,
		      struct repository *repo, int port);
int vibes_webui_run(struct vibes_webui *ui);
void vibes_webui_stop(struct vibes_webui *ui);

/* --- API endpoints (api.c) --- */

int vibes_api_handle(struct vibes_webui *ui, int client_fd,
		     const char *method, const char *path);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_WEBUI_WEBUI_H */
