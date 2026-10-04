// The Nintendo DS screens: the touch screen plays (and holds the whole game, for a
// one-screen skin); the top screen shows the card you are looking at, the score and
// what to do next. Targets, navigation and presses are shared: core/snc_ui.h.
#ifndef VIEW_H
#define VIEW_H

#include <stdint.h>

#include "snc_client.h"
#include "snc_gfx.h"
#include "snc_ui.h"

void view_draw(view_t *v, snc_client *c, snc_surf *top, snc_surf *bottom, uint32_t now);

#endif
