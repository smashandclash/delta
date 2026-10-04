# Smash&Clash for the Nintendo DS, built with BlocksDS (https://blocksds.skylyrac.net).
#   make            -> smashandclash.nds
# Needs BlocksDS with blocksds-mbedtls (wf-pacman -S blocksds-toolchain blocksds-mbedtls),
# and Python 3 with Pillow and fontTools for the art (pip install pillow fonttools).

BLOCKSDS	?= /opt/wonderful/thirdparty/blocksds/core
BLOCKSDSEXT	?= /opt/wonderful/thirdparty/blocksds/external

NAME		:= smashandclash
GAME_TITLE	:= Smash&Clash
GAME_SUBTITLE	:= Played online
GAME_AUTHOR	:= smashandclash.in
GAME_ICON	:= build/assets/icon.gif

# The art and fonts come from the real game, converted for the DS (cached in ../tools/cache).
ifeq ($(wildcard build/assets/assets.h),)
    $(info Making the art: python3 ../tools/make_assets.py ds build/assets)
    $(shell python3 ../tools/make_assets.py ds build/assets 1>&2)
endif

# audio/: the music and effects (made by ../tools/audio/make_audio.py, committed)
SOURCEDIRS	:= source ../core build/assets audio
INCLUDEDIRS	:= source ../core build/assets audio

ARM7ELF		:= $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi.elf

LIBS		:= -ldswifi9 -lnds9 -lmbedtls -lmbedx509 -lmbedcrypto -lm
LIBDIRS		:= $(BLOCKSDS)/libs/dswifi $(BLOCKSDS)/libs/libnds $(BLOCKSDSEXT)/mbedtls

include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
