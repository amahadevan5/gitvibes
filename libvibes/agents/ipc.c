#include "git-compat-util.h"
#ifdef VIBES_ENABLED

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include "strbuf.h"
#include "libvibes/agents/agents.h"

/*
 * IPC: Unix domain socket communication between orchestrator and agents.
 * Simple protocol: [4-byte length][message payload]
 */

int vibes_ipc_server_start(const char *socket_path)
{
	struct sockaddr_un addr;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return error_errno("gitvibes: cannot create socket");

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, socket_path, sizeof(addr.sun_path));

	unlink(socket_path);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return error_errno("gitvibes: cannot bind socket");
	}

	if (listen(fd, 5) < 0) {
		close(fd);
		return error_errno("gitvibes: cannot listen on socket");
	}

	return fd;
}

int vibes_ipc_client_connect(const char *socket_path)
{
	struct sockaddr_un addr;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return error_errno("gitvibes: cannot create socket");

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, socket_path, sizeof(addr.sun_path));

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

int vibes_ipc_send(int fd, const char *message)
{
	uint32_t len = strlen(message);
	uint32_t net_len = htonl(len);

	if (write(fd, &net_len, 4) != 4)
		return error_errno("gitvibes: ipc write length failed");

	if (write(fd, message, len) != (ssize_t)len)
		return error_errno("gitvibes: ipc write message failed");

	return 0;
}

int vibes_ipc_recv(int fd, struct strbuf *buffer)
{
	uint32_t net_len, len;
	struct pollfd pfd;
	ssize_t n;
	char *buf;

	/* Wait up to 30 seconds for data */
	pfd.fd = fd;
	pfd.events = POLLIN;
	if (poll(&pfd, 1, 30000) <= 0)
		return -1;

	if (read(fd, &net_len, 4) != 4)
		return -1;

	len = ntohl(net_len);
	if (len > 1024 * 1024) /* 1MB max message */
		return error("gitvibes: ipc message too large (%u bytes)", len);

	buf = xmalloc(len);
	n = read(fd, buf, len);
	if (n != (ssize_t)len) {
		free(buf);
		return -1;
	}

	strbuf_add(buffer, buf, len);
	free(buf);
	return 0;
}

void vibes_ipc_close(int fd)
{
	if (fd >= 0)
		close(fd);
}

#endif /* VIBES_ENABLED */
