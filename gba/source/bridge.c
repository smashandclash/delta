// The core's SDK calls, the GBA way (see snc_bridge.h): the GBA has no network, so each
// call is written into a mailbox in its RAM. The mGBA script finds the mailbox by its
// magic, beats its heartbeat every frame, sends each new request to the SDK bridge and
// writes the answer straight into the buffer the request names, then its sequence number.
//
// The mailbox (little-endian words after the 16-byte magic):
//   0 magic "SNC-GBA-BRIDGE1"  16 req_seq  20 req_len  24 resp_seq  28 resp_len
//   32 resp_status  36 heartbeat  40 connected  44 resp_cap  48 resp_addr  64 req[1024]
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gba_bridge.h"
#include "snc_bridge.h"
#include "snc_errors.h"
#include "snc_json.h"
#include "snc_platform.h"

#define REQ_CAP 1024
#define MAGIC "SNC-GBA-BRIDGE1"

typedef struct {
	char magic[16];
	volatile uint32_t req_seq, req_len, resp_seq, resp_len, resp_status, beat, connected, resp_cap, resp_addr, spare[3];
	char req[REQ_CAP];
} mailbox_t;

_Static_assert(__builtin_offsetof(mailbox_t, req) == 64, "the script reads the request at 64");

__attribute__((section(".ewram_bss"), aligned(4))) static mailbox_t box;
static volatile int cancelled;
static uint32_t last_beat, beat_at, down_since;

static const char NO_SCRIPT[] = "The game lost the mGBA script. In mGBA: Tools > Scripting > File > Load script > smashandclash.lua.";
static const char NO_BRIDGE[] = "The SDK bridge is not running. On this computer: node bridge.mjs (in the bridge folder).";

void gba_bridge_init(void) {
	memset(&box, 0, sizeof box);
	memcpy(box.magic, MAGIC, sizeof MAGIC);  // the script looks for this
}

void gba_bridge_poll(void) {
	uint32_t now = snc_now_ms();
	if (box.beat != last_beat) {
		last_beat = box.beat;
		beat_at = now ? now : 1;
	}
	if (box.connected && gba_bridge_script()) down_since = 0;
	else if (!down_since) down_since = now ? now : 1;
}

int gba_bridge_script(void) {
	return beat_at && snc_now_ms() - beat_at < 1500;
}

int gba_bridge_connected(void) {
	return gba_bridge_script() && box.connected;
}

void snc_bridge_cancel(void) {
	cancelled = 1;
}

int snc_bridge_request(const char *op, const char *method, const char *path, const char *token, const char *body, char *out, size_t cap,
                       int timeout_ms, int *status, char *err, size_t errcap) {
	char qop[48], qmethod[16], qpath[200], qtoken[96];
	sj_quote(qop, sizeof qop, op);
	sj_quote(qmethod, sizeof qmethod, method);
	sj_quote(qpath, sizeof qpath, path);
	if (token) sj_quote(qtoken, sizeof qtoken, token);
	else snprintf(qtoken, sizeof qtoken, "null");
	int n = snprintf(box.req, REQ_CAP, "{\"op\":%s,\"method\":%s,\"path\":%s,\"token\":%s,\"body\":%s}", qop, qmethod, qpath, qtoken, body ? body : "null");
	if (n < 0 || n >= REQ_CAP) {
		snprintf(err, errcap, "The request was too big to send.");
		return SNC_E_TOOBIG;
	}
	cancelled = 0;
	uint32_t seq = box.req_seq + 1;
	if (!seq) seq = 1;
	box.resp_addr = (uint32_t)(uintptr_t)out;
	box.resp_cap = (uint32_t)cap - 1;
	box.req_len = (uint32_t)n;
	box.req_seq = seq;  // last: the script sends it on its next frame
	uint32_t start = snc_now_ms();
	while (box.resp_seq != seq) {
		if (cancelled) {
			cancelled = 0;
			snprintf(err, errcap, "Cancelled.");
			return SNC_E_CANCEL;
		}
		uint32_t now = snc_now_ms();
		if (!gba_bridge_script()) {
			snprintf(err, errcap, "%s", NO_SCRIPT);
			return SNC_E_BRIDGE;
		}
		if (down_since && now - down_since > 3000) {  // the script reconnects every two seconds: give it one try
			snprintf(err, errcap, "%s", NO_BRIDGE);
			return SNC_E_BRIDGE;
		}
		if ((int32_t)(now - start) > timeout_ms) {
			snprintf(err, errcap, "The SDK bridge did not answer in time.");
			return SNC_E_TIMEOUT;
		}
		gba_bridge_frame();
		gba_bridge_poll();
	}
	uint32_t len = box.resp_len;
	*status = (int)box.resp_status;
	if (len > cap - 1) {
		snprintf(err, errcap, "The answer was too big for the GBA's memory.");
		return SNC_E_TOOBIG;
	}
	out[len] = 0;
	return (int)len;
}
