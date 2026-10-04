// Smash&Clash for the Nintendo DS: a client of the real game at smashandclash.in,
// written against the Smash&Clash API (the one @smashandclash/sdk wraps).
//
// Plays in Delta (iPhone/iPad, DS online play on), melonDS, and on DS hardware.
// Both screens are 16-bit bitmaps drawn by the shared software renderer; the touch
// screen alone holds the whole game, so a one-screen skin loses nothing.
#include <dswifi9.h>
#include <fat.h>
#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "assets.h"
#include "snc_client.h"
#include "snc_draw.h"
#include "snc_platform.h"
#include "view.h"

#define VERSION "1.0.0"
#define USER_AGENT "smashandclash-ds/" VERSION " (Nintendo DS; +https://github.com/smashandclash/delta)"
#define SDK_TAG "smashandclash-c/" VERSION " (ds)"
#define RECORD_DIR "/data"
#define RECORD_PATH "/data/smashandclash.txt"

static snc_client client;
static snc_api api;
static view_t view;
static uint16_t top_buf[256 * 192] __attribute__((aligned(32)));
static uint16_t bot_buf[256 * 192] __attribute__((aligned(32)));
static snc_surf top, bot;
static uint16_t *vram_top, *vram_bot;
static int net_ok = -1;  // the TLS client: -1 starting, 0 ready, > 0 failed
static int have_fat;
static int frame_ready;

/* --------------------------------- the network -------------------------------- */

static int net_main(void *arg) {
	(void)arg;
	net_ok = snc_api_init(&api, USER_AGENT, SDK_TAG) ? 1 : 0;
	for (;;) {
		if (!net_ok && snc_client_net_step(&client, &api)) continue;
		cothread_yield_irq(IRQ_VBLANK);
	}
	return 0;
}

/* --------------------------------- the record --------------------------------- */

static void record_load(char *out, size_t cap) {
	out[0] = 0;
	if (!have_fat) return;
	FILE *f = fopen(RECORD_PATH, "rb");
	if (!f) return;
	size_t n = fread(out, 1, cap - 1, f);
	out[n] = 0;
	fclose(f);
}

static void record_save(void) {
	client.rec_dirty = 0;
	if (!have_fat) return;
	char text[600];
	snc_record_text(&client.rec, text, sizeof text);
	mkdir(RECORD_DIR, 0777);
	FILE *f = fopen(RECORD_PATH, "wb");
	if (!f) return;
	fwrite(text, 1, strlen(text), f);
	fclose(f);
}

// The console's nickname, from the firmware settings (UTF-16 -> UTF-8).
static void nickname(char *out, size_t cap) {
	size_t n = 0;
	int len = PersonalData->nameLen;
	if (len > 10) len = 10;
	for (int i = 0; i < len && n + 4 < cap; i++) {
		unsigned c = (uint16_t)PersonalData->name[i];
		if (c < 0x80) out[n++] = (char)c;
		else if (c < 0x800) out[n++] = (char)(0xC0 | (c >> 6)), out[n++] = (char)(0x80 | (c & 63));
		else out[n++] = (char)(0xE0 | (c >> 12)), out[n++] = (char)(0x80 | ((c >> 6) & 63)), out[n++] = (char)(0x80 | (c & 63));
	}
	out[n] = 0;
	if (!n) snprintf(out, cap, "DS player");
}

/* ---------------------------------- drawing ----------------------------------- */

static void present(void) {
	DC_FlushRange(top_buf, sizeof top_buf);
	DC_FlushRange(bot_buf, sizeof bot_buf);
	dmaCopyWords(3, top_buf, vram_top, sizeof top_buf);
	dmaCopyWords(3, bot_buf, vram_bot, sizeof bot_buf);
}

// The screen before the game: connecting to Wi-Fi, or why we could not.
static void boot_screen(const char *title, const char *text, int bad) {
	sd_world(&top);
	sd_logo_l(&top, (256 - LOGO_L_W) / 2, 20);
	sg_text_outline(&top, &font_label, (256 - sg_text_w(&font_label, "Played online at smashandclash.in")) / 2, 140,
	                "Played online at smashandclash.in", C_SUGAR, C_INK, 1);
	sd_world(&bot);
	sd_glass(&bot, 8, 8, 240, 176, 10, 215);
	sg_text(&bot, &font_body, 18, 16, title, bad ? 0xFF8CC8 : C_SUN1);
	sg_wrap(&bot, &font_small, 18, 20 + font_body.line, 220, font_small.line + 1, text, C_SUGAR, 13);
	present();
}

static const char WIFI_HELP[] =
	"Delta: turn on online play for DS games. If Delta asks you to choose a WFC server, pick any one "
	"(Wiimmfi works), then restart this game. Smash&Clash does not use the server: Delta only needs one "
	"chosen to switch the Wi-Fi on.\n"
	"melonDS: the Wi-Fi works out of the box.\n"
	"A real DS: set up a connection in a Wi-Fi game's settings (DS: open or WEP only).";

static void connect_wifi(void) {
	for (;;) {
		boot_screen("Connecting to Wi-Fi...", WIFI_HELP, 0);
		Wifi_AutoConnect();
		int status;
		do {
			cothread_yield_irq(IRQ_VBLANK);
			status = Wifi_AssocStatus();
		} while (status != ASSOCSTATUS_ASSOCIATED && status != ASSOCSTATUS_CANNOTCONNECT);
		if (status == ASSOCSTATUS_ASSOCIATED) return;
		boot_screen("No Wi-Fi connection. Press A to try again.", WIFI_HELP, 1);
		for (;;) {
			cothread_yield_irq(IRQ_VBLANK);
			scanKeys();
			if (keysDown() & (KEY_A | KEY_TOUCH)) break;
		}
	}
}

/* ----------------------------------- input ------------------------------------ */

static void handle_input(void) {
	uint32_t down = keysDown(), rep = keysDownRepeat();
	if (down & KEY_TOUCH) {
		touchPosition t;
		touchRead(&t);
		int i = view_hit_at(&view, t.px, t.py);
		view.show_focus = 0;
		if (i >= 0) view_press(&view, &client, i);
	}
	if (client.screen == SC_RULES) {
		if (rep & KEY_UP) view_scroll(&view, -2);
		if (rep & KEY_DOWN) view_scroll(&view, 2);
		if (rep & KEY_LEFT) view_scroll(&view, -10);
		if (rep & KEY_RIGHT) view_scroll(&view, 10);
		if (down & (KEY_A | KEY_B | KEY_X)) snc_act_back(&client);
		return;
	}
	if (rep & KEY_UP) view_nav(&view, 0, -1);
	if (rep & KEY_DOWN) view_nav(&view, 0, 1);
	if (rep & KEY_LEFT) view_nav(&view, -1, 0);
	if (rep & KEY_RIGHT) view_nav(&view, 1, 0);
	if (down & KEY_A) view_activate(&view, &client);
	if (down & KEY_B) view_back(&view, &client);
	if (down & KEY_L) view_step_hand(&view, &client, -1);  // step through your playable cards
	if (down & KEY_R) view_step_hand(&view, &client, 1);
	if (down & KEY_X) snc_act_how_to_play(&client);
	if (down & KEY_Y) snc_act_action(&client);
	if ((down & KEY_START) && client.screen == SC_GAME) snc_act_resign(&client);
	if ((down & KEY_SELECT) && snc_client_over(&client)) view.show_replay = !view.show_replay;
}

// A cheap fingerprint of what the screens show, to redraw only when it changes.
static uint32_t signature(void) {
	const snc_client *c = &client;
	uint32_t h = 2166136261u;
	const int parts[] = {c->screen, c->job.state, c->busy[0], (int)c->notice_until, c->game.move_count, c->game.status, c->game.your_turn,
	                     c->pick.selected, c->pick.path_n, c->code_len, c->rules_ready, c->review.ok, c->have_game, c->opp_hand,
	                     c->armed, (int)c->flash, c->pending_cell, view.scroll, view.show_replay, view.focus_id, view.focus_arg,
	                     view.show_focus, snc_client_notice(c) != NULL, c->rec.ruleset};
	for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) h = (h ^ (uint32_t)parts[i]) * 16777619u;
	return h;
}

// One line per change to the debug log (no$gba / melonDS): what an automated test reads.
static void log_state(void) {
	const snc_client *c = &client;
	const snc_game *g = &c->game;
	unsigned playable = 0;
	for (int i = 0; i < g->hand_n; i++)
		if (snc_playable(g, i)) playable |= 1u << i;
	snc_status_t st;
	snc_client_status(c, &st);
	const char *notice = snc_client_notice(c);
	fprintf(stderr, "[snc] code=%s scr=%d have=%d st=%d seat=%c turn=%d busy=%d hop=%d sel=%d path=%d hand=%d play=%x targets=%x action=%d over=%d score=%d-%d moves=%d main=\"%s\" notice=\"%s\"\n",
	        g->code[0] ? g->code : "-", c->screen, c->have_game, g->status, g->seat ? g->seat : '-', g->your_turn, c->busy[0] != 0, g->has_hop, c->pick.selected,
	        c->pick.path_n, g->hand_n, playable, snc_targets(g, &c->pick), st.action_id, snc_client_over(c), g->score_you, g->score_opp,
	        g->move_count, st.main, notice ? notice : "");
}

/* ----------------------------------- main ------------------------------------- */

int main(int argc, char **argv) {
	(void)argc, (void)argv;
	defaultExceptionHandler();
	systemCounterSetup();
	consoleDebugInit(DebugDevice_NOCASH);  // stderr -> the emulator's debug log

	videoSetMode(MODE_5_2D);
	videoSetModeSub(MODE_5_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankC(VRAM_C_SUB_BG);
	lcdMainOnTop();
	int bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	int bgs = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	vram_top = bgGetGfxPtr(bg);
	vram_bot = bgGetGfxPtr(bgs);
	sg_init(&top, top_buf, 256, 192, 256);
	sg_init(&bot, bot_buf, 256, 192, 256);
	keysSetRepeat(18, 6);

	boot_screen("Starting...", "", 0);
	have_fat = fatInitDefault();
	char record[600], name[48];
	record_load(record, sizeof record);
	nickname(name, sizeof name);
	snc_client_init(&client, name, "Nintendo DS", record);
	view_init(&view);

	if (!Wifi_InitDefault(INIT_ONLY | WIFI_ATTEMPT_DSI_MODE)) {
		boot_screen("Wi-Fi did not start.", "This DS (or emulator) has no Wi-Fi the game can use.", 1);
		for (;;) cothread_yield_irq(IRQ_VBLANK);
	}
	connect_wifi();
	fprintf(stderr, "[snc] wifi connected\n");

	boot_screen("Connecting to smashandclash.in...", "Setting up a secure connection.", 0);
	cothread_create(net_main, NULL, 64 * 1024, COTHREAD_DETACHED);
	while (net_ok < 0) cothread_yield_irq(IRQ_VBLANK);
	if (net_ok > 0) {
		boot_screen("The secure connection could not start.", api.err, 1);
		for (;;) cothread_yield_irq(IRQ_VBLANK);
	}
	snc_client_start(&client);

	uint32_t last_sig = 0, frames = 0;
	for (;;) {
		cothread_yield_irq(IRQ_VBLANK);
		if (frame_ready) {  // what was drawn last frame goes up during the blank
			present();
			frame_ready = 0;
		}
		frames++;
		scanKeys();
		int input = keysDown() || keysDownRepeat();
		handle_input();
		snc_client_tick(&client);
		if (client.rec_dirty) record_save();
		uint32_t sig = signature();
		int animating = client.busy[0] || (client.screen == SC_GAME && client.game.your_turn && snc_targets(&client.game, &client.pick));
		if (sig != last_sig) log_state();
		if (input || sig != last_sig || (animating && frames % 4 == 0) || frames % 30 == 0) {
			view_draw(&view, &client, &top, &bot, snc_now_ms());
			frame_ready = 1;
			last_sig = sig;
		}
	}
	return 0;
}
