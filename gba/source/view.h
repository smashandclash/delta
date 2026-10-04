// The Game Boy Advance's screen: one 240 x 160 bitmap in a retro take on the Arena Pop
// look (pixel fonts, hard edges, dithered blue world), plus what the hardware sprites
// show over it: the selection brackets and the "thinking" dots.
#ifndef GBA_VIEW_H
#define GBA_VIEW_H

#include <stdint.h>

#include "snc_client.h"
#include "snc_gfx.h"
#include "snc_ui.h"

#define GBA_W 240
#define GBA_H 160

typedef struct {
	int inspect;              // the card inspector (SELECT) is open
	int inspect_card;         // ... showing this card (index in snc_cards)
	int inspect_owner;        // 'y' / 'o' / 0
	int inspect_vals[4];
	char inspect_does[200];
	int cur_x, cur_y, cur_w, cur_h, cur_on;  // the brackets, around what has the focus
	int dots_x, dots_y, dots_on;             // the thinking dots, after the busy text
	int bridge_lua, bridge_up;               // the boot screen's checklist
} gba_ui;

extern gba_ui ui;

void view_draw(view_t *v, snc_client *c, snc_surf *s, uint32_t now);
void view_boot(snc_surf *s, const char *title, const char *text, int bad);

// SELECT: open the inspector on the focused card (hand or board), or close it.
void view_toggle_inspect(view_t *v, snc_client *c);

// The sprites' art: one corner of the brackets (8 x 8, top-left; flipped for the others)
// and one dot, as palette indices (0 clear, 1 ink, 2 sun, 3 light sun).
extern const uint8_t CURSOR_CORNER[8][8];
extern const uint8_t DOT[8][8];
extern const uint32_t SPRITE_COLORS[4];
// Where the brackets sit for a frame: they breathe out and in.
int view_cursor_offset(uint32_t frame);

#endif
