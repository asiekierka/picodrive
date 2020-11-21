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

#ifdef USE_ZLIB
#include "zlib.h"
#endif

// #include "fat/gba_nds_fat.h"
// #include "fat/disc_io.h"

#ifdef ARM9_SOUND
TransferSoundData picosound;
#endif

#include <sys/stat.h>
#define DEVICE_TYPE_SCSD 0x44534353
#define DEVICE_TYPE_SCCF 0x46434353
#define OPERA_UNLOCK *((vuint16*)0x08240000)
#define OPERA_RAM ((vuint16*)0x09000000)
#define SC_UNLOCK ((vuint16*)0x09fffffe)

static unsigned char *RomData=NULL;
static unsigned int RomSize=0;

static bool UsingAppendedRom = false;
// separate from UsingAppendedRom, we need to track if we're using
// GBAROM as writable memory
static bool UsingGBAROM = false;

pm_file *romfile = NULL;

int choosingfile = 1;

u32 dsFrameCount = 0;
u32 pdFrameCount = 0;
u32 FPS = 0;

static u16 conf_x = -1, conf_y = -1, conf_w = -1, conf_h = -1;
static u32 conf_x_scale, conf_y_scale, conf_x_step, conf_y_step;
static u8 conf_tick;

void screen_alphalerp(void) {

}

void set_screen_size(u16 x, u16 y, u16 width, u16 height) {
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

		REG_BG2CNT = BG_BMP16_512x256;
		REG_BG3CNT = BG_BMP16_512x256;

		REG_BG2X  = ((x) << 8) + (conf_x_step >> 1);
		REG_BG2Y  = (y) << 8;
		REG_BG2PA = conf_x_scale;
		REG_BG2PD = conf_y_scale;

		REG_BG3X  = (x) << 8;
		REG_BG3Y  = ((y) << 8) + (conf_y_step >> 1);
		REG_BG3PA = conf_x_scale;
		REG_BG3PD = conf_y_scale;

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

#ifdef ARM9_SOUND
void InitSound()
{
	if(PsndOut)
		free(PsndOut);

	PsndOut = (short *)malloc(PsndLen);
	setGenericSound(
		PsndRate,
		127,
		64,
		0
	);
	/*
	picosound = (TransferSoundData) {
		PsndOut,	// sample address
		PsndLen<<2,	// sample length
		PsndRate,	// sample rate
		127,		// volume
		64,			// panning
		1			// format
	};
	*/
}
#endif

#ifdef USE_ZLIB
#define CHUNK 512

int compressSaveState(void)
{
	FILE *source = fopen("testsave.nc", "rb");
	FILE *dest = fopen("testsave.z", "wb");
	int ret, flush;
	int totalbytes = 0;
	int maxchunk;
	unsigned have;
	z_stream strm;
	// char in[CHUNK];
	// char out[CHUNK];
	char *in = malloc(CHUNK*sizeof(char));
	char *out = malloc(CHUNK*sizeof(char));

	struct Cyclone *cpustate = &PicoCpu;
	struct Pico *emustate = &Pico;

	// original routine
	/*
	PmovFile = fopen(saveFname, "wb");
	res = fwrite(cpustate,1,sizeof(struct Cyclone),(FILE *) PmovFile);
	res = (res != sizeof(PicoCpu) ? -1 : 0);
	res = fwrite(emustate,1,sizeof(struct Pico),(FILE *) PmovFile);
	res = (res != sizeof(Pico) ? -1 : 0);
	fclose((FILE *) PmovFile);
	*/
	
	// compressed routine
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	ret = deflateInit(&strm, Z_BEST_COMPRESSION);
	if(ret != Z_OK) {
		return ret;
	}

// original code for compressing state without using an intermediate file
#ifdef NOT_USED
	// compress all of the save state
	do {
		if((totalbytes + CHUNK) <= sizeof(struct Cyclone)) {
			maxchunk = CHUNK;
		}
		else {
			maxchunk = sizeof(struct Cyclone)-totalbytes;
		}
		totalbytes += maxchunk;
		memcpy(in,cpustate, maxchunk);
		strm.avail_in = maxchunk;
		strm.next_in = (unsigned char *) in;
		do {
			strm.avail_out = CHUNK;
			strm.next_out = (unsigned char *) out;
			ret = deflate(&strm, Z_NO_FLUSH);    /* no bad return value */
			have = CHUNK - strm.avail_out;
			if(fwrite(out, 1, have, dest) != have) {
				(void)deflateEnd(&strm);
				return Z_ERRNO;
			}
		} while (strm.avail_out == 0);
	} while (totalbytes < sizeof(struct Cyclone));

	totalbytes = 0;

	do {
		if((totalbytes + CHUNK) <= sizeof(struct Pico)) {
			maxchunk = CHUNK;
			flush = Z_NO_FLUSH;
		}
		else {
			maxchunk = sizeof(struct Pico)-totalbytes;
			flush = Z_FINISH;
		}
		totalbytes += maxchunk;
		memcpy(in, emustate, maxchunk);
		strm.avail_in = maxchunk;
		strm.next_in = (unsigned char *) in;
		do {
			strm.avail_out = CHUNK;
			strm.next_out = (unsigned char *) out;
			ret = deflate(&strm, flush);    /* no bad return value */
			have = CHUNK - strm.avail_out;
			if (fwrite(out, 1, have, dest) != have) {
				(void)deflateEnd(&strm);
				return Z_ERRNO;
			}
		} while (strm.avail_out == 0);
	} while (totalbytes < sizeof(struct Pico));
#endif

	/* compress until end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK, source);

		flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = (unsigned char *) in;

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = (unsigned char *) out;
            ret = deflate(&strm, flush);    /* no bad return value */
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have) {
                (void)deflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);

        /* done when last data in file processed */
    } while (flush != Z_FINISH);
	(void)deflateEnd(&strm);
	free(in);
	free(out);
	fclose(source);
	fclose(dest);
	return Z_OK;
}

int decompressSaveState(void)
{
	FILE *source = fopen("testsave.z", "rb");
	FILE *dest = fopen("testsave.uz", "wb");
	int ret;
	unsigned have;
	z_stream strm;
	// char in[CHUNK];
	// char out[CHUNK];
	char *in = malloc(CHUNK*sizeof(char));
	char *out = malloc(CHUNK*sizeof(char));

	// compressed routine
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit(&strm);
	if(ret != Z_OK) {
		return ret;
	}

	/* decompress until deflate stream ends or end of file */
	do {
		strm.avail_in = fread(in, 1, CHUNK, source);
		if (strm.avail_in == 0)
			break;
		strm.next_in = (unsigned char *) in;

		/* run inflate() on input until output buffer not full */
		do {
			strm.avail_out = CHUNK;
			strm.next_out = (unsigned char *) out;
			ret = inflate(&strm, Z_NO_FLUSH);
			switch (ret) {
				case Z_NEED_DICT:
					ret = Z_DATA_ERROR;     /* and fall through */
				case Z_DATA_ERROR:
				case Z_MEM_ERROR:
					(void)inflateEnd(&strm);
					return ret;
			}
			have = CHUNK - strm.avail_out;
			if (fwrite(out, 1, have, dest) != have ) {
				(void)inflateEnd(&strm);
				return Z_ERRNO;
            }
		} while (strm.avail_out == 0);

		/* done when inflate() says it's done */
	} while (ret != Z_STREAM_END);

	/* clean up and return */
	(void)inflateEnd(&strm);
	free(in);
	free(out);
	fclose(source);
	fclose(dest);

	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}
#endif // USE_ZLIB

int saveLoadGame(int load, int sram)
{
	int i;
	int res = 0;
	FILE *PmovFile;
	if(!UsingAppendedRom && fileName[0] == 0) return -1;

	// make save filename
	char saveFname[256];
	if(!UsingAppendedRom) {
		strcpy(saveFname, fileName);
		if(saveFname[strlen(saveFname)-4] == '.') saveFname[strlen(saveFname)-4] = 0;
		strcat(saveFname, sram ? ".srm" : ".pds");
		// iprintf("\x1b[0;0HSavename: %s\n",saveFname);
	}

	if(sram) {
		if(load) {
			if(UsingAppendedRom) {
				for(i = 0; i < (Pico.sv.end - Pico.sv.start + 1); i++) {
					((uint8*)(Pico.sv.data))[i] = SRAM[i];
				}
				// memcpy(Pico.sv.data,SRAM,Pico.sv.end - Pico.sv.start + 1);
			}
			else {
				PmovFile = fopen(saveFname, "rb");
				if(!PmovFile) return -1;
				fread(Pico.sv.data, 1, Pico.sv.end - Pico.sv.start + 1, (FILE *) PmovFile);
				fclose((FILE *) PmovFile);
			}
		} else {
			// sram save needs some special processing
			int sram_size = Pico.sv.end-Pico.sv.start+1;
			// see if we have anything to save
			for(; sram_size > 0; sram_size--)
				if(Pico.sv.data[sram_size-1]) break;

			if(sram_size) {
				if(UsingAppendedRom) {
					for(i = 0; i < (Pico.sv.end - Pico.sv.start + 1); i++) {
						SRAM[i] = ((uint8*)(Pico.sv.data))[i];
					}
					// memcpy(SRAM,Pico.sv.data,sram_size);
				}
				else {
					PmovFile = fopen(saveFname, "wb");
					res = fwrite(Pico.sv.data, 1, sram_size, (FILE *) PmovFile);
					res = (res != sram_size) ? -1 : 0;
					fclose((FILE *) PmovFile);
				}
			}
		}
		PmovFile = 0;
		return res;
	}
	else { // We're dealing with a save state
		struct Cyclone *cpustate = &PicoCpuCM68k;
		struct Pico *emustate = &Pico;
		if(load) { // load save state
			PmovFile = fopen(saveFname, "rb");
			if(!PmovFile) return -1;
			fread(cpustate, 1, sizeof(struct Cyclone), (FILE *) PmovFile);
			fread(emustate, 1, sizeof(struct Pico), (FILE *) PmovFile);
			fclose((FILE *) PmovFile);
		}
		else { // write save state
			// Write out PicoCpu and Pico
			PmovFile = fopen(saveFname, "wb");
			res = fwrite(cpustate,1,sizeof(struct Cyclone),(FILE *) PmovFile);
			res = (res != sizeof(PicoCpuCM68k) ? -1 : 0);
			res = fwrite(emustate,1,sizeof(struct Pico),(FILE *) PmovFile);
			res = (res != sizeof(Pico) ? -1 : 0);
			fclose((FILE *) PmovFile);
		}
	}
	
	return 0;
}

int32 cx=32,cy=16;

void ConvertToGrayscale()
{
	int i,j;
	u8 b,g,r;
	for(i = conf_y; i < (conf_y + conf_h); i++) {
		for(j = conf_x; j < (conf_x + conf_w); j++) {
			// ABBBBBGGGGGRRRRR
			// ABBBBB
			// 011111 = 31
			u16 v = BG_GFX[(i*512)+j];
			b = (v>>10)&31;
			g = (v>>5)&31;
			r = (v)&31;
			BG_GFX[(i*512)+j] = ( ((b + (r * 3) + (g << 2)) >> 3) * 0x0421) | 0x8000;
		}
	}
}

static int SaveStateMenu()
{
	int position = 0;
	
	// TODO: Compressed save states for appended ROM mode
	if(UsingAppendedRom) {
		return 0;
	}

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
				saveLoadGame(0,0);
				iprintf("DONE!\n");
				return 0;
			}
			else { // load state
				iprintf("Loading state...");
				saveLoadGame(1,0);
				iprintf("DONE!");
				return 0;
			}
		}
	}
	return -1;
}

static int DoFrame()
{
	if(DEBUG)
		iprintf("HIT DOFRAME\n");
	int pad=0;
	// char map[8]={0,1,2,3,5,6,4,7}; // u/d/l/r/b/c/a/start
	
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

#ifdef ARM9_SOUND
	if(PsndOut)
		playGenericSound(PsndOut,PsndLen);
#endif

	// FramesDone++;
	// pdFrameCount++;
	return 0;
}

#ifdef SW_SCAN_RENDERER
static int EmulateScanBG3(unsigned int scan)
{
	Pico.est.DrawLineDest = BG_GFX + (scan << 9);
	return 0;
}
#endif

static int DrawFrame()
{
	if(DEBUG)
		iprintf("HIT DRAWFRAME\n");

	// Now final frame is drawn:
#ifdef SW_FRAME_RENDERER
	framebuff = realbuff;
#endif

#ifdef SW_SCAN_RENDERER
	PicoDrawSetOutFormat(PDF_RGB555, 0);
	PicoDrawSetCallbacks(EmulateScanBG3, NULL);
#endif
		
	DoFrame();

#ifdef SW_FRAME_RENDERER
	unsigned int scan;
	
	for(scan = 223+8; scan > 8; scan--) {
		dmaCopy(realbuff+(328*scan)+8,BG_GFX+(512*(scan-8)),320*2);
	}
#endif
		
#ifdef SW_SCAN_RENDERER
	PicoScanBegin=NULL;
#endif
	
	pdFrameCount++;
	return 0;
}


void EmulateFrame()
{
	if(DEBUG)
		iprintf("HIT EMULATEFRAME\n");

	int i=0,need=0;
	// int time=0,frame=0;

	if (choosingfile || RomData==NULL) {
		//iprintf("YOUR ROM DATA IS NULL THAT IS NOT GOOD\n");
		// swiDelay(100000);
		return;
	}

	need = DESIRED_FPS - FPS;
	// iprintf("\x1b[19;0HFPS: %d     \n",FPS);
	// iprintf("Need: %d    ",need);

	if(need <= 0) {
		return;
	}

	if (need>MAX_FRAMESKIP) need=MAX_FRAMESKIP; // Limit frame skipping

	for (i=0;i<need-1;i++) {
		PicoIn.skipFrame = 1;
		DoFrame(); // Frame skip if needed
		// if(PsndOut)
		//		playSound(&picosound);
	}
	PicoIn.skipFrame = 0;
	
	DrawFrame();
	
	if(DEBUG)
		iprintf("LEAVING EMULATEFRAME\n");
	
	return;
}

void processtimer()
{
	// CurrentTimeInms+=10;
}

void processvblank()
{
	screen_alphalerp();

	if(!choosingfile) {
		dsFrameCount++;
		// FPS = pdFrameCount;
		// FPS = ((float)pdFrameCount / (float)dsFrameCount)*60;
		// FPS = (60.0/(float)dsFrameCount)*pdFrameCount;
		if(dsFrameCount > 59) {
			FPS = pdFrameCount;
			pdFrameCount = dsFrameCount = 0;
		}
		else if((60 % dsFrameCount) == 0) {
			FPS = (60/dsFrameCount)*pdFrameCount;
		}
		//  EmulateFrame();
	}
}

/*
void EmulateSound() {
	PsndRate=WaveRate;
	PsndLen=WaveLen;
	PsndOut=WaveDest;

	// Clear the FIFO queue
	REG_IPC_FIFO_CR = IPC_FIFO_ENABLE | IPC_FIFO_SEND_CLEAR;
	// Send the FIFO msg
	REG_IPC_FIFO_TX = 1;

	DrawFrame();
	// Convert to signed:
	//  for (p=0;p<PsndLen<<1;p++) PsndOut[p]+=0x8000;

	PsndRate=PsndLen=0;
	PsndOut=NULL;
}
*/

void on_irq()
{
	if(REG_IF & IRQ_VBLANK) {
		// DrawFrame();
		if(choosingfile) {
			// if(choosingfile == 1)
			//
		}
		else {
			//iprintf("I AM ABOUT TO CALL EMULATEFRAME THIS IS SO EXCITING\n");
			// EmulateFrame();
			// EmulateSound();
			// DrawFrame();
		}
		// Tell the DS we handled the VBLANK interrupt
		REG_IE |= IRQ_VBLANK;
		REG_IF |= IRQ_VBLANK;
	}
	else if(REG_IF & IRQ_TIMER0) {
		processtimer();
		REG_IE |= IRQ_TIMER0;
	}
	else {
		// Ignore all other interrupts
		REG_IF = REG_IF;
	}
}

/*
void LidHandler() {
		if(IPC->buttons ==  0x00FF) {
			swiChangeSoundBias(0,0x400);
			powerOFF(POWER_ALL);
			// REG_IE=IRQ_PM;
		}
		else {
			swiChangeSoundBias(1,0x400);
			powerON(POWER_ALL);
		}
}
*/

void InitInterruptHandler()
{
	irqInit();
	irqEnable(IRQ_VBLANK);
	irqSet(IRQ_VBLANK, processvblank);
	// irqSet(IRQ_LID, LidHandler);
	// irqEnable(IRQ_LID);
	// Setup a 100Hz = 10ms timer
	/*
	TIMER0_DATA = TIMER_FREQ_1024(100);
	TIMER0_CR = TIMER_ENABLE | TIMER_DIV_1024 | TIMER_IRQ_REQ;

	irqEnable(IRQ_TIMER0);
	irqSet(IRQ_TIMER0, processtimer);
	*/

	/*REG_IME = 0;
	IRQ_HANDLER = on_irq;
	REG_IE = IRQ_VBLANK | IRQ_TIMER0;
	REG_IF = ~0;
	DISP_SR = DISP_VBLANK_IRQ;
	REG_IME = 1;
	*/
}


int FileChoose()
{
	consoleClear();

	romfile = loadFile();

	if(romfile == NULL) {
		return 0; // we didn't get a file
	}
	else {
		return 1; // we got a file
	}
}

int EmulateExit()
{
	if(!UsingAppendedRom && FileChoose()) {
		// Save SRAM
		if(Pico.sv.changed) {
			saveLoadGame(0,1);
			Pico.sv.changed = 0;
		}

		// Remove cartridge
		PicoCartInsert(NULL,0,NULL);
		if (RomData && !UsingAppendedRom && !UsingGBAROM) {
			free(RomData); RomData=NULL; RomSize=0;
		}
		else if(RomData && UsingGBAROM) {
			RomData=NULL; RomSize=0;
		}

		PicoExit();
		return 1;
	}
	else {
		return 0;
	}
}

/* void LoadROMToMemory(uint16* location, int size)
{
	size=(size+3)&~3; // Round up to a multiple of 4
	
	fseek(romfile,0,SEEK_SET);
	fread(location,1,size,romfile);
	fclose(romfile);
	
	// Check for SMD:
	if ((size&0x3fff)==0x200) {
		// TODO: SMD support
		DecodeSmd((unsigned char *)location,size); size-=0x200;
	} // Decode and byteswap SMD
	else Byteswap(location,location,size); // Just byteswap

	RomData = (unsigned char *)location;
	RomSize = size;
} */

int EmulateInit()
{
	int i;
	PicoInit();

//	romfile=pm_open("/SONIC2_W.68K");
//	romfile=pm_open("/SHADOW.BIN");

	if(UsingAppendedRom) {
		PicoCartInsert(RomData,RomSize,NULL);
#ifdef ARM9_SOUND
		InitSound();
#endif
		// Load SRAM
		saveLoadGame(1,1);
	}
	else {	
		if(romfile != NULL) {
			i = pm_seek(romfile,0,SEEK_END);
			pm_seek(romfile,0,SEEK_SET);
			// iprintf("ftell: %i\n",i);
#ifdef USE_EXTRA_RAM
			/* if(i >= 3200000) {
				struct stat st;
				stat("/",&st);
				if((st.st_dev == DEVICE_TYPE_SCSD) || (st.st_dev == DEVICE_TYPE_SCCF)) { // cart is SCSD/SCCF
					iprintf("Using SuperCard RAM\n");

					UsingGBAROM = true;

					// Unlock SDRAM
					*SC_UNLOCK = 0xa55a;
					*SC_UNLOCK = 0xa55a;
					*SC_UNLOCK = 0x0007;
					*SC_UNLOCK = 0x0007;
										
					LoadROMToMemory(GBAROM,i);
				}
				else { // check for Opera RAM expansion
					OPERA_UNLOCK = 0x0001;
					DC_FlushAll( );
					*OPERA_RAM = 0xF00D;
					if(*OPERA_RAM == 0xF00D) { // we successfully wrote into OPERA_RAM
						iprintf("Using Opera RAM Expansion\n");
						
						UsingGBAROM = true;
						
						LoadROMToMemory((uint16*)OPERA_RAM,i);
					}
				}
			}
			else { */
			// TODO ^
#endif
				UsingGBAROM = false;
				PicoCartLoad(romfile,&RomData,&RomSize,0);
#ifdef USE_EXTRA_RAM
			}
#endif
			pm_close(romfile);
			iprintf("Loaded.\n");

			PicoCartInsert(RomData,RomSize,NULL);
#ifdef ARM9_SOUND
			InitSound();
#endif
		}
		else {
			iprintf("Error opening file");
		}

		// Load SRAM
		saveLoadGame(1,1);
	}
	
	iprintf("ROM Size: %d\n",RomSize);
	iprintf("ROM Header Info:\n");
	for(i = 0; i < 128; i+=2) {
		if(!(RomData[0x100+i] == ' ' && RomData[0x100+i+1] == ' ' && RomData[0x100+i+2] == ' ')) {
			iprintf("%c",RomData[0x100+i+1]);
			iprintf("%c",RomData[0x100+i]);
		}
	}
	
	// iprintf("\n%#x\n",Pico.m.hardware);
	// PrintRegion();
	
	// iprintf("\x1b[10;0H");
	iprintf("\n\t\tPicoDriveDS ");
	// iprintf(VERSION_NO);
	iprintf("\n");

	choosingfile = 0;

	return 0;
}

/* void FindAppendedRom(void)
{
	iprintf("Appended ROM check...");
	
	sysSetBusOwners(BUS_OWNER_ARM9,BUS_OWNER_ARM9);

	unsigned char *rom=NULL; int size=0;
	
	char *rompointer = (char *) GBAROM;
	char *genheader;
	bool foundrom = false;
	bool smdformat = false;

	while (!foundrom && (rompointer != ((char *)(GBAROM+0x02000000)))) {
		genheader = rompointer + 0x100; // Genesis ROM header is 0x100 in to the ROM
		// check for "SEGA " at genheader location (without including "SEGA " in our compiled binary)
		if( (*genheader == 'S') && (*(genheader+1) == 'E') && 
			(*(genheader+2) == 'G') && (*(genheader+3) == 'A') && 
			(*(genheader+4) == ' ') ) { // we have a match
			iprintf("FOUND ROM!\n");
			smdformat = false;
			foundrom = true;
		}
		// SMD format ROMs should have 0xaa and 0xbb @ 0x08 and 0x09
		else if( (*(rompointer+0x08) == 0xaa) && (*(rompointer+0x09) == 0xbb) ) { // check for SMD format ROM
			iprintf("FOUND SMD!\n");
			smdformat = true;
			foundrom = true;
		}
		else {
			rompointer += 256;
		}
	}
	if(foundrom) {
		if(smdformat) {
			// best guess is that size for SMD ROM is at 0x2d1
			genheader = rompointer + 0x2d1;
		}
		else {
			// End/size of normal ROM should be a uint at 0x1a4
			genheader = rompointer + 0x1a4;
		}
		
		size = (*(genheader) << 24) | (*(genheader+1) << 16) | (*(genheader+2) << 8) | (*(genheader+3));
		size=(size+3)&~3; // Round up to a multiple of 4
		if(smdformat) {
			size += 0x200; // add SMD header to size
		}
		
		// This if check is kind of pointless, how many SC users
		// are going to use appended ROMs if FAT works?
		// Might be useful for some other card where we can detect
		// insertion and use GBAROM but not FAT.
		if(0) {
			iprintf("Supercard detected.\n");
			rom=(unsigned char *)rompointer;
		}
		else {
			//Allocate space for the rom plus padding
			rom=(unsigned char *)malloc(size+4);

			memcpy(rom,rompointer,size);
		}
		
		// Check for SMD:
		if(smdformat) {
			DecodeSmd(rom,size); // decode and byteswap SMD
			size -= 0x200;
		}
		else {
			// Byteswap the ROM
			Byteswap(rom,size);
		}

		RomData = rom;
		RomSize = size;	
		
		UsingAppendedRom = true;
	}
	else {
		iprintf("ROM NOT FOUND!\n");
		RomData = NULL;
		UsingAppendedRom = false;
	}
} */

void emu_video_mode_change(int start_line, int line_count, int is_32cols) {
	if (is_32cols) {
		PicoIn.opt |= POPT_DIS_32C_BORDER;
		set_screen_size(0, start_line, 256, line_count);
	} else {
		PicoIn.opt &= ~POPT_DIS_32C_BORDER;
		set_screen_size(0, start_line, 320, line_count);
	}
}

int main(void)
{
	static bool fatPresent = false;
	
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
		fatPresent = false;
	}

	// videoSetMode(MODE_FB0);
	// vramSetBankA(VRAM_A_LCD);

	videoSetMode(MODE_5_2D | DISPLAY_BG2_ACTIVE | DISPLAY_BG3_ACTIVE);

	// Set up the sub screen
	videoSetModeSub(MODE_5_2D | DISPLAY_BG0_ACTIVE);
	// vramSetBankA(VRAM_A_MAIN_BG);
	// vramSetBankC(VRAM_C_SUB_BG);

	vramSetPrimaryBanks(VRAM_A_MAIN_BG, VRAM_B_MAIN_BG, VRAM_C_SUB_BG , VRAM_D_LCD);

	for (u16 val = 0; val < 256; val++) {
		BG_PALETTE[val] = RGB15(val&31,val&31,val&31);
	}

	set_screen_size(0, 0, 320, 240);

	if (fatPresent) {
		FileChoose();
	}

	// lcdSwap();

	// SoundInit();

	InitInterruptHandler();

	// iprintf("About to call InitFiles()...\n");

#ifdef ARM9_SOUND
	PsndRate = 11025;
#endif

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
			saveLoadGame(0,1);
			Pico.sv.changed = 0;
		}
		// dmaCopy(BG_GFX,BG_GFX_SUB,512*256*2);
		// iprintf("\x1b[17;0H                        ");

		iprintf("\x1b[17;0HFPS: %d   ",FPS);

		/*
		swiWaitForVBlank();
		// LastSecond = (IPC->curtime)[7];
		*/
	}
	return 0;
}
