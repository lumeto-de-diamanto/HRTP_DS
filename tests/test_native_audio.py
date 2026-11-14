#!/usr/bin/env python3
"""Native compiler regressions and frozen approved-playback fingerprints."""
from collections import Counter
from fractions import Fraction
import json
from pathlib import Path
import struct
import sys
import types
import unittest

import build_native_audio as native
from audio_mix import load_mix_profile
from native_audio.approval import fingerprint


def selected_manifest():
    path = Path("build/native_audio/manifest.json")
    if not path.exists():
        path = Path("build/native_soundtrack/tracks/kami2/manifest.json")
    return json.loads(path.read_text())


def native_onsets(blob, end=None):
    h = struct.unpack_from("<8s14I", blob)
    end = h[13] if end is None else end
    tracks, events = h[7], h[8]
    result = []
    for slot in range(15):
        source, _, first, count, loop_index, loop_tick, end_tick, _ = struct.unpack_from("<8I", blob, tracks + slot * 32)
        stream = [struct.unpack_from("<IBBHII", blob, events + (first + i) * 16) for i in range(count)]
        epoch, index = 0, 0
        while True:
            for tick, op, _, a, b, c in stream[index:]:
                if tick + epoch >= end:
                    continue
                if op == native.NOTE:
                    result.append((tick + epoch, slot, a, c & 65535, b & 65535))
            if loop_index == native.NO_LOOP or end_tick + epoch >= end:
                break
            epoch += end_tick - loop_tick
            index = loop_index
    return result


class NativeCompilerTests(unittest.TestCase):
    def channel(self, events, source=0x80):
        return native.compile_channel(types.SimpleNamespace(id=source, events=events))[0]

    def test_native_lengths_never_round(self):
        self.assertEqual(native.clocks("64"), 3)
        self.assertEqual(native.clocks("%1"), 1)
        self.assertEqual(native.clocks("8."), 36)
        with self.assertRaisesRegex(ValueError, "non-native"):
            native.clocks("128")

    def test_tied_64ths_keep_one_attack(self):
        track = self.channel([["O", 4], ["[", 2], ["g64"], ["&"], ["]"], ["g64"]])
        notes = [e for e in track["events"] if e["op"] == native.NOTE]
        self.assertEqual(len(notes), 1)
        self.assertEqual((notes[0]["tick"], notes[0]["gate"], track["endTick"]), (0, 9, 9))

    def test_octave_restored_on_structural_loop(self):
        track = self.channel([["O", 4], ["[", 2], [">c16"], ["]"]])
        self.assertEqual([e["key"] for e in track["events"] if e["op"] == native.NOTE], [72, 72])

    def test_native_control_bytes_survive_without_midi(self):
        track = self.channel([["@V", 119], ["S", 200, 5, 7, 30], ["P", 3]])
        self.assertEqual(track["events"][0]["a"], 119)
        self.assertEqual(track["events"][1]["b"], 200 | 5 << 8 | 7 << 16 | 30 << 24)

    def test_patch_selection_does_not_transpose_notes(self):
        track = self.channel([["O", 4], ["@", 6], ["c16"], ["@", 7], ["c16"]])
        notes = [e for e in track["events"] if e["op"] == native.NOTE]
        self.assertEqual([e["patch"] for e in notes], [6, 7])
        self.assertEqual([e["key"] for e in notes], [60, 60])

    def test_independent_loop_records_intro_and_trailing_rest(self):
        track = self.channel([["O", 4], ["r8"], ["\\"], ["c16"], ["r2"]])
        self.assertEqual(track["loopTick"], 24)
        self.assertEqual(track["endTick"], 132)
        self.assertEqual(track["loopIndex"], 0)

    def test_unknown_commands_fail_loudly(self):
        for event in (["U", 0], ["(4c,5c)4"], ["Y", 1, 2], ["SH", 1, 0, 0, 0]):
            with self.subTest(event=event), self.assertRaises(ValueError):
                self.channel([event])

    def test_detune_survives_signed_and_is_applied_in_chip_domain(self):
        track = self.channel([["^", -5], ["O", 4], ["c16"]])
        self.assertEqual(track["events"][0]["a"], 65531)
        self.assertEqual(track["events"][1]["detune"], -5)
        self.assertAlmostEqual(native.detune_ratio(60, 5, False), (native.fm_fnum(60) + 5) / native.fm_fnum(60))
        self.assertAlmostEqual(native.detune_ratio(60, 5, True), 3816 / 3811)

    def test_tempo_changes_keep_source_tick_and_loop_position(self):
        track = self.channel([["t", 90], ["r4"], ["\\"], ["t", 77], ["r8"], ["t", 90]])
        self.assertEqual([(e["tick"], e["a"]) for e in track["events"]], [(0, 90), (48, 77), (72, 90)])

    def test_rhythm_tempo_executes_once_not_six_times(self):
        tracks = native.compile_channel(types.SimpleNamespace(id=0x10, events=[["t", 60], ["c4"]]))
        self.assertEqual([(t['slot'], e['a']) for t in tracks for e in t['events'] if e['op'] == native.TEMPO], [(9, 60)])

    def test_fractional_gate_uses_integer_counter_and_q0_is_ungated(self):
        track = self.channel([["Q", 7], ["c32"], ["Q", 0], ["c32"]])
        self.assertEqual([e['gate'] for e in track['events']], [5, native.NO_GATE])

    def test_relative_volume_keeps_signed_runtime_delta(self):
        for source in (0x80, 0x41):
            track = self.channel([["V", 12], ["@V-", 2], ["@V+", 1]], source)
            self.assertEqual([(e['op'], e['a']) for e in track['events'][1:]],
                             [(native.REL_VOLUME, 65534), (native.REL_VOLUME, 1)])

    def test_macro_calls_share_rhythm_levels_and_tick_stream(self):
        macro = types.SimpleNamespace(events=[["@", 9], ["@V", 3, 16], ["c16"]])
        tracks = native.compile_channel(types.SimpleNamespace(id=0x10,
            events=[["U", 0], ["U", 0], ["c16"]]), macros={0: macro}, original_rhythm=True)
        self.assertEqual([e['tick'] for e in tracks[0]['events'] if e['op'] == native.NOTE], [0, 12, 24])
        self.assertEqual([e['b'] for e in tracks[3]['events'] if e['op'] == native.RHYTHM_LEVEL], [16, 16])

    def test_recursive_missing_or_escaping_macros_fail(self):
        for events, macros in (([["U", 0]], {}),
                ([["U", 0]], {0: types.SimpleNamespace(events=[["U", 0]])}),
                ([["U", 0]], {0: types.SimpleNamespace(events=[["\\"]])})):
            with self.assertRaises(ValueError):
                native.compile_channel(types.SimpleNamespace(id=0x10, events=events), macros=macros)

    def test_identical_pcm_is_shared_without_merging_sample_metadata(self):
        pcm = bytes([1, 2, 3, 4]) * 4
        sample = dict(pcm=pcm, loopStart=0, loopEnd=16, c5=8000, name='synthetic', envelope=[255] * 6)
        blob, _, _ = native.pack_song(types.SimpleNamespace(channels=[], macros={}),
            [sample, sample | dict(c5=9000)], [], {})
        a = struct.unpack_from('<6I', blob, 64)
        b = struct.unpack_from('<6I', blob, 128)
        self.assertEqual(a[0], b[0])
        self.assertEqual(blob[a[0]:a[0] + 16], pcm)
        self.assertNotEqual(a[4], b[4])

    def test_approval_fingerprint_rejects_pcm_gain_and_event_mutations(self):
        manifest = selected_manifest()
        blob = Path(manifest['sequence_path']).read_bytes()
        expected = fingerprint(blob)
        h = struct.unpack_from('<8s14I', blob)
        event_count = h[5]
        sample_index = next(struct.unpack_from('<IBBHII', blob, h[8] + i * 16)[4] & 65535
                            for i in range(event_count) if blob[h[8] + i * 16 + 4] == native.NOTE)
        pcm_offset = struct.unpack_from('<I', blob, h[6] + sample_index * 64)[0]
        for offset in (pcm_offset, h[14] + 143 * 2, h[8]):
            modified = bytearray(blob); modified[offset] ^= 1
            self.assertNotEqual(fingerprint(modified), expected)

    def test_portamento_is_opt_in_one_attack_with_explicit_diagnostic(self):
        diagnostic = []
        track = native.compile_channel(types.SimpleNamespace(id=0x80,
            events=[["@^", 5], ["(6d,6c)16"], ["&"], ["c16"]]),
            allow_portamento=True, diagnostics=diagnostic)[0]
        notes = [e for e in track["events"] if e["op"] == native.NOTE]
        self.assertEqual(len(notes), 1)
        self.assertEqual((notes[0]["key"], notes[0]["glide_target"], notes[0]["gate"]), (91, 89, 24))
        self.assertEqual(len(diagnostic), 1)

    def test_rhythm_mask_preserves_separate_instruments(self):
        tracks = native.compile_channel(types.SimpleNamespace(id=0x10,
            events=[["V", 60, 31, 30, 24, 22, 24, 24], ["@", 9], ["c32"]]))
        self.assertEqual([t["slot"] for t in tracks if any(e["op"] == native.NOTE for e in t["events"])], [9, 12])
        self.assertEqual(tracks[0]["events"][0]["a"], 60 * 31)

    def test_imported_rhythm_keeps_raw_levels_and_natural_decay(self):
        tracks = native.compile_channel(types.SimpleNamespace(id=0x10,
            events=[["V", 60, 31, 30, 24, 22, 24, 24], ["@", 1], ["c32"]]),
            original_rhythm=True)
        first = tracks[0]["events"]
        self.assertEqual((first[0]["op"], first[0]["a"], first[0]["b"]), (native.RHYTHM_LEVEL, 60, 31))
        self.assertEqual(first[1]["gate"], native.NO_GATE)

    def test_ssg_raw_volume_and_octave(self):
        track = self.channel([["V", 13], ["O", 3], ["@^", 3], ["c16"]], 0x40)
        self.assertEqual(track["slot"], 6)
        self.assertEqual(track["events"][0]["a"], 13)
        self.assertEqual(track["events"][1]["key"], 63)

    def check_onsets(self, manifest):
        blob = Path(manifest["sequence_path"]).read_bytes()
        expected = json.loads(Path('native_audio/testdata/approved_soundtrack.json').read_text())['tracks'][manifest['track']]
        self.assertEqual(manifest['source_sha256'], expected['source_sha256'])
        self.assertEqual(fingerprint(blob), {k: v for k, v in expected.items() if k != 'source_sha256'})

    def test_complete_soundtrack_onsets(self):
        path = Path("build/native_soundtrack/manifest.json")
        if not path.exists():
            self.skipTest("build native-soundtrack first")
        manifest = json.loads(path.read_text())
        self.assertEqual(len(manifest['tracks']), 16)
        for track in manifest['tracks']:
            with self.subTest(track=track['id']):
                self.check_onsets(track)

    def test_selected_track_has_no_silent_sample_payloads(self):
        manifest = selected_manifest()
        blob = Path(manifest["sequence_path"]).read_bytes()
        header = struct.unpack_from("<8s14I", blob)
        for i in range(header[3]):
            offset, length = struct.unpack_from("<II", blob, header[6] + i * 64)
            self.assertTrue(any(blob[offset:offset + length]), f"silent sample {i}")

    def test_ssg_mix_applied_once_without_changing_dac_curve_or_fm_gain(self):
        manifest = selected_manifest()
        profile = load_mix_profile()
        self.assertEqual(manifest["mix"], profile)
        blob = Path(manifest["sequence_path"]).read_bytes()
        header = struct.unpack_from("<8s14I", blob)
        gains = struct.unpack_from("<144H", blob, header[14])
        self.assertEqual(gains[127], 65535)  # FM unaffected
        self.assertEqual(gains[143], round(65535 * profile["ssg_global_volume"] / 64))
        self.assertEqual(gains[128], 0)
        dac = (0, 141, 222, 306, 441, 585, 836, 1112, 1595, 2146, 3081, 4135, 6006, 8155, 11976, 16382)
        for i, value in enumerate(dac):
            self.assertEqual(gains[128 + i], round(65535 * value / dac[-1] * profile["ssg_global_volume"] / 64))
        for sample in manifest["samples"]:
            expected = profile["ssg_global_volume"] if sample["name"].startswith("SSG @") else 64
            self.assertEqual(sample["global_volume"], expected)


def check_trace(path, manifest=None):
    manifest = manifest or selected_manifest()
    blob = Path(manifest["sequence_path"]).read_bytes()
    expected = Counter(native_onsets(blob))
    actual = Counter()
    for line in Path(path).read_text().splitlines():
        tick, slot, key, gate, sample, gain, period = map(int, line.split(","))
        actual[tick, slot, key, gate, sample] += 1
        if not 0 <= gain <= 65535 or not 1 <= period <= 65535:
            raise ValueError("invalid gain/timer in runtime trace")
    if actual != expected:
        raise ValueError(f"native engine/compiler mismatch: missing {expected - actual}, extra {actual - expected}")
    print(f"Native engine trace: {sum(actual.values())} onsets/gates/assets match independent event traversal")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--trace":
        check_trace(sys.argv[2])
    else:
        unittest.main()
