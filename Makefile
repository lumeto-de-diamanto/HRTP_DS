# SPDX-License-Identifier: CC0-1.0
#
# SPDX-FileContributor: Antonio Niño Díaz, 2023-2026

BLOCKSDS	?= $(if $(wildcard /opt/blocksds/core),/opt/blocksds/core,/opt/wonderful/thirdparty/blocksds/core)
BLOCKSDSEXT	?= /opt/blocksds/external
WONDERFUL_TOOLCHAIN	?= /opt/wonderful

NITROFSDIR	:= nitrofs

NAME		:= hrtp_ds

GAME_TITLE	:= Touhou Reiiden -
GAME_SUBTITLE	:= The Highly Responsive
GAME_AUTHOR	:= to Prayers
GAME_ICON	:= icon.gif

COMPDB		?= 0

MAKE		:= make
RM		:= rm -rf
CP		:= cp
INSTALL	:= install

ifeq ($(VERBOSE),1)
V		:=
else
V		:= @
endif

# Directories
# -----------

ARM9DIR		:= arm9
ARM7DIR		:= arm7

# Build artfacts
# --------------

ROM		:= $(NAME).nds

# Targets
# -------

.PHONY: all clean arm9 arm7 dldipatch sdimage assets assets-clean
.PHONY: audio-tests audio-release-checks native-audio native-soundtrack native-tests native-sanitize native-sfx sfx-tests boss-tests boss-sanitize boss-release-checks
.PHONY: viewport-tests viewport-sanitize viewport-release-checks
.PHONY: konngara-tests konngara-sanitize konngara-release-checks
.PHONY: asset-tests font-tests rom-checks
.PHONY: timer-tests timer-sanitize
.PHONY: projectile-tests projectile-sanitize

# Required host source dependencies live outside the disposable build tree.
MDT_TOOLS ?= tools/deps/mdt-tools
HOST_CXX ?= g++
YMFM ?= tools/deps/ymfm
export PYTHONDONTWRITEBYTECODE := 1
export PYTHONPATH := $(CURDIR)/tools:$(CURDIR)$(if $(PYTHONPATH),:$(PYTHONPATH))

all: $(ROM)

asset-tests:
	python3 -B tests/test_asset_build.py

font-tests:
	python3 -B tests/test_font16x16.py

rom-checks: all
	python3 -B tests/test_rom_packaging.py

TIMER_TEST_SOURCES := tests/test_timer.cpp source/engine/main/stage/timer.cpp
timer-tests:
	@mkdir -p build/timer/tests
	$(HOST_CXX) -O2 -std=c++17 -Wall -Wextra -I$(BLOCKSDS)/libs/libnds/include -Isource/engine $(TIMER_TEST_SOURCES) -o build/timer/tests/timer
	build/timer/tests/timer

timer-sanitize:
	@mkdir -p build/timer/tests
	$(HOST_CXX) -O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -I$(BLOCKSDS)/libs/libnds/include -Isource/engine $(TIMER_TEST_SOURCES) -o build/timer/tests/timer_sanitized
	UBSAN_OPTIONS=halt_on_error=1 build/timer/tests/timer_sanitized

PROJECTILE_TEST_SOURCES := tests/test_projectiles_portals.cpp source/engine/main/stage/stageobj.cpp source/engine/main/bullet/pellet.cpp source/engine/main/player/orb.cpp source/engine/main/player/shot.cpp source/engine/main/math/vector.cpp source/engine/main/math/trig.cpp
projectile-tests:
	@mkdir -p build/projectiles/tests
	$(HOST_CXX) -O2 -std=c++17 -Wall -Wextra -I$(BLOCKSDS)/libs/libnds/include -Isource/engine -Isource/platform $(PROJECTILE_TEST_SOURCES) -o build/projectiles/tests/projectiles
	build/projectiles/tests/projectiles

projectile-sanitize:
	@mkdir -p build/projectiles/tests
	$(HOST_CXX) -O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -I$(BLOCKSDS)/libs/libnds/include -Isource/engine -Isource/platform $(PROJECTILE_TEST_SOURCES) -o build/projectiles/tests/projectiles_sanitized
	UBSAN_OPTIONS=halt_on_error=1 build/projectiles/tests/projectiles_sanitized

audio-tests: native-tests sfx-tests
	@mkdir -p build/native_tools/tests
	$(HOST_CXX) -O2 -std=c++17 -Wall -Wextra -Itools tests/test_audio_lfo.cpp -o build/native_tools/tests/lfo
	build/native_tools/tests/lfo
	python3 -B tests/test_opna_rhythm.py
	python3 -B tests/test_recording_comparison.py

audio-release-checks:
	python3 tools/check_audio_release.py

native-audio:
	$(MAKE) -f native_audio/Makefile --no-print-directory

native-soundtrack: assets
	python3 -B tools/build_native_soundtrack.py --tools-dir $(MDT_TOOLS) --ymfm $(YMFM) --cxx $(HOST_CXX)

native-sfx: assets
	python3 -B tools/build_native_sfx.py --ymfm $(YMFM) --cxx $(HOST_CXX)

sfx-tests: native-sfx
	python3 tests/test_native_sfx.py
	@mkdir -p build/native_sfx/tests
	$(HOST_CXX) -O2 -std=c++17 -Wall -Wextra -I. -Inative_audio/service_stubs -Itools tests/test_native_sfx.cpp -o build/native_sfx/tests/player
	build/native_sfx/tests/player nitrofs/audio/sfx.bin
	$(HOST_CXX) -O2 -std=c++17 -Wall -Wextra -I$(BLOCKSDS)/libs/libnds/include -Isource/engine -Isource/platform tests/test_sfx_gameplay_hooks.cpp source/engine/main/player/shot.cpp source/engine/main/stage/card.cpp source/engine/main/stage/item.cpp -o build/native_sfx/tests/hooks
	build/native_sfx/tests/hooks

native-tests: native-soundtrack
	python3 tests/test_native_audio.py
	python3 tests/test_native_soundtrack.py

native-sanitize: native-soundtrack native-sfx
	python3 -B tests/test_native_soundtrack.py --sanitize
	@mkdir -p build/native_sfx/tests
	$(HOST_CXX) -O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -I. -Inative_audio/service_stubs -Itools tests/test_native_sfx.cpp -o build/native_sfx/tests/player_sanitized
	build/native_sfx/tests/player_sanitized nitrofs/audio/sfx.bin

BOSS_TEST_SOURCES := tests/test_boss.cpp source/engine/main/boss/boss.cpp source/engine/main/boss/singyoku.cpp source/engine/main/player/orb.cpp source/engine/main/player/shot.cpp source/engine/main/bullet/pellet.cpp source/engine/main/math/vector.cpp source/engine/main/math/trig.cpp source/engine/main/random.cpp
boss-tests: assets
	python3 -B tests/test_boss_assets.py
	@mkdir -p build/boss/tests
	$(HOST_CXX) -O2 -std=c++17 -Wall -Wextra -I$(BLOCKSDS)/libs/libnds/include -Isource/engine -Isource/platform $(BOSS_TEST_SOURCES) -o build/boss/tests/boss
	build/boss/tests/boss

boss-sanitize:
	@mkdir -p build/boss/tests
	$(HOST_CXX) -O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -I$(BLOCKSDS)/libs/libnds/include -Isource/engine -Isource/platform $(BOSS_TEST_SOURCES) -o build/boss/tests/boss_sanitized
	UBSAN_OPTIONS=halt_on_error=1 build/boss/tests/boss_sanitized

boss-release-checks: all
	python3 -B tests/test_boss_assets.py --rom

VIEWPORT_TEST_SOURCES := tests/test_viewport.cpp $(wildcard source/engine/main/player/*.cpp source/engine/main/boss/*.cpp source/engine/main/bullet/*.cpp source/engine/main/stage/stageobj.cpp source/engine/main/stage/card.cpp source/engine/main/stage/item.cpp source/engine/main/stage/timer.cpp source/engine/main/math/*.cpp source/engine/main/random.cpp) $(addprefix source/platform/,camera.cpp canvas.cpp video.cpp tiles.cpp stage_render.cpp background.cpp upper_hud.cpp hud_render.cpp font16x16.cpp player_render.cpp orb_render.cpp shot_render.cpp item_render.cpp pellet_render.cpp popup.cpp boss_render.cpp palette_fx.cpp text_layer.cpp)
VIEWPORT_TEST_SOURCES += tests/heap_probe.cpp
HEAP_PROBE_LDFLAGS := -Wl,--wrap=malloc,--wrap=free
viewport-tests: assets
	@mkdir -p build/viewport/tests
	$(HOST_CXX) -O2 -std=c++17 -Wall -Wextra -Itools/viewport_stubs -I$(BLOCKSDS)/libs/libnds/include -Isource/engine -Isource/platform -Iassets/processed $(VIEWPORT_TEST_SOURCES) $(HEAP_PROBE_LDFLAGS) -o build/viewport/tests/viewport
	python3 -B tests/test_viewport_assets.py --run build/viewport/tests/viewport

viewport-sanitize: assets
	@mkdir -p build/viewport/tests
	$(HOST_CXX) -O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -Itools/viewport_stubs -I$(BLOCKSDS)/libs/libnds/include -Isource/engine -Isource/platform -Iassets/processed $(VIEWPORT_TEST_SOURCES) $(HEAP_PROBE_LDFLAGS) -o build/viewport/tests/viewport_sanitized
	UBSAN_OPTIONS=halt_on_error=1 python3 -B tests/test_viewport_assets.py --run build/viewport/tests/viewport_sanitized

viewport-release-checks: all
	python3 -B tests/test_viewport_assets.py --rom

KONNGARA_TEST_SOURCES := tests/test_konngara.cpp source/engine/main/boss/konngara_intro.cpp source/engine/main/math/trig.cpp $(addprefix source/platform/,konngara_intro_render.cpp camera.cpp canvas.cpp video.cpp font16x16.cpp)
KONNGARA_TEST_SOURCES += tests/heap_probe.cpp
konngara-tests: assets native-soundtrack
	@mkdir -p build/konngara/tests
	$(HOST_CXX) -O2 -std=c++17 -Wall -Wextra -Itools/viewport_stubs -I$(BLOCKSDS)/libs/libnds/include -Isource/engine -Isource/platform -Iassets/processed -Ibuild/native_soundtrack $(KONNGARA_TEST_SOURCES) $(HEAP_PROBE_LDFLAGS) -o build/konngara/tests/intro
	python3 -B tests/test_konngara_assets.py --run build/konngara/tests/intro

konngara-sanitize: assets native-soundtrack
	@mkdir -p build/konngara/tests
	$(HOST_CXX) -O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -Itools/viewport_stubs -I$(BLOCKSDS)/libs/libnds/include -Isource/engine -Isource/platform -Iassets/processed -Ibuild/native_soundtrack $(KONNGARA_TEST_SOURCES) $(HEAP_PROBE_LDFLAGS) -o build/konngara/tests/intro_sanitized
	UBSAN_OPTIONS=halt_on_error=1 python3 -B tests/test_konngara_assets.py --run build/konngara/tests/intro_sanitized

konngara-release-checks: all
	python3 -B tests/test_konngara_assets.py --rom

clean:
	@echo "  CLEAN"
	$(V)$(RM) build hrtp_ds.nds hrtp_native_audio.nds nitrofs/audio

assets:
	$(V)$(MAKE) -f assets/Makefile assets --no-print-directory

assets-clean:
	$(V)$(MAKE) -f assets/Makefile assets-clean --no-print-directory

arm9: assets native-soundtrack native-sfx
	$(V)+$(MAKE) -f Makefile.arm9 COMPDB=$(COMPDB) --no-print-directory

arm7:
	$(V)+$(MAKE) -f Makefile.arm7 COMPDB=$(COMPDB) --no-print-directory

ifeq ($(COMPDB),1)
all: compile_commands.json

compile_commands.json: arm9 arm7
	@echo "  MERGE   compile_commands.json"
	$(V)$(WONDERFUL_TOOLCHAIN)/bin/wf-compile-commands-merge $@ \
		build/*/compile_commands.json
endif

ifeq ($(strip $(GAME_SUBTITLE)),)
    GAME_FULL_TITLE := $(GAME_TITLE);$(GAME_AUTHOR)
else
    GAME_FULL_TITLE := $(GAME_TITLE);$(GAME_SUBTITLE);$(GAME_AUTHOR)
endif

# A 0x200 header omits TWL sections: package actual DS-only, not DS+DSi.
$(ROM): arm9 arm7
	@echo "  NDSTOOL $@"
	$(V)$(BLOCKSDS)/tools/ndstool/ndstool -c $@ \
		-7 build/arm7.elf -9 build/arm9.elf \
		-h 0x200 \
		-b $(GAME_ICON) "$(GAME_FULL_TITLE)" \
		-d $(NITROFSDIR) $(NDSTOOL_ARGS)

sdimage:
	@echo "  MKFATIMG $(SDIMAGE) $(SDROOT)"
	$(V)$(BLOCKSDS)/tools/mkfatimg/mkfatimg -t $(SDROOT) $(SDIMAGE)

dldipatch: $(ROM)
	@echo "  DLDIPATCH $(ROM)"
	$(V)$(BLOCKSDS)/tools/dldipatch/dldipatch patch \
		$(BLOCKSDS)/sys/dldi_r4/r4tf.dldi $(ROM)
