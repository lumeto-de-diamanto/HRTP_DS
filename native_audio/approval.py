"""Storage-independent fingerprint of the user-approved native arrangement.

Sample indices and offsets may change when unused samples are removed. Audible
PCM, tuning, loops, envelopes, event controls, timing and gains may not.
"""
import hashlib
import json
import struct


def fingerprint(blob):
    h = struct.unpack_from('<8s14I', blob)
    assert h[0] == b'HRNSQ01\0' and h[1] == 1 and h[2] == len(blob)
    samples = []
    for i in range(h[3]):
        p = h[6] + i * 64
        offset, size = struct.unpack_from('<II', blob, p)
        samples.append(hashlib.sha256(blob[p + 4:p + 64] + blob[offset:offset + size]).hexdigest())
    tracks = []
    attacks = 0
    for slot in range(h[4]):
        source, _, first, count, loop_index, loop_tick, end_tick, _ = struct.unpack_from('<8I', blob, h[7] + slot * 32)
        events = []
        for i in range(count):
            tick, op, s, a, b, c = struct.unpack_from('<IBBHII', blob, h[8] + (first + i) * 16)
            sample = None
            if op == 1:
                sample = samples[b & 65535]
                b >>= 16
                attacks += 1
            events.append([tick, op, s, a, b, c, sample])
        tracks.append([source, slot, loop_index, loop_tick, end_tick, events])
    data = dict(timing=h[10:14], gain=blob[h[14]:h[14] + 288].hex(), tracks=tracks)
    return dict(sha256=hashlib.sha256(json.dumps(data, separators=(',', ':')).encode()).hexdigest(),
                first_pass_attacks=attacks)
