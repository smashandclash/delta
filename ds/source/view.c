// The Nintendo DS screens. See view.h.
//
// Bottom (touch) screen: the board (5 x 3 tiles of 32 x 44), your hand under it, and a
// column of buttons on the right. Top screen: the other player, the card you are
// looking at, the score, and what to do next.
#include "view.h"

#include <stdio.h>
#include <string.h>

#include "assets.h"
#include "snc_draw.h"
#include "snc_platform.h"

#define SW 256
#define SH 192
#define TW ART_S_W
#define TH ART_S_H
#define BX 5
#define BY 4
#define GX 2
#define GY 3
#define HAND_Y 147
#define RX 180
#define RW 72

#define hit view_add
#define focused view_focused
#define tile_vals(c, t, v) view_tile_vals(&(c)->game, t, v)
#define hand_vals view_hand_vals
#define chess_at view_chess_at
#define power_at view_power_at
#define overrun_at view_overrun_at

static int state(const view_t *v, int id, int arg, int off) {
	return (focused(v, id, arg) ? SD_FOCUS : 0) | (off ? SD_OFF : 0);
}

static void cell_xy(const snc_client *c, int cell, int *x, int *y) {
	int col, row;
	view_cell_slot(&c->game, cell, &col, &row);
	*x = BX + col * (TW + GX);
	*y = BY + row * (TH + GY);
}


static int pulse(uint32_t now) {
	int t = (int)((now / 6) % 200);
	return t < 100 ? t : 200 - t;  // 0..100..0 over 1.2 s
}

/* ------------------------------- the top screen ------------------------------- */

static void status_plate(snc_surf *s, const snc_client *c, int y, int h) {
	snc_status_t st;
	snc_client_status(c, &st);
	sd_glass(s, 0, y, SW, h, 0, 215);
	sg_text_fit(s, &font_body, 7, y + 3, SW - 14, st.main, C_SUGAR);
	const char *notice = snc_client_notice(c);
	int ly = y + 4 + font_body.line;
	int lines = (h - (ly - y) - 2) / (font_small.line + 1);
	if (notice) sg_wrap(s, &font_small, 7, ly, SW - 14, font_small.line + 1, notice, c->notice_bad ? 0xFF8CC8 : C_SUN1, lines);
	else if (st.detail[0]) sg_wrap(s, &font_small, 7, ly, SW - 14, font_small.line + 1, st.detail, C_SUGAR3, lines);
}

static void draw_top_lobby(view_t *v, snc_surf *s, const snc_client *c) {
	(void)v;
	sd_world(s);
	sd_logo_l(s, (SW - LOGO_L_W) / 2, 6);
	char line[96];
	snprintf(line, sizeof line, "Rating %d  \xC2\xB7  %d played  \xC2\xB7  %d won", c->rec.rating, c->rec.played, c->rec.won);
	int w = sg_text_w(&font_small, line) + 16;
	sd_glass(s, (SW - w) / 2, 114, w, font_small.line + 6, 6, 170);
	sg_text_c(s, &font_small, SW / 2, 117, line, C_SUGAR);
	snprintf(line, sizeof line, "Hi, %s! Rules: %s", c->name, c->rec.ruleset == 'c' ? "Classic" : "Mutators");
	sg_text_outline(s, &font_label, (SW - sg_text_w(&font_label, line)) / 2, 132, line, C_SUGAR, C_INK, 1);
	if (c->busy[0] || snc_client_notice(c)) {
		status_plate(s, c, 150, 42);
		return;
	}
	sd_glass(s, 0, 150, SW, 42, 0, 215);
	sg_text(s, &font_body, 7, 153, "Pick a game to start.", C_SUGAR);
	sg_wrap(s, &font_small, 7, 154 + font_body.line, SW - 14, font_small.line + 1,
	        "Real games, online at smashandclash.in. The touch screen plays; this one shows.", C_SUGAR3, 2);
}

// A notice (or what we are waiting on) over the bottom of the top screen.
static void toast(snc_surf *s, const snc_client *c) {
	const char *notice = snc_client_notice(c);
	const char *text = notice ? notice : c->busy[0] ? c->busy : NULL;
	if (!text) return;
	int lines = sg_wrap(NULL, &font_small, 0, 0, SW - 24, font_small.line + 1, text, 0, 3);
	int h = lines * (font_small.line + 1) + 8;
	sd_glass(s, 6, SH - h - 4, SW - 12, h, 6, 225);
	sg_wrap(s, &font_small, 12, SH - h, SW - 24, font_small.line + 1, text, notice && c->notice_bad ? 0xFF8CC8 : C_SUN1, 3);
}

static void draw_preview(snc_surf *s, const snc_client *c, int y) {
	const snc_game *g = &c->game;
	char caption[64] = "";
	const snc_hcard *hc = NULL;
	int i = -1;
	if (c->pick.selected >= 0 && c->pick.selected < g->hand_n) {
		hc = &g->hand[c->pick.selected];
		i = sd_hand_card(hc);
		snprintf(caption, sizeof caption, "Your card");
	} else if (g->has_hop && g->hop_from >= 0) {
		i = sd_tile_card(g, g->hop_from);
		snprintf(caption, sizeof caption, "Hopping");
	} else if (g->last_move[0]) {
		i = sd_move_card(g, g->last_move);
		snprintf(caption, sizeof caption, "Last move: %s", g->last_move);
	}
	sd_glass(s, 4, y, SW - 8, ART_L_H + 8, 6, 150);
	if (i < 0) {
		sd_logo_s(s, (SW - LOGO_S_W) / 2, y + (ART_L_H + 8 - LOGO_S_H) / 2);
		return;
	}
	sd_card_large(s, 8, y + 4, i);
	int tx = 8 + ART_L_W + 8, tw = SW - 8 - tx;
	sg_text_fit(s, &font_tiny, tx, y + 4, tw, caption, C_SUGAR3);
	sg_text_fit(s, &font_label, tx, y + 4 + font_tiny.line, tw, snc_cards[i].name, C_SUGAR);
	int ty = y + 6 + font_tiny.line + font_label.line;
	if (i >= 0 && snc_cards[i].effect) {
		const char *does = hc ? hc->does : "";
		if (!does[0]) does = "An effect card: it plays when it can do something.";
		sg_wrap(s, &font_small, tx, ty, tw, font_small.line + 1, does, C_SUGAR2, 5);
	} else if (i >= 0) {
		int vals[4] = {snc_cards[i].top, snc_cards[i].right, snc_cards[i].bottom, snc_cards[i].left};
		if (hc) vals[0] = hc->top, vals[1] = hc->right, vals[2] = hc->bottom, vals[3] = hc->left;
		// the four sides as a cross, in the card's own colours
		static const uint32_t SIDE[4] = {0xF0525A, 0xF5C431, 0x4D6CF5, 0x5DC24A};
		int cx = tx + 26, cy = ty + 24;
		const int dx[4] = {0, 18, 0, -18}, dy[4] = {-15, 0, 15, 0};
		for (int k = 0; k < 4; k++) {
			char d[4];
			snprintf(d, sizeof d, "%d", vals[k]);
			int bx = cx + dx[k] - 7, by = cy + dy[k] - 7;
			sg_rrect(s, bx, by, 15, 14, 3, C_INK_DEEP, 255);
			sg_rrect(s, bx + 1, by + 1, 13, 12, 2, SIDE[k], 255);
			sg_text_c(s, &font_label, cx + dx[k] + 1, by + (14 - font_label.line) / 2 + 1, d, C_INK_DEEP);
			sg_text_c(s, &font_label, cx + dx[k], by + (14 - font_label.line) / 2, d, C_SUGAR);
		}
		const char *color = snc_cards[i].color;
		if (color && color[0]) {
			int px = tx + 60, py = cy - 6;
			sg_rrect(s, px, py, 12, 12, 6, sd_power_color(color), 255);
			sg_text(s, &font_small, px + 16, py, color, C_SUGAR2);
		}
	}
}

static void draw_top_game(view_t *v, snc_surf *s, const snc_client *c) {
	const snc_game *g = &c->game;
	sd_world(s);
	char opp[48];
	snc_opponent_name(g, opp, sizeof opp);
	// the other side: name, face-down hand, the deck
	sd_glass(s, 0, 0, SW, 22, 0, 200);
	sg_rrect(s, 6, 6, 10, 10, 5, C_OPP, 255);
	int n = snc_client_over(c) ? 0 : (g->status == SNC_WAITING ? 0 : c->opp_hand);
	int backs_x = SW - 6 - n * (BACK_S_W + 2);
	sg_text_fit(s, &font_label, 20, 4, backs_x - 26, g->status == SNC_WAITING ? "Waiting for a player" : opp, C_SUGAR);
	for (int i = 0; i < n; i++) sd_card_back(s, backs_x + i * (BACK_S_W + 2), 2);

	if (v->show_replay && snc_client_over(c) && g->replay_url[0]) {
		int size = sg_qr(NULL, 0, 0, 2, g->replay_url);
		if (size) {
			sg_qr(s, (SW - size) / 2, 24 + (SH - 24 - size) / 2, 2, g->replay_url);
			return;
		}
	}
	draw_preview(s, c, 25);

	// the score: you in blue, them in orange; the side to move wears the ring
	int py = 25 + ART_L_H + 12;
	int moving = c->busy[0] != 0;  // a move on its way: the turn is passing
	int turn_you = g->status == SNC_ACTIVE && g->your_turn && !moving, turn_them = g->status == SNC_ACTIVE && (!g->your_turn || moving);
	char t[64];
	sg_rrect(s, 4, py + 2, 122, 22, 6, C_YOU_LIP, 255);
	sg_rrect(s, 4, py, 122, 21, 6, C_YOU, 255);
	snprintf(t, sizeof t, "%d", g->score_you);
	sg_text(s, &font_label, 10, py + 3, "You", C_SUGAR);
	sg_text_r(s, &font_title, 120, py + 1, t, C_SUGAR);
	sg_rrect(s, 130, py + 2, 122, 22, 6, C_OPP_LIP, 255);
	sg_rrect(s, 130, py, 122, 21, 6, C_OPP, 255);
	snprintf(t, sizeof t, "%d", g->score_opp);
	sg_text(s, &font_title, 136, py + 1, t, C_SUGAR);
	sg_text_fit(s, &font_label, 136 + sg_text_w(&font_title, t) + 6, py + 3, 246 - (136 + sg_text_w(&font_title, t) + 6), opp, C_SUGAR);
	if (turn_you) sd_focus(s, 4, py, 122, 23, 6);
	if (turn_them) sd_focus(s, 130, py, 122, 23, 6);
	status_plate(s, c, py + 27, SH - (py + 27));
}

static void draw_top_waiting(view_t *v, snc_surf *s, const snc_client *c) {
	(void)v;
	const snc_game *g = &c->game;
	sd_world(s);
	if (c->mode == 'i' && g->invite_url[0]) {
		sg_text_outline(s, &font_title, (SW - sg_text_w(&font_title, "Invite a friend")) / 2, 3, "Invite a friend", C_SUGAR, C_INK, 1);
		int scale = 4, size = sg_qr(NULL, 0, 0, scale, g->invite_url);
		while (size > 150 && scale > 2) size = sg_qr(NULL, 0, 0, --scale, g->invite_url);
		sg_qr(s, (SW - size) / 2, 24, scale, g->invite_url);
		sg_text_outline(s, &font_small, (SW - sg_text_w(&font_small, "Scan it with a phone: they play in their browser.")) / 2, 24 + size + 3,
		                "Scan it with a phone: they play in their browser.", C_SUGAR, C_INK, 1);
		return;
	}
	if (c->mode == 'c' && g->code[0]) {
		sg_text_outline(s, &font_title, (SW - sg_text_w(&font_title, "Play by code")) / 2, 6, "Play by code", C_SUGAR, C_INK, 1);
		sd_panel(s, 28, 36, SW - 56, 70, 8);
		sg_text_c(s, &font_small, SW / 2, 41, "Your code", C_INK2);
		sg_text_c(s, &font_big, SW / 2, 54, g->code, C_INK);
		sg_text_outline(s, &font_small, (SW - sg_text_w(&font_small, "Another DS or a PSP: Play by code, then Join.")) / 2, 116,
		                "Another DS or a PSP: Play by code, then Join.", C_SUGAR, C_INK, 1);
		toast(s, c);
		return;
	}
	sg_text_outline(s, &font_title, (SW - sg_text_w(&font_title, "Quick match")) / 2, 6, "Quick match", C_SUGAR, C_INK, 1);
	sd_logo_l(s, (SW - LOGO_L_W) / 2, 30);
	toast(s, c);
}

static void draw_top_code(view_t *v, snc_surf *s, const snc_client *c) {
	(void)v;
	sd_world(s);
	sg_text_outline(s, &font_title, (SW - sg_text_w(&font_title, "Play by code")) / 2, 6, "Play by code", C_SUGAR, C_INK, 1);
	int bw = 30, gap = 6, x0 = (SW - (6 * bw + 5 * gap)) / 2;
	for (int i = 0; i < 6; i++) {
		int x = x0 + i * (bw + gap);
		sd_panel(s, x, 34, bw, 40, 6);
		if (i < c->code_len) {
			char ch[2] = {c->code[i], 0};
			sg_text_c(s, &font_big, x + bw / 2, 38, ch, C_INK);
		} else if (i == c->code_len) {
			sg_fill(s, x + 7, 66, bw - 14, 2, C_INK3);
		}
	}
	sd_glass(s, 8, 86, SW - 16, 62, 8, 170);
	sg_wrap(s, &font_small, 16, 90, SW - 32, font_small.line + 1,
	        "Join: type the code another console (or a terminal) shows, then Join.\nHost: get a code to share. They join from a DS, a PSP, or with npx smashandclash duel join.",
	        C_SUGAR, 5);
	toast(s, c);
}

static void draw_top_rules(view_t *v, snc_surf *s, const snc_client *c) {
	(void)v, (void)c;
	sd_world(s);
	sg_text_outline(s, &font_title, (SW - sg_text_w(&font_title, "How to play")) / 2, 6, "How to play", C_SUGAR, C_INK, 1);
	sd_glass(s, 6, 32, SW - 12, SH - 38, 8, 200);
	sg_text(s, &font_body, 14, 37, "On the touch screen", C_SUN1);
	sg_wrap(s, &font_small, 14, 39 + font_body.line, SW - 28, font_small.line + 1,
	        "Tap a card in your hand, then a green tile. An effect that needs a target lights its tiles the same way (Recruit! takes two: "
	        "the card, then where it goes). Flip! and Swap!: tap the card again. A hop: tap a green tile, or Stay.\n"
	        "With buttons: the D-pad moves, A picks, B puts a card back, X shows these rules, START resigns, SELECT shows the replay QR "
	        "after a game.",
	        C_SUGAR, 11);
}

/* ----------------------------- the bottom screen ------------------------------ */

static void draw_board(view_t *v, snc_surf *s, const snc_client *c, uint32_t now, int interactive) {
	const snc_game *g = &c->game;
	int mutators = g->ruleset != 'c';
	uint16_t lit = interactive && !c->busy[0] ? snc_targets(g, &c->pick) : 0;
	for (int cell = 0; cell < 15; cell++) {
		int x, y;
		cell_xy(c, cell, &x, &y);
		const snc_tile *t = &g->board[cell];
		const char *piece = chess_at(g, cell);
		int pw = power_at(g, cell);
		int card = t->card[0] != 0;
		int ghost = !card && c->pending_cell == cell;
		if (!card && !ghost) {
			uint32_t tint = pw >= 0 ? sd_power_color(g->power[pw].color) : C_SUGAR;
			sg_rrect(s, x, y, TW, TH, 3, tint, pw >= 0 ? 150 : 56);
			if (mutators && overrun_at(g, cell))  // overrun zone: a dashed inner line
				for (int k = 0; k < TW - 8; k += 4) {
					sg_fill(s, x + 4 + k, y + 3, 2, 1, C_SUGAR);
					sg_fill(s, x + 4 + k, y + TH - 4, 2, 1, C_SUGAR);
				}
			char name[3];
			snc_cell_name(cell, name);
			sg_text_c(s, &font_tiny, x + TW / 2, y + TH - font_tiny.line - 3, name, pw >= 0 ? C_INK : 0xCFE6FF);
			if (piece) {
				int iw = sd_icon_w(piece), ih = sd_icon_h(piece);
				sg_rrect(s, x + TW / 2 - 8, y + 9, 16, 16, 8, C_SUGAR, 230);
				sd_icon(s, x + (TW - iw) / 2, y + 17 - ih / 2, piece, C_GRAPE2);
			}
			if (pw >= 0) {
				char b[8];
				snprintf(b, sizeof b, "+%d", g->power[pw].boost);
				sg_text_c(s, &font_label, x + TW / 2, piece ? y + 26 : y + 12, b, C_SUGAR);
			}
		} else {
			int vals[4];
			const char *name = ghost ? c->pending_card : t->card;
			int owner = ghost ? 'y' : t->owner;
			int flags = (owner == 'o' ? CARD_ROT : 0) | (t->frozen ? CARD_FROZEN : 0) | (ghost ? CARD_GHOST : 0) |
			            ((c->flash & (1u << cell)) ? CARD_FLASH : 0);
			if (ghost) vals[0] = vals[1] = vals[2] = vals[3] = -1;
			else tile_vals(c, t, vals);
			sd_card(s, x, y, ghost ? snc_card_index(name) : sd_tile_card(g, cell), name, owner, vals, flags, &font_digit);
			if (piece) {  // a chess tile under a card: its piece in the corner
				sg_rrect(s, x + 1, y + 1, 11, 11, 5, C_SUGAR, 235);
				sd_icon(s, x + 1 + (11 - sd_icon_w(piece)) / 2, y + 1 + (11 - sd_icon_h(piece)) / 2, piece, C_GRAPE2);
			}
			if (pw >= 0) {
				sg_rrect(s, x + TW - 12, y + 1, 11, 11, 5, sd_power_color(g->power[pw].color), 255);
				char b[4];
				snprintf(b, sizeof b, "%d", g->power[pw].boost);
				sg_text_c(s, &font_tiny, x + TW - 6, y + 1, b, C_SUGAR);
			}
		}
		int chosen = 0;
		for (int k = 0; k < c->pick.path_n; k++)
			if (c->pick.path[k] == cell) chosen = 1;
		if (lit & (1u << cell)) {
			sg_rrect(s, x, y, TW, TH, 3, C_MINT2, 40 + pulse(now) * 60 / 100);
			sg_rframe(s, x, y, TW, TH, 3, 2, C_MINT2, 255);
		} else if (chosen) {
			sg_rrect(s, x, y, TW, TH, 3, C_MINT2, 150);
		}
		if (interactive) {
			hit(v, x, y, TW, TH, H_CELL, cell);
			if (focused(v, H_CELL, cell)) sd_focus(s, x, y, TW, TH, 3);
		}
	}
}

static void draw_hand(view_t *v, snc_surf *s, const snc_client *c, int interactive) {
	const snc_game *g = &c->game;
	for (int i = 0; i < g->hand_n && i < 5; i++) {
		int x = BX + i * (TW + GX), y = HAND_Y;
		int sel = c->pick.selected == i;
		int dim = g->your_turn && !g->has_hop && !snc_playable(g, i);
		int vals[4];
		hand_vals(&g->hand[i], vals);
		sd_card(s, x, sel ? y - 3 : y, sd_hand_card(&g->hand[i]), g->hand[i].card, 'y', vals, (dim ? CARD_DIM : 0) | (sel ? CARD_SELECTED : 0), &font_digit);
		if (interactive) {
			hit(v, x, y, TW, TH, H_HAND, i);
			if (focused(v, H_HAND, i)) sd_focus(s, x, sel ? y - 3 : y, TW, TH, 3);
		}
	}
}

static void button(view_t *v, snc_surf *s, int x, int y, int w, int h, const snc_font *f, const char *label, sd_fill fill, int id, int arg,
                   int off) {
	sd_button(s, x, y, w, h, f, label, fill, state(v, id, arg, off));
	if (!off) hit(v, x, y, w, h, id, arg);
}

static void button2(view_t *v, snc_surf *s, int x, int y, int w, int h, const char *label, const char *caption, sd_fill fill, int id) {
	sd_button2(s, x, y, w, h, &font_label, label, &font_tiny, caption, fill, state(v, id, 0, 0));
	hit(v, x, y, w, h, id, 0);
}

// A notice, or what we are waiting on, as a plate across the touch screen at y.
static void bottom_toast(snc_surf *s, const snc_client *c, int y) {
	const char *notice = snc_client_notice(c);
	const char *text = notice ? notice : c->busy[0] ? c->busy : NULL;
	if (!text) return;
	int lines = sg_wrap(NULL, &font_small, 0, 0, SW - 28, font_small.line + 1, text, 0, 3);
	int h = lines * (font_small.line + 1) + 10;
	sd_glass(s, 8, y, SW - 16, h, 6, 235);
	sg_rframe(s, 8, y, SW - 16, h, 6, 1, notice && c->notice_bad ? C_GUM2 : C_SUN2, 255);
	sg_wrap(s, &font_small, 14, y + 5, SW - 28, font_small.line + 1, text, notice && c->notice_bad ? 0xFF8CC8 : C_SUN1, 3);
}

// The score, one plate per side; the side to move wears the ring.
static void score_plates(snc_surf *s, const snc_client *c, int x, int y, int w) {
	const snc_game *g = &c->game;
	char opp[48], t[16];
	snc_opponent_name(g, opp, sizeof opp);
	int moving = c->busy[0] != 0;
	int you_move = g->status == SNC_ACTIVE && g->your_turn && !moving;
	int they_move = g->status == SNC_ACTIVE && (!g->your_turn || moving);
	int h = 19;
	for (int k = 0; k < 2; k++) {
		int py = y + k * (h + 4);
		uint32_t base = k ? C_OPP : C_YOU, lip = k ? C_OPP_LIP : C_YOU_LIP;
		sg_rrect(s, x, py + 2, w, h, 5, lip, 255);
		sg_rrect(s, x, py, w, h, 5, base, 255);
		snprintf(t, sizeof t, "%d", k ? g->score_opp : g->score_you);
		int tw = sg_text_w(&font_label, t);
		sg_text(s, &font_label, x + w - 5 - tw, py + (h - font_label.line) / 2 + 1, t, C_SUGAR);
		sg_text_fit(s, &font_small, x + 5, py + (h - font_small.line) / 2 + 1, w - 14 - tw, k ? opp : "You", C_SUGAR);
		if ((k == 0 && you_move) || (k == 1 && they_move)) sd_focus(s, x, py, w, h + 1, 5);
	}
}

// What to do next, in the narrow column beside the board.
static void status_card(snc_surf *s, const snc_client *c, int x, int y, int w, int h) {
	snc_status_t st;
	snc_client_status(c, &st);
	sd_glass(s, x, y, w, h, 6, 205);
	int lh = font_small.line;
	int main_lines = sg_wrap(NULL, &font_small, 0, 0, w - 8, lh, st.main, 0, 3);
	sg_wrap(s, &font_small, x + 4, y + 3, w - 8, lh, st.main, C_SUGAR, 3);
	int dy = y + 5 + main_lines * lh;
	int room = (y + h - 3 - dy) / font_tiny.line;
	const char *notice = snc_client_notice(c);
	if (room > 0) {
		if (notice) sg_wrap(s, &font_tiny, x + 4, dy, w - 8, font_tiny.line, notice, c->notice_bad ? 0xFF8CC8 : C_SUN1, room);
		else if (st.detail[0]) sg_wrap(s, &font_tiny, x + 4, dy, w - 8, font_tiny.line, st.detail, C_SUGAR3, room);
	}
}

static void draw_bottom_waiting(view_t *v, snc_surf *s, const snc_client *c) {
	const snc_game *g = &c->game;
	snc_status_t st;
	snc_client_status(c, &st);
	if (c->mode == 'i' && g->invite_url[0]) {
		// the invite as a QR code a phone can scan, with what it is beside it
		int scale = 3, size = sg_qr(NULL, 0, 0, scale, g->invite_url);
		if (size > 138) size = sg_qr(NULL, 0, 0, scale = 2, g->invite_url);
		sg_qr(s, 6, 6, scale, g->invite_url);
		int tx = 6 + size + 8, tw = SW - 6 - tx;
		sd_glass(s, tx - 2, 6, tw + 2, size, 6, 190);
		sg_wrap(s, &font_body, tx + 3, 10, tw - 6, font_body.line, "Invite a friend", C_SUN1, 2);
		sg_wrap(s, &font_small, tx + 3, 12 + font_body.line * 2, tw - 6, font_small.line + 1,
		        "Scan the code with a phone. Your friend plays you in their browser: no app needed.", C_SUGAR, 7);
	} else if (c->mode == 'c' && g->code[0]) {
		sd_panel(s, 6, 6, SW - 12, 66, 8);
		sg_text_c(s, &font_small, SW / 2, 10, "Your code", C_INK2);
		sg_text_c(s, &font_big, SW / 2, 24, g->code, C_INK);
		sd_glass(s, 6, 80, SW - 12, 60, 8, 190);
		sg_wrap(s, &font_small, 14, 85, SW - 28, font_small.line + 1, st.detail, C_SUGAR, 5);
	} else {
		sd_logo_s(s, (SW - LOGO_S_W) / 2, 8);
		sd_glass(s, 6, 54, SW - 12, 86, 8, 190);
		sg_text_fit(s, &font_body, 14, 60, SW - 28, st.main, C_SUGAR);
		sg_wrap(s, &font_small, 14, 62 + font_body.line, SW - 28, font_small.line + 1, st.detail, C_SUGAR3, 5);
	}
	button(v, s, 6, 148, 110, 38, &font_label, "How to play", FILL_SUGAR, H_HOW, 0, 0);
	button(v, s, SW - 6 - 110, 148, 110, 38, &font_label, "Cancel", FILL_CHERRY, H_RESIGN, 0, c->busy[0] != 0);
	if (snc_client_notice(c)) bottom_toast(s, c, 96);
}

static void draw_bottom_game(view_t *v, snc_surf *s, const snc_client *c, uint32_t now) {
	const snc_game *g = &c->game;
	sd_world(s);
	if (g->status == SNC_WAITING) {
		draw_bottom_waiting(v, s, c);
		return;
	}
	int over = snc_client_over(c);
	if (over && v->show_replay && g->replay_url[0] && sg_qr(NULL, 0, 0, 2, g->replay_url)) {
		int size = sg_qr(NULL, 0, 0, 2, g->replay_url);
		sg_qr(s, BX + (5 * (TW + GX) - GX - size) / 2, (SH - size) / 2, 2, g->replay_url);
	} else {
		draw_board(v, s, c, now, !over);
		if (!over) draw_hand(v, s, c, 1);
	}
	score_plates(s, c, RX, 3, RW);
	snc_status_t st;
	snc_client_status(c, &st);
	if (over) {
		// the result, across the hand row
		if (!v->show_replay) {
			sd_glass(s, 2, HAND_Y - 2, RX - 6, SH - HAND_Y + 1, 6, 225);
			sg_text_fit(s, &font_body, 8, HAND_Y + 1, RX - 18, st.main, C_SUN1);
			const char *notice = snc_client_notice(c);
			sg_wrap(s, &font_tiny, 8, HAND_Y + 3 + font_body.line, RX - 18, font_tiny.line, notice ? notice : st.detail,
			        notice && c->notice_bad ? 0xFF8CC8 : C_SUGAR, 3);
		}
		button(v, s, RX, 52, RW, 40, &font_label, "Again", FILL_SUN, H_AGAIN, 0, c->busy[0] != 0 || c->mode == 'j' || c->mode == 'c');
		button(v, s, RX, 98, RW, 40, &font_label, "Lobby", FILL_SUGAR, H_LOBBY, 0, c->busy[0] != 0);
		if (g->replay_url[0]) button(v, s, RX, 144, RW, 44, &font_small, v->show_replay ? "Hide QR" : "Replay QR", FILL_SKY, H_REPLAY, 0, 0);
		return;
	}
	int action = st.action[0] != 0;
	status_card(s, c, RX, 50, RW, action ? 66 : 94);
	if (action) button(v, s, RX, 120, RW, 28, &font_small, st.action, FILL_MINT, H_ACTION, 0, c->busy[0] != 0);
	button(v, s, RX, 152, RW, 16, &font_tiny, "Rules", FILL_SUGAR, H_HOW, 0, 0);
	button(v, s, RX, 171, RW, 18, &font_small, "Resign", FILL_CHERRY, H_RESIGN, 0, c->busy[0] != 0);
}

static void draw_bottom_lobby(view_t *v, snc_surf *s, const snc_client *c) {
	sd_world(s);
	// who you are, on the touch screen too (it may be the only screen shown)
	sd_logo_s(s, 6, 2);
	char line[64];
	snprintf(line, sizeof line, "Rating %d", c->rec.rating);
	sg_text_outline(s, &font_label, 6 + LOGO_S_W + 8, 4, line, C_SUGAR, C_INK, 1);
	snprintf(line, sizeof line, "%d played \xC2\xB7 %d won \xC2\xB7 %s", c->rec.played, c->rec.won, c->rec.ruleset == 'c' ? "Classic" : "Mutators");
	sg_text_outline(s, &font_small, 6 + LOGO_S_W + 8, 6 + font_label.line, line, C_SUGAR, C_INK, 1);
	int w = 119, h = 46, x0 = 6, x1 = SW - 6 - w;
	button2(v, s, x0, 44, w, h, "New game", "at your level", FILL_SUN, H_NEW);
	button2(v, s, x1, 44, w, h, "Quick match", "whoever is online", FILL_SUGAR, H_QUICK);
	button2(v, s, x0, 94, w, h, "Invite a friend", "they play in a browser", FILL_SUGAR, H_INVITE);
	button2(v, s, x1, 94, w, h, "Play by code", "DS, PSP or a terminal", FILL_SUGAR, H_CODE);
	button2(v, s, x0, 144, w, h, c->rec.ruleset == 'c' ? "Rules: Classic" : "Rules: Mutators", "press to switch",
	        c->rec.ruleset == 'c' ? FILL_SKY : FILL_GRAPE, H_RULESET);
	button2(v, s, x1, 144, w, h, "How to play", "the rules, in full", FILL_SUGAR, H_HOW);
	bottom_toast(s, c, 2);
}

static void draw_bottom_code(view_t *v, snc_surf *s, const snc_client *c) {
	sd_world(s);
	int bw = 24, gap = 4, bx = (SW - (6 * bw + 5 * gap)) / 2;
	for (int i = 0; i < 6; i++) {  // the code so far
		int x = bx + i * (bw + gap);
		sd_panel(s, x, 3, bw, 30, 5);
		if (i < c->code_len) {
			char ch[2] = {c->code[i], 0};
			sg_text_c(s, &font_title, x + bw / 2, 6, ch, C_INK);
		} else if (i == c->code_len) {
			sg_fill(s, x + 6, 24, bw - 12, 2, C_INK3);
		}
	}
	int kw = 28, kh = 24, kg = 3, cols = 8;
	int x0 = (SW - (cols * kw + (cols - 1) * kg)) / 2;
	for (int i = 0; CODE_ALPHABET[i]; i++) {
		int x = x0 + (i % cols) * (kw + kg), y = 38 + (i / cols) * (kh + 3);
		char ch[2] = {CODE_ALPHABET[i], 0};
		button(v, s, x, y, kw, kh, &font_label, ch, FILL_SUGAR, H_KEY, i, c->code_len >= 6);
	}
	int y = 38 + 4 * (kh + 3) + 4;
	button(v, s, 6, y, 58, SH - 4 - y, &font_label, "Back", FILL_SUGAR, H_BACK, 0, 0);
	button(v, s, 68, y, 58, SH - 4 - y, &font_label, "Delete", FILL_SUGAR, H_DEL, 0, c->code_len == 0);
	button(v, s, 130, y, 58, SH - 4 - y, &font_label, "Host", FILL_SKY, H_HOST, 0, c->busy[0] != 0);
	button(v, s, 192, y, 58, SH - 4 - y, &font_label, "Join", FILL_SUN, H_JOIN, 0, c->code_len != 6 || c->busy[0] != 0);
	bottom_toast(s, c, 60);
}

static void draw_bottom_rules(view_t *v, snc_surf *s, const snc_client *c) {
	sd_world(s);
	sd_glass(s, 4, 4, SW - 8, 140, 8, 225);
	snc_surf clipped = *s;
	sg_clip(&clipped, 4, 7, SW - 8, 134);
	const char *text = c->rules_ready ? c->rules : "Fetching the rules...";
	int lh = font_small.line + 1;
	int lines = sg_wrap(NULL, &font_small, 0, 0, SW - 24, lh, text, 0, 0);
	int visible = 134 / lh;
	v->scroll_max = lines > visible ? lines - visible : 0;
	if (v->scroll > v->scroll_max) v->scroll = v->scroll_max;
	sg_wrap(&clipped, &font_small, 11, 8 - v->scroll * lh, SW - 24, lh, text, C_SUGAR, 0);
	if (v->scroll_max > 0) {  // where we are in it
		int bar = 128 * visible / lines;
		int pos = (128 - bar) * v->scroll / v->scroll_max;
		sg_rrect(s, SW - 9, 10 + pos, 3, bar, 1, C_SUGAR_EDGE, 220);
	}
	button(v, s, 6, 150, 72, 36, &font_label, "Up", FILL_SUGAR, H_UP, 0, v->scroll == 0);
	button(v, s, 84, 150, 72, 36, &font_label, "Down", FILL_SUGAR, H_DOWN, 0, v->scroll >= v->scroll_max);
	button(v, s, SW - 6 - 88, 150, 88, 36, &font_label, "Back", FILL_SUN, H_BACK, 0, 0);
	bottom_toast(s, c, 60);
}

/* ----------------------------------- frame ------------------------------------ */

void view_draw(view_t *v, snc_client *c, snc_surf *top, snc_surf *bottom, uint32_t now) {
	view_begin(v, c);
	int screen = c->screen;
	if (screen == SC_GAME && !c->have_game) screen = SC_LOBBY;  // a game on its way: the lobby stays up under the status
	switch (screen) {
	case SC_RULES:
		draw_top_rules(v, top, c);
		draw_bottom_rules(v, bottom, c);
		break;
	case SC_CODE:
		draw_top_code(v, top, c);
		draw_bottom_code(v, bottom, c);
		break;
	case SC_GAME:
		if (c->have_game && c->game.status == SNC_WAITING) draw_top_waiting(v, top, c);
		else draw_top_game(v, top, c);
		draw_bottom_game(v, bottom, c, now);
		break;
	default:
		draw_top_lobby(v, top, c);
		draw_bottom_lobby(v, bottom, c);
		break;
	}
	view_end(v, c);
}
