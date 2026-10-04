// The Game Boy Advance screen. See view.h.
//
// One 240 x 160 screen holds the game: the board (5 x 3 cards of 28 x 38) with your hand
// under it, and a column on the right with the score, what to do next and the buttons.
// Everything is drawn crisp: pixel fonts, square corners cut by a pixel, a dithered
// world, translucency as a dot pattern. The focus is a pair of sprite brackets, not ink.
#include "view.h"

#include <stdio.h>
#include <string.h>

#include "assets.h"
#include "snc_draw.h"
#include "snc_game.h"
#include "snc_platform.h"

// On the GBA the per-pixel loops run from IWRAM as ARM code (the Makefile sets SNC_GBA).
#ifdef SNC_GBA
#define FAST __attribute__((section(".iwram.view"), target("arm"), long_call, noinline))
#else
#define FAST
#endif

#define SW GBA_W
#define SH GBA_H
#define TW ART_S_W
#define TH ART_S_H
#define BX 2
#define BY 2
#define GX 2
#define GY 2
#define BOARD_W (5 * (TW + GX) - GX)
#define HAND_Y (BY + 3 * (TH + GY))
#define RX 154
#define RW 84


gba_ui ui;

/* ------------------------------- sprite art ------------------------------------ */

const uint8_t CURSOR_CORNER[8][8] = {
	{1, 1, 1, 1, 1, 1, 0, 0},
	{1, 3, 3, 3, 3, 1, 0, 0},
	{1, 3, 2, 2, 2, 1, 0, 0},
	{1, 3, 2, 1, 1, 1, 0, 0},
	{1, 3, 2, 1, 0, 0, 0, 0},
	{1, 1, 1, 1, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0},
};
const uint8_t DOT[8][8] = {
	{0, 1, 1, 1, 0, 0, 0, 0},
	{1, 3, 3, 2, 1, 0, 0, 0},
	{1, 3, 2, 2, 1, 0, 0, 0},
	{1, 2, 2, 2, 1, 0, 0, 0},
	{0, 1, 1, 1, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0},
};
const uint32_t SPRITE_COLORS[4] = {0, C_INK_DEEP, C_SUN2, C_SUN1};

int view_cursor_offset(uint32_t frame) {
	static const int8_t breathe[16] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 0, 0, 0};
	return breathe[(frame / 3) & 15];
}

/* ------------------------------ pixel primitives ------------------------------- */

#define R8(c) ((int)(((c) >> 16) & 255))
#define G8(c) ((int)(((c) >> 8) & 255))
#define B8(c) ((int)((c) & 255))

// A box with its corners cut by r pixels (0..3): the pixel-art rounded rectangle.
static void pbox(snc_surf *s, int x, int y, int w, int h, int r, uint32_t rgb) {
	static const int8_t cut[4][3] = {{0, 0, 0}, {1, 0, 0}, {2, 1, 0}, {3, 1, 1}};
	if (r > 3) r = 3;
	if (r * 2 > h) r = h / 2;
	for (int k = 0; k < r; k++) {
		int c = cut[r][k];
		sg_fill(s, x + c, y + k, w - 2 * c, 1, rgb);
		sg_fill(s, x + c, y + h - 1 - k, w - 2 * c, 1, rgb);
	}
	sg_fill(s, x, y + r, w, h - 2 * r, rgb);
}

// Its outline, t pixels thick.
static void pframe(snc_surf *s, int x, int y, int w, int h, int t, uint32_t rgb) {
	sg_fill(s, x + 1, y, w - 2, t, rgb);
	sg_fill(s, x + 1, y + h - t, w - 2, t, rgb);
	sg_fill(s, x, y + 1, t, h - 2, rgb);
	sg_fill(s, x + w - t, y + 1, t, h - 2, rgb);
}

// Retro translucency: one pixel in four (or two, dense) set to rgb.
FAST static void dots(snc_surf *s, int x, int y, int w, int h, uint32_t rgb, int dense) {
	if (x < s->cx0) w -= s->cx0 - x, x = s->cx0;
	if (y < s->cy0) h -= s->cy0 - y, y = s->cy0;
	if (x + w > s->cx1) w = s->cx1 - x;
	if (y + h > s->cy1) h = s->cy1 - y;
	if (w <= 0 || h <= 0) return;
	uint16_t c = SNC_PX(R8(rgb), G8(rgb), B8(rgb));
	for (int j = 0; j < h; j++) {
		int yy = y + j;
		if (!dense && (yy & 1)) continue;
		int phase = dense ? yy : yy >> 1;  // dense: a checkerboard; else every other pixel of every other row
		uint16_t *p = s->px + yy * s->stride;
		for (int xx = x + ((x + phase) & 1); xx < x + w; xx += 2) p[xx] = c;
	}
}

// A dithered blue world: the Arena Pop gradient in 15-bit colour, with faint diagonals.
FAST static void world(snc_surf *s) {
	static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
	for (int y = 0; y < s->h; y++) {
		int t = y * 256 / (s->h - 1);
		int r = R8(C_WORLD_TOP) + ((R8(C_WORLD_BOT) - R8(C_WORLD_TOP)) * t >> 8);
		int g = G8(C_WORLD_TOP) + ((G8(C_WORLD_BOT) - G8(C_WORLD_TOP)) * t >> 8);
		int b = B8(C_WORLD_TOP) + ((B8(C_WORLD_BOT) - B8(C_WORLD_TOP)) * t >> 8);
		uint16_t row[8];
		for (int x = 0; x < 8; x++) {
			int d = BAYER[y & 3][x & 3];
			int rr = r + (d >> 1), gg = g + (d >> 1), bb = b + (d >> 1);
			if (((x + y) & 7) == 0) rr += 14, gg += 14, bb += 10;  // the diagonals
			row[x] = SNC_PX(rr > 255 ? 255 : rr, gg > 255 ? 255 : gg, bb > 255 ? 255 : bb);
		}
		uint32_t pair[4] = {row[0] | (uint32_t)row[1] << 16, row[2] | (uint32_t)row[3] << 16, row[4] | (uint32_t)row[5] << 16,
		                    row[6] | (uint32_t)row[7] << 16};
		uint32_t *p = (uint32_t *)(s->px + y * s->stride);  // two pixels a write: the rows are word-aligned
		for (int x = 0; x < s->w / 2; x++) p[x] = pair[x & 3];
	}
}

// A window: ink, with a dark rim and a lighter top edge (the RPG box, Arena Pop colours).
static void window(snc_surf *s, int x, int y, int w, int h) {
	pbox(s, x, y, w, h, 2, C_INK_DEEP);
	pbox(s, x + 1, y + 1, w - 2, h - 2, 1, C_INK);
	sg_fill(s, x + 3, y + 1, w - 6, 1, C_INK2);
}

// The sugar panel (codes, the inspector's card stand).
static void panel(snc_surf *s, int x, int y, int w, int h) {
	pbox(s, x, y, w, h, 2, C_INK_DEEP);
	pbox(s, x + 1, y + 1, w - 2, h - 2, 1, C_SUGAR_EDGE);
	pbox(s, x + 1, y + 1, w - 2, h - 4, 1, C_SUGAR2);
	sg_fill(s, x + 2, y + 2, w - 4, (h - 4) / 2, C_SUGAR);
}

static int text_shadow(snc_surf *s, const snc_font *f, int x, int y, const char *t, uint32_t rgb) {
	sg_text(s, f, x + 1, y + 1, t, C_INK_DEEP);
	return sg_text(s, f, x, y, t, rgb);
}

static void text_c_shadow(snc_surf *s, const snc_font *f, int cx, int y, const char *t, uint32_t rgb) {
	text_shadow(s, f, cx - sg_text_w(f, t) / 2, y, t, rgb);
}

// A title on the world: ink outline, sugar inside.
static void title(snc_surf *s, const snc_font *f, int cx, int y, const char *t) {
	sg_text_outline(s, f, cx - sg_text_w(f, t) / 2, y, t, C_SUGAR, C_INK_DEEP, 1);
}

/* ---------------------------------- buttons ------------------------------------ */

static void fill_colors(sd_fill fill, uint32_t *hi, uint32_t *base, uint32_t *lip, uint32_t *on) {
	switch (fill) {
	case FILL_SUN: *hi = C_SUN1, *base = C_SUN2, *lip = C_SUN_LIP, *on = C_INK; break;
	case FILL_MINT: *hi = C_MINT1, *base = C_MINT2, *lip = C_MINT_LIP, *on = C_INK; break;
	case FILL_CHERRY: *hi = C_CHERRY1, *base = C_CHERRY2, *lip = C_CHERRY_LIP, *on = C_SUGAR; break;
	case FILL_GRAPE: *hi = C_GRAPE1, *base = C_GRAPE2, *lip = C_GRAPE_LIP, *on = C_SUGAR; break;
	case FILL_SKY: *hi = C_SKY1, *base = C_SKY2, *lip = C_SKY_LIP, *on = C_INK; break;
	default: *hi = C_SUGAR, *base = 0xE6F0FF, *lip = C_SUGAR_EDGE, *on = C_INK; break;
	}
}

// A candy button: ink rim, a light top, the base, and a hard lip at the bottom.
static uint32_t candy(snc_surf *s, int x, int y, int w, int h, sd_fill fill, int off) {
	uint32_t hi, base, lip, on;
	fill_colors(fill, &hi, &base, &lip, &on);
	if (off) hi = C_SUGAR3, base = C_SUGAR3, lip = C_SUGAR_EDGE, on = C_INK3;
	int lipd = h >= 24 ? 3 : 2;
	pbox(s, x, y, w, h, 2, C_INK_DEEP);
	pbox(s, x + 1, y + 1, w - 2, h - 2, 1, lip);
	pbox(s, x + 1, y + 1, w - 2, h - 2 - lipd, 1, base);
	sg_fill(s, x + 2, y + 2, w - 4, (h - 2 - lipd) / 2 - 1, hi);
	return on;
}

static void button(view_t *v, snc_surf *s, int x, int y, int w, int h, const snc_font *f, const char *label, sd_fill fill, int id, int arg,
                   int off) {
	uint32_t on = candy(s, x, y, w, h, fill, off);
	int lipd = h >= 24 ? 3 : 2;
	int cap = f == &font_label ? 10 : f == &font_tiny ? 5 : 6;  // the caps' height
	int top = f == &font_label ? 1 : 1;
	int ty = y + 1 + (h - 2 - lipd - cap) / 2 - top;
	snc_surf clip = *s;
	sg_clip(&clip, x + 2, y + 1, w - 4, h - 2);
	if (sg_text_w(f, label) <= w - 6) sg_text_c(&clip, f, x + w / 2, ty, label, on);
	else sg_text_fit(&clip, f, x + 3, ty, w - 6, label, on);
	if (!off) view_add(v, x, y, w, h, id, arg);
}

// A two-line button: the label and a caption under it.
static void button2(view_t *v, snc_surf *s, int x, int y, int w, int h, const char *label, const char *caption, sd_fill fill, int id) {
	uint32_t on = candy(s, x, y, w, h, fill, 0);
	int lipd = 3;
	int block = 10 + 3 + 5;  // label caps, a gap, the caption's caps
	int ty = y + 1 + (h - 2 - lipd - block) / 2;
	snc_surf clip = *s;
	sg_clip(&clip, x + 2, y + 1, w - 4, h - 2);
	sg_text_c(&clip, &font_label, x + w / 2, ty - 1, label, on);
	uint32_t muted = (fill == FILL_CHERRY || fill == FILL_GRAPE) ? 0xF0E8FF : C_INK2;
	sg_text_c(&clip, &font_tiny, x + w / 2, ty + 13 - 1, caption, muted);
	view_add(v, x, y, w, h, id, 0);
}

/* ----------------------------------- cards ------------------------------------- */

static const uint32_t SIDE_COLOR[4] = {0xF0525A, 0xF5C431, 0x4D6CF5, 0x5DC24A};  // printed: top red, right yellow, bottom blue, left green

// A value badge centred on (cx, cy): ink rim, the side's colour, white digits with a shadow.
static void badge(snc_surf *s, int cx, int cy, int value, uint32_t bg, const snc_font *f) {
	char t[12];
	snprintf(t, sizeof t, "%d", value);
	int tw = sg_text_w(f, t) - 1;
	int bw = tw + 4, bh = 9;
	if (bw < 7) bw = 7;
	int x = cx - bw / 2, y = cy - bh / 2;
	pbox(s, x, y, bw, bh, 1, C_INK_DEEP);
	sg_fill(s, x + 1, y + 1, bw - 2, bh - 2, bg);
	int tx = x + (bw - tw) / 2, ty = y + 2 - 1;
	sg_text(s, f, tx + 1, ty + 1, t, C_INK_DEEP);
	sg_text(s, f, tx, ty, t, C_SUGAR);
}

static void card_badges(snc_surf *s, int x, int y, int w, int h, const int vals[4], int rot, const snc_font *f) {
	int cx = x + w / 2, cy = y + h / 2;
	if (vals[0] >= 0) badge(s, cx, y + 5, vals[0], SIDE_COLOR[rot ? 2 : 0], f);
	if (vals[1] >= 0) badge(s, x + w - 5, cy, vals[1], SIDE_COLOR[rot ? 3 : 1], f);
	if (vals[2] >= 0) badge(s, cx, y + h - 5, vals[2], SIDE_COLOR[rot ? 0 : 2], f);
	if (vals[3] >= 0) badge(s, x + 4, cy, vals[3], SIDE_COLOR[rot ? 1 : 3], f);
}

// A card: its art, an ink rim with the owner's colour inside it, the side values.
static void card(snc_surf *s, int x, int y, int idx, const char *name, int owner, const int vals[4], int flags) {
	int imgflags = ((flags & CARD_ROT) ? SG_ROT180 : 0) | ((flags & CARD_DIM) ? SG_DIM : 0) | ((flags & CARD_GHOST) ? SG_GHOST : 0);
	if (idx >= 0 && idx < SNC_CARDS) sg_img(s, x, y, snc_art_s + (size_t)idx * TW * TH, TW, TH, imgflags);
	else {
		pbox(s, x, y, TW, TH, 1, C_SUGAR);
		sg_wrap(s, &font_tiny, x + 2, y + TH / 2 - 8, TW - 4, 8, name ? name : "?", C_INK, 2);
	}
	if (flags & CARD_FROZEN) {
		dots(s, x + 1, y + 1, TW - 2, TH - 2, 0xD8F4FF, 1);
		int iw = ICON_FROZEN_W, ih = ICON_FROZEN_H;
		pbox(s, x + (TW - iw) / 2 - 2, y + (TH - ih) / 2 - 2, iw + 4, ih + 4, 2, C_SKY_LIP);
		sd_icon(s, x + (TW - iw) / 2, y + (TH - ih) / 2, "frozen", C_SUGAR);
	}
	// the corners off, a rim, the owner's colour where the card's white edge was
	uint32_t rim = owner == 'y' ? C_YOU : owner == 'o' ? C_OPP : C_SUGAR_EDGE;
	if (!(flags & CARD_GHOST)) pframe(s, x, y, TW, TH, 1, C_INK_DEEP);
	pframe(s, x + 1, y + 1, TW - 2, TH - 2, 1, rim);
	if (vals && !(flags & CARD_GHOST)) card_badges(s, x, y, TW, TH, vals, (flags & CARD_ROT) != 0, &font_digit);
	if (flags & CARD_FLASH) pframe(s, x - 1, y - 1, TW + 2, TH + 2, 1, C_SUGAR);  // changed with the last move
	if (flags & CARD_SELECTED) {
		pframe(s, x - 2, y - 2, TW + 4, TH + 4, 2, C_SUN2);
		pframe(s, x - 3, y - 3, TW + 6, TH + 6, 1, C_INK_DEEP);
	}
}

/* ----------------------------------- pieces ------------------------------------ */

static void cell_xy(const snc_client *c, int cell, int *x, int *y) {
	int col, row;
	view_cell_slot(&c->game, cell, &col, &row);
	*x = BX + col * (TW + GX);
	*y = BY + row * (TH + GY);
}

// What we are waiting on, or the live notice, as a strip across the screen: its top at y,
// or (y < 0) its bottom at -y.
static void toast(snc_surf *s, const snc_client *c, int y) {
	const char *notice = snc_client_notice(c);
	const char *text = notice ? notice : c->busy[0] ? c->busy : NULL;
	if (!text) return;
	int lines = sg_wrap(NULL, &font_small, 0, 0, SW - 28, 10, text, 0, 3);
	int h = lines * 10 + 8;
	if (y < 0) y = -y - h;
	window(s, 6, y, SW - 12, h);
	uint32_t col = notice && c->notice_bad ? 0xFF8CC8 : C_SUN1;
	sg_wrap(s, &font_small, 13, y + 4, SW - 28, 10, text, col, 3);
	if (!notice && c->busy[0] && lines == 1) {
		ui.dots_on = 1;
		ui.dots_x = 13 + sg_text_w(&font_small, text) + 3;
		ui.dots_y = y + 4;
	}
}

static void score_plates(snc_surf *s, const snc_client *c) {
	const snc_game *g = &c->game;
	char opp[48], t[8];
	snc_opponent_name(g, opp, sizeof opp);
	int moving = c->busy[0] != 0;
	int you_move = g->status == SNC_ACTIVE && g->your_turn && !moving;
	int they_move = g->status == SNC_ACTIVE && (!g->your_turn || moving);
	for (int k = 0; k < 2; k++) {
		int y = 2 + k * 18, h = 16;
		uint32_t hi = k ? C_OPP_HI : C_YOU_HI, base = k ? C_OPP : C_YOU, lip = k ? C_OPP_LIP : C_YOU_LIP;
		pbox(s, RX, y, RW, h, 2, C_INK_DEEP);
		pbox(s, RX + 1, y + 1, RW - 2, h - 2, 1, lip);
		pbox(s, RX + 1, y + 1, RW - 2, h - 4, 1, base);
		sg_fill(s, RX + 2, y + 2, RW - 4, 1, hi);
		snprintf(t, sizeof t, "%d", k ? g->score_opp : g->score_you);
		int tw = sg_text_w(&font_label, t);
		text_shadow(s, &font_label, RX + RW - 5 - tw, y + 1, t, C_SUGAR);
		snc_surf clip = *s;
		sg_clip(&clip, RX + 3, y, RW - 12 - tw, h);
		int nx = RX + 4;
		if ((k == 0 && you_move) || (k == 1 && they_move)) {  // the side to move: a sun arrow
			sg_fill(&clip, nx, y + 4, 1, 7, C_SUN1);
			sg_fill(&clip, nx + 1, y + 5, 1, 5, C_SUN1);
			sg_fill(&clip, nx + 2, y + 6, 1, 3, C_SUN1);
			sg_fill(&clip, nx + 3, y + 7, 1, 1, C_SUN1);
			nx += 6;
		}
		sg_text_fit(&clip, &font_small, nx + 1, y + 4, RX + RW - 9 - tw - nx, k ? opp : "You", C_INK_DEEP);
		sg_text_fit(&clip, &font_small, nx, y + 3, RX + RW - 9 - tw - nx, k ? opp : "You", C_SUGAR);
	}
	// the other side's hand, face down
	int n = snc_client_over(c) || g->status == SNC_WAITING ? 0 : c->opp_hand;
	if (n > 6) n = 6;
	for (int i = 0; i < n; i++) sd_card_back(s, RX + RW - BACK_S_W - i * (BACK_S_W + 1), 37);
}

// What to do next: the status line, then the notice or the detail.
static void status_window(snc_surf *s, const snc_client *c, int x, int y, int w, int h) {
	snc_status_t st;
	snc_client_status(c, &st);
	window(s, x, y, w, h);
	int lines = sg_wrap(s, &font_small, x + 5, y + 4, w - 9, 10, st.main, C_SUGAR, 4);
	if (c->busy[0] && lines == 1 && sg_text_w(&font_small, st.main) + 18 < w) {
		ui.dots_on = 1;
		ui.dots_x = x + 5 + sg_text_w(&font_small, st.main) + 2;
		ui.dots_y = y + 4;
	}
	int dy = y + 6 + lines * 10;
	int room = (y + h - 3 - dy) / 8;
	const char *notice = snc_client_notice(c);
	if (room > 0) {
		if (notice) sg_wrap(s, &font_tiny, x + 5, dy, w - 9, 8, notice, c->notice_bad ? 0xFF8CC8 : C_SUN1, room);
		else if (st.detail[0]) sg_wrap(s, &font_tiny, x + 5, dy, w - 9, 8, st.detail, C_SUGAR3, room);
	}
}

/* ------------------------------------ board ------------------------------------ */

static void draw_board(view_t *v, snc_surf *s, const snc_client *c, int interactive) {
	const snc_game *g = &c->game;
	int mutators = g->ruleset != 'c';
	uint16_t lit = interactive && !c->busy[0] ? snc_targets(g, &c->pick) : 0;
	for (int cell = 0; cell < 15; cell++) {
		int x, y;
		cell_xy(c, cell, &x, &y);
		const snc_tile *t = &g->board[cell];
		const char *piece = view_chess_at(g, cell);
		int pw = view_power_at(g, cell);
		int has = t->card[0] != 0;
		int ghost = !has && c->pending_cell == cell;
		if (!has && !ghost) {
			if (pw >= 0) {
				uint32_t col = sd_power_color(g->power[pw].color);
				pbox(s, x, y, TW, TH, 2, C_INK_DEEP);
				pbox(s, x + 1, y + 1, TW - 2, TH - 2, 1, col);
				dots(s, x + 2, y + 2, TW - 4, TH - 4, C_SUGAR, 0);
			} else {
				pframe(s, x, y, TW, TH, 1, 0x7FD6FF);
				dots(s, x + 1, y + 1, TW - 2, TH - 2, 0xCFEFFF, 0);
			}
			if (mutators && view_overrun_at(g, cell))  // an overrun zone: a dashed line inside
				for (int k = 0; k < TW - 8; k += 4) {
					sg_fill(s, x + 4 + k, y + 3, 2, 1, C_SUGAR);
					sg_fill(s, x + 4 + k, y + TH - 4, 2, 1, C_SUGAR);
				}
			char name[3];
			snc_cell_name(cell, name);
			text_c_shadow(s, &font_tiny, x + TW / 2, y + TH - 10, name, C_SUGAR);
			if (piece) {
				int iw = sd_icon_w(piece), ih = sd_icon_h(piece);
				pbox(s, x + TW / 2 - 7, y + 6, 14, 13, 2, C_INK_DEEP);
				pbox(s, x + TW / 2 - 6, y + 7, 12, 11, 1, C_SUGAR);
				sd_icon(s, x + (TW - iw) / 2, y + 7 + (11 - ih) / 2, piece, C_GRAPE2);
			}
			if (pw >= 0) {
				char b[8];
				snprintf(b, sizeof b, "+%d", g->power[pw].boost);
				text_c_shadow(s, &font_label, x + TW / 2, piece ? y + 19 : y + 9, b, C_SUGAR);
			}
		} else {
			int vals[4];
			const char *name = ghost ? c->pending_card : t->card;
			int owner = ghost ? 'y' : t->owner;
			int flags = (owner == 'o' ? CARD_ROT : 0) | (t->frozen ? CARD_FROZEN : 0) | (ghost ? CARD_GHOST : 0) |
			            ((c->flash & (1u << cell)) ? CARD_FLASH : 0);
			if (ghost) vals[0] = vals[1] = vals[2] = vals[3] = -1;
			else view_tile_vals(g, t, vals);
			card(s, x, y, ghost ? snc_card_index(name) : sd_tile_card(g, cell), name, owner, vals, flags);
			if (piece) {  // a chess tile under a card: its piece in the corner
				pbox(s, x + 1, y + 1, 12, 11, 1, C_SUGAR);
				sd_icon(s, x + 1 + (12 - sd_icon_w(piece)) / 2, y + 1 + (11 - sd_icon_h(piece)) / 2, piece, C_GRAPE2);
			}
			if (pw >= 0) {
				char b[4];
				snprintf(b, sizeof b, "%d", g->power[pw].boost);
				pbox(s, x + TW - 9, y + 1, 8, 9, 1, sd_power_color(g->power[pw].color));
				sg_text_c(s, &font_tiny, x + TW - 5, y + 2, b, C_SUGAR);
			}
		}
		int chosen = 0;
		for (int k = 0; k < c->pick.path_n; k++)
			if (c->pick.path[k] == cell) chosen = 1;
		if (lit & (1u << cell)) {  // a tile the pick can go to: mint
			dots(s, x + 2, y + 2, TW - 4, TH - 4, C_MINT1, 1);
			pframe(s, x, y, TW, TH, 2, C_MINT2);
			pframe(s, x + 2, y + 2, TW - 4, TH - 4, 1, C_MINT_LIP);
		} else if (chosen) {
			dots(s, x + 1, y + 1, TW - 2, TH - 2, C_MINT2, 1);
		}
		if (interactive) view_add(v, x, y, TW, TH, H_CELL, cell);
	}
}

static void draw_hand(view_t *v, snc_surf *s, const snc_client *c) {
	const snc_game *g = &c->game;
	int n = g->hand_n;
	int step = n <= 5 ? TW + GX : (BOARD_W - TW) / (n - 1);
	for (int i = 0; i < n; i++) {
		int x = BX + i * step, y = HAND_Y;
		int sel = c->pick.selected == i;
		int dim = g->your_turn && !g->has_hop && !snc_playable(g, i);
		int vals[4];
		view_hand_vals(&g->hand[i], vals);
		card(s, x, sel ? y - 3 : y, sd_hand_card(&g->hand[i]), g->hand[i].card, 'y', vals, (dim ? CARD_DIM : 0) | (sel ? CARD_SELECTED : 0));
		view_add(v, x, y, i + 1 < n ? step : TW, TH, H_HAND, i);
	}
}

/* ----------------------------------- screens ----------------------------------- */

static void draw_waiting(view_t *v, snc_surf *s, const snc_client *c) {
	const snc_game *g = &c->game;
	snc_status_t st;
	snc_client_status(c, &st);
	if (c->mode == 'i' && g->invite_url[0]) {
		// the invite as a QR code for a phone, with what it is beside it
		int size = sg_qr(NULL, 0, 0, 2, g->invite_url);
		int qx = 4, qy = 4;
		pbox(s, qx - 1, qy - 1, size + 2, size + 2, 1, C_INK_DEEP);
		sg_qr(s, qx, qy, 2, g->invite_url);
		int tx = qx + size + 6, tw = SW - 4 - tx;
		window(s, tx, 4, tw, 128);
		sg_wrap(s, &font_label, tx + 5, 6, tw - 9, 12, "Invite a friend", C_SUN1, 2);
		sg_wrap(s, &font_small, tx + 5, 32, tw - 9, 10,
		        "Scan the code with a phone. Your friend plays you in their browser: no app needed.", C_SUGAR, 9);
	} else if (c->mode == 'c' && g->code[0]) {
		title(s, &font_label, SW / 2, 3, "Play by code");
		panel(s, 40, 20, SW - 80, 48);
		sg_text_c(s, &font_tiny, SW / 2, 23, "YOUR CODE", C_INK2);
		sg_text_c(s, &font_big, SW / 2, 33, g->code, C_INK);
		window(s, 6, 72, SW - 12, 60);
		sg_wrap(s, &font_small, 12, 76, SW - 24, 10, st.detail, C_SUGAR, 5);
	} else {
		sd_logo_s(s, (SW - LOGO_S_W) / 2, 6);
		window(s, 6, 46, SW - 12, 86);
		sg_text_fit(s, &font_label, 12, 49, SW - 24, st.main, C_SUN1);
		sg_wrap(s, &font_small, 12, 66, SW - 24, 10, st.detail, C_SUGAR3, 6);
	}
	button(v, s, 6, 136, 112, 20, &font_label, "How to play", FILL_SUGAR, H_HOW, 0, 0);
	button(v, s, SW - 6 - 112, 136, 112, 20, &font_label, "Cancel", FILL_CHERRY, H_RESIGN, 0, c->busy[0] != 0);
	if (snc_client_notice(c) || c->busy[0]) toast(s, c, -133);
}

static void draw_qr(snc_surf *s, const char *url, int x, int y, int w, int h) {
	int size = sg_qr(NULL, 0, 0, 2, url);
	if (!size) return;
	int qx = x + (w - size) / 2, qy = y + (h - size) / 2;
	pbox(s, qx - 2, qy - 2, size + 4, size + 4, 2, C_INK_DEEP);
	sg_qr(s, qx, qy, 2, url);
}

static void draw_game(view_t *v, snc_surf *s, const snc_client *c) {
	const snc_game *g = &c->game;
	world(s);
	if (g->status == SNC_WAITING) {
		draw_waiting(v, s, c);
		return;
	}
	int over = snc_client_over(c);
	if (over && v->show_replay && g->replay_url[0] && sg_qr(NULL, 0, 0, 2, g->replay_url)) {
		draw_qr(s, g->replay_url, BX, BY, BOARD_W, SH - 2 * BY);
	} else {
		draw_board(v, s, c, !over);
		if (!over) draw_hand(v, s, c);
	}
	score_plates(s, c);
	snc_status_t st;
	snc_client_status(c, &st);
	if (over) {
		if (!v->show_replay) {  // the result, across the hand row
			window(s, BX, HAND_Y - 1, BOARD_W, SH - HAND_Y);
			sg_text_fit(s, &font_label, BX + 5, HAND_Y + 1, BOARD_W - 10, st.main, C_SUN1);
			const char *notice = snc_client_notice(c);
			sg_wrap(s, &font_tiny, BX + 5, HAND_Y + 15, BOARD_W - 10, 8, notice ? notice : st.detail, notice && c->notice_bad ? 0xFF8CC8 : C_SUGAR, 3);
		}
		button(v, s, RX, 52, RW, 30, &font_label, "Again", FILL_SUN, H_AGAIN, 0, c->busy[0] != 0 || c->mode == 'j' || c->mode == 'c');
		button(v, s, RX, 86, RW, 30, &font_label, "Lobby", FILL_SUGAR, H_LOBBY, 0, c->busy[0] != 0);
		if (g->replay_url[0]) button(v, s, RX, 120, RW, 30, &font_label, v->show_replay ? "Hide QR" : "Replay QR", FILL_SKY, H_REPLAY, 0, 0);
		return;
	}
	int action = st.action[0] != 0;
	status_window(s, c, RX, 50, RW, action ? 68 : 88);
	if (action) button(v, s, RX, 120, RW, 18, &font_label, st.action, FILL_MINT, H_ACTION, 0, c->busy[0] != 0);
	button(v, s, RX, 141, 40, 17, &font_small, "Rules", FILL_SUGAR, H_HOW, 0, 0);
	button(v, s, RX + 44, 141, 40, 17, &font_small, "Resign", FILL_CHERRY, H_RESIGN, 0, c->busy[0] != 0);
}

static void draw_lobby(view_t *v, snc_surf *s, const snc_client *c) {
	world(s);
	sd_logo_s(s, 4, 1);
	char line[80];
	int tx = 4 + LOGO_S_W + 6;
	snprintf(line, sizeof line, "Hi, %s!", c->name);
	sg_text_outline(s, &font_label, tx, 2, line, C_SUGAR, C_INK_DEEP, 1);
	snprintf(line, sizeof line, "Rating %d \xC2\xB7 %d played \xC2\xB7 %d won", c->rec.rating, c->rec.played, c->rec.won);
	text_shadow(s, &font_small, tx, 16, line, C_SUGAR);
	snprintf(line, sizeof line, "Rules: %s \xC2\xB7 online at smashandclash.in", c->rec.ruleset == 'c' ? "Classic" : "Mutators");
	text_shadow(s, &font_tiny, tx, 27, line, C_SUGAR2);
	int w = 114, h = 37, x0 = 4, x1 = SW - 4 - w;
	button2(v, s, x0, 40, w, h, "New game", "at your level", FILL_SUN, H_NEW);
	button2(v, s, x1, 40, w, h, "Quick match", "whoever is online", FILL_SUGAR, H_QUICK);
	button2(v, s, x0, 80, w, h, "Invite a friend", "they play in a browser", FILL_SUGAR, H_INVITE);
	button2(v, s, x1, 80, w, h, "Play by code", "DS, PSP, GBA or a terminal", FILL_SUGAR, H_CODE);
	button2(v, s, x0, 120, w, h, c->rec.ruleset == 'c' ? "Rules: Classic" : "Rules: Mutators", "press to switch", c->rec.ruleset == 'c' ? FILL_SKY : FILL_GRAPE,
	        H_RULESET);
	button2(v, s, x1, 120, w, h, "How to play", "the rules, in full", FILL_SUGAR, H_HOW);
	toast(s, c, 2);
}

static void draw_code(view_t *v, snc_surf *s, const snc_client *c) {
	world(s);
	int bw = 22, gap = 4, bx = (SW - (6 * bw + 5 * gap)) / 2;
	for (int i = 0; i < 6; i++) {  // the code so far
		int x = bx + i * (bw + gap);
		panel(s, x, 3, bw, 28);
		if (i < c->code_len) {
			char ch[2] = {c->code[i], 0};
			sg_text_c(s, &font_title, x + bw / 2 + 1, 7, ch, C_INK);
		} else if (i == c->code_len) {
			sg_fill(s, x + 5, 23, bw - 10, 2, C_INK3);
		}
	}
	int kw = 26, kh = 18, kg = 3, cols = 8;
	int x0 = (SW - (cols * kw + (cols - 1) * kg)) / 2;
	for (int i = 0; CODE_ALPHABET[i]; i++) {
		int x = x0 + (i % cols) * (kw + kg), y = 36 + (i / cols) * (kh + 3);
		char ch[2] = {CODE_ALPHABET[i], 0};
		button(v, s, x, y, kw, kh, &font_label, ch, FILL_SUGAR, H_KEY, i, c->code_len >= 6);
	}
	int y = 36 + 4 * (kh + 3) + 3, h = SH - 3 - y;
	button(v, s, 3, y, 56, h, &font_label, "Back", FILL_SUGAR, H_BACK, 0, 0);
	button(v, s, 62, y, 56, h, &font_label, "Delete", FILL_SUGAR, H_DEL, 0, c->code_len == 0);
	button(v, s, 121, y, 56, h, &font_label, "Host", FILL_SKY, H_HOST, 0, c->busy[0] != 0);
	button(v, s, 180, y, 57, h, &font_label, "Join", FILL_SUN, H_JOIN, 0, c->code_len != 6 || c->busy[0] != 0);
	toast(s, c, 58);
}

static void draw_rules(view_t *v, snc_surf *s, const snc_client *c) {
	world(s);
	window(s, 2, 2, SW - 4, 136);
	snc_surf clip = *s;
	sg_clip(&clip, 3, 4, SW - 6, 132);
	static const char HOW[] =
		"On the Game Boy Advance\n"
		"D-pad: move. A: pick a card, then a green tile. B: put the card back. L / R: your next playable card. "
		"SELECT: look at a card up close (after a game: the replay's QR code). START: resign.\n"
		"An effect that needs a target lights its tiles the same way (Recruit! takes two: the card, then where it goes). "
		"Flip! and Swap!: pick the card again. A hop: pick a green tile, or Stay.\n\n";
	int lh = 10, w = SW - 20;
	int head = sg_wrap(NULL, &font_small, 0, 0, w, lh, HOW, 0, 0);
	const char *text = c->rules_ready ? c->rules : "Fetching the rules...";
	int lines = head + sg_wrap(NULL, &font_small, 0, 0, w, lh, text, 0, 0);
	int visible = 130 / lh;
	v->scroll_max = lines > visible ? lines - visible : 0;
	if (v->scroll > v->scroll_max) v->scroll = v->scroll_max;
	int y = 6 - v->scroll * lh;
	sg_wrap(&clip, &font_small, 9, y, w, lh, HOW, C_SUN1, 0);
	sg_wrap(&clip, &font_small, 9, y + head * lh, w, lh, text, C_SUGAR, 0);
	if (v->scroll_max > 0) {  // where we are in it
		int bar = 124 * visible / lines;
		int pos = (124 - bar) * v->scroll / v->scroll_max;
		sg_fill(s, SW - 8, 8 + pos, 2, bar, C_SUGAR_EDGE);
	}
	button(v, s, 4, 141, 70, 17, &font_label, "Up", FILL_SUGAR, H_UP, 0, v->scroll == 0);
	button(v, s, 78, 141, 70, 17, &font_label, "Down", FILL_SUGAR, H_DOWN, 0, v->scroll >= v->scroll_max);
	button(v, s, SW - 4 - 84, 141, 84, 17, &font_label, "Back", FILL_SUN, H_BACK, 0, 0);
	toast(s, c, 58);
}

// The card inspector: the card big, its name, its sides, and what it does.
static void draw_inspect(snc_surf *s) {
	int i = ui.inspect_card;
	window(s, 4, 4, SW - 8, SH - 8);
	panel(s, 9, 9, ART_L_W + 6, ART_L_H + 8);
	sd_card_large(s, 12, 12, i);
	int tx = 9 + ART_L_W + 14, tw = SW - 10 - tx;
	sg_text_fit(s, &font_tiny, tx, 10, tw, ui.inspect_owner == 'o' ? "THEIR CARD" : ui.inspect_owner == 'y' ? "YOUR CARD" : "CARD", C_SUGAR3);
	sg_text_fit(s, &font_title, tx, 18, tw, snc_cards[i].name, C_SUGAR);
	int ty = 40;
	if (snc_cards[i].effect) {
		const char *does = ui.inspect_does[0] ? ui.inspect_does : "An effect card: it plays when it can do something.";
		sg_wrap(s, &font_small, tx, ty, tw, 10, does, C_SUGAR2, 9);
	} else {
		// the four sides as a cross, in the card's own colours
		int cx = tx + 24, cy = ty + 22;
		static const int dx[4] = {0, 16, 0, -16}, dy[4] = {-13, 0, 13, 0};
		for (int k = 0; k < 4; k++) {
			char d[4];
			snprintf(d, sizeof d, "%d", ui.inspect_vals[k]);
			int bx = cx + dx[k] - 7, by = cy + dy[k] - 7;
			pbox(s, bx, by, 15, 14, 2, C_INK_DEEP);
			pbox(s, bx + 1, by + 1, 13, 12, 1, SIDE_COLOR[k]);
			text_c_shadow(s, &font_label, cx + dx[k] + 1, by + 1, d, C_SUGAR);
		}
		const char *color = snc_cards[i].color;
		if (color && color[0]) {
			pbox(s, tx, ty + 50, 10, 10, 2, sd_power_color(color));
			sg_text(s, &font_small, tx + 14, ty + 51, color, C_SUGAR2);
			sg_wrap(s, &font_tiny, tx, ty + 64, tw, 8, "On a power tile of its colour, its sides grow.", C_SUGAR3, 3);
		}
	}
	sg_text_r(s, &font_tiny, SW - 10, SH - 14, "B: CLOSE", C_SUGAR3);
}

/* ----------------------------------- frame ------------------------------------- */

void view_draw(view_t *v, snc_client *c, snc_surf *s, uint32_t now) {
	(void)now;
	ui.dots_on = 0;
	v->show_focus = 1;  // no touch screen: the brackets always show where you are
	view_begin(v, c);
	int screen = c->screen;
	if (screen == SC_GAME && !c->have_game) screen = SC_LOBBY;  // a game on its way: the lobby stays up under the status
	if (ui.inspect && (screen != SC_GAME || snc_client_over(c))) ui.inspect = 0;
	switch (screen) {
	case SC_RULES: draw_rules(v, s, c); break;
	case SC_CODE: draw_code(v, s, c); break;
	case SC_GAME: draw_game(v, s, c); break;
	default: draw_lobby(v, s, c); break;
	}
	view_end(v, c);
	ui.cur_on = 0;
	if (ui.inspect) {
		draw_inspect(s);
		ui.dots_on = 0;
		return;
	}
	int f = view_find(v, v->focus_id, v->focus_arg);
	if (f >= 0) {
		const view_hit *t = &v->hit[f];
		ui.cur_x = t->x, ui.cur_y = t->y, ui.cur_w = t->w, ui.cur_h = t->h;
		if (t->id == H_HAND && c->pick.selected == t->arg) ui.cur_y -= 3;  // it stands up when picked
		if (t->id == H_HAND) ui.cur_w = TW;
		ui.cur_on = 1;
	}
}

void view_toggle_inspect(view_t *v, snc_client *c) {
	if (ui.inspect) {
		ui.inspect = 0;
		return;
	}
	if (c->screen != SC_GAME || !c->have_game || snc_client_over(c)) return;
	const snc_game *g = &c->game;
	int idx = -1;
	ui.inspect_does[0] = 0;
	if (v->focus_id == H_HAND && v->focus_arg >= 0 && v->focus_arg < g->hand_n) {
		const snc_hcard *h = &g->hand[v->focus_arg];
		idx = sd_hand_card(h);
		ui.inspect_owner = 'y';
		ui.inspect_vals[0] = h->top, ui.inspect_vals[1] = h->right, ui.inspect_vals[2] = h->bottom, ui.inspect_vals[3] = h->left;
		snprintf(ui.inspect_does, sizeof ui.inspect_does, "%s", h->does);
	} else if (v->focus_id == H_CELL && v->focus_arg >= 0 && v->focus_arg < 15 && g->board[v->focus_arg].card[0]) {
		const snc_tile *t = &g->board[v->focus_arg];
		idx = sd_tile_card(g, v->focus_arg);
		ui.inspect_owner = t->owner;
		view_tile_vals(g, t, ui.inspect_vals);
		if (t->owner == 'o')  // their card faces them: its printed top is at the bottom of your screen
			for (int k = 0; k < 2; k++) {
				int tmp = ui.inspect_vals[k];
				ui.inspect_vals[k] = ui.inspect_vals[k + 2];
				ui.inspect_vals[k + 2] = tmp;
			}
	}
	if (idx < 0) return;
	ui.inspect_card = idx;
	ui.inspect = 1;
}

/* ------------------------------- the boot screen -------------------------------- */

void view_boot(snc_surf *s, const char *head, const char *text, int bad) {
	world(s);
	sd_logo_l(s, (SW - LOGO_L_W) / 2, 2);
	window(s, 4, 94, SW - 8, 62);
	sg_text_fit(s, &font_label, 10, 96, SW - 20, head, bad ? 0xFF8CC8 : C_SUN1);
	sg_wrap(s, &font_small, 10, 111, SW - 20, 10, text, C_SUGAR, 4);
}
