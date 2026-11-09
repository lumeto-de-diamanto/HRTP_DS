TOOLS_DIR       := tools
ASSETS_DIR      := assets

HDI_DUMP_DIR     := $(ASSETS_DIR)/hdi_dump
PACKFILE_DIR     := $(ASSETS_DIR)/packfile

PROCESSED_DIR   := $(ASSETS_DIR)/processed
SPRITES_DIR     := $(PROCESSED_DIR)/sprites
BACKGROUND_DIR  := $(PROCESSED_DIR)/backgrounds

NITROFS_DIR     := nitrofs

FONT_DIR        := $(PROCESSED_DIR)/fonts
SHIFTJIS_DIR    := ReC98-anniversary/th01/shiftjis
SHIFTJIS_HEADERS := $(wildcard $(SHIFTJIS_DIR)/*.hpp)

STAGEDAT_DIR    := $(PROCESSED_DIR)/stagedat
SCENES          := 0 1 2 3 4 5 6 7
STAGEDAT_HEADERS := $(addprefix $(STAGEDAT_DIR)/stagedat,$(addsuffix .h,$(SCENES)))

GAME_HDI        := $(ASSETS_DIR)/th01.hdi
PACKFILE        := $(HDI_DUMP_DIR)/東方靈異.伝

PYTHON          := python3

PACKFILE_STAMP   := $(PACKFILE_DIR)/.stamp
HDI_DUMP_STAMP   := $(HDI_DUMP_DIR)/.stamp

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

assets-clean:
	@echo "  ASSETS clean"
	$(V)$(RM) $(PROCESSED_DIR) $(HDI_DUMP_DIR) $(PACKFILE_DIR) $(NITROFS_DIR)

$(NITROFS_DIR)/konngara_intro.bin: $(HDI_DUMP_DIR)/BOSS7_D1.GRP $(HDI_DUMP_DIR)/BOSS8_A1.GRP $(wildcard $(HDI_DUMP_DIR)/BOSS8_D[1-4].GRP) $(TOOLS_DIR)/konngara_to_bin.py $(TOOLS_DIR)/grp_to_bin.py
	$(PYTHON) $(TOOLS_DIR)/konngara_to_bin.py $(HDI_DUMP_DIR) $@

$(SPRITES_DIR)/HudIconsSmall.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/STG.PTN $(TOOLS_DIR)/ptn_to_h.py $(TOOLS_DIR)/halve_image.py
	$(PYTHON) $(TOOLS_DIR)/ptn_to_h.py $(PACKFILE_DIR)/STG.PTN $(SPRITES_DIR) HudIconsSmall -s 0 --quarter --halve dither --linear

$(SPRITES_DIR)/SingyokuSphere.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/BOSS1.BOS $(TOOLS_DIR)/bos_to_h.py $(TOOLS_DIR)/halve_image.py
	$(PYTHON) $(TOOLS_DIR)/bos_to_h.py $(PACKFILE_DIR)/BOSS1.BOS $(SPRITES_DIR) SingyokuSphere --halve dither

$(SPRITES_DIR)/SingyokuFlash.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/BOSS1_2.BOS $(TOOLS_DIR)/bos_to_h.py $(TOOLS_DIR)/halve_image.py
	$(PYTHON) $(TOOLS_DIR)/bos_to_h.py $(PACKFILE_DIR)/BOSS1_2.BOS $(SPRITES_DIR) SingyokuFlash --halve dither

$(SPRITES_DIR)/SingyokuPerson.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/BOSS1_3.BOS $(TOOLS_DIR)/bos_to_h.py $(TOOLS_DIR)/halve_image.py
	$(PYTHON) $(TOOLS_DIR)/bos_to_h.py $(PACKFILE_DIR)/BOSS1_3.BOS $(SPRITES_DIR) SingyokuPerson --halve dither

$(BACKGROUND_DIR)/boss1.h: $(HDI_DUMP_DIR)/BOSS1.GRP $(TOOLS_DIR)/grp_to_bin.py $(TOOLS_DIR)/halve_image.py
	$(PYTHON) $(TOOLS_DIR)/grp_to_bin.py $(HDI_DUMP_DIR)/BOSS1.GRP $(BACKGROUND_DIR) $(NITROFS_DIR) boss1 --halve topleft2

$(PACKFILE_STAMP): $(HDI_DUMP_STAMP) $(PACKFILE) $(TOOLS_DIR)/extract_pf.py
	@echo "  ASSETS  extracting $(PACKFILE)"
	@mkdir -p $(PACKFILE_DIR)
	$(PYTHON) $(TOOLS_DIR)/extract_pf.py "$(PACKFILE)" $(PACKFILE_DIR)
	@touch $@

$(HDI_DUMP_STAMP): $(GAME_HDI) $(TOOLS_DIR)/hdi_extract.py
	@echo "  ASSETS  extracting $(GAME_HDI)"
	@mkdir -p $(HDI_DUMP_DIR)
	$(PYTHON) $(TOOLS_DIR)/hdi_extract.py "$(GAME_HDI)" -o $(HDI_DUMP_DIR)
	@touch $@

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
$(BACKGROUND_DIR)/st%.h: $(PACKFILE_STAMP) $(HDI_DUMP_DIR)/ST%.GRP $(TOOLS_DIR)/grp_to_bin.py $(TOOLS_DIR)/halve_image.py
	@echo "  ASSETS  creating st$*.h and st$*.bin"
	$(PYTHON) $(TOOLS_DIR)/grp_to_bin.py $(HDI_DUMP_DIR)/ST$*.GRP $(BACKGROUND_DIR) $(NITROFS_DIR) "st$*" --halve topleft2
	@touch $@
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

$(FONT_DIR)/font16x16.h: assets/font.bmp $(TOOLS_DIR)/font16x16.py $(SHIFTJIS_HEADERS)
	@echo "  ASSETS  creating compact font16x16.h"
	$(PYTHON) $(TOOLS_DIR)/font16x16.py assets/font.bmp $(SHIFTJIS_DIR) $(FONT_DIR)

$(SPRITES_DIR)/Pellet.h: assets/sprites/pellet.bmp $(TOOLS_DIR)/mono_bmp_to_h.py $(TOOLS_DIR)/halve_image.py
	@echo "  ASSETS  creating Pellet.h"
	$(PYTHON) $(TOOLS_DIR)/mono_bmp_to_h.py assets/sprites/pellet.bmp $(SPRITES_DIR) --name Pellet --halve rare

$(SPRITES_DIR)/Item.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/STG.PTN $(TOOLS_DIR)/ptn_to_h.py $(TOOLS_DIR)/halve_image.py
	@echo "  ASSETS  creating Item.h"
	$(PYTHON) $(TOOLS_DIR)/ptn_to_h.py $(PACKFILE_DIR)/STG.PTN $(SPRITES_DIR) "Item" --halve rare:32-33

$(SPRITES_DIR)/HudIcons.h: $(PACKFILE_STAMP) $(PACKFILE_DIR)/STG.PTN $(TOOLS_DIR)/ptn_to_h.py
	@echo "  ASSETS  creating HudIcons.h"
	$(PYTHON) $(TOOLS_DIR)/ptn_to_h.py $(PACKFILE_DIR)/STG.PTN $(SPRITES_DIR) "HudIcons" -s 0 --quarter --linear
