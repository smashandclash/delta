// Runs a console's view on a desktop against the live API and saves its screens as
// images at each step: the fastest way to see a layout change. It presses the same
// targets the console does, through view_press. Built twice (host/build.sh): for the
// Nintendo DS (two 256 x 192 screens) and, with -DPREVIEW_GBA, for the Game Boy Advance
// (240 x 160, with its sprite brackets drawn in).
//   run: ./ds_preview OUT_DIR [full|game|screens]   or   ./gba_preview OUT_DIR [...]
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../core/snc_client.h"
#include "../core/snc_gfx.h"
#include "../core/snc_platform.h"
#include "../core/snc_ui.h"
#include "view.h"

#ifdef PREVIEW_PSP
#define W 480
#define H 272
#define SCREENS 1
#define PLATFORM "PSP"
#elif defined(PREVIEW_GBA)
#define W 240
#define H 160
#define SCREENS 1
#define PLATFORM "Game Boy Advance"
#define ONE_SCREEN
#else
#define W 256
#define H 192
#define SCREENS 2
#define PLATFORM "Nintendo DS"
#endif

static snc_client client;
static snc_api api;
static view_t view;
static uint16_t px[SCREENS][W * H];
static snc_surf surf[SCREENS];
static const char *out_dir = ".";
static int shot_n;

static void *net_thread(void *arg) {
	(void)arg;
	for (;;)
		if (!snc_client_net_step(&client, &api)) usleep(5000);
	return NULL;
}

static void frame(void) {
	snc_client_tick(&client);
#if defined(PREVIEW_PSP) || defined(ONE_SCREEN)
	view_draw(&view, &client, &surf[0], snc_now_ms());
#else
	view_draw(&view, &client, &surf[0], &surf[1], snc_now_ms());
#endif
}

#ifdef PREVIEW_GBA
// What the GBA's sprites add over the bitmap: the brackets and the thinking dots.
static void sprite(int x, int y, const uint8_t art[8][8], int hflip, int vflip) {
	for (int j = 0; j < 8; j++)
		for (int i = 0; i < 8; i++) {
			int k = art[vflip ? 7 - j : j][hflip ? 7 - i : i];
			int px_x = x + i, px_y = y + j;
			if (!k || px_x < 0 || px_y < 0 || px_x >= W || px_y >= H) continue;
			uint32_t c = SPRITE_COLORS[k];
			px[0][px_y * W + px_x] = SNC_PX((c >> 16) & 255, (c >> 8) & 255, c & 255);
		}
}

static void sprites(void) {
	if (ui.cur_on) {
		int o = 1, x0 = ui.cur_x - 4 - o, y0 = ui.cur_y - 4 - o, x1 = ui.cur_x + ui.cur_w - 4 + o, y1 = ui.cur_y + ui.cur_h - 4 + o;
		sprite(x0, y0, CURSOR_CORNER, 0, 0);
		sprite(x1, y0, CURSOR_CORNER, 1, 0);
		sprite(x0, y1, CURSOR_CORNER, 0, 1);
		sprite(x1, y1, CURSOR_CORNER, 1, 1);
	}
	if (ui.dots_on)
		for (int i = 0; i < 3; i++) sprite(ui.dots_x + i * 6, ui.dots_y + (i == 1 ? 0 : 1), DOT, 0, 0);
}
#endif

static void shot(const char *name) {
	frame();
#ifdef PREVIEW_GBA
	sprites();
#endif
	char path[512];
	snprintf(path, sizeof path, "%s/%02d-%s.ppm", out_dir, shot_n++, name);
	FILE *f = fopen(path, "wb");
	fprintf(f, "P6\n%d %d\n255\n", W, H * SCREENS);
	for (int s = 0; s < SCREENS; s++)
		for (int i = 0; i < W * H; i++) {
			uint16_t p = px[s][i];
			unsigned char rgb[3] = {(unsigned char)SNC_PR(p), (unsigned char)SNC_PG(p), (unsigned char)SNC_PB(p)};
			fwrite(rgb, 1, 3, f);
		}
	fclose(f);
	printf("shot %s\n", path);
}

static void run_ms(int ms) {
	uint32_t until = snc_now_ms() + (uint32_t)ms;
	while ((int32_t)(snc_now_ms() - until) < 0) {
		frame();
		usleep(16000);
	}
}

static int idle(void) {
	return !client.busy[0] && client.job.state == 0 && client.pend_n == 0;
}

// Runs frames until cond() or the timeout. Returns cond().
static int until(int (*cond)(void), int ms) {
	uint32_t end = snc_now_ms() + (uint32_t)ms;
	while (!cond() && (int32_t)(snc_now_ms() - end) < 0) {
		frame();
		usleep(16000);
	}
	frame();
	return cond();
}

static int my_turn_or_over(void) {
	return !client.busy[0] && client.have_game && (client.game.your_turn || snc_client_over(&client));
}
static int not_busy(void) {
	return !client.busy[0];
}
static int waiting_room(void) {
	return client.have_game && client.game.status == SNC_WAITING && !client.busy[0];
}
static int rules_ready(void) {
	return client.rules_ready;
}
static int reviewed(void) {
	return client.review.ok;
}

static int press(int id, int arg) {
	frame();
	for (int i = 0; i < view.n; i++)
		if (view.hit[i].id == id && view.hit[i].arg == arg) {
			view_press(&view, &client, i);
			frame();
			return 1;
		}
	printf("no target %d/%d on screen\n", id, arg);
	return 0;
}

// One turn through the touch targets: a playable card, then green tiles until it plays.
static void play_turn(void) {
	snc_game *g = &client.game;
	if (g->has_hop) {
		uint16_t lit = snc_targets(g, &client.pick);
		for (int cell = 0; cell < 15; cell++)
			if (lit & (1u << cell)) {
				press(H_CELL, cell);
				return;
			}
		press(H_ACTION, 0);
		return;
	}
	int pick = -1;
	for (int i = 0; i < g->hand_n; i++)
		if (snc_playable(g, i) && (pick < 0 || g->hand[i].effect)) pick = i;
	if (pick < 0) return;
	press(H_HAND, pick);
	for (int step = 0; step < 3 && client.pick.selected >= 0; step++) {
		uint16_t lit = snc_targets(g, &client.pick);
		if (!lit) {
			press(H_ACTION, 0);  // Flip! / Swap!: no tile
			return;
		}
		for (int cell = 0; cell < 15; cell++)
			if (lit & (1u << cell)) {
				press(H_CELL, cell);
				break;
			}
	}
}

int main(int argc, char **argv) {
	out_dir = argc > 1 ? argv[1] : ".";
	const char *scenario = argc > 2 ? argv[2] : "full";
	mkdir(out_dir, 0755);
	for (int s = 0; s < SCREENS; s++) sg_init(&surf[s], px[s], W, H, W);
	view_init(&view);
	snc_client_init(&client, "Harshit", PLATFORM, NULL);
	if (snc_api_init(&api, "smashandclash-preview/1.0 (desktop)", "smashandclash-c/1.0 (preview)")) {
		printf("api init: %s\n", api.err);
		return 1;
	}
	pthread_t t;
	pthread_create(&t, NULL, net_thread, NULL);
	snc_client_start(&client);

	shot("lobby");
	if (!strcmp(scenario, "full") || !strcmp(scenario, "game")) {
		press(H_NEW, 0);
		shot("dealing");
		until(my_turn_or_over, 30000);
		shot("game-start");
		int turn = 0;
		while (client.have_game && !snc_client_over(&client) && turn < 20) {
			if (!until(my_turn_or_over, 40000)) break;
			if (snc_client_over(&client)) break;
			turn++;
			// show the pick on the first turns
			snc_game *g = &client.game;
			if (turn <= 2 && !g->has_hop) {
				int pick = -1;
				for (int i = 0; i < g->hand_n; i++)
					if (snc_playable(g, i)) {
						pick = i;
						break;
					}
				if (pick >= 0) {
					view.show_focus = turn == 2;
					press(H_HAND, pick);
					shot(turn == 1 ? "card-picked" : "card-picked-focus");
#ifdef PREVIEW_GBA
					if (turn == 1) {  // SELECT: the inspector
						view_toggle_inspect(&view, &client);
						shot("inspect");
						view_toggle_inspect(&view, &client);
					}
#endif
					press(H_HAND, pick);  // put it back
				}
			}
			if (g->has_hop) shot("hop");
			char before[48];
			snprintf(before, sizeof before, "%s", g->last_move);
			play_turn();
			if (turn == 1) shot("thinking");
			until(my_turn_or_over, 40000);
			char name[32];
			snprintf(name, sizeof name, "turn-%02d", turn);
			shot(name);
		}
		until(reviewed, 15000);
		shot("game-over");
		press(H_REPLAY, 0);
		shot("replay-qr");
		press(H_REPLAY, 0);
		press(H_LOBBY, 0);
		shot("lobby-after");
	}
	if (!strcmp(scenario, "full") || !strcmp(scenario, "screens")) {
		press(H_HOW, 0);
		until(rules_ready, 15000);
		shot("rules");
		press(H_DOWN, 0);
		shot("rules-scrolled");
		press(H_BACK, 0);
		press(H_RULESET, 0);
		shot("rules-classic");
		press(H_RULESET, 0);
		press(H_CODE, 0);
		shot("code-empty");
		for (int i = 0; i < 4; i++) press(H_KEY, (i * 5) % 32);
		shot("code-typing");
		press(H_BACK, 0);
		press(H_INVITE, 0);
		until(waiting_room, 30000);
		shot("invite");
		press(H_RESIGN, 0);
		until(not_busy, 30000);
		run_ms(300);
		press(H_CODE, 0);
		press(H_HOST, 0);
		until(waiting_room, 30000);
		shot("host-code");
		press(H_RESIGN, 0);
		until(not_busy, 30000);
		press(H_QUICK, 0);
		until(waiting_room, 30000);
		shot("quick-match");
		press(H_RESIGN, 0);
		until(not_busy, 30000);
		run_ms(300);
		shot("lobby-end");
	}
	(void)idle;
	return 0;
}
