#include "font16x16.hpp"

#include "fonts/font16x16.h"

const u16 *font16x16_find(u16 jis)
{
    int lo = 0;
    int hi = FONT16X16_GLYPH_COUNT - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) / 2);
        u16 code = font16x16_jis[mid];
        if (code == jis)
            return font16x16_rows[mid];
        if (code < jis)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}
