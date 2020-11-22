#define DEBUG 0
#define MAX_FRAMESKIP 4
#define DESIRED_FPS 60

#include <nds.h>
#include <nds/bios.h>
#include <nds/arm9/console.h>
#include <stdio.h>
#include <sys/unistd.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <fat.h>

#include "pico/pico_int.h"
#include "file.h"
#include "config.h"
#include "fifo.h"
#include "sound_comms.h"

#ifdef USE_ZLIB
#include "zlib.h"
#endif

#include <sys/stat.h>

static char fileName[256];

int choosingfile = 1;

static u32 frame_counter_local = 0;
static u32 frame_counter_console = 0;
#define FRAME_DISCREPANCY_COUNT 8
static u32 frame_discrepancy[FRAME_DISCREPANCY_COUNT];
static u32 frame_discrepancy_idx;
static u32 frame_fps;

static u16 conf_x = -1, conf_y = -1, conf_w = -1, conf_h = -1;
static u32 conf_x_scale, conf_y_scale, conf_x_step, conf_y_step;
static u8 conf_tick;
static u16 *displayed_screen_ptr = BG_GFX;
static u16 *screen_ptr = BG_GFX;

void screen_alphalerp(void) {
	u32 screen_ptr_val = ((u32) displayed_screen_ptr);
	u32 bgcnt = BG_BMP16_512x256 | BG_TILE_BASE((screen_ptr_val & 0x40000) ? 16 : 0);

	REG_BG2CNT = bgcnt;
	REG_BG3CNT = bgcnt;
}

void screen_swap_buffers(void) {
	u32 screen_ptr_val = ((u32) screen_ptr);
	displayed_screen_ptr = (u16*) (screen_ptr_val);
//  TODO: On DS, we're using VRAM banks C/D for storing the giant YM2612 table of doom 
//	screen_ptr = (u16*) (screen_ptr_val ^ 0x40000);
}

void screen_set_size(u16 x, u16 y, u16 width, u16 height) {
	if (conf_x != x || conf_y != y || conf_w != width || conf_h != height) {
		swiWaitForVBlank();

		conf_x = x;
		conf_y = y;
		conf_w = width;
		conf_h = height;

		conf_x_scale = divf32(width << 12, 256 << 12) >> 4;
		conf_y_scale = divf32(height << 12, 192 << 12) >> 4;
		conf_x_step = divf32(256 << 12, width << 12) >> 4;
		conf_y_step = divf32(192 << 12, height << 12) >> 4;

		REG_BLDCNT = BLEND_ALPHA | BLEND_SRC_BG2 | BLEND_DST_BG3;
		REG_BLDALPHA = 0x0808;

		REG_BG2X  = ((x) << 8) + (conf_x_step >> 1);
		REG_BG2Y  = (y) << 8;
		REG_BG2PA = conf_x_scale;
		REG_BG2PD = conf_y_scale;

		REG_BG3X  = (x) << 8;
		REG_BG3Y  = ((y) << 8) + (conf_y_step >> 1);
		REG_BG3PA = conf_x_scale;
		REG_BG3PD = conf_y_scale;

		screen_alphalerp();

		conf_tick = 0;
	}
}

void PrintRegion()
{
	iprintf("\n");
	switch(Pico.m.hardware)
	{
		case 0xe0:
			iprintf("Europe\n");
			break;
		case 0xa0:
			iprintf("USA\n");
			break;
		case 0x60:
			iprintf("Japan PAL\n");
			break;
		case 0x20:
			iprintf("Japan NTSC\n");
			break;
		default:
			iprintf("Unknown\n");
			break;
	}
}

void ConvertToGrayscale()
{
	int i,j;
	u8 b,g,r;
	for(i = conf_y; i < (conf_y + conf_h); i++) {
		for(j = conf_x; j < (conf_x + conf_w); j++) {
			// ABBBBBGGGGGRRRRR
			// ABBBBB
			// 011111 = 31
			u16 v = screen_ptr[(i*512)+j];
			b = (v>>10)&31;
			g = (v>>5)&31;
			r = (v)&31;
			screen_ptr[(i*512)+j] = ( ((b + (r * 3) + (g << 2)) >> 3) * 0x0421) | 0x8000;
		}
	}
}

static int SaveStateMenu()
{
	int position = 0;

	ConvertToGrayscale();
	consoleClear();
	iprintf("\x1b[1;10HLoad State");
	iprintf("\x1b[2;10HSave State");
	while(1) {
		if(!position) {
			iprintf("\x1b[1;7H-> ");
			iprintf("\x1b[2;7H   ");
		}
		else {
			iprintf("\x1b[1;7H   ");
			iprintf("\x1b[2;7H-> ");
		}
		scanKeys();
		if((keysDown() & KEY_DOWN) || (keysDown() & KEY_UP)) {
			position = !position;
		}
		if(keysDown() & KEY_B) {
			consoleClear();
			return 0;
		}
		if(keysDown() & KEY_A) {
			consoleClear();
			if(position) { // save state
				iprintf("Saving state...");
				// TODO saveLoadGame(0,0);
				iprintf("DONE!\n");
				return 0;
			}
			else { // load state
				iprintf("Loading state...");
				// TODO saveLoadGame(1,0);
				iprintf("DONE!");
				return 0;
			}
		}
	}
	return -1;
}

static int DoFrame()
{
	int pad=0;
	
	scanKeys();
	int keysPressed = keysHeld();
	int kd = keysDown();

	if (keysPressed & KEY_UP) pad|=1<<0;
	if (keysPressed & KEY_DOWN) pad|=1<<1;
	if (keysPressed & KEY_LEFT) pad|=1<<2;
	if (keysPressed & KEY_RIGHT) pad|=1<<3;
	if (keysPressed & KEY_B) pad|=1<<4;
	if (keysPressed & KEY_A) pad|=1<<5;
	if (keysPressed & KEY_Y) pad|=1<<6;
	if (keysPressed & KEY_START) pad|=1<<7;
	
	if (keysPressed & KEY_X) {
		SaveStateMenu();
	}

	if (kd & KEY_SELECT) {
		choosingfile = 2;
	}

	PicoIn.pad[0]=pad;

	PicoFrame();
	return 0;
}

#ifdef SW_SCAN_RENDERER
static int EmulateScanBG3(unsigned int scan)
{
	Pico.est.DrawLineDest = screen_ptr + (scan << 9);
	return 0;
}
#endif

static int DrawFrame()
{
#ifdef SW_SCAN_RENDERER
	PicoDrawSetOutFormat(PDF_RGB555, 0);
	PicoDrawSetCallbacks(EmulateScanBG3, NULL);
#endif
		
	DoFrame();

#ifdef SW_SCAN_RENDERER
	screen_swap_buffers();
	PicoScanBegin=NULL;
#endif
	
	return 0;
}


void EmulateFrame()
{
	if (choosingfile) {
		return;
	}

	int fc_console_at = frame_counter_console;
	int diff = fc_console_at - frame_counter_local;
	int need = diff;
	if(need <= 0) {
		return;
	}
	if (need>MAX_FRAMESKIP) need=MAX_FRAMESKIP; // Limit frame skipping

	for (int i=0;i<need-1;i++) {
		PicoIn.skipFrame = 1;
		DoFrame(); // Frame skip if needed
	}
	PicoIn.skipFrame = 0;
	frame_counter_local = fc_console_at;

	DrawFrame();
	frame_discrepancy[frame_discrepancy_idx++] = diff;
	if (frame_discrepancy_idx >= FRAME_DISCREPANCY_COUNT) {
		for (int i = 0; i < FRAME_DISCREPANCY_COUNT; i++) {
			frame_fps += divf32(60 << 12, frame_discrepancy[i] << 12);
		}
		frame_fps = divf32(frame_fps, FRAME_DISCREPANCY_COUNT << 12) >> 12;
		iprintf("\x1b[17;0HFPS: %ld   ", frame_fps);
		frame_discrepancy_idx = 0;
	}
	
	return;
}

void processvblank()
{
	screen_alphalerp();

	if(!choosingfile) {
		frame_counter_console++;
	}
}

int FileChoose()
{
	consoleClear();

	if (selectFile(fileName, sizeof(fileName))) {
		return 1;
	} else {
		return 0;
	}
}

int EmulateExit()
{
	if (FileChoose()) {
		// Save SRAM
		if(Pico.sv.changed) {
			// TODO saveLoadGame(0,1);
			Pico.sv.changed = 0;
		}

		PicoExit();
		return 1;
	}
	else {
		return 0;
	}
}

int EmulateInit()
{
	iprintf("\x1b[6;0H\x1b[0J");
	PicoIn.opt = POPT_EN_FM | POPT_EN_Z80 | POPT_EXT_FM;
	PicoInit();
	iprintf("Loading %s ... \n", fileName);

	frame_counter_local = 0;
	frame_counter_console = 0;
	frame_fps = 0;

	enum media_type_e media_type = PicoLoadMedia(fileName, NULL, NULL, NULL);
	switch (media_type) {
	case PM_BAD_DETECT:
		iprintf("Error: File is not a valid ROM.\n");
		return 0;
	case PM_ERROR:
		iprintf("Error: Could not load file.\n");
		return 0;
	default:
		iprintf("OK\n");
		choosingfile = 0;
		return 1;
	}
}

void emu_video_mode_change(int start_line, int line_count, int is_32cols) {
	if (is_32cols) {
		PicoIn.opt |= POPT_DIS_32C_BORDER;
		screen_set_size(0, start_line, 256, line_count);
	} else {
		PicoIn.opt &= ~POPT_DIS_32C_BORDER;
		screen_set_size(0, start_line, 320, line_count);
	}
}

void picoFifoDatamsgHandler(int num_bytes, void *userdata) {
	u8 buffer[64];
	fifoGetDatamsg(FIFO_CHANNEL_PICO, 64, buffer);
	u16 id = *((u16*)buffer);
	switch (id) {
	case FIFO_CMD_DBG_OUT: {
		PicoDSMessageDbgOut *msg = buffer;
		iprintf("%s\n", msg->debug_data);
	} break;
	}
}

static short sound_buffer[1600];

int main(void)
{
	static bool fatPresent = false;
	fileName[0] = 0;

	vramSetPrimaryBanks(VRAM_A_MAIN_BG, VRAM_B_MAIN_BG, VRAM_C_ARM7, VRAM_D_ARM7);
	vramSetBankH(VRAM_H_SUB_BG);
	vramSetBankI(VRAM_I_SUB_BG_0x06208000);

	PrintConsole *defConsole = consoleGetDefault();
	consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, defConsole->mapBase, defConsole->gfxBase, false, true);

	consoleDemoInit();
	
	iprintf("\nTrying to init FAT...\n");
	if(fatInitDefault()) {
		iprintf("\x1b[2J");

		// Wait two VBlanks as instructed in the FAT docs
		swiWaitForVBlank();
		swiWaitForVBlank();
		
		chdir("/");
		fatPresent = true;
	}
	else {
		iprintf("FAT init failed.\n");
		swiWaitForVBlank();
		fatPresent = false;
	}

	videoSetMode(MODE_5_2D | DISPLAY_BG2_ACTIVE | DISPLAY_BG3_ACTIVE);
	videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE);

	screen_set_size(0, 0, 320, 240);

	if (fatPresent) {
		FileChoose();
	}

	irqEnable(IRQ_VBLANK);
	irqSet(IRQ_VBLANK, processvblank);

	fifoSetDatamsgHandler(FIFO_CHANNEL_PICO, picoFifoDatamsgHandler, NULL);
	ndsFifoSendNdsInit(sound_buffer, sizeof(sound_buffer));

	EmulateInit();

	while(1) {
		if(choosingfile) {
			ConvertToGrayscale();
			if(EmulateExit()) {
				EmulateInit();
			}
			else {
				consoleClear();
			}
			choosingfile = 0;
		}

		EmulateFrame();
		// Save SRAM
		if(Pico.sv.changed) {
			// iprintf("\x1b[17:0HSaving SRAM");
			// TODO saveLoadGame(0,1);
			Pico.sv.changed = 0;
		}
	}
	return 0;
}
