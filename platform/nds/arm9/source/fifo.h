#ifndef __PICO_NDS_FIFO_H__
#define __PICO_NDS_FIFO_H__

#include <stdbool.h>
#include <stdint.h>

#ifndef FIFO_CHANNEL_PICO
#define FIFO_CHANNEL_PICO 8
#endif

void ndsFifoSendPsndGetSamples(int y);
void ndsFifoSendNdsInit(short *sound_buffer, int buffer_len);
void ndsFifoSendPsndStartFrame(void);
void ndsFifoSendPsndRerate(bool preserve_state, bool pal, unsigned int opt);
void ndsFifoSendYM2612InitTables(void *ym_tl_tab, void *lfo_pm_table, void *fn_table);
void ndsFifoSendYM2612ResetChip(void);
void ndsFifoSendYM2612DacSetLine(unsigned char d, int scanline);
void ndsFifoSendYM2612DacOut(unsigned char d, int scanline);
int ndsFifoSendYM2612Write(unsigned int a, unsigned int d, int scanline);

#endif