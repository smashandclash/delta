// The platform layer on the Nintendo DS: DSWiFi's sockets (lwIP), the system tick
// counter, the RTC. Runs on the network cothread; blocking calls yield to the others.
#include <dswifi9.h>
#include <netdb.h>
#include <netinet/in.h>
#include <nds.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "snc_platform.h"

int snc_net_resolve(const char *host, uint32_t *ip) {
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
	(void)timeout_ms;  // lwIP gives up on its own when nobody answers
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = ip;
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
		closesocket(fd);
		return -1;
	}
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	return fd;
}

int snc_net_send(int fd, const unsigned char *buf, size_t len) {
	int n = send(fd, buf, len, 0);
	return n > 0 ? n : -1;
}

int snc_net_recv(int fd, unsigned char *buf, size_t len, int timeout_ms) {
	struct pollfd p = {fd, POLLIN, 0};
	int r = poll(&p, 1, timeout_ms);
	if (r == 0) return SNC_NET_TIMEOUT;
	if (r < 0) return -1;
	int n = recv(fd, buf, len, 0);
	return n >= 0 ? n : -1;
}

void snc_net_close(int fd) {
	closesocket(fd);
}

int snc_net_udp_exchange(uint32_t ip, uint16_t port, const unsigned char *req, size_t reqlen, unsigned char *resp, size_t cap, int timeout_ms) {
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = ip;
	int n = -1;
	if (sendto(fd, req, reqlen, 0, (struct sockaddr *)&sa, sizeof sa) >= 0) {
		struct pollfd p = {fd, POLLIN, 0};
		if (poll(&p, 1, timeout_ms) == 1) n = recvfrom(fd, resp, cap, 0, NULL, NULL);
	}
	closesocket(fd);
	return n;
}

uint32_t snc_now_ms(void) {
	return (uint32_t)(systemCounterGetTicks() * 1000ull / 523656ull);
}

void snc_entropy(unsigned char *out, size_t len) {
	// No hardware RNG on a DS: stir what moves (the tick counter, the scanline, the
	// clock, the stylus, the radio) through a mixing function.
	uint32_t s = (uint32_t)systemCounterGetTicks() ^ ((uint32_t)REG_VCOUNT << 16) ^ (uint32_t)time(NULL);
	unsigned char mac[6] = {0};
	Wifi_GetData(WIFIGETDATA_MACADDRESS, 6, mac);
	for (int i = 0; i < 6; i++) s = (s ^ mac[i]) * 16777619u;
	touchPosition t;
	touchRead(&t);
	s ^= ((uint32_t)t.rawx << 20) ^ ((uint32_t)t.rawy << 8) ^ (uint32_t)Wifi_GetData(WIFIGETDATA_RSSI, 0, NULL);
	for (size_t i = 0; i < len; i++) {
		s ^= (uint32_t)systemCounterGetTicks() + (uint32_t)REG_VCOUNT * 2654435761u;
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		out[i] = (unsigned char)(s >> 11);
	}
}

int snc_clock_year(void) {
	time_t t = time(NULL);
	struct tm *tm = gmtime(&t);
	return tm ? tm->tm_year + 1900 : 2000;
}
