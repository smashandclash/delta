#!/bin/sh
# Desktop builds of the shared core: the API smoke test and the screen previews
# (the DS and GBA views, against the live API). The PSP draws with its GPU: test it in
# PPSSPP (tests/psp_play.py). Run from the repository root:  sh host/build.sh
# (needs gcc, mbedTLS 3 dev files, Python + Pillow + fontTools)
set -e
mkdir -p build/host
[ -f build/host/assets/assets.h ] || python3 tools/make_assets.py host build/host/assets
[ -f build/host-gba/assets/assets.h ] || python3 tools/make_assets.py host-gba build/host-gba/assets
CORE="core/snc_http.c core/snc_json.c core/snc_api.c core/snc_game.c core/snc_client.c core/snc_gfx.c core/snc_draw.c core/snc_ui.c core/snc_cards.c core/qrcodegen.c"
CFLAGS="-O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -Icore"
LIBS="-lmbedtls -lmbedx509 -lmbedcrypto -lm -lpthread"
gcc $CFLAGS -o build/host/api_test host/api_test.c host/net_host.c core/snc_http.c core/snc_json.c core/snc_api.c $LIBS
gcc $CFLAGS -Ids/source -Ibuild/host/assets -o build/host/ds_preview host/preview.c host/net_host.c ds/source/view.c $CORE \
	build/host/assets/assets_data.c build/host/assets/assets_bin.s $LIBS
gcc $CFLAGS -DPREVIEW_GBA -Igba/source -Ibuild/host-gba/assets -o build/host/gba_preview host/preview.c host/net_host.c gba/source/view.c $CORE \
	build/host-gba/assets/assets_data.c build/host-gba/assets/assets_bin.s $LIBS
echo "built build/host/api_test, build/host/ds_preview and build/host/gba_preview"
