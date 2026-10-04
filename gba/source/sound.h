// The GBA's sound: DirectSound A plays the music, DirectSound B the effects, both 8-bit
// PCM at 13379 Hz fed by DMA from the cartridge (224 samples a frame).
#ifndef GBA_SOUND_H
#define GBA_SOUND_H

#include "snc_sound.h"

void gba_sound_init(void);
void gba_sound_music(snc_music m);   // starts it if it is not already playing
void gba_sound_sfx(unsigned fx);     // a mask of SFX_*: the one that matters most plays
void gba_sound_vblank(void);         // from the vertical-blank interrupt: loops and ends

#endif
