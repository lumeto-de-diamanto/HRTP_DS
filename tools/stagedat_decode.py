#!/usr/bin/env python3
"""
Decodes a TH01 STAGE?.DAT file: the per-scene layout of cards (flip
tiles) and obstacles (bumpers, turrets, portals, walls) across all 5
stages in that scene.

Format ported directly from ReC98's real struct definitions
(th01/formats/stagedat.hpp) and the actual field-offset/bit-layout
logic in th01/main/stage/stageobj.cpp (cards_begin/obstacles_begin,
the card_bit switch statement, card_left_from/card_top_from) -- not
reverse-engineered from the raw bytes. The computed total file size
(header 22 bytes + 5 stages * 250 bytes/stage = 1272 bytes) was checked
directly against every real STAGE0.DAT..STAGE7.DAT file before writing
any parsing logic, and all 8 matched exactly, which is strong direct
evidence this struct layout is right before trusting it further.

Layout, per stagedat.hpp's real structs:

    stagedat_header_t (22 bytes):
        magic     5 bytes   "STAGE"
        id        1 byte    (per ReC98's own comment: nothing in the
                             real game ever reads this field, or the
                             two filename fields below -- beta leftovers)
        bgm_fn    8 bytes
        grf_fn    8 bytes

    Then STAGES_PER_SCENE (5) copies of stagedat_stage_t (250 bytes each):
        cards       10 rows x 5 bytes  = 50 bytes  (STAGEOBJS_Y x STAGEOBJS_X/4)
        obstacles   10 rows x 20 bytes = 200 bytes (STAGEOBJS_Y x STAGEOBJS_X)

The 5th stage of each scene (index 4, ZUN's BOSS_STAGE) is a boss fight
-- confirmed directly (see docs/PORTING_NOTES.md / this project's own
history): stageobjs_init_and_render() returns immediately without
reading anything for that stage, so whatever bytes are physically
present for it in the file are unused by the real game. This script
still decodes and prints them (for completeness / in case they contain
meaningful leftover beta data), but flags that stage as unused.

Usage:
    python3 stagedat_decode.py STAGE0.DAT
    python3 stagedat_decode.py STAGE0.DAT --json
    python3 stagedat_decode.py STAGE0.DAT --stage 2 --grid
"""

import argparse
import json
import sys
from pathlib import Path

STAGEDAT_MAGIC = b"STAGE"

STAGEOBJS_X = 20
STAGEOBJS_Y = 10
CARDS_PER_BYTE = 4
CARD_BYTES_PER_ROW = STAGEOBJS_X // CARDS_PER_BYTE  # 5

STAGES_PER_SCENE = 5
BOSS_STAGE = STAGES_PER_SCENE - 1  # 4

HEADER_SIZE = 5 + 1 + 8 + 8  # magic + id + bgm_fn + grf_fn
CARDS_SIZE = STAGEOBJS_Y * CARD_BYTES_PER_ROW    # 50
OBSTACLES_SIZE = STAGEOBJS_Y * STAGEOBJS_X        # 200
STAGE_SIZE = CARDS_SIZE + OBSTACLES_SIZE          # 250
TOTAL_SIZE = HEADER_SIZE + STAGES_PER_SCENE * STAGE_SIZE  # 1272

# obstacle_type_t, verbatim from th01/formats/stagedat.hpp.
OBSTACLE_TYPES = {
    0: "OT_NONE",
    1: "OT_BUMPER",
    2: "OT_TURRET_SLOW_1_AIMED",
    3: "OT_TURRET_SLOW_1_RANDOM_NARROW_AIMED",
    4: "OT_TURRET_SLOW_2_SPREAD_WIDE_AIMED",
    5: "OT_TURRET_SLOW_3_SPREAD_WIDE_AIMED",
    6: "OT_TURRET_SLOW_4_SPREAD_WIDE_AIMED",
    7: "OT_TURRET_SLOW_5_SPREAD_WIDE_AIMED",
    8: "OT_TURRET_QUICK_1_AIMED",
    9: "OT_TURRET_QUICK_1_RANDOM_NARROW_AIMED",
    10: "OT_TURRET_QUICK_2_SPREAD_WIDE_AIMED",
    11: "OT_TURRET_QUICK_3_SPREAD_WIDE_AIMED",
    12: "OT_TURRET_QUICK_4_SPREAD_WIDE_AIMED",
    13: "OT_TURRET_QUICK_5_SPREAD_WIDE_AIMED",
    # ZUN quirk, preserved: cards with >=1 HP are NOT stored in the
    # `cards` bitfield at all -- they're stored HERE, in the obstacle
    # array, at the same (x,y) position, abusing these three obstacle
    # type values instead. This means a card with HP can never overlap
    # a real obstacle at the same tile -- a real constraint in the
    # original data, not a decoder limitation. See stagedat.hpp's own
    # comment on OT_ACTUALLY_A_CARD.
    14: "OT_ACTUALLY_A_2FLIP_CARD",
    15: "OT_ACTUALLY_A_3FLIP_CARD",
    16: "OT_ACTUALLY_A_4FLIP_CARD",
    17: "OT_PORTAL",
    18: "OT_BAR_TOP",
    19: "OT_BAR_BOTTOM",
    20: "OT_BAR_LEFT",
    21: "OT_BAR_RIGHT",
}

# Which bit (MSB=bit3 down to LSB=bit0) of a cards byte corresponds to
# which horizontal position (0=leftmost..3=rightmost) within that
# byte's 4-tile group -- verbatim from stageobj.cpp's own switch
# statement (nth_bit=1<<3 -> card_bit=0, ... nth_bit=1<<0 -> card_bit=3),
# i.e. bits are read MSB-first, matching left-to-right tile order. Kept
# as an explicit table rather than a formula, matching the source's own
# explicit switch rather than assuming the obvious-looking pattern
# (3-bit_index) is what ZUN's code actually does -- it happens to be in
# this case, but was verified against the switch's literal cases, not
# assumed.
BIT_TO_CARD_POSITION = {3: 0, 2: 1, 1: 2, 0: 3}


def parse_stagedat(data: bytes):
    if len(data) != TOTAL_SIZE:
        print(
            f"warning: file is {len(data)} bytes, expected exactly "
            f"{TOTAL_SIZE} -- parsing may be wrong",
            file=sys.stderr,
        )

    if data[0:5] != STAGEDAT_MAGIC:
        raise ValueError(f"bad magic {data[0:5]!r}, expected {STAGEDAT_MAGIC!r}")

    header = {
        "magic": data[0:5].decode("ascii"),
        # Per ReC98's own comment on stagedat_header_t: NOTHING in the
        # real game ever reads id/bgm_fn/grf_fn -- beta leftovers.
        # Decoded here for completeness/curiosity only.
        "id_unused": data[5],
        "bgm_fn_unused": data[6:14].rstrip(b"\x00").decode("ascii", errors="replace"),
        "grf_fn_unused": data[14:22].rstrip(b"\x00").decode("ascii", errors="replace"),
    }

    stages = []
    offset = HEADER_SIZE
    for stage_index in range(STAGES_PER_SCENE):
        stage_bytes = data[offset : offset + STAGE_SIZE]
        offset += STAGE_SIZE

        cards_bytes = stage_bytes[0:CARDS_SIZE]
        obstacles_bytes = stage_bytes[CARDS_SIZE : CARDS_SIZE + OBSTACLES_SIZE]

        cards = []  # list of (x, y) tile positions with a plain (0 HP) card
        for row in range(STAGEOBJS_Y):
            for byte_in_row in range(CARD_BYTES_PER_ROW):
                byte_val = cards_bytes[row * CARD_BYTES_PER_ROW + byte_in_row]
                for bit in (3, 2, 1, 0):
                    if byte_val & (1 << bit):
                        col_in_group = BIT_TO_CARD_POSITION[bit]
                        x = byte_in_row * CARDS_PER_BYTE + col_in_group
                        cards.append({"x": x, "y": row})

        obstacles = []
        for row in range(STAGEOBJS_Y):
            for x in range(STAGEOBJS_X):
                type_id = obstacles_bytes[row * STAGEOBJS_X + x]
                if type_id == 0:
                    continue
                type_name = OBSTACLE_TYPES.get(type_id, f"UNKNOWN_{type_id}")
                obstacles.append({"x": x, "y": row, "type": type_id, "type_name": type_name})

        stages.append({
            "stage_index": stage_index,
            "is_boss_stage": stage_index == BOSS_STAGE,
            "cards": cards,
            "obstacles": obstacles,
        })

    return {"header": header, "stages": stages}


def print_grid(stage: dict):
    """
    Prints an ASCII-art top-down view of one stage's tile grid: cards as
    'C' (or a specific digit for HP-bearing cards, per the
    OT_ACTUALLY_A_*FLIP_CARD quirk), obstacles as a short code, '.' for
    empty tiles.
    """
    grid = [["." for _ in range(STAGEOBJS_X)] for _ in range(STAGEOBJS_Y)]

    for c in stage["cards"]:
        grid[c["y"]][c["x"]] = "C"

    obstacle_short = {
        "OT_BUMPER": "B",
        "OT_PORTAL": "P",
        "OT_BAR_TOP": "^",
        "OT_BAR_BOTTOM": "v",
        "OT_BAR_LEFT": "<",
        "OT_BAR_RIGHT": ">",
        "OT_ACTUALLY_A_2FLIP_CARD": "2",
        "OT_ACTUALLY_A_3FLIP_CARD": "3",
        "OT_ACTUALLY_A_4FLIP_CARD": "4",
    }
    for o in stage["obstacles"]:
        name = o["type_name"]
        if name.startswith("OT_TURRET"):
            short = "T"
        else:
            short = obstacle_short.get(name, "?")
        grid[o["y"]][o["x"]] = short

    print(f"Stage {stage['stage_index']}"
          + (" (BOSS STAGE -- unused by the real game)" if stage["is_boss_stage"] else "")
          + ":")
    for row in grid:
        print("  " + "".join(row))
    print("  Legend: C=card B=bumper T=turret P=portal ^v<>=bar 2/3/4=HP card . =empty")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dat_file", type=Path)
    parser.add_argument("--json", action="store_true", help="Print the full decoded structure as JSON.")
    parser.add_argument("--grid", action="store_true", help="Print an ASCII-art grid for each stage (default if --json not given).")
    parser.add_argument("--stage", type=int, default=None, help="Only show this stage index (0-4).")
    args = parser.parse_args()

    data = args.dat_file.read_bytes()
    result = parse_stagedat(data)

    if args.json:
        print(json.dumps(result, indent=2))
        return

    print(f"Header: {result['header']}")
    print()
    for stage in result["stages"]:
        if args.stage is not None and stage["stage_index"] != args.stage:
            continue
        print_grid(stage)
        print(f"  {len(stage['cards'])} cards, {len(stage['obstacles'])} obstacles")
        print()


if __name__ == "__main__":
    main()
