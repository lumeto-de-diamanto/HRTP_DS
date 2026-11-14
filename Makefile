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
.PHONY: native-audio native-soundtrack native-sfx test sanitize check-release

# Required host source dependencies live outside the disposable build tree.
MDT_TOOLS ?= tools/deps/mdt-tools
HOST_CXX ?= g++
YMFM ?= tools/deps/ymfm
export PYTHONDONTWRITEBYTECODE := 1
export PYTHONPATH := $(CURDIR)/tools:$(CURDIR)$(if $(PYTHONPATH),:$(PYTHONPATH))

all: $(ROM)

TEST_SUITE ?= all
TEST_RUNNER = python3 -B tests/run.py --suite $(TEST_SUITE) --cxx "$(HOST_CXX)" --blocksds "$(BLOCKSDS)"
TEST_INPUTS = $(if $(filter gameplay,$(TEST_SUITE)),,native-soundtrack $(if $(filter all audio,$(TEST_SUITE)),native-sfx))

# Three entry points; feature cases stay modular. Host builds/graphics fixtures
# are content-cached, and sanitizer/release runs don't repeat the Python suite.
test: $(TEST_INPUTS)
	$(TEST_RUNNER) test

sanitize: $(TEST_INPUTS)
	UBSAN_OPTIONS=halt_on_error=1 $(TEST_RUNNER) sanitize

check-release: all
	$(MAKE) native-audio
	python3 -B tests/run.py release

native-audio:
	$(MAKE) -f native_audio/Makefile --no-print-directory

native-soundtrack: assets
	python3 -B tools/build_native_soundtrack.py --tools-dir $(MDT_TOOLS) --ymfm $(YMFM) --cxx $(HOST_CXX)

native-sfx: assets
	python3 -B tools/build_native_sfx.py --ymfm $(YMFM) --cxx $(HOST_CXX)

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

$(ROM): arm9 arm7
	@echo "  NDSTOOL $@"
	$(V)$(BLOCKSDS)/tools/ndstool/ndstool -c $@ \
		-7 build/arm7.elf -9 build/arm9.elf \
		-h 0x4000 -uc 0 \
		-b $(GAME_ICON) "$(GAME_FULL_TITLE)" \
		-d $(NITROFSDIR) $(NDSTOOL_ARGS)

sdimage:
	@echo "  MKFATIMG $(SDIMAGE) $(SDROOT)"
	$(V)$(BLOCKSDS)/tools/mkfatimg/mkfatimg -t $(SDROOT) $(SDIMAGE)

dldipatch: $(ROM)
	@echo "  DLDIPATCH $(ROM)"
	$(V)$(BLOCKSDS)/tools/dldipatch/dldipatch patch \
		$(BLOCKSDS)/sys/dldi_r4/r4tf.dldi $(ROM)
