#!/usr/bin/env python3
"""Validate native-only game/listening ROMs against source identities and approval."""
import hashlib
import json
from pathlib import Path
import struct

from audio_mix import load_mix_profile
from native_audio.approval import fingerprint
from nds_files import nitro_files


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def check_release():
    manifest = json.loads(Path('build/native_soundtrack/manifest.json').read_text())
    canonical = json.loads(Path('audio/tracks.json').read_text())['tracks']
    approved = json.loads(Path('native_audio/testdata/approved_soundtrack.json').read_text())
    expected = {t['id'] for t in canonical if t.get('playable', True)}
    if not manifest.get('complete') or {t['id'] for t in manifest['tracks']} != expected:
        raise ValueError('incomplete native soundtrack')
    if manifest['mix'] != load_mix_profile():
        raise ValueError('stale native mix profile')
    game = nitro_files(Path('hrtp_ds.nds').read_bytes())
    listening = nitro_files(Path('hrtp_native_audio.nds').read_bytes())
    if {p for p in game if p.startswith('audio/native/') and p.endswith('.nsq')} != {
            f'audio/native/{name}.nsq' for name in expected}:
        raise ValueError('unexpected gameplay native song set')
    if set(listening) != {f'{name}/song.nsq' for name in expected}:
        raise ValueError('listening ROM must contain only native sequences')
    if any(p.endswith(('.it', '.mod', 'soundbank.bin', '.wav', '.mp3', '.f32')) for p in game):
        raise ValueError('obsolete playback/analysis files in gameplay NitroFS')
    for track in manifest['tracks']:
        name = track['id']
        if digest(track['source']) != track['source_sha256'] or track['mix'] != manifest['mix']:
            raise ValueError(f'stale source/profile: {name}')
        if not track.get('build_inputs') or any(digest(p) != h for p, h in track['build_inputs'].items()):
            raise ValueError(f'stale compiler/renderer: {name}')
        rhythm_dir = Path(track['rhythm']['directory'])
        # The importer records source/PCM identities, not a declaration file.
        for sample in track['rhythm']['samples']:
            if digest(rhythm_dir / sample['source_file']) != sample['source_sha256']:
                raise ValueError(f'stale rhythm input: {name}')
        for alias in track['aliases']:
            if digest(Path(track['source']).parent / alias) != track['source_sha256']:
                raise ValueError(f'nonidentical alias: {alias}')
        blob = Path(track['sequence_path']).read_bytes()
        if hashlib.sha256(blob).hexdigest() != track['sequence_sha256']:
            raise ValueError(f'stale sequence: {name}')
        if game.get(f'audio/native/{name}.nsq') != blob or listening.get(f'{name}/song.nsq') != blob:
            raise ValueError(f'ROM contains missing/stale native data: {name}')
        reference = approved['tracks'][name]
        if fingerprint(blob) != {k: v for k, v in reference.items() if k != 'source_sha256'}:
            raise ValueError(f'approved audible playback changed: {name}')
        h = struct.unpack_from('<8s14I', blob)
        if h[3] != len(track['samples']):
            raise ValueError('sample descriptor count mismatch')
        for i, sample in enumerate(track['samples']):
            offset, size = struct.unpack_from('<II', blob, h[6] + i * 64)
            if hashlib.sha256(blob[offset:offset + size]).hexdigest() != sample['pcm_sha256']:
                raise ValueError(f'sample hash mismatch: {name}/{i}')
        if len(blob) >= 1024 * 1024:
            raise ValueError(f'native per-song RAM budget exceeded: {name}')
    for path in ('build/arm7.map', 'build/arm9.map', 'build/native_audio/arm7.map', 'build/native_audio/arm9.map'):
        if 'libmm' in Path(path).read_text():
            raise ValueError(f'obsolete mixer library linked: {path}')
    check_sfx(game, approved)
    print('Release checks passed: 16 native songs, 17 SFX, approved playback, exact NitroFS paths and source hashes')


def check_sfx(game, approved):
    manifest = json.loads(Path('build/native_sfx/manifest.json').read_text())
    identity = manifest['identity']
    if not manifest.get('complete') or {e['id'] for e in manifest['effects']} != set(range(1, 18)):
        raise ValueError('incomplete SFX set')
    if digest(manifest['source']) != identity['source_sha256'] or digest('assets/hdi_dump/MDRV98.COM') != identity['driver_sha256']:
        raise ValueError('stale SFX source/driver')
    if identity['mix'] != load_mix_profile() or any(digest(p) != h for p, h in identity['renderer_sources'].items()):
        raise ValueError('stale SFX renderer/profile')
    bank = Path('nitrofs/audio/sfx.bin').read_bytes()
    if hashlib.sha256(bank).hexdigest() != manifest['bank_sha256'] or manifest['bank_sha256'] != approved['sfx_sha256']:
        raise ValueError('approved SFX bank changed')
    if game.get('audio/sfx.bin') != bank:
        raise ValueError('gameplay ROM has stale/missing SFX')
    magic, version, size, count, table, period, gain = struct.unpack_from('<8s6I', bank)
    if (magic, version, size, count, table, period, gain) != (b'HRNSFX1\0', 1, len(bank), 17, 32, 760, 46079):
        raise ValueError('SFX header/gain mismatch')
    for effect in manifest['effects']:
        offset, length = struct.unpack_from('<II', bank, table + 8 * (effect['id'] - 1))
        if offset % 4 or length % 4 or length < 16 or offset + length > len(bank):
            raise ValueError('invalid SFX bounds')
        if hashlib.sha256(bank[offset:offset + length]).hexdigest() != effect['pcm_sha256']:
            raise ValueError('SFX sample identity mismatch')
    if len(bank) >= 1024 * 1024:
        raise ValueError('SFX bank exceeds RAM budget')


if __name__ == '__main__':
    check_release()
