#pragma once

#include "nds/ndstypes.h"

class fixed {
    int32 raw = 0;

    public:
    static constexpr fixed from_raw(int32 r) {
        fixed f;
        f.raw = r;
        return f;
    }

    constexpr fixed() {}
    constexpr fixed(const int x) {raw = x << 16;} 
    constexpr fixed(const float x) {raw = int(x * (1 << 16));} 
    constexpr fixed(const double x) {raw = int(x * (1 << 16));}

    operator int() const {
        return (raw + 0x8000) >> 16;
        //return raw >> 16;
    }

    // Explicit source-style floating-point -> integer conversion. Keep the
    // established nearest-pixel operator int() semantics for other callers.
    constexpr int trunc_to_int() const { return raw / 65536; }

    constexpr fixed operator-() const { return from_raw(-raw); }

    constexpr fixed operator+(fixed o) const { return from_raw(raw + o.raw); }
    constexpr fixed operator-(fixed o) const { return from_raw(raw - o.raw); }
    constexpr fixed operator*(fixed o) const {
        // Widen to 64-bit to avoid overflow during the multiply
        int64 tmp = static_cast<int64>(raw) * static_cast<int64>(o.raw);
        return from_raw(static_cast<int32>(tmp >> 16));
    }
    constexpr fixed operator/(fixed o) const {
        int64 tmp = (static_cast<int64>(raw) << 16);
        tmp = tmp / o.raw;
        return from_raw(static_cast<int32>(tmp));
    }
    constexpr bool operator>(fixed o) const { return raw > o.raw; }
    constexpr bool operator<(fixed o) const { return raw < o.raw; }

    constexpr fixed operator+(int o) const { return from_raw(raw + (o << 16)); }
    constexpr fixed operator-(int o) const { return from_raw(raw - (o << 16)); }
    constexpr fixed operator/(int o) const {
        return from_raw(raw / o);
    }

    constexpr fixed operator/(double o) const {
        return *this / fixed(o);
    }
};
