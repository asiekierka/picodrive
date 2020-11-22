/*---------------------------------------------------------------------------------

	PicoDriveDS ARM7 core

		Copyright (C) 2020 Adrian "asie" Siekierka

	derived from the default ARM7 core

		Copyright (C) 2005 - 2010
		Michael Noland (joat)
		Jason Rogers (dovoto)
		Dave Murphy (WinterMute)

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any
	damages arising from the use of this software.

	Permission is granted to anyone to use this software for any
	purpose, including commercial applications, and to alter it and
	redistribute it freely, subject to the following restrictions:

	1.	The origin of this software must not be misrepresented; you
		must not claim that you wrote the original software. If you use
		this software in a product, an acknowledgment in the product
		documentation would be appreciated but is not required.

	2.	Altered source versions must be plainly marked as such, and
		must not be misrepresented as being the original software.

	3.	This notice may not be removed or altered from any source
		distribution.

---------------------------------------------------------------------------------*/
#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "sound_comms.h"
#include "sound_int.h"
#include "ym2612.h"
#include "sn76496.h"

u16 *ym_tl_tab;
s32 *lfo_pm_table;
short *sound_buffer;
int sound_buffer_pos;
int sound_buffer_len;

extern PicoDSSound PicoSound;

#define DEBUG

void a9dprintf(const char *format, ...) {
#ifdef DEBUG
	va_list ap;
	va_start(ap, format);

    PicoDSMessageDbgOut msg;
    msg.id = FIFO_CMD_DBG_OUT;
	vsniprintf(msg.debug_data, sizeof(msg.debug_data), format, ap);
    fifoSendDatamsg(FIFO_CHANNEL_PICO, sizeof(msg), &msg);

	va_end(ap);
#endif
}

static void picoYm2612Write(u32 value) {
	u8 a = (value >> 18) & 0x1FF;
	u8 d = (value >> 10) & 0xFF;
	u16 scanline = value & 0x3FF;
	// ym2612.REGS[a] = d;
	if (a == 0x2a) { /* DAC data */
		ym2612.dacout = ((int)d - 0x80) << 6;
		if (ym2612.dacen) {
			PsndDoDAC(scanline);
		}
	} else {
		ym2612.OPN.ST.address = a & 0xFF;
		ym2612.addr_A1 = a >> 8;
		YM2612Write_(1 + (ym2612.addr_A1 << 1), d);
	}
}

#define PSND_FRAME_COUNT 32
static PicoDSMessagePsndFrame frames[PSND_FRAME_COUNT];
static uint16_t frame_playback_pos = 0;
static uint16_t frame_pos = 0;
static bool frame_playing = false;

void picoFifoDatamsgHandler(int num_bytes, void *userdata) {
	u8 buffer[64];
	fifoGetDatamsg(FIFO_CHANNEL_PICO, 64, buffer);
	u16 id = *((u16*)buffer);

	switch (id) {
	case FIFO_CMD_YM2612_RESET_CHIP: {
		YM2612ResetChip();
	} break;
	case FIFO_CMD_PSND_RERATE: {
		// init sound hardware
		powerOn(POWER_SOUND);
		writePowerManagement(PM_CONTROL_REG, ( readPowerManagement(PM_CONTROL_REG) & ~PM_SOUND_MUTE ) | PM_SOUND_AMP );
		REG_SOUNDCNT = SOUND_ENABLE | 127;

		// init local
		frame_playback_pos = 0;
		frame_pos = 0;
		frame_playing = false;
		
		// init psnd
		PicoDSMessagePsndRerate *msg = buffer;
		PicoSound.opt = msg->opt;
		PicoSound.pal = msg->pal;
		PsndRerate(msg->preserve_state);
	} break;
	case FIFO_CMD_YM2612_INIT_TABLES: {
		PicoDSMessageYM2612InitTables *msg = buffer;
		ym_tl_tab = msg->ym_tl_tab;
		lfo_pm_table = msg->lfo_pm_table;
	} break;
	case FIFO_CMD_NDS_INIT: {
		PicoDSMessageNDSInit *msg = buffer;
		sound_buffer = msg->buffer;
		sound_buffer_pos = 0;
		sound_buffer_len = msg->buffer_len >> 1;
		PicoSound.sndOut = sound_buffer;
	} break;
	case FIFO_CMD_PSND_FRAME: {
		PicoDSMessagePsndFrame *msg = buffer;
		memcpy(&frames[frame_pos], msg, sizeof(PicoDSMessagePsndFrame));
		frame_pos = (frame_pos + 1) & (PSND_FRAME_COUNT - 1);
	} break;
	}
}

static uint16_t vbls = 0;
static uint16_t last_sound_vbls = 0;

void VblankHandler(void) {
	vbls++;
}

void VcountHandler() {
	inputGetAndSend();
}

volatile bool exitflag = false;

void powerButtonCB() {
	exitflag = true;
}

int cid = 0;

void writeSoundCB(int len) {
	SCHANNEL_CR(cid) = 0;
	SCHANNEL_TIMER(cid) = ((BUS_CLOCK >> 1) / PicoSound.sndRate) ^ 0xFFFF;
	SCHANNEL_SOURCE(cid) = (u32) PicoSound.sndOut;
	SCHANNEL_LENGTH(cid) = len >> 2;

	if (vbls == last_sound_vbls) {
		swiWaitForVBlank();
	}
	SCHANNEL_CR(cid) = SCHANNEL_ENABLE | SOUND_VOL(96) | SOUND_PAN(64) | SOUND_FORMAT_16BIT | SOUND_ONE_SHOT;
	last_sound_vbls = vbls;
	cid = (cid + 1) & 7;

	sound_buffer_pos += (len >> 1);
	if (sound_buffer_pos > (sound_buffer_len - (len >> 1))) {
		sound_buffer_pos = 0;
	}
	PicoSound.sndOut = sound_buffer + sound_buffer_pos;
}

int get_ticks(void) {
	return TIMER_DATA(2) | (TIMER_DATA(3) << 16);
}

//---------------------------------------------------------------------------------
int main() {
//---------------------------------------------------------------------------------
	// clear sound registers
	dmaFillWords(0, (void*)0x04000400, 0x100);

	REG_SOUNDCNT |= SOUND_ENABLE;
	writePowerManagement(PM_CONTROL_REG, ( readPowerManagement(PM_CONTROL_REG) & ~PM_SOUND_MUTE ) | PM_SOUND_AMP );
	powerOn(POWER_SOUND);

	readUserSettings();
	ledBlink(0);

	irqInit();
	// Start the RTC tracking IRQ
	initClockIRQ();
	fifoInit();
	touchInit();

	SetYtrigger(80);
	
	TIMER_DATA(2) = 0;
	TIMER_DATA(3) = 0;
	TIMER_CR(2) = TIMER_ENABLE;
	TIMER_CR(3) = TIMER_ENABLE | TIMER_CASCADE;

	memset(&PicoSound, 0, sizeof(PicoSound));
	PicoSound.sndRate = 15617;
	PicoSound.writeSound = writeSoundCB;

	fifoSetDatamsgHandler(FIFO_CHANNEL_PICO, picoFifoDatamsgHandler, NULL);

	installSystemFIFO();

	irqSet(IRQ_VCOUNT, VcountHandler);
	irqSet(IRQ_VBLANK, VblankHandler);

	irqEnable(IRQ_VBLANK | IRQ_VCOUNT);

	setPowerButtonCB(powerButtonCB);

	// Keep the ARM7 mostly idle
	while (!exitflag) {
		if ( 0 == (REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R))) {
			exitflag = true;
		}

		int diff = (frame_pos + PSND_FRAME_COUNT - frame_playback_pos) & (PSND_FRAME_COUNT - 1);
		if (diff > 12) {
			while (diff > 4) {
				// skip frames
				PicoDSMessagePsndFrame *frame = &frames[frame_playback_pos];

				for (int i = 0; i < frame->ym2612_entry_count; i++) {
					picoYm2612Write(frame->ym2612_entries[i]);
				}

				frame_playback_pos = (frame_playback_pos + 1) & (PSND_FRAME_COUNT - 1);
				diff = (frame_pos + PSND_FRAME_COUNT - frame_playback_pos) & (PSND_FRAME_COUNT - 1);
			}
		}

		if (diff > 0) {
			PicoDSMessagePsndFrame *frame = &frames[frame_playback_pos];

			int ticks = get_ticks();
			if (frame->scanline < 224)
				PsndStartFrame();
			for (int i = 0; i < frame->ym2612_entry_count; i++) {
				picoYm2612Write(frame->ym2612_entries[i]);
			}
			PsndGetSamples(frame->scanline);
			ticks = get_ticks() - ticks;
			a9dprintf("aframe, d%d, t%d", diff, ticks);

			frame_playback_pos = (frame_playback_pos + 1) & (PSND_FRAME_COUNT - 1);
			if (frame_playback_pos == frame_pos) {
				frame_playing = false;
			}
		} else {
			swiWaitForVBlank();
			last_sound_vbls = vbls;
		}
	}
	return 0;
}
