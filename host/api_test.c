// Plays one game against the house through the C core, on a desktop: the core's smoke test.
//   cc -o api_test host/api_test.c host/net_host.c core/*.c -lmbedtls -lmbedx509 -lmbedcrypto
#include <stdio.h>
#include <string.h>

#include "../core/snc_api.h"

static snc_api api;
static snc_game g;

int main(int argc, char **argv) {
	const char *code = argc > 1 ? argv[1] : NULL;
	if (snc_api_init(&api, "smashandclash-c-test/1.0", "smashandclash-c/1.0 (test)")) {
		printf("init failed: %s\n", api.err);
		return 1;
	}
	char rules[8192];
	int r = snc_rules(&api, rules, sizeof rules);
	printf("rules: %d (%zu chars)%s\n", r, strlen(rules), r ? api.err : "");

	r = code ? snc_join_code(&api, code, "C test", &g) : snc_start_house(&api, "C test", 'm', 1000, &g);
	if (r) {
		printf("start failed %d: %s\n", r, api.err);
		return 1;
	}
	printf("game %s seat %c status %d vs %s (%c) moves %d hand %d draw %d chess %d power %d overrun %d\n", g.id, g.seat, g.status,
	       g.players[g.seat == 'A' ? 1 : 0], g.kinds[g.seat == 'A' ? 1 : 0], g.moves_n, g.hand_n, g.draw_pile, g.chess_n, g.power_n, g.overrun_n);
	int turns = 0;
	while (g.status == SNC_ACTIVE || g.status == SNC_WAITING) {
		if (!g.your_turn) {
			r = snc_wait(&api, &g, 20);
			if (r) {
				printf("wait failed %d: %s\n", r, api.err);
				break;
			}
			continue;
		}
		// prefer an effect or a hop when there is one, to exercise the move names
		int pick = 0;
		for (int i = 0; i < g.moves_n; i++)
			if (!strchr(g.moves[i], '@')) pick = i;
		printf("turn %d: %d moves, playing %s (reused=%d)\n", ++turns, g.moves_n, g.moves[pick], api.http.connected);
		r = snc_play(&api, &g, g.moves[pick]);
		if (r) {
			printf("play failed %d: %s\n", r, api.err);
			break;
		}
		printf("  -> last %s score you %d opp %d status %d\n", g.last_move, g.score_you, g.score_opp, g.status);
	}
	printf("over: winner %c score %d-%d replay %s\n", g.winner, g.score[0], g.score[1], g.replay_url);
	snc_review_t rv;
	r = snc_review(&api, g.id, &rv);
	printf("review %d: A %d%% B %d%% %s\n", r, rv.accuracy[0], rv.accuracy[1], r ? api.err : "");
	return 0;
}
