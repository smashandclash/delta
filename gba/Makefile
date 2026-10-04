# Smash&Clash for the Game Boy Advance, built with Wonderful (https://wonderful.asie.pl).
#   make            -> smashandclash.gba
# Needs Wonderful with the GBA target and libtonc (wf-pacman -S target-gba target-gba-libtonc),
# and Python 3 with Pillow and fontTools for the art (pip install pillow fonttools).
# The ROM reaches the game through the SDK bridge: see bridge/README.md.

export WONDERFUL_TOOLCHAIN ?= /opt/wonderful
TARGET := gba/rom
include $(WONDERFUL_TOOLCHAIN)/target/$(TARGET)/makedefs.mk

NAME		:= smashandclash
BUILDDIR	:= build
ASSETS		:= $(BUILDDIR)/assets
ROM		:= $(NAME).gba
ELF		:= $(BUILDDIR)/$(NAME).elf

# The art and fonts come from the real game, converted for the GBA (cached in ../tools/cache).
ifeq ($(wildcard $(ASSETS)/assets.h),)
    $(info Making the art: python3 ../tools/make_assets.py gba $(ASSETS))
    $(shell python3 ../tools/make_assets.py gba $(ASSETS) 1>&2)
endif

# The shared core, minus the HTTPS client: the SDK bridge makes the calls.
CORE		:= snc_json.c snc_api.c snc_game.c snc_client.c snc_gfx.c snc_draw.c snc_ui.c snc_cards.c snc_sound.c qrcodegen.c
SOURCES		:= $(wildcard source/*.c) $(CORE) assets_data.c
VPATH		:= source ../core $(ASSETS) audio

DEFINES		:= -DSNC_GBA -DSNC_BRIDGE -DSNC_BUF_CAP=16384 -DSNC_MAX_TOKS=1500
LIBDIRS		:= $(WF_EXTLIB_DIR)/libtonc
INCLUDES	:= -Isource -I../core -I$(ASSETS) -Iaudio $(foreach path,$(LIBDIRS),-isystem $(path)/include)

CFLAGS		+= -std=gnu11 -O2 -mthumb -Wall -Wno-format-truncation -ffunction-sections -fdata-sections \
		   $(DEFINES) $(WF_ARCH_CFLAGS) $(INCLUDES)
# malloc's heap: the rest of EWRAM
LDFLAGS		:= $(foreach path,$(LIBDIRS),-L$(path)/lib) -Wl,-Map,$(BUILDDIR)/$(NAME).map -Wl,--gc-sections \
		   -Wl,--defsym=__heap_start=__eheap_start -Wl,--defsym=__heap_end=__eheap_end $(WF_ARCH_LDFLAGS) -ltonc -lgba

OBJS		:= $(addprefix $(BUILDDIR)/obj/,$(notdir $(SOURCES:.c=.o)) assets_bin.o audio.o)

.PHONY: all clean usage

all: $(ROM)

$(ROM): $(OBJS) wfconfig.toml
	@echo "  ROM     $@"
	@$(ROMLINK) -o $(ROM) --output-elf $(ELF) -- $(OBJS) $(LDFLAGS)

$(BUILDDIR)/obj/%.o: %.c | $(BUILDDIR)/obj
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# The renderer runs from IWRAM as ARM code: several times faster than Thumb from the cartridge.
$(BUILDDIR)/obj/snc_gfx.o: snc_gfx.c | $(BUILDDIR)/obj
	@echo "  CC      $< (ARM, IWRAM)"
	@$(CC) $(filter-out -mthumb -ffunction-sections,$(CFLAGS)) -marm -mlong-calls -fno-function-sections -MMD -MP -c -o $@ $<
	@$(OBJCOPY) --rename-section .text=.iwram.snc_gfx $@

$(BUILDDIR)/obj/assets_bin.o: $(ASSETS)/assets_bin.s | $(BUILDDIR)/obj
	@echo "  AS      $<"
	@$(CC) -x assembler-with-cpp $(WF_ARCH_CFLAGS) -c -o $@ $<

# the music and effects (made by tools/audio/make_audio.py, committed in audio/)
$(BUILDDIR)/obj/audio.o: audio/audio.s $(wildcard audio/*.bin) | $(BUILDDIR)/obj
	@echo "  AS      $<"
	@$(CC) -x assembler-with-cpp $(WF_ARCH_CFLAGS) -c -o $@ $<

$(BUILDDIR)/obj:
	@$(MKDIR) -p $@

usage: $(ELF)
	@$(ROMUSAGE) $<

clean:
	@echo "  CLEAN"
	@$(RM) $(BUILDDIR)/obj $(BUILDDIR)/$(NAME).* $(ROM)

-include $(OBJS:.o=.d)
