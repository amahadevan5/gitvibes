#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include "libvibes/webui/webui.h"
#include "libvibes/storage/db.h"

static volatile int g_running = 1;

static void handle_signal(int sig)
{
	(void)sig;
	g_running = 0;
}

int vibes_webui_start(struct vibes_webui *ui, struct vibes_db *db,
		      struct repository *repo, int port)
{
	struct sockaddr_in addr;
	int opt = 1;

	memset(ui, 0, sizeof(*ui));
	ui->db = db;
	ui->repo = repo;
	ui->port = port;
	ui->running = 1;
	ui->last_event_ts = time(NULL);

	ui->server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (ui->server_fd < 0)
		return error("webui: cannot create socket");

	setsockopt(ui->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(port);

	if (bind(ui->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(ui->server_fd);
		return error("webui: cannot bind to port %d", port);
	}

	if (listen(ui->server_fd, 5) < 0) {
		close(ui->server_fd);
		return error("webui: listen failed");
	}

	printf("gitvibes web UI running at http://localhost:%d\n", port);
	return 0;
}

static void parse_request(const char *buf, char *method, int method_sz,
			   char *path, int path_sz)
{
	const char *p = buf;
	int i;

	/* Parse method */
	for (i = 0; i < method_sz - 1 && *p && *p != ' '; i++)
		method[i] = *p++;
	method[i] = '\0';

	/* Skip space */
	while (*p == ' ') p++;

	/* Parse path */
	for (i = 0; i < path_sz - 1 && *p && *p != ' ' && *p != '?'; i++)
		path[i] = *p++;
	path[i] = '\0';
}

static void send_response(int fd, int status, const char *content_type,
			   const char *body, int body_len)
{
	char header[512];
	int hlen;

	hlen = snprintf(header, sizeof(header),
			"HTTP/1.1 %d OK\r\n"
			"Content-Type: %s\r\n"
			"Content-Length: %d\r\n"
			"Connection: close\r\n"
			"Access-Control-Allow-Origin: *\r\n"
			"\r\n",
			status, content_type, body_len);

	if (write(fd, header, hlen) < 0)
		return; /* client disconnected */
	if (body && body_len > 0)
		(void)write(fd, body, body_len);
}

static void serve_index(int fd)
{
	static const char html[] =
		"<!DOCTYPE html><html><head>"
		"<title>gitvibes</title>"
		"<style>"
		"body{background:#1a1a2e;color:#e0e0e0;font-family:monospace;"
		"margin:0;padding:20px;}"
		"h1{color:#00d4aa;}"
		".card{background:#16213e;padding:16px;margin:8px 0;"
		"border-radius:8px;border-left:3px solid #00d4aa;}"
		".card h3{margin:0 0 8px;color:#00d4aa;}"
		".status{display:inline-block;padding:2px 8px;"
		"border-radius:4px;font-size:12px;}"
		".active{background:#2d5a1e;color:#5fff5f;}"
		".done{background:#1e4a5a;color:#5fb3ff;}"
		".ws-status{position:fixed;top:10px;right:20px;"
		"font-size:11px;padding:4px 10px;border-radius:4px;}"
		".ws-connected{background:#2d5a1e;color:#5fff5f;}"
		".ws-disconnected{background:#5a1e1e;color:#ff5f5f;}"
		"</style></head><body>"
		"<div id='ws-indicator' class='ws-status ws-disconnected'>"
		"disconnected</div>"
		"<h1>gitvibes dashboard</h1>"
		"<div class='card'><h3>Intents</h3>"
		"<div id='intents'>Loading...</div></div>"
		"<div class='card'><h3>Tasks</h3>"
		"<div id='tasks'>Loading...</div></div>"
		"<div class='card'><h3>Events (live)</h3>"
		"<div id='events'>Waiting for events...</div></div>"
		"<script>"
		"let ws,eventLog=[];"
		"function connectWS(){"
		"let proto=location.protocol==='https:'?'wss:':'ws:';"
		"ws=new WebSocket(proto+'//'+location.host+'/ws');"
		"ws.onopen=function(){"
		"document.getElementById('ws-indicator').className="
		"'ws-status ws-connected';"
		"document.getElementById('ws-indicator').textContent="
		"'live';};"
		"ws.onclose=function(){"
		"document.getElementById('ws-indicator').className="
		"'ws-status ws-disconnected';"
		"document.getElementById('ws-indicator').textContent="
		"'disconnected';"
		"setTimeout(connectWS,3000);};"
		"ws.onmessage=function(e){"
		"try{let d=JSON.parse(e.data);"
		"if(d.type==='event'){"
		"eventLog.unshift(d.data);"
		"if(eventLog.length>50)eventLog.length=50;"
		"renderEvents();}"
		"}catch(err){}};"
		"}"
		"function renderEvents(){"
		"document.getElementById('events').innerHTML="
		"eventLog.map(e=>'<div>'+e.type+' '+"
		"(e.agent_id?e.agent_id.slice(0,8):'')+"
		"(e.payload?' - '+e.payload:'')+'</div>').join('')"
		"||'Waiting for events...';"
		"}"
		"async function load(){"
		"try{"
		"let r=await fetch('/api/intents');"
		"let d=await r.json();"
		"document.getElementById('intents').innerHTML="
		"d.map(i=>'<div>'+i.id.slice(0,8)+' ['+i.status+'] '+"
		"i.raw_input+'</div>').join('')||'None';"
		"r=await fetch('/api/tasks');"
		"d=await r.json();"
		"document.getElementById('tasks').innerHTML="
		"d.map(t=>'<div>'+t.id.slice(0,8)+' ['+t.status+'] '+"
		"t.title+'</div>').join('')||'None';"
		"}catch(e){console.error(e);}"
		"}"
		"connectWS();load();setInterval(load,10000);"
		"</script></body></html>";

	send_response(fd, 200, "text/html",
		      html, sizeof(html) - 1);
}

int vibes_webui_run(struct vibes_webui *ui)
{
	struct pollfd pfd;
	int64_t last_push = 0;

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	signal(SIGPIPE, SIG_IGN);

	while (ui->running && g_running) {
		int64_t now;

		/* Poll the server socket for new connections */
		pfd.fd = ui->server_fd;
		pfd.events = POLLIN;

		if (poll(&pfd, 1, 500) > 0 && (pfd.revents & POLLIN)) {
			struct sockaddr_in client_addr;
			socklen_t addr_len = sizeof(client_addr);
			int client_fd;
			char buf[4096];
			ssize_t nr;
			char method[16], path[256];

			client_fd = accept(ui->server_fd,
					   (struct sockaddr *)&client_addr,
					   &addr_len);
			if (client_fd < 0)
				goto ws_work;

			nr = read(client_fd, buf, sizeof(buf) - 1);
			if (nr <= 0) {
				close(client_fd);
				goto ws_work;
			}
			buf[nr] = '\0';

			/* Check for WebSocket upgrade */
			if (vibes_ws_is_upgrade(buf)) {
				if (vibes_ws_handshake(client_fd, buf) == 0) {
					vibes_ws_add_client(ui, client_fd);
					/* Don't close — fd is now a WS client */
				} else {
					close(client_fd);
				}
				goto ws_work;
			}

			parse_request(buf, method, sizeof(method),
				      path, sizeof(path));

			if (!strncmp(path, "/api/", 5)) {
				vibes_api_handle(ui, client_fd, method, path);
			} else {
				serve_index(client_fd);
			}

			close(client_fd);
		}

ws_work:
		/* Process incoming WebSocket messages (ping/pong, close) */
		vibes_ws_process_clients(ui);

		/* Push new events to WebSocket clients every 2 seconds */
		now = time(NULL);
		if (now - last_push >= 2) {
			vibes_ws_push_events(ui);
			last_push = now;
		}
	}

	return 0;
}

void vibes_webui_stop(struct vibes_webui *ui)
{
	int i;

	ui->running = 0;

	/* Close all WebSocket clients */
	for (i = 0; i < ui->nr_ws_clients; i++) {
		vibes_ws_send_close(ui->ws_clients[i]);
		close(ui->ws_clients[i]);
	}
	ui->nr_ws_clients = 0;

	if (ui->server_fd >= 0) {
		close(ui->server_fd);
		ui->server_fd = -1;
	}
}

#endif /* VIBES_ENABLED */
