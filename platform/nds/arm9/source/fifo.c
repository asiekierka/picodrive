#include <nds.h>
#include <stdio.h>
#include "fifo.h"
#include "sound_comms.h"

#define YM2612_WBUF_SIZE 512
#define YM2612_WBUF_BUFS 16
static u32 ym2612_wbuf[YM2612_WBUF_BUFS][YM2612_WBUF_SIZE];
static u16 ym2612_wbuf_pos;
static u16 ym2612_wbuf_buf = 0;

static void ndsYm2612Clear() {
    ym2612_wbuf_pos = 0;
}

static inline void ndsYm2612Append(u32 value) {
    if (ym2612_wbuf_pos >= YM2612_WBUF_SIZE) {
        return; // flushing is not an option
    }
    ym2612_wbuf[ym2612_wbuf_buf][ym2612_wbuf_pos++] = value;
}

void ndsFifoSendPsndRerate(bool preserve_state, bool pal, unsigned int opt) {
    ndsYm2612Clear();

    PicoDSMessagePsndRerate msg;
    msg.id = FIFO_CMD_PSND_RERATE;
    msg.opt = opt;
    msg.preserve_state = preserve_state;
    msg.pal = pal;
    while (!fifoSendDatamsg(FIFO_CHANNEL_PICO, sizeof(msg), &msg));
}

void ndsFifoSendYM2612InitTables(void *ym_tl_tab, void *lfo_pm_table, void *fn_table) {
    PicoDSMessageYM2612InitTables msg;
    msg.id = FIFO_CMD_YM2612_INIT_TABLES;
    msg.ym_tl_tab = ym_tl_tab;
    msg.lfo_pm_table = lfo_pm_table;
    msg.fn_table = fn_table;
    while (!fifoSendDatamsg(FIFO_CHANNEL_PICO, sizeof(msg), &msg));
}

void ndsFifoSendNdsInit(short *sound_buffer, int buffer_len) {
    ndsYm2612Clear();

    PicoDSMessageNDSInit msg;
    msg.id = FIFO_CMD_NDS_INIT;
    msg.buffer = sound_buffer;
    msg.buffer_len = buffer_len;
    while (!fifoSendDatamsg(FIFO_CHANNEL_PICO, sizeof(msg), &msg));
}

static inline void ndsFifoSendEmpty(u16 id) {
    PicoDSMessageEmpty msg;
    msg.id = id;

    while (!fifoSendDatamsg(FIFO_CHANNEL_PICO, sizeof(msg), &msg));
}

void ndsFifoSendYM2612ResetChip(void) {
    ndsYm2612Clear();

    ndsFifoSendEmpty(FIFO_CMD_YM2612_RESET_CHIP);
}

void ndsFifoSendPsndGetSamples(int y) {

    PicoDSMessagePsndFrame msg;
    msg.id = FIFO_CMD_PSND_FRAME;
    msg.scanline = y;
    msg.ym2612_entry_count = ym2612_wbuf_pos;
    msg.ym2612_entries = ym2612_wbuf[ym2612_wbuf_buf];
    while (!fifoSendDatamsg(FIFO_CHANNEL_PICO, sizeof(msg), &msg));

    if (ym2612_wbuf_pos > 0) {
        ym2612_wbuf_pos = 0;
        ym2612_wbuf_buf = (ym2612_wbuf_buf + 1) % YM2612_WBUF_BUFS;
    }
}

void ndsFifoSendPsndStartFrame(void) {

}

void ndsFifoSendYM2612DacSetLine(unsigned char d, int scanline) {
//    while (!fifoSendValue32(FIFO_CHANNEL_PICO, (scanline & 0xFFFF) | (d << 16) | FIFO_CMD_YM2612_DAC_OUT));
}

void ndsFifoSendYM2612DacOut(unsigned char d, int scanline) {
//    while (!fifoSendValue32(FIFO_CHANNEL_PICO, (scanline & 0xFFFF) | (d << 16) | FIFO_CMD_YM2612_DAC_OUT));
}

int ndsFifoSendYM2612Write(unsigned int a, unsigned int d, int scanline) {
    u32 value = (scanline & 0x3FF) | ((d & 0xFF) << 10) | ((a & 0x1FF) << 18);
    ndsYm2612Append(value);
    return 1;
}
