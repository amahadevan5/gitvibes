#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
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
		"#data{white-space:pre;font-size:13px;}"
		".status{display:inline-block;padding:2px 8px;"
		"border-radius:4px;font-size:12px;}"
		".active{background:#2d5a1e;color:#5fff5f;}"
		".done{background:#1e4a5a;color:#5fb3ff;}"
		"</style></head><body>"
		"<h1>gitvibes dashboard</h1>"
		"<div class='card'><h3>Intents</h3>"
		"<div id='intents'>Loading...</div></div>"
		"<div class='card'><h3>Tasks</h3>"
		"<div id='tasks'>Loading...</div></div>"
		"<div class='card'><h3>Events</h3>"
		"<div id='events'>Loading...</div></div>"
		"<script>"
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
		"r=await fetch('/api/events');"
		"d=await r.json();"
		"document.getElementById('events').innerHTML="
		"d.map(e=>'<div>'+e.type+' '+(e.agent_id||'')+'</div>')"
		".join('')||'None';"
		"}catch(e){console.error(e);}"
		"}"
		"load();setInterval(load,5000);"
		"</script></body></html>";

	send_response(fd, 200, "text/html",
		      html, sizeof(html) - 1);
}

int vibes_webui_run(struct vibes_webui *ui)
{
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	signal(SIGPIPE, SIG_IGN);

	while (ui->running && g_running) {
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
			continue;

		nr = read(client_fd, buf, sizeof(buf) - 1);
		if (nr <= 0) {
			close(client_fd);
			continue;
		}
		buf[nr] = '\0';

		parse_request(buf, method, sizeof(method),
			      path, sizeof(path));

		if (!strncmp(path, "/api/", 5)) {
			vibes_api_handle(ui, client_fd, method, path);
		} else {
			serve_index(client_fd);
		}

		close(client_fd);
	}

	return 0;
}

void vibes_webui_stop(struct vibes_webui *ui)
{
	ui->running = 0;
	if (ui->server_fd >= 0) {
		close(ui->server_fd);
		ui->server_fd = -1;
	}
}

#endif /* VIBES_ENABLED */
