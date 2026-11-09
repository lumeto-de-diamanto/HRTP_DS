#!/usr/bin/env python3
"""Load caller-supplied YM2608 rhythm WAVs as native PCM8 one-shots.

No rhythm audio is bundled or fetched. A source/permission declaration is
required, retained as provenance, and is not independently authenticated.
"""
import hashlib
import json
import math
from pathlib import Path
import struct
import wave

DRUMS = ((36, "BD"), (38, "SD"), (46, "TOP"), (42, "HH"), (48, "TOM"), (37, "RIM"))


def load_rhythm(directory):
    directory = Path(directory)
    samples = {}
    for key, name in DRUMS:
        filename = f"2608_{name}.WAV"
        matches = [p for p in directory.iterdir() if p.is_file() and p.name.casefold() == filename.casefold()]
        if len(matches) != 1:
            raise ValueError(f"{filename}: expected exactly one case-insensitive filename match")
        path = matches[0]
        original = path.read_bytes()
        with wave.open(str(path), "rb") as f:
            channels, width, rate, count = f.getnchannels(), f.getsampwidth(), f.getframerate(), f.getnframes()
            if channels not in (1, 2) or width not in (1, 2) or f.getcomptype() != "NONE":
                raise ValueError(f"{filename}: expected uncompressed mono/stereo PCM8/PCM16")
            if not 4000 <= rate <= 192000 or not 16 <= count <= rate * 8:
                raise ValueError(f"{filename}: invalid rate or not a short percussion one-shot")
            raw = f.readframes(count)
        if len(raw) != count * channels * width:
            raise ValueError(f"{filename}: truncated WAV")
        values = list(struct.unpack(f"<{len(raw) // 2}h", raw)) if width == 2 else [(v - 128) * 256 for v in raw]
        mono = [sum(values[i:i + channels]) / channels for i in range(0, len(values), channels)]
        output_rate = min(rate, 32768)
        output_count = math.floor(count * output_rate / rate)
        pcm = bytearray()
        # Average decimation windows; one common PCM16->PCM8 scale for all
        # six voices, never per-drum normalization or a guessed tuning shift.
        for i in range(output_count):
            a = i * count // output_count
            b = max(a + 1, (i + 1) * count // output_count)
            value = max(-128, min(127, round(sum(mono[a:b]) / (b - a) / 256)))
            pcm.append(value & 255)
        if not any(pcm):
            raise ValueError(f"{filename}: silent after PCM8 conversion")
        pcm.extend(bytes(max(0, 16 - len(pcm))))
        pcm.extend(bytes((-len(pcm)) & 3))
        samples[key] = dict(name=name, pcm=bytes(pcm), rate=output_rate,
            source_file=path.name, source_sha256=hashlib.sha256(original).hexdigest(),
            pcm_sha256=hashlib.sha256(pcm).hexdigest())
    return samples

