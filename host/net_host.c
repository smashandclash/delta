// The platform layer on a desktop (Linux/macOS sockets): for testing the core.
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../core/snc_platform.h"

int snc_net_resolve(const char *host, uint32_t *ip) {
	if (getenv("SNC_NO_SYSTEM_DNS")) return -1;  // test the fallback resolver
	struct addrinfo hints, *res = NULL;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return -1;
	*ip = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
	freeaddrinfo(res);
	return 0;
}

int snc_net_tcp_connect(uint32_t ip, uint16_t port, int timeout_ms) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = ip;
	int fl = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	int r = connect(fd, (struct sockaddr *)&sa, sizeof sa);
	if (r < 0 && errno != EINPROGRESS) {
		close(fd);
		return -1;
	}
	if (r < 0) {
		struct pollfd p = {fd, POLLOUT, 0};
		if (poll(&p, 1, timeout_ms) != 1) {
			close(fd);
			return -1;
		}
		int err = 0;
		socklen_t len = sizeof err;
		getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
		if (err) {
			close(fd);
			return -1;
		}
	}
	fcntl(fd, F_SETFL, fl);
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	return fd;
}

int snc_net_send(int fd, const unsigned char *buf, size_t len) {
	ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);
	return n > 0 ? (int)n : -1;
}

int snc_net_recv(int fd, unsigned char *buf, size_t len, int timeout_ms) {
	struct pollfd p = {fd, POLLIN, 0};
	int r = poll(&p, 1, timeout_ms);
	if (r == 0) return SNC_NET_TIMEOUT;
	if (r < 0) return -1;
	ssize_t n = recv(fd, buf, len, 0);
	return n >= 0 ? (int)n : -1;
}

void snc_net_close(int fd) {
	close(fd);
}

int snc_net_udp_exchange(uint32_t ip, uint16_t port, const unsigned char *req, size_t reqlen, unsigned char *resp, size_t cap, int timeout_ms) {
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = ip;
	if (sendto(fd, req, reqlen, 0, (struct sockaddr *)&sa, sizeof sa) < 0) {
		close(fd);
		return -1;
	}
	struct pollfd p = {fd, POLLIN, 0};
	int r = poll(&p, 1, timeout_ms);
	int n = r == 1 ? (int)recv(fd, resp, cap, 0) : -1;
	close(fd);
	return n;
}

uint32_t snc_now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

void snc_entropy(unsigned char *out, size_t len) {
	FILE *f = fopen("/dev/urandom", "rb");
	if (f && fread(out, 1, len, f) == len) {
		fclose(f);
		return;
	}
	if (f) fclose(f);
	for (size_t i = 0; i < len; i++) out[i] = (unsigned char)rand();
}

int snc_clock_year(void) {
	time_t t = time(NULL);
	struct tm tm;
	gmtime_r(&t, &tm);
	return tm.tm_year + 1900;
}
