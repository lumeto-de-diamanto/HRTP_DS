#pragma once

#include <nds.h>

// Returns the 16 rows of a compact PC-98 fullwidth/JIS glyph, or null if the
// glyph was not selected by tools/font16x16.py.
const u16 *font16x16_find(u16 jis);

// JIS codes used by the stage entrance and fullwidth numeric renderer.
static const u16 JIS_FULLWIDTH_DIGIT_0 = 0x2330;
static const u16 JIS_TOUHOU_REIIDEN[6] = {
    0x456C, // 東
    0x4A7D, // 方
    0x217A, // ★
    0x704D, // 靈
    0x305B, // 異
    0x4541, // 伝
};
