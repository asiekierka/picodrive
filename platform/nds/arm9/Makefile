#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules

#---------------------------------------------------------------------------------
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
# DATA is a list of directories containing binary files
# all directories are relative to this makefile
#---------------------------------------------------------------------------------
BUILD		:=	build
SOURCE_DIRS	:= source ../../../pico ../../../pico/sound ../../../pico/carthw \
	../../../cpu/cyclone ../../../cpu/cyclone/tools ../../../cpu/DrZ80
SOURCES		:= \
	source/file.c \
	source/main.c \
	source/pico_nds.c \
	../../../pico/pico.c ../../../pico/cart.c ../../../pico/memory.c \
	../../../pico/state.c ../../../pico/sek.c ../../../pico/z80if.c \
	../../../pico/videoport.c ../../../pico/draw2.c ../../../pico/draw.c \
	../../../pico/mode4.c ../../../pico/misc.c ../../../pico/eeprom.c \
	../../../pico/patch.c ../../../pico/debug.c ../../../pico/media.c \
	../../../pico/sms.c \
	../../../pico/carthw/carthw.c \
	../../../pico/carthw/eeprom_spi.c \
	../../../pico/sound/sound.c \
	../../../pico/sound/sn76496.c \
	../../../pico/sound/ym2612.c \
	../../../pico/sound/mix.c

THUMB_OBJECTS 	:= \
	file.o main.o pico_nds.o \
	patch.o debug.o media.o cart.o \
	state.o

SOURCES_ASM	:= \
	../../../pico/m68kif_cyclone.s ../../../cpu/cyclone/Cyclone.s \
	../../../cpu/cyclone/tools/idle.s \
	../../../cpu/DrZ80/drz80.s \
	../../../pico/memory_arm.s \
	../../../pico/misc_arm.s \
	../../../pico/draw_arm.s \
	../../../pico/draw2_arm.s

INCLUDES	:=	include source ../../..
DATA		:=


#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-mthumb-interwork -marm
$(THUMB_OBJECTS): ARCH := -mthumb-interwork -mthumb

CFLAGS	:=	-g -Wall -O2 \
 			-march=armv5te -mtune=arm946e-s -fomit-frame-pointer \
			-ffast-math -fno-strict-aliasing

CFLAGS	+=	$(INCLUDE) -DARM9 -D_NDS

CFLAGS	+=	-DEMU_C68K -D_USE_DRZ80 -DNO_32X -DNO_ZIP -DNO_MCD -DNO_PICO -DNO_SVP -DUSE_BGR555
CFLAGS	+=	-D_ASM_MISC_C -D_ASM_MEMORY_C -D_ASM_DRAW_C

CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH) -march=armv5te -mtune=arm946e-s -DARM9 -D_NDS

LDFLAGS	=	-specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
LIBS	:=	-lfat -lnds9 -lz -lm
 
#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=	$(LIBNDS) $(DEVKITPRO)/portlibs/nds
 
#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
 
export ARM9ELF	:=	$(CURDIR)/$(TARGET).elf
export DEPSDIR := $(CURDIR)/$(BUILD)

export VPATH	:=	$(foreach dir,$(SOURCE_DIRS),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))
 
CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(dir)))
CPPFILES	:=	$(foreach dir,$(SOURCES_CXX),$(notdir $(dir)))
SFILES		:=	$(foreach dir,$(SOURCES_ASM),$(notdir $(dir)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
 
#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
					$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
 
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)
 
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)
 
.PHONY: $(BUILD) clean
 
#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
 
#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) *.elf *.nds* *.bin 
 
 
#---------------------------------------------------------------------------------
else
 
#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(ARM9ELF)	:	$(OFILES)
	@echo linking $(notdir $@)
	@$(LD)  $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data 
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

%.o: %.c
	$(SILENTMSG) $(notdir $<)
	$(SILENTCMD)$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) $(ARCH) -c $< -o $@ $(ERROR_FILTER)

-include $(DEPSDIR)/*.d
 
#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
