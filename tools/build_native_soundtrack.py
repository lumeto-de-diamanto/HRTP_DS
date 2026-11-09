#!/usr/bin/env python3
"""Compile the complete native soundtrack; stage sequence/PCM assets for the game.

Events and instrument PCM are built directly from MDT and local rhythm inputs.
"""
import argparse
import json
from pathlib import Path
from types import SimpleNamespace

from audio_mix import load_mix_profile
from build_native_audio import build_track, sha


def write_changed(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_bytes() != data:
        path.write_bytes(data)


def build(args):
    specs = json.loads(Path("audio/tracks.json").read_text())["tracks"]
    reports = []
    for spec in specs:
        source = Path("assets") / "hdi_dump" / spec["source"]
        for alias in spec.get("aliases", []):
            if (Path("assets") / "hdi_dump" / alias).read_bytes() != source.read_bytes():
                raise ValueError(f"nonidentical alias: {alias}")
        if not spec.get("playable", True):
            continue
        track_out = args.out / "tracks" / spec["id"]
        report = build_track(SimpleNamespace(out=track_out, track=spec["id"],
            tools_dir=args.tools_dir, ymfm=args.ymfm, cxx=args.cxx, rhythm_dir=args.rhythm_dir,
            approximate_portamento=True))
        target = args.out / "nitrofs" / spec["id"]
        target.mkdir(parents=True, exist_ok=True)
        for key, name in (("sequence_path", "song.nsq"),):
            write_changed(target / name, Path(report[key]).read_bytes())
        report.update(id=spec["id"], aliases=spec.get("aliases", []), role=spec["role"])
        reports.append(report)
        if args.game_dir:
            write_changed(args.game_dir / f"{spec['id']}.nsq", Path(report["sequence_path"]).read_bytes())
    manifest = dict(version=1, complete=True, mix=load_mix_profile(),
        tracks=reports, silent=[t for t in specs if not t.get("playable", True)])
    write_changed(args.out / "manifest.json", (json.dumps(manifest, indent=2) + "\n").encode())
    header = ['// Generated from audio/tracks.json; do not edit.', '#pragma once',
        'enum NativeTrackId {']
    header += [f'    NATIVE_{r["id"].upper()} = {i},' for i, r in enumerate(reports)]
    header += ['    NATIVE_TRACK_COUNT', '};',
        'struct NativeTrackInfo { const char *id; const char *role; unsigned slides; };',
        'static constexpr NativeTrackInfo nativeTracks[] = {']
    header += [f'    {{{json.dumps(r["id"])}, {json.dumps(r["role"])}, {len(r["approximations"])}}},'
               for r in reports]
    header += ['};', '']
    write_changed(args.out / "native_catalog.h", '\n'.join(header).encode())
    if args.game_dir:
        write_changed(args.game_dir / "manifest.json", (json.dumps(dict(version=1,
            mix=load_mix_profile(), tracks=[dict(id=r["id"], sha256=r["sequence_sha256"],
                bytes=Path(r["sequence_path"]).stat().st_size) for r in reports]), indent=2) + "\n").encode())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=Path('build/native_soundtrack'))
    parser.add_argument('--game-dir', type=Path, default=Path('nitrofs/audio/native'))
    parser.add_argument('--tools-dir', type=Path, default=Path('tools/deps/mdt-tools'))
    parser.add_argument('--ymfm', type=Path, default=Path('tools/deps/ymfm'))
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--rhythm-dir', type=Path, default=Path('assets'))
    build(parser.parse_args())
