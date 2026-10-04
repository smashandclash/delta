// Smash&Clash for the Game Boy Advance: a client of the real game at smashandclash.in,
// built on the Smash&Clash SDK.
//
// The GBA has no network, so every SDK call goes out through a mailbox in RAM: the mGBA
// script (gba/bridge/smashandclash.lua) carries it to the SDK bridge (gba/bridge/bridge.mjs),
// which makes the call with @smashandclash/sdk and sends back what the SDK returned.
// The screen is a mode 3 bitmap drawn by the shared software renderer into EWRAM and
// copied up in the vertical blank; the selection brackets and the thinking dots are
// hardware sprites, so moving around never redraws the screen.
#include <stdio.h>
#include <string.h>
#include <tonc.h>

#include "assets.h"
#include "gba_bridge.h"
#include "snc_client.h"
#include "snc_draw.h"
#include "snc_platform.h"
#include "view.h"

#define VERSION "1.0.0"

#define EWRAM_BSS_ATTR __attribute__((section(".ewram_bss"), aligned(4)))

EWRAM_BSS_ATTR static uint16_t back[GBA_W * GBA_H];
EWRAM_BSS_ATTR static snc_client client;
EWRAM_BSS_ATTR static snc_api api;
EWRAM_BSS_ATTR static view_t view;
EWRAM_BSS_ATTR static char state_line[1200];  // "SNC-STATE: ..." for automated tests (they read RAM)
static snc_surf scr;
static OBJ_ATTR obj_buffer[8];
static int frame_ready;
static uint32_t frames, last_sig;
static int booting = 1;
static uint32_t draw_ms;  // how long the last redraw took

// mGBA reads the save type from this tag: 32 KB of battery-backed SRAM.
__attribute__((used, aligned(4))) const char save_type[] = "SRAM_V113";

/* ------------------------------- time and logs -------------------------------- */

uint32_t snc_now_ms(void) {
	uint16_t hi = REG_TM3D, lo = REG_TM2D;
	if (REG_TM3D != hi) hi = REG_TM3D, lo = REG_TM2D;  // the low half wrapped while we read
	uint32_t ticks = ((uint32_t)hi << 16) | lo;           // 16384 a second
	return (uint32_t)(((uint64_t)ticks * 125) >> 11);
}

// mGBA's debug log (Tools > View logs), when the game runs in mGBA.
#define REG_DEBUG_ENABLE (*(volatile uint16_t *)0x4FFF780)
#define REG_DEBUG_FLAGS (*(volatile uint16_t *)0x4FFF700)
#define REG_DEBUG_STRING ((volatile char *)0x4FFF600)
static int mgba_log_on;

static void mgba_log(const char *text) {
	if (!mgba_log_on) return;
	int i = 0;
	for (; text[i] && i < 255; i++) REG_DEBUG_STRING[i] = text[i];
	REG_DEBUG_STRING[i] = 0;
	REG_DEBUG_FLAGS = 3 | 0x100;  // info
}

/* --------------------------------- the record --------------------------------- */

#define SRAM ((volatile uint8_t *)0x0E000000)

static void record_load(char *out, size_t cap) {
	out[0] = 0;
	if (SRAM[0] != 'S' || SRAM[1] != 'N' || SRAM[2] != 'C' || SRAM[3] != '1') return;
	size_t n = (size_t)SRAM[4] | ((size_t)SRAM[5] << 8);
	if (n >= cap) return;
	uint8_t sum = 0;
	for (size_t i = 0; i < n; i++) out[i] = (char)SRAM[6 + i], sum += (uint8_t)out[i];
	out[n] = 0;
	if (sum != SRAM[6 + n]) out[0] = 0;  // half written: start fresh
}

static void record_save(void) {
	client.rec_dirty = 0;
	char text[600];
	snc_record_text(&client.rec, text, sizeof text);
	size_t n = strlen(text);
	uint8_t sum = 0;
	SRAM[0] = 0;  // invalid until the end is written
	SRAM[4] = (uint8_t)n, SRAM[5] = (uint8_t)(n >> 8);
	for (size_t i = 0; i < n; i++) SRAM[6 + i] = (uint8_t)text[i], sum += (uint8_t)text[i];
	SRAM[6 + n] = sum;
	SRAM[1] = 'N', SRAM[2] = 'C', SRAM[3] = '1', SRAM[0] = 'S';
}

/* ---------------------------------- sprites ----------------------------------- */

static void sprite_tile(int index, const uint8_t art[8][8]) {
	uint32_t *t = (uint32_t *)&tile_mem[5][index];  // mode 3 leaves the sprites the upper half
	for (int y = 0; y < 8; y++) {
		uint32_t row = 0;
		for (int x = 0; x < 8; x++) row |= (uint32_t)art[y][x] << (x * 4);
		t[y] = row;
	}
}

static void sprites_init(void) {
	sprite_tile(0, CURSOR_CORNER);
	sprite_tile(1, DOT);
	for (int i = 1; i < 4; i++) {
		uint32_t c = SPRITE_COLORS[i];
		pal_obj_mem[i] = RGB15(((c >> 16) & 255) >> 3, ((c >> 8) & 255) >> 3, (c & 255) >> 3);
	}
	oam_init(obj_buffer, 8);
}

static void sprite_at(int i, int tile, int x, int y, int hflip, int vflip) {
	obj_set_attr(&obj_buffer[i], ATTR0_SQUARE | ATTR0_4BPP | ATTR0_REG, ATTR1_SIZE_8 | (hflip ? ATTR1_HFLIP : 0) | (vflip ? ATTR1_VFLIP : 0),
	             ATTR2_ID(512 + tile) | ATTR2_PALBANK(0) | ATTR2_PRIO(0));
	obj_set_pos(&obj_buffer[i], x & 511, y & 255);
}

// The brackets round the focus (breathing) and the dots after a busy line (bouncing).
static void sprites_update(void) {
	for (int i = 0; i < 8; i++) obj_hide(&obj_buffer[i]);
	if (!booting && ui.cur_on) {
		int o = view_cursor_offset(frames);
		int x0 = ui.cur_x - 4 - o, y0 = ui.cur_y - 4 - o, x1 = ui.cur_x + ui.cur_w - 4 + o, y1 = ui.cur_y + ui.cur_h - 4 + o;
		sprite_at(0, 0, x0, y0, 0, 0);
		sprite_at(1, 0, x1, y0, 1, 0);
		sprite_at(2, 0, x0, y1, 0, 1);
		sprite_at(3, 0, x1, y1, 1, 1);
	}
	if (!booting && ui.dots_on)
		for (int i = 0; i < 3; i++) {
			int phase = (int)((frames / 6 + 3 - i) % 4);
			sprite_at(4 + i, 1, ui.dots_x + i * 6, ui.dots_y + (phase == 0 ? 0 : 1), 0, 0);
		}
	oam_copy(oam_mem, obj_buffer, 8);
}

/* --------------------------------- the screen --------------------------------- */

static void present(void) {
	dma3_cpy(vid_mem, back, sizeof back);  // in the blank: the copy stays ahead of the beam
}

// The cursor follows the focus without a redraw: the targets from the last draw still hold.
static void follow_focus(void) {
	int f = view_find(&view, view.focus_id, view.focus_arg);
	if (f < 0 || ui.inspect) {
		ui.cur_on = 0;
		return;
	}
	const view_hit *t = &view.hit[f];
	ui.cur_x = t->x, ui.cur_y = t->y, ui.cur_w = t->w, ui.cur_h = t->h;
	if (t->id == H_HAND) {
		ui.cur_w = ART_S_W;
		if (client.pick.selected == t->arg) ui.cur_y -= 3;
	}
	ui.cur_on = 1;
}

// A fingerprint of what the screen shows (the focus aside: the sprites show that).
static uint32_t signature(void) {
	const snc_client *c = &client;
	uint32_t h = 2166136261u;
	const int parts[] = {c->screen, c->job.state, c->busy[0], (int)c->notice_until, c->game.move_count, c->game.status, c->game.your_turn,
	                     c->pick.selected, c->pick.path_n, c->code_len, c->rules_ready, c->review.ok, c->have_game, c->opp_hand,
	                     c->armed, (int)c->flash, c->pending_cell, view.scroll, view.show_replay, snc_client_notice(c) != NULL,
	                     c->rec.ruleset, ui.inspect, c->game.has_hop, c->code[c->code_len ? c->code_len - 1 : 0]};
	for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) h = (h ^ (uint32_t)parts[i]) * 16777619u;
	return h;
}

// One line per change, in RAM (tests read it) and in mGBA's log.
static void log_state(void) {
	const snc_client *c = &client;
	const snc_game *g = &c->game;
	unsigned playable = 0;
	for (int i = 0; i < g->hand_n; i++)
		if (snc_playable(g, i)) playable |= 1u << i;
	snc_status_t st;
	snc_client_status(c, &st);
	const char *notice = snc_client_notice(c);
	char *p = state_line;
	size_t cap = sizeof state_line;
	int n = snprintf(p, cap,
	                 "SNC-STATE: boot=%d code=%s scr=%d have=%d st=%d seat=%c turn=%d busy=%d hop=%d sel=%d path=%d hand=%d play=%x targets=%x action=%d over=%d "
	                 "score=%d-%d moves=%d rating=%d focus=%d:%d insp=%d draw=%d main=\"%s\" notice=\"%s\" hits=",
	                 booting, g->code[0] ? g->code : "-", c->screen, c->have_game, g->status, g->seat ? g->seat : '-', g->your_turn, c->busy[0] != 0,
	                 g->has_hop, c->pick.selected, c->pick.path_n, g->hand_n, playable, snc_targets(g, &c->pick), st.action_id, snc_client_over(c),
	                 g->score_you, g->score_opp, g->move_count, c->rec.rating, view.focus_id, view.focus_arg, ui.inspect, (int)draw_ms, st.main,
	                 notice ? notice : "");
	for (int i = 0; i < view.n && n > 0 && (size_t)n + 24 < cap; i++) {
		const view_hit *t = &view.hit[i];
		n += snprintf(p + n, cap - (size_t)n, "%d:%d:%d:%d:%d:%d;", t->id, t->arg, t->x, t->y, t->w, t->h);
	}
	char brief[256];
	snprintf(brief, sizeof brief, "[snc] scr=%d st=%d turn=%d busy=%d score=%d-%d main=\"%s\"", c->screen, g->status, g->your_turn, c->busy[0] != 0,
	         g->score_you, g->score_opp, st.main);
	mgba_log(brief);
}

/* ----------------------------------- input ------------------------------------ */

// The buttons are read in the vertical-blank interrupt, so a press is never lost while
// the screen redraws (a full redraw takes several frames).
static volatile uint16_t keys_held, keys_new;
static volatile uint32_t vbl_count;
static uint16_t hit, rep;  // this frame: newly pressed, and pressed-or-repeating (the D-pad)

static void vblank_isr(void) {
	uint16_t cur = ~REG_KEYINPUT & KEY_MASK;
	keys_new |= cur & ~keys_held;
	keys_held = cur;
	vbl_count++;
}

static void read_keys(void) {
	static uint32_t next_rep[10];
	REG_IME = 0;
	hit = keys_new;
	keys_new = 0;
	uint16_t held = keys_held;
	uint32_t now = vbl_count;
	REG_IME = 1;
	rep = hit;
	for (int k = 4; k < 10; k++) {  // right, left, up, down, R, L
		uint16_t bit = (uint16_t)(1u << k);
		if (hit & bit) next_rep[k] = now + 18;
		else if ((held & bit) && now >= next_rep[k]) rep |= bit, next_rep[k] = now + 5;
	}
}

#define key_hit(k) (hit & (k))
#define key_repeat(k) (rep & (k))

static void handle_input(void) {
	if (ui.inspect) {
		if (key_hit(KEY_B | KEY_A | KEY_SELECT)) view_toggle_inspect(&view, &client);
		return;
	}
	if (client.screen == SC_RULES) {
		if (key_repeat(KEY_UP)) view_scroll(&view, -2);
		if (key_repeat(KEY_DOWN)) view_scroll(&view, 2);
		if (key_repeat(KEY_LEFT | KEY_L)) view_scroll(&view, -10);
		if (key_repeat(KEY_RIGHT | KEY_R)) view_scroll(&view, 10);
		if (key_hit(KEY_A | KEY_B)) snc_act_back(&client);
		return;
	}
	if (key_repeat(KEY_UP)) view_nav(&view, 0, -1);
	if (key_repeat(KEY_DOWN)) view_nav(&view, 0, 1);
	if (key_repeat(KEY_LEFT)) view_nav(&view, -1, 0);
	if (key_repeat(KEY_RIGHT)) view_nav(&view, 1, 0);
	if (key_hit(KEY_A)) view_activate(&view, &client);
	if (key_hit(KEY_B)) view_back(&view, &client);
	if (key_hit(KEY_L)) view_step_hand(&view, &client, -1);  // step through your playable cards
	if (key_hit(KEY_R)) view_step_hand(&view, &client, 1);
	if (key_hit(KEY_START) && client.screen == SC_GAME) snc_act_resign(&client);
	if (key_hit(KEY_SELECT)) {
		if (snc_client_over(&client)) view.show_replay = !view.show_replay;
		else view_toggle_inspect(&view, &client);
	}
}

/* ----------------------------------- frames ----------------------------------- */

static void vblank(void) {
	VBlankIntrWait();
	if (frame_ready) {  // what was drawn goes up during the blank
		present();
		frame_ready = 0;
	}
	sprites_update();
	frames++;
	gba_bridge_poll();
}

// One frame of the game. The SDK bridge runs these while a call is out, too.
void gba_bridge_frame(void) {
	vblank();
	if (booting) return;
	read_keys();
	handle_input();
	snc_client_tick(&client);
	if (client.rec_dirty) record_save();
	uint32_t sig = signature();
	if (sig != last_sig) {
		uint32_t t0 = snc_now_ms();
		view_draw(&view, &client, &scr, t0);
		draw_ms = snc_now_ms() - t0;
		frame_ready = 1;
		last_sig = sig;
	} else {
		follow_focus();
	}
	static int last_focus = -1;
	int focus = view.focus_id * 256 + view.focus_arg;
	if (frame_ready || focus != last_focus) log_state();
	last_focus = focus;
}

// Before the game: wait for the mGBA script and the SDK bridge, saying what to do.
static void boot(void) {
	int shown = -1;
	while (!gba_bridge_connected()) {
		int lua = gba_bridge_script();
		int stage = lua ? 1 : 0;
		if (stage != shown) {
			if (lua)
				view_boot(&scr, "Almost there: start the SDK bridge",
				          "The mGBA script is running. Now, on this computer, in the bridge folder:  npm install, then  node bridge.mjs", 0);
			else
				view_boot(&scr, "Connecting to the SDK bridge...",
				          "1. On this computer:  node bridge.mjs  (the bridge folder: npm install first).\n"
				          "2. In mGBA: Tools > Scripting > File > Load script > smashandclash.lua",
				          0);
			frame_ready = 1;
			shown = stage;
			snprintf(state_line, sizeof state_line, "SNC-STATE: boot=1 lua=%d up=0", lua);
		}
		vblank();
	}
	view_boot(&scr, "Connected!", "The Smash&Clash SDK is on the line.", 0);
	frame_ready = 1;
	vblank();
	mgba_log("[snc] the SDK bridge is on the line");
}

int main(void) {
	REG_WAITCNT = 0x4317;  // SRAM 8 cycles, ROM 3/1 with the prefetch on
	irq_init(NULL);
	irq_add(II_VBLANK, vblank_isr);
	REG_TM2CNT = 0;
	REG_TM3CNT = 0;
	REG_TM2D = 0;
	REG_TM3D = 0;
	REG_TM3CNT = TM_CASCADE | TM_ENABLE;
	REG_TM2CNT = TM_FREQ_1024 | TM_ENABLE;
	REG_DEBUG_ENABLE = 0xC0DE;
	mgba_log_on = REG_DEBUG_ENABLE == 0x1DEA;
	(void)*(volatile const char *)save_type;

	REG_DISPCNT = DCNT_MODE3 | DCNT_BG2 | DCNT_OBJ | DCNT_OBJ_1D;
	sg_init(&scr, back, GBA_W, GBA_H, GBA_W);
	sprites_init();
	gba_bridge_init();

	char record[600];
	record_load(record, sizeof record);
	snc_client_init(&client, "GBA player", "Game Boy Advance", record);
	view_init(&view);
	snc_api_init(&api, "smashandclash-gba/" VERSION, "smashandclash-c/" VERSION " (gba)");

	boot();
	booting = 0;
	snc_client_start(&client);
	for (;;) {
		gba_bridge_frame();
		if (client.job.state == 1) snc_client_net_step(&client, &api);  // waits through frames of its own
	}
}
