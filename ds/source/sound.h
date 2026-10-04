// The Nintendo DS's sound: its sound hardware plays the themes as IMA-ADPCM (looping in
// hardware) and the jingles and effects as 8-bit PCM, through libnds (the ARM7 mixes).
#ifndef DS_SOUND_H
#define DS_SOUND_H

#include "snc_sound.h"

void ds_sound_init(void);
void ds_sound_music(snc_music m);  // starts it if it is not already playing
void ds_sound_sfx(unsigned fx);    // a mask of SFX_*

#endif
