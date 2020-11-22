#ifndef __PICO_NDS_SOUND_COMMS_H__
#define __PICO_NDS_SOUND_COMMS_H__

#include <nds/ndstypes.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef FIFO_CHANNEL_PICO
#define FIFO_CHANNEL_PICO 8
#endif

typedef struct {
	unsigned int opt; // POPT_* bitfield (PicoIn.opt)
    int sndRate; // rate in Hz (PicoIn.sndRate)
    short *sndOut; // PicoIn.sndOut
	void (*writeSound)(int len);   // write .sndOut callback, called once per frame (PicoIn.writeSound)
    unsigned char pal; // Pico.m.pal
    unsigned char status; // rapid_ym2612, multi_ym_updates (Pico.m.status)

    // PicoSound
    short len;                            // number of mono samples
    short len_use;                        // adjusted
    int len_e_add;                        // for non-int samples/frame
    int len_e_cnt;
    short dac_line;
    short psg_line;
} PicoDSSound;

// datamsg
#define FIFO_CMD_PSND_RERATE 0x0001
#define FIFO_CMD_YM2612_INIT_TABLES 0x0002
#define FIFO_CMD_NDS_INIT 0x0003
#define FIFO_CMD_DBG_OUT 0x0004 // ARM7 -> ARM9
#define FIFO_CMD_PSND_FRAME 0x0005
#define FIFO_CMD_YM2612_RESET_CHIP 0x0006

typedef struct {
    u16 id;
} PicoDSMessageEmpty;

typedef struct {
    u16 id;
    u32 opt;
    bool preserve_state;
    bool pal;
} PicoDSMessagePsndRerate;

typedef struct {
    u16 id;
    u16 *ym_tl_tab;
    s32 *lfo_pm_table;
    u32 *fn_table;
} PicoDSMessageYM2612InitTables;

typedef struct {
    u16 id;
    short *buffer;
    int buffer_len;
} PicoDSMessageNDSInit;

typedef struct {
    u16 id;
    char debug_data[60];
} PicoDSMessageDbgOut;

typedef struct {
    u16 id;
    u16 scanline;
    u16 ym2612_entry_count;
    u32 *ym2612_entries;
} PicoDSMessagePsndFrame;

// return

#endif