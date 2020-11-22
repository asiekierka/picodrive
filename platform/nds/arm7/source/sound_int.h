#ifndef __PICO_NDS_SOUND_INT_H__
#define __PICO_NDS_SOUND_INT_H__

#include <stdint.h>

#define PICO_INTERNAL
void a9dprintf(const char *format, ...);

// main oscillator clock which controls timing
#define OSC_NTSC 53693100
#define OSC_PAL  53203424

#define POPT_EN_FM          (1<< 0)
#define POPT_EN_PSG         (1<< 1)
#define POPT_EN_STEREO      (1<< 3)

// sound/sound.c
extern void (*PsndMix_32_to_16l)(short *dest, int *src, int count);
void PsndRerate(int preserve_state);
void PsndClear(void);
void PsndStartFrame(void);
void PsndGetSamples(int scanline);
void PsndDoDAC(int line_to);
void PsndDoPSG(int line_to);

void ym2612_sync_timers(int z80_cycles, int mode_old, int mode_new);
void ym2612_pack_state(void);
void ym2612_unpack_state(void);

#endif