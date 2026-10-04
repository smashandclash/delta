// The Nintendo DS's sound. See sound.h.
#include "sound.h"

#include <nds.h>

#include "audio.h"

#define MUSIC_VOLUME 88   // of 127: the themes sit under the effects
#define JINGLE_VOLUME 110
#define SFX_VOLUME 127

static int music_id = -1;
static snc_music playing = MUS_NONE;

static const struct { const uint8_t *data; uint32_t bytes; } SFX[SFX_COUNT] = {
	[SFX_MOVE] = {snd_sfx_move, SND_SFX_MOVE_BYTES},          [SFX_SELECT] = {snd_sfx_select, SND_SFX_SELECT_BYTES},
	[SFX_BACK] = {snd_sfx_back, SND_SFX_BACK_BYTES},          [SFX_PLACE] = {snd_sfx_place, SND_SFX_PLACE_BYTES},
	[SFX_CAPTURE] = {snd_sfx_capture, SND_SFX_CAPTURE_BYTES}, [SFX_TURN] = {snd_sfx_turn, SND_SFX_TURN_BYTES},
	[SFX_ERROR] = {snd_sfx_error, SND_SFX_ERROR_BYTES},       [SFX_START] = {snd_sfx_start, SND_SFX_START_BYTES},
};

void ds_sound_init(void) {
	soundEnable();
}

void ds_sound_music(snc_music m) {
	if (m == playing) return;
	playing = m;
	if (music_id >= 0) soundKill(music_id);
	music_id = -1;
	switch (m) {
	// ADPCM loops from the first word after its header (the hardware keeps the decoder's state there)
	case MUS_LOBBY: music_id = soundPlaySample(snd_lobby, SoundFormat_ADPCM, SND_LOBBY_BYTES, SND_LOBBY_RATE, MUSIC_VOLUME, 64, true, 1); break;
	case MUS_GAME: music_id = soundPlaySample(snd_game, SoundFormat_ADPCM, SND_GAME_BYTES, SND_GAME_RATE, MUSIC_VOLUME, 64, true, 1); break;
	case MUS_WIN: music_id = soundPlaySample(snd_win, SoundFormat_8Bit, SND_WIN_BYTES, SND_WIN_RATE, JINGLE_VOLUME, 64, false, 0); break;
	case MUS_LOSE: music_id = soundPlaySample(snd_lose, SoundFormat_8Bit, SND_LOSE_BYTES, SND_LOSE_RATE, JINGLE_VOLUME, 64, false, 0); break;
	default: break;
	}
}

void ds_sound_sfx(unsigned mask) {
	for (int i = 0; i < SFX_COUNT; i++)  // the DS has channels to spare: every one plays
		if (mask & (1u << i)) soundPlaySample(SFX[i].data, SoundFormat_8Bit, SFX[i].bytes, 16384, SFX_VOLUME, 64, false, 0);
}
