TOOLS_DIR       := tools
ASSETS_DIR      := assets

HDI_DUMP_DIR     := $(ASSETS_DIR)/hdi_dump
PACKFILE_DIR     := $(ASSETS_DIR)/packfile

PROCESSED_DIR   := $(ASSETS_DIR)/processed
SPRITES_DIR     := $(PROCESSED_DIR)/sprites
BACKGROUND_DIR  := $(PROCESSED_DIR)/backgrounds

NITROFS_DIR     := nitrofs

FONT_DIR        := $(PROCESSED_DIR)/fonts

STAGEDAT_DIR    := $(PROCESSED_DIR)/stagedat
SCENES          := 0 1 2 3 4 5 6 7
STAGEDAT_HEADERS := $(addprefix $(STAGEDAT_DIR)/stagedat,$(addsuffix .h,$(SCENES)))

GAME_HDI        := $(ASSETS_DIR)/th01.hdi
PACKFILE        := $(HDI_DUMP_DIR)/東方靈異.伝

PYTHON          := python3

PACKFILE_STAMP   := $(PACKFILE_DIR)/.stamp
HDI_DUMP_STAMP   := $(HDI_DUMP_DIR)/.stamp

# Extraction creates multiple files at once. Explicit grouped targets let Make
# find these inputs on the FIRST build and recover a deleted member safely
# under -j. A stamp alone cannot describe an implicit rule's missing input.
HDI_GRP_NAMES := $(addprefix ST,0 1 2 3 4 5 6) BOSS1 CLEAR3 BOSS7_D1 BOSS8_A1 $(addprefix BOSS8_D,1 2 3 4)
HDI_FILES := $(PACKFILE) $(addprefix $(HDI_DUMP_DIR)/,$(addsuffix .GRP,$(HDI_GRP_NAMES))) $(HDI_DUMP_DIR)/BOSS8.GRZ
PACKFILE_NAMES := MIKO.PTN MIKO_AC2.BOS MIKO_AC.BOS STG.PTN KUZI1.GRC KUZI2.GRC BOSS1.BOS BOSS1_2.BOS BOSS1_3.BOS BOSS8_1.BOS BOSS8_E1.BOS BOSS8_E2.BOS $(addprefix STAGE,$(addsuffix .DAT,$(SCENES)))
PACKFILE_FILES := $(addprefix $(PACKFILE_DIR)/,$(PACKFILE_NAMES))
BACKGROUND_BINS := $(addprefix $(NITROFS_DIR)/,$(addsuffix .bin,st0 st1 st2 st3 st4 st5 st6 boss1))

# If ANY extraction peer was deleted, regenerate the whole group before its
# first consumer. Otherwise a late missing peer could re-extract files after
# Make already checked an earlier consumer, requiring a second invocation.
ifneq ($(filter-out $(wildcard $(HDI_FILES)),$(HDI_FILES)),)
HDI_EXTRACT_FORCE := force-hdi-extract
endif
ifneq ($(filter-out $(wildcard $(PACKFILE_FILES)),$(PACKFILE_FILES)),)
PACKFILE_EXTRACT_FORCE := force-packfile-extract
endif

.PHONY: assets assets-clean force-hdi-extract force-packfile-extract
.DELETE_ON_ERROR:

ifeq ($(VERBOSE),1)
V		:=
else
V		:= @
endif

RM		:= rm -rf

assets: $(PACKFILE_STAMP) $(HDI_DUMP_STAMP) $(SPRITES_DIR)/Miko.h $(SPRITES_DIR)/Miko32.h $(SPRITES_DIR)/Miko48.h $(SPRITES_DIR)/Shot.h $(SPRITES_DIR)/Orb.h $(SPRITES_DIR)/Stgobj.h $(SPRITES_DIR)/Pellet.h $(SPRITES_DIR)/Item.h $(SPRITES_DIR)/HudIcons.h $(STAGEDAT_HEADERS) $(BACKGROUND_DIR)/st0.h $(BACKGROUND_DIR)/st1.h $(BACKGROUND_DIR)/st2.h $(BACKGROUND_DIR)/st3.h $(BACKGROUND_DIR)/st4.h $(BACKGROUND_DIR)/st5.h $(BACKGROUND_DIR)/st6.h $(FONT_DIR)/font8x16.h $(FONT_DIR)/font16x16.h

assets: $(SPRITES_DIR)/SingyokuSphere.h $(SPRITES_DIR)/SingyokuFlash.h $(SPRITES_DIR)/SingyokuPerson.h $(BACKGROUND_DIR)/boss1.h
assets: $(SPRITES_DIR)/HudIconsSmall.h
assets: $(NITROFS_DIR)/konngara_intro.bin
assets: $(NITROFS_DIR)/konngara_art.bin
assets: $(NITROFS_DIR)/konngara_combat.bin
assets: $(NITROFS_DIR)/konngara_slash.bin
assets: $(NITROFS_DIR)/konngara_defeat.bin
assets: $(BACKGROUND_BINS)
assets: $(SPRITES_DIR)/OrbArrow.h
assets: $(SPRITES_DIR)/Shape8x8.h
assets: $(SPRITES_DIR)/BombKuji.h
assets: $(BACKGROUND_DIR)/HudPattern.h

$(BACKGROUND_DIR)/HudPattern.h: $(HDI_DUMP_DIR)/CLEAR3.GRP $(TOOLS_DIR)/hud_pattern_to_h.py $(TOOLS_DIR)/grp_to_bin.py
	$(PYTHON) $(TOOLS_DIR)/hud_pattern_to_h.py $< $@

$(SPRITES_DIR)/BombKuji.h: $(PACKFILE_DIR)/KUZI1.GRC $(PACKFILE_DIR)/KUZI2.GRC $(TOOLS_DIR)/bomb_kuji_to_h.py $(TOOLS_DIR)/halve_image.py
	$(PYTHON) $(TOOLS_DIR)/bomb_kuji_to_h.py $(PACKFILE_DIR)/KUZI1.GRC $(PACKFILE_DIR)/KUZI2.GRC $@

$(SPRITES_DIR)/Shape8x8.h: $(ASSETS_DIR)/sprites/shape8x8.bmp $(TOOLS_DIR)/mono_bmp_to_h.py $(TOOLS_DIR)/halve_image.py
	$(PYTHON) $(TOOLS_DIR)/mono_bmp_to_h.py $< $(SPRITES_DIR) --name Shape8x8 --halve rare

$(SPRITES_DIR)/OrbArrow.h: $(ASSETS_DIR)/sprites/orb_arrow.xpm $(TOOLS_DIR)/orb_arrow_to_h.py
	$(PYTHON) $(TOOLS_DIR)/orb_arrow_to_h.py $< $@

assets-clean:
	@echo "  ASSETS clean"
	$(V)$(RM) $(PROCESSED_DIR) $(HDI_DUMP_DIR) $(PACKFILE_DIR) $(NITROFS_DIR)

$(NITROFS_DIR)/konngara_intro.bin: $(HDI_DUMP_DIR)/BOSS7_D1.GRP $(HDI_DUMP_DIR)/BOSS8_A1.GRP $(addprefix $(HDI_DUMP_DIR)/BOSS8_D,1.GRP 2.GRP 3.GRP 4.GRP) $(TOOLS_DIR)/konngara_to_bin.py $(TOOLS_DIR)/grp_to_bin.py
	$(PYTHON) $(TOOLS_DIR)/konngara_to_bin.py $(HDI_DUMP_DIR) $@

$(NITROFS_DIR)/konngara_defeat.bin: $(HDI_DUMP_DIR)/BOSS7_D1.GRP $(HDI_DUMP_DIR)/BOSS8_A1.GRP $(HDI_DUMP_DIR)/BOSS8.GRZ $(TOOLS_DIR)/konngara_defeat_to_bin.py $(TOOLS_DIR)/grp_to_bin.py $(TOOLS_DIR)/grz_decode.py
	$(PYTHON) $(TOOLS_DIR)/konngara_defeat_to_bin.py $(HDI_DUMP_DIR) $@

$(NITROFS_DIR)/konngara_art.bin: $(NITROFS_DIR)/konngara_intro.bin $(HDI_DUMP_DIR)/BOSS8.GRZ $(addprefix $(PACKFILE_DIR)/,BOSS8_1.BOS BOSS8_E1.BOS BOSS8_E2.BOS) $(TOOLS_DIR)/konngara_art_to_bin.py $(TOOLS_DIR)/grz_decode.py $(TOOLS_DIR)/konngara_to_bin.py $(TOOLS_DIR)/grp_to_bin.py
	$(PYTHON) $(TOOLS_DIR)/konngara_art_to_bin.py $(HDI_DUMP_DIR) $(PACKFILE_DIR) $@

$(NITROFS_DIR)/konngara_combat.bin $(NITROFS_DIR)/konngara_slash.bin &: $(NITROFS_DIR)/konngara_art.bin $(TOOLS_DIR)/konngara_slash_to_bin.py $(TOOLS_DIR)/konngara_combat_to_bin.py $(TOOLS_DIR)/analyze_konngara_hybrid.py
	$(PYTHON) $(TOOLS_DIR)/konngara_slash_to_bin.py $< $(NITROFS_DIR)/konngara_combat.bin $(NITROFS_DIR)/konngara_slash.bin

$(SPRITES_DIR)/HudIconsSmall.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/STG.PTN $(TOOLS_DIR)/ptn_to_h.py $(TOOLS_DIR)/halve_image.py
	$(PYTHON) $(TOOLS_DIR)/ptn_to_h.py $(PACKFILE_DIR)/STG.PTN $(SPRITES_DIR) HudIconsSmall -s 0 --quarter --halve dither --linear

$(SPRITES_DIR)/SingyokuSphere.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/BOSS1.BOS $(TOOLS_DIR)/bos_to_h.py $(TOOLS_DIR)/halve_image.py
	$(PYTHON) $(TOOLS_DIR)/bos_to_h.py $(PACKFILE_DIR)/BOSS1.BOS $(SPRITES_DIR) SingyokuSphere --halve dither

$(SPRITES_DIR)/SingyokuFlash.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/BOSS1_2.BOS $(TOOLS_DIR)/bos_to_h.py $(TOOLS_DIR)/halve_image.py
	$(PYTHON) $(TOOLS_DIR)/bos_to_h.py $(PACKFILE_DIR)/BOSS1_2.BOS $(SPRITES_DIR) SingyokuFlash --halve dither

$(SPRITES_DIR)/SingyokuPerson.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/BOSS1_3.BOS $(TOOLS_DIR)/bos_to_h.py $(TOOLS_DIR)/halve_image.py
	$(PYTHON) $(TOOLS_DIR)/bos_to_h.py $(PACKFILE_DIR)/BOSS1_3.BOS $(SPRITES_DIR) SingyokuPerson --halve dither

$(BACKGROUND_DIR)/boss1.h $(NITROFS_DIR)/boss1.bin &: $(HDI_DUMP_DIR)/BOSS1.GRP $(TOOLS_DIR)/grp_to_bin.py $(TOOLS_DIR)/halve_image.py
	$(PYTHON) $(TOOLS_DIR)/grp_to_bin.py $(HDI_DUMP_DIR)/BOSS1.GRP $(BACKGROUND_DIR) $(NITROFS_DIR) boss1 --halve topleft2

$(PACKFILE_STAMP) $(PACKFILE_FILES) &: $(PACKFILE) $(TOOLS_DIR)/extract_pf.py $(PACKFILE_EXTRACT_FORCE)
	@echo "  ASSETS  extracting $(PACKFILE)"
	@mkdir -p $(PACKFILE_DIR)
	$(PYTHON) $(TOOLS_DIR)/extract_pf.py "$(PACKFILE)" $(PACKFILE_DIR)
	@for file in $(PACKFILE_FILES); do test -f "$$file" || exit 1; done
	@touch $(PACKFILE_STAMP) $(PACKFILE_FILES)

$(HDI_DUMP_STAMP) $(HDI_FILES) &: $(GAME_HDI) $(TOOLS_DIR)/hdi_extract.py $(HDI_EXTRACT_FORCE)
	@echo "  ASSETS  extracting $(GAME_HDI)"
	@mkdir -p $(HDI_DUMP_DIR)
	$(PYTHON) $(TOOLS_DIR)/hdi_extract.py "$(GAME_HDI)" -o $(HDI_DUMP_DIR)
	@for file in $(HDI_FILES); do test -f "$$file" || exit 1; done
	@touch $(HDI_DUMP_STAMP) $(HDI_FILES)

$(SPRITES_DIR)/Miko.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/MIKO.PTN $(TOOLS_DIR)/ptn_to_h.py
	@echo "  ASSETS  creating Miko.h"
	$(PYTHON) $(TOOLS_DIR)/ptn_to_h.py $(PACKFILE_DIR)/MIKO.PTN $(SPRITES_DIR) "Miko" --halve dither
	@touch $@

$(SPRITES_DIR)/Miko32.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/MIKO_AC2.BOS $(TOOLS_DIR)/bos_to_h.py
	@echo "  ASSETS  creating Miko32.h"
	$(PYTHON) $(TOOLS_DIR)/bos_to_h.py $(PACKFILE_DIR)/MIKO_AC2.BOS $(SPRITES_DIR) "Miko32" --halve dither
	@touch $@

$(SPRITES_DIR)/Miko48.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/MIKO_AC.BOS $(TOOLS_DIR)/bos_to_h.py
	@echo "  ASSETS  creating Miko48.h"
	$(PYTHON) $(TOOLS_DIR)/bos_to_h.py $(PACKFILE_DIR)/MIKO_AC.BOS $(SPRITES_DIR) "Miko48" --halve dither
	@touch $@

$(SPRITES_DIR)/Shot.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/STG.PTN $(TOOLS_DIR)/ptn_to_h.py
	@echo "  ASSETS  creating Shot.h"
	$(PYTHON) $(TOOLS_DIR)/ptn_to_h.py $(PACKFILE_DIR)/STG.PTN $(SPRITES_DIR) "Shot" --halve topleft -s 1 -q
	@touch $@

$(SPRITES_DIR)/Orb.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/STG.PTN $(TOOLS_DIR)/ptn_to_h.py
	@echo "  ASSETS  creating Orb.h"
	$(PYTHON) $(TOOLS_DIR)/ptn_to_h.py $(PACKFILE_DIR)/STG.PTN $(SPRITES_DIR) "Orb" --halve topleft -s 3 4 5 6
	@touch $@

# One background per scene (ST0..ST6; scene 7 is unused leftover content).
# Indices 0..15 of each generated palette keep the original GRP colors --
# the stage tiles and items are drawn through these (palette_fx.cpp).
$(BACKGROUND_DIR)/st%.h $(NITROFS_DIR)/st%.bin &: $(HDI_DUMP_DIR)/ST%.GRP $(TOOLS_DIR)/grp_to_bin.py $(TOOLS_DIR)/halve_image.py
	@echo "  ASSETS  creating st$*.h and st$*.bin"
	$(PYTHON) $(TOOLS_DIR)/grp_to_bin.py $(HDI_DUMP_DIR)/ST$*.GRP $(BACKGROUND_DIR) $(NITROFS_DIR) "st$*" --halve topleft2
$(SPRITES_DIR)/Stgobj.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/STG.PTN $(TOOLS_DIR)/ptn_to_h.py $(TOOLS_DIR)/halve_image.py
	@echo "  ASSETS  creating Stgobj.h"
	$(PYTHON) $(TOOLS_DIR)/ptn_to_h.py $(PACKFILE_DIR)/STG.PTN $(SPRITES_DIR) "Stgobj" --halve dither:7-21 --halve rare:22-33
	@touch $@

$(STAGEDAT_DIR)/stagedat%.h: $(PACKFILE_DIR)/STAGE%.DAT $(TOOLS_DIR)/stagedat_to_h.py $(TOOLS_DIR)/stagedat_decode.py
	@echo "  ASSETS  creating stagedat$*.h"
	$(PYTHON) $(TOOLS_DIR)/stagedat_to_h.py $(PACKFILE_DIR)/STAGE$*.DAT -o $(STAGEDAT_DIR)

$(FONT_DIR)/font8x16.h: assets/font.bmp $(TOOLS_DIR)/font_atlas.py
	@echo "  ASSETS  creating font8x16.h"
	$(PYTHON) $(TOOLS_DIR)/font_atlas.py assets/font.bmp $(FONT_DIR)

$(FONT_DIR)/font16x16.h: assets/font.bmp $(TOOLS_DIR)/font16x16.py $(TOOLS_DIR)/font16x16_chars.py
	@echo "  ASSETS  creating compact font16x16.h"
	$(PYTHON) $(TOOLS_DIR)/font16x16.py assets/font.bmp $(FONT_DIR)

$(SPRITES_DIR)/Pellet.h: assets/sprites/pellet.bmp $(TOOLS_DIR)/mono_bmp_to_h.py $(TOOLS_DIR)/halve_image.py
	@echo "  ASSETS  creating Pellet.h"
	$(PYTHON) $(TOOLS_DIR)/mono_bmp_to_h.py assets/sprites/pellet.bmp $(SPRITES_DIR) --name Pellet --halve rare

$(SPRITES_DIR)/Item.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/STG.PTN $(TOOLS_DIR)/ptn_to_h.py $(TOOLS_DIR)/halve_image.py
	@echo "  ASSETS  creating Item.h"
	$(PYTHON) $(TOOLS_DIR)/ptn_to_h.py $(PACKFILE_DIR)/STG.PTN $(SPRITES_DIR) "Item" --halve rare:32-33

$(SPRITES_DIR)/HudIcons.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/STG.PTN $(TOOLS_DIR)/ptn_to_h.py
	@echo "  ASSETS  creating HudIcons.h"
	$(PYTHON) $(TOOLS_DIR)/ptn_to_h.py $(PACKFILE_DIR)/STG.PTN $(SPRITES_DIR) "HudIcons" -s 0 --quarter --linear
