# Native-only listening diagnostic; separate CPU objects from gameplay.
BLOCKSDS ?= $(if $(wildcard /opt/blocksds/core),/opt/blocksds/core,/opt/wonderful/thirdparty/blocksds/core)
WONDERFUL_TOOLCHAIN ?= /opt/wonderful
export BLOCKSDS WONDERFUL_TOOLCHAIN
CXX := $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-g++
OUT := build/native_audio
NATIVE_TRACK ?= kami2
NATIVE_APPROX_FLAGS ?= $(if $(filter kami kami2 alice,$(NATIVE_TRACK)),--approximate-portamento,)
NATIVE_RHYTHM_DIR ?= $(if $(wildcard assets/drums/provenance.json),assets/drums,)
MDT_TOOLS ?= tools/deps/mdt-tools
YMFM ?= tools/deps/ymfm
HOST_CXX ?= g++
INC := -Inative_audio -Itools -Ibuild/native_soundtrack -I$(OUT) -I$(BLOCKSDS)/libs/libnds/include
COMMON := -O2 -Wall -Wextra -std=c++17 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -D__NDS__ -D__BLOCKSDS__ $(INC)
ARM7 := -mthumb -mcpu=arm7tdmi -DARM7 -DNS_DIAGNOSTIC_CAPTURE=1 -specs=$(BLOCKSDS)/sys/crts/ds_arm7.specs
ARM9 := -mthumb -mcpu=arm946e-s+nofp -DARM9 -specs=$(BLOCKSDS)/sys/crts/ds_arm9.specs
LIBPATH := -L$(BLOCKSDS)/libs/libnds/lib
HEADERS := native_audio/format.h native_audio/engine.hpp tools/audio_lfo.hpp native_audio/arm7/service.hpp native_audio/client.hpp native_audio/sfx.hpp

.PHONY: all assets tests
all: hrtp_native_audio.nds

assets:
	python3 -B tools/build_native_audio.py --track $(NATIVE_TRACK) $(NATIVE_APPROX_FLAGS) --tools-dir $(MDT_TOOLS) --ymfm $(YMFM) --cxx $(HOST_CXX) $(if $(NATIVE_RHYTHM_DIR),--rhythm-dir "$(NATIVE_RHYTHM_DIR)",)
	python3 -B tools/build_native_soundtrack.py --tools-dir $(MDT_TOOLS) --ymfm $(YMFM) --cxx $(HOST_CXX) $(if $(NATIVE_RHYTHM_DIR),--rhythm-dir "$(NATIVE_RHYTHM_DIR)",)

tests: assets
	python3 tests/test_native_audio.py
	python3 tests/test_native_soundtrack.py
	python3 tests/test_opna_rhythm.py
	g++ -O2 -std=c++17 -Wall -Wextra -Itools -Inative_audio tests/test_native_audio.cpp -o $(OUT)/test_native_audio
	$(OUT)/test_native_audio $(OUT)/nitrofs/$(NATIVE_TRACK)/song.nsq
	python3 tests/test_native_audio.py --trace $(OUT)/native_trace.csv

$(OUT)/arm7.o: native_audio/arm7/main.cpp $(HEADERS)
	@mkdir -p $(OUT)
	$(CXX) $(COMMON) $(ARM7) -MMD -MP -c $< -o $@

$(OUT)/arm9.o: native_audio/arm9/main.cpp native_audio/format.h assets
	$(CXX) $(COMMON) $(ARM9) -MMD -MP -c $< -o $@

$(OUT)/arm7.elf: $(OUT)/arm7.o
	$(CXX) $(ARM7) $(LIBPATH) -Wl,--gc-sections,-Map,$(OUT)/arm7.map $< -Wl,--start-group -lnds7 -lc -Wl,--end-group -o $@

$(OUT)/arm9.elf: $(OUT)/arm9.o
	$(CXX) $(ARM9) $(LIBPATH) -Wl,--gc-sections,-Map,$(OUT)/arm9.map -Wl,-u,gettimeofday $< -Wl,--start-group -lnds9 -lc -Wl,--end-group -o $@

hrtp_native_audio.nds: tests $(OUT)/arm7.elf $(OUT)/arm9.elf
	$(BLOCKSDS)/tools/ndstool/ndstool -c $@ -7 $(OUT)/arm7.elf -9 $(OUT)/arm9.elf -b icon.gif "HRtP soundtrack;Native diagnostics;16 native songs" -d build/native_soundtrack/nitrofs

-include $(OUT)/arm7.d $(OUT)/arm9.d
