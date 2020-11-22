/*
 * PicoDrive
 * (c) Copyright Dave, 2004
 * (C) notaz, 2006-2009
 *
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */

#include <nds.h>
#include <stdlib.h>
#include <string.h>
#include "ym2612.h"
#include "sn76496.h"
#include "mix.h"
#include "sound_comms.h"
#include "sound_int.h"

PicoDSSound PicoSound;

// master int buffer to mix to
static int PsndBuffer[2*(44100+100)/50];

// dac, psg
static unsigned short dac_info[312+4]; // pos in sample buffer

// sn76496
extern int *sn76496_regs;


static void dac_recalculate(void)
{
  int lines = PicoSound.pal ? 313 : 262;
  int mid = PicoSound.pal ? 68 : 93;
  int i, dac_cnt, pos, len;

  if (PicoSound.len <= lines)
  {
    // shrinking algo
    dac_cnt = -PicoSound.len;
    len=1; pos=0;
    dac_info[225] = 1;

    for(i=226; i != 225; i++)
    {
      if (i >= lines) i = 0;
      if(dac_cnt < 0) {
        pos++;
        dac_cnt += lines;
      }
      dac_cnt -= PicoSound.len;
      dac_info[i] = pos;
    }
  }
  else
  {
    // stretching
    dac_cnt = PicoSound.len;
    pos=0;
    for(i = 225; i != 224; i++)
    {
      if (i >= lines) i = 0;
      len=0;
      while(dac_cnt >= 0) {
        dac_cnt -= lines;
        len++;
      }
      if (i == mid) // midpoint
        while(pos+len < PicoSound.len/2) {
          dac_cnt -= lines;
          len++;
        }
      dac_cnt += PicoSound.len;
      pos += len;
      dac_info[i] = pos;
    }
  }
  for (i = lines; i < sizeof(dac_info) / sizeof(dac_info[0]); i++)
    dac_info[i] = dac_info[0];
}


PICO_INTERNAL void PsndReset(void)
{
  // PsndRerate calls YM2612Init, which also resets
  PsndRerate(0);
  // timers_reset(); TODO: Is this necessary?
}


// to be called after changing sound rate or chips
void PsndRerate(int preserve_state)
{
  void *state = NULL;
  // TODO NDS: Is this fine? We always tick at 60FPS...
  int target_fps = PicoSound.pal ? 50 : 60;

  /* if (preserve_state) {
    state = malloc(0x204);
    if (state == NULL) return;
    ym2612_pack_state();
    memcpy(state, YM2612GetRegs(), 0x204);
  } */

  YM2612Init(PicoSound.pal ? OSC_PAL/7 : OSC_NTSC/7, PicoSound.sndRate);
  
  /* if (preserve_state) {
    // feed it back it's own registers, just like after loading state
    memcpy(YM2612GetRegs(), state, 0x204);
    ym2612_unpack_state();
  } */

  // if (preserve_state) memcpy(state, sn76496_regs, 28*4); // remember old state
  SN76496_init(PicoSound.pal ? OSC_PAL/15 : OSC_NTSC/15, PicoSound.sndRate);
  // if (preserve_state) memcpy(sn76496_regs, state, 28*4); // restore old state

  if (state)
    free(state);

  // calculate PicoSound.len
  PicoSound.len = PicoSound.sndRate / target_fps;
  PicoSound.len_e_add = ((PicoSound.sndRate - PicoSound.len * target_fps) << 16) / target_fps;
  PicoSound.len_e_cnt = 0;

  // recalculate dac info
  dac_recalculate();

  // clear all buffers
  memset(PsndBuffer, 0, sizeof(PsndBuffer));
  if (PicoSound.sndOut)
    PsndClear();

  // NDS: NO_PICO
  // if (PicoIn.AHW & PAHW_PICO)
  //  PicoReratePico();
}


PICO_INTERNAL void PsndStartFrame(void)
{
  // compensate for float part of PicoSound.len
  PicoSound.len_use = PicoSound.len;
  PicoSound.len_e_cnt += PicoSound.len_e_add;
  if (PicoSound.len_e_cnt >= 0x10000) {
    PicoSound.len_e_cnt -= 0x10000;
    PicoSound.len_use++;
  }

  PicoSound.dac_line = PicoSound.psg_line = 0;
  PicoSound.status &= ~1;
  dac_info[224] = PicoSound.len_use;
}

PICO_INTERNAL void PsndDoDAC(int line_to)
{
  int pos, pos1, len;
  int dout = ym2612.dacout;
  int line_from = PicoSound.dac_line;

  if (line_to >= 313)
    line_to = 312;

  pos  = dac_info[line_from];
  pos1 = dac_info[line_to + 1];
  len = pos1 - pos;
  if (len <= 0)
    return;

  PicoSound.dac_line = line_to + 1;

  if (!PicoSound.sndOut)
    return;

  short *d = PicoSound.sndOut + pos;
  for (; len > 0; len--, d++)  *d += dout;
}

PICO_INTERNAL void PsndDoPSG(int line_to)
{
  int line_from = PicoSound.psg_line;
  int pos, pos1, len;

  if (line_to >= 313)
    line_to = 312;

  pos  = dac_info[line_from];
  pos1 = dac_info[line_to + 1];
  len = pos1 - pos;
  //elprintf(EL_STATUS, "%3d %3d %3d %3d %3d",
  //  pos, pos1, len, line_from, line_to);
  if (len <= 0)
    return;

  PicoSound.psg_line = line_to + 1;

  if (!PicoSound.sndOut || !(PicoSound.opt & POPT_EN_PSG))
    return;

  SN76496Update(PicoSound.sndOut + pos, len, 0);
}

// NDS: CDDA code removed

PICO_INTERNAL void PsndClear(void)
{
  int len = PicoSound.len;
  if (PicoSound.len_e_add) len++;
  short *out = PicoSound.sndOut;
  memset(out, 0, len * 2);
}


static int PsndRender(int offset, int length)
{
  int *buf32 = PsndBuffer+offset;

  // NDS: Pico code removed here

  if (PicoSound.opt & POPT_EN_FM) {
    YM2612UpdateOne(buf32, length, 0, 1);
  } else {
    memset(buf32, 0, length * 4);
  }

  // NDS: CD, 32X code removed here

  // convert + limit to normal 16bit output
  mix_32_to_16_mono(PicoSound.sndOut+offset, buf32, length);

  return length;
}

// to be called on 224 or line_sample scanlines only
PICO_INTERNAL void PsndGetSamples(int y)
{
  static int curr_pos = 0;

  if (ym2612.dacen && PicoSound.dac_line < y)
    PsndDoDAC(y - 1);
  // PsndDoPSG(y - 1);

  if (y == 224)
  {
    if (PicoSound.status & 2)
         curr_pos += PsndRender(curr_pos, PicoSound.len-PicoSound.len/2);
    else curr_pos  = PsndRender(0, PicoSound.len_use);
    if (PicoSound.status & 1)
         PicoSound.status |=  2;
    else PicoSound.status &= ~2;
    if (PicoSound.writeSound)
      PicoSound.writeSound(curr_pos * 2);
    // clear sound buffer
    PsndClear();
    PicoSound.dac_line = 224;
    dac_info[224] = 0;
  }
  else if (PicoSound.status & 3) {
    PicoSound.status |=  2;
    PicoSound.status &= ~1;
    curr_pos = PsndRender(0, PicoSound.len/2);
  }
}

PICO_INTERNAL void PsndGetSamplesMS(void)
{
  int length = PicoSound.len_use;

  PsndDoPSG(223);

  if (PicoSound.writeSound != NULL)
    PicoSound.writeSound(length * 2);
  PsndClear();

  dac_info[224] = 0;
}

// vim:shiftwidth=2:ts=2:expandtab
