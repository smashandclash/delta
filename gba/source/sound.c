// The GBA's sound. See sound.h.
//
// Timer 0 ticks at 13379 Hz (16.78 MHz / 1254), exactly 224 samples a frame, so a sound's
// length in frames says when it ends: the vertical-blank interrupt counts them down, then
// loops the theme (or stops a jingle or an effect) by restarting its DMA.
#include "sound.h"

#include <tonc.h>

#include "audio.h"

typedef struct {
	const uint8_t *data;
	uint32_t frames;  // its length
	volatile uint32_t left;
	int loop;
} voice_t;

static voice_t music, fx;
static snc_music playing = MUS_NONE;
static int fx_now = -1;

static const struct { const uint8_t *data; uint32_t samples; } SFX[SFX_COUNT] = {
	[SFX_MOVE] = {snd_sfx_move, SND_SFX_MOVE_SAMPLES},       [SFX_SELECT] = {snd_sfx_select, SND_SFX_SELECT_SAMPLES},
	[SFX_BACK] = {snd_sfx_back, SND_SFX_BACK_SAMPLES},       [SFX_PLACE] = {snd_sfx_place, SND_SFX_PLACE_SAMPLES},
	[SFX_CAPTURE] = {snd_sfx_capture, SND_SFX_CAPTURE_SAMPLES}, [SFX_TURN] = {snd_sfx_turn, SND_SFX_TURN_SAMPLES},
	[SFX_ERROR] = {snd_sfx_error, SND_SFX_ERROR_SAMPLES},    [SFX_START] = {snd_sfx_start, SND_SFX_START_SAMPLES},
};

// DirectSound A (music) on DMA 1, B (effects) on DMA 2.
static void start(int b, const uint8_t *data) {
	if (b) {
		REG_DMA2CNT = 0;
		REG_SNDDSCNT |= SDS_BRESET;
		REG_DMA2SAD = (uint32_t)data;
		REG_DMA2DAD = (uint32_t)&REG_FIFO_B;
		REG_DMA2CNT = DMA_DST_FIXED | DMA_REPEAT | DMA_32 | DMA_AT_FIFO | DMA_ENABLE;
	} else {
		REG_DMA1CNT = 0;
		REG_SNDDSCNT |= SDS_ARESET;
		REG_DMA1SAD = (uint32_t)data;
		REG_DMA1DAD = (uint32_t)&REG_FIFO_A;
		REG_DMA1CNT = DMA_DST_FIXED | DMA_REPEAT | DMA_32 | DMA_AT_FIFO | DMA_ENABLE;
	}
}

static void stop(int b) {
	if (b) REG_DMA2CNT = 0, REG_SNDDSCNT |= SDS_BRESET;
	else REG_DMA1CNT = 0, REG_SNDDSCNT |= SDS_ARESET;
}

void gba_sound_init(void) {
	REG_SNDSTAT = SSTAT_ENABLE;  // first: the other sound registers need it on
	// A (music) at half volume, B (effects) at full, both to both speakers, both on timer 0
	REG_SNDDSCNT = SDS_B100 | SDS_AR | SDS_AL | SDS_ATMR0 | SDS_BR | SDS_BL | SDS_BTMR0 | SDS_ARESET | SDS_BRESET;
	REG_TM0CNT = 0;
	REG_TM0D = (uint16_t)(65536 - 1254);
	REG_TM0CNT = TM_ENABLE;
}

void gba_sound_music(snc_music m) {
	if (m == playing) return;
	playing = m;
	const uint8_t *data = NULL;
	uint32_t samples = 0;
	switch (m) {
	case MUS_LOBBY: data = snd_lobby, samples = SND_LOBBY_SAMPLES; break;
	case MUS_GAME: data = snd_game, samples = SND_GAME_SAMPLES; break;
	case MUS_WIN: data = snd_win, samples = SND_WIN_SAMPLES; break;
	case MUS_LOSE: data = snd_lose, samples = SND_LOSE_SAMPLES; break;
	default: break;
	}
	music.left = 0;
	if (!data) {
		stop(0);
		return;
	}
	music.data = data;
	music.frames = samples / 224;
	music.loop = m == MUS_LOBBY || m == MUS_GAME;
	start(0, data);
	music.left = music.frames;
}

void gba_sound_sfx(unsigned mask) {
	int best = -1;
	for (int i = 0; i < SFX_COUNT; i++)
		if ((mask & (1u << i)) && (best < 0 || SFX_PRIORITY[i] > SFX_PRIORITY[best])) best = i;
	if (best < 0) return;
	if (fx.left && fx_now >= 0 && SFX_PRIORITY[fx_now] > SFX_PRIORITY[best]) return;  // something more important is playing
	fx_now = best;
	fx.left = 0;
	fx.data = SFX[best].data;
	fx.frames = SFX[best].samples / 224;
	fx.loop = 0;
	start(1, fx.data);
	fx.left = fx.frames;
}

void gba_sound_vblank(void) {
	if (music.left && --music.left == 0) {
		if (music.loop) {
			start(0, music.data);
			music.left = music.frames;
		} else {
			stop(0);
		}
	}
	if (fx.left && --fx.left == 0) {
		stop(1);
		fx_now = -1;
	}
}
