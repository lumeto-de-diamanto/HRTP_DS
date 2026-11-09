"""Shared chip-domain mix profile; the native and tracker paths apply it once."""
import hashlib
import json
from pathlib import Path


def load_mix_profile(path=Path("audio/opna_mix.json")):
    raw = Path(path).read_bytes()
    profile = json.loads(raw)
    value = profile.get("ssg_global_volume")
    if profile.get("version") != 1 or profile.get("unity_global_volume") != 64 or type(value) is not int or not 0 <= value <= 64:
        raise ValueError("invalid OPNA mix profile: expected SSG global volume 0..64, unity 64, version 1")
    return dict(path=str(path), sha256=hashlib.sha256(raw).hexdigest(),
                ssg_global_volume=value, unity_global_volume=64)
