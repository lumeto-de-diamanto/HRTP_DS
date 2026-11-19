"""Generated audio reuse must follow content, never timestamps or test results."""
import json
from contextlib import chdir
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import build_native_audio as native
import native_instruments


class NativeBuildCacheTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='native-cache-', dir='/tmp/opencode')
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.source, self.tool = root / 'song.MDT', root / 'compiler.py'
        self.source.write_bytes(b'source')
        self.tool.write_bytes(b'compiler')
        self.drums = root / 'drums'
        self.drums.mkdir()
        (self.drums / '2608_BD.WAV').write_bytes(b'drum')
        self.args = SimpleNamespace(out=root / 'output', track='synthetic', rhythm_dir=self.drums,
                                    approximate_portamento=True, cxx='compiler')
        self.args.out.mkdir()
        for name in native.track_outputs(self.args.track):
            path = self.args.out / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'generated')
        (self.args.out / 'manifest.json').write_text(json.dumps(dict(track=self.args.track)))
        inputs = patch('build_native_audio.build_inputs', return_value=[self.tool])
        compiler = patch('native_instruments.compiler_identity', side_effect=lambda cxx: dict(command=cxx))
        inputs.start(); compiler.start()
        self.addCleanup(inputs.stop); self.addCleanup(compiler.stop)
        self.identity = native.track_identity(self.args, self.source)
        self.outputs = native.track_outputs(self.args.track)
        native.cache_track(self.args.out, self.identity, self.outputs)

    def lookup(self):
        return native.cached_track(self.args.out, native.track_identity(self.args, self.source), self.outputs)

    def test_unchanged_track_skips_generation_without_touching_outputs(self):
        before = {p: p.stat().st_mtime_ns for p in self.args.out.rglob('*') if p.is_file()}
        with patch('build_native_audio.track_identity', return_value=self.identity), \
                patch('native_instruments.instrument_assets') as render:
            self.assertEqual(native.build_track(self.args), dict(track='synthetic'))
            render.assert_not_called()
        self.assertEqual(before, {p: p.stat().st_mtime_ns for p in before})

    def test_source_tool_and_wav_content_invalidate_even_with_preserved_mtime(self):
        for path in (self.source, self.tool, self.drums / '2608_BD.WAV'):
            with self.subTest(path=path.name):
                original, stat = path.read_bytes(), path.stat()
                path.write_bytes(bytes([original[0] ^ 1]) + original[1:])
                os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns))
                self.assertIsNone(self.lookup())
                path.write_bytes(original)
                self.assertIsNotNone(self.lookup())

    def test_options_compiler_and_rhythm_inventory_invalidate(self):
        for field, value in (('approximate_portamento', False), ('cxx', 'other-compiler'),
                             ('rhythm_dir', None)):
            original = getattr(self.args, field)
            setattr(self.args, field, value)
            self.assertIsNone(self.lookup())
            setattr(self.args, field, original)
        extra = self.drums / '2608_bd.wav'
        extra.write_bytes(b'duplicate')
        self.assertIsNone(self.lookup())
        extra.unlink()
        drum = self.drums / '2608_BD.WAV'
        drum.rename(extra)
        self.assertIsNone(self.lookup())
        extra.unlink()
        self.assertIsNone(self.lookup())

    def test_each_missing_or_corrupt_output_invalidates(self):
        for name in self.outputs:
            with self.subTest(output=name):
                path = self.args.out / name
                original = path.read_bytes()
                path.unlink()
                self.assertIsNone(self.lookup())
                path.write_bytes(b'corrupt')
                self.assertIsNone(self.lookup())
                path.write_bytes(original)
                self.assertIsNotNone(self.lookup())

    def test_invalid_stamp_and_incomplete_publication_are_not_hits(self):
        stamp = self.args.out / 'build-cache.json'
        for data in ('{', '{}', 'null'):
            stamp.write_text(data)
            self.assertIsNone(self.lookup())
        stamp.unlink()
        (self.args.out / self.outputs[-1]).unlink()
        with self.assertRaises(OSError):
            native.cache_track(self.args.out, self.identity, self.outputs)
        self.assertFalse(stamp.exists())

    def test_renderer_recovers_each_missing_or_modified_executable(self):
        def compile_or_run(command, **kwargs):
            if '-o' in command:
                Path(command[command.index('-o') + 1]).write_bytes(b'executable')

        with chdir(self.args.out), \
                patch('native_instruments.renderer_inputs', return_value=[self.tool]), \
                patch('native_instruments.subprocess.run', side_effect=compile_or_run) as run:
            native_instruments.host_renderer(Path('ymfm'))
            run.reset_mock()
            native_instruments.host_renderer(Path('ymfm'))
            run.assert_not_called()
            for name in ('render_opna_fm', 'test_opna_patch', 'test_opna_tuning'):
                executable = Path('build/native_tools') / name
                for corrupt in (False, True):
                    with self.subTest(output=name, corrupt=corrupt):
                        if corrupt:
                            executable.write_bytes(b'modified')
                        else:
                            executable.unlink()
                        native_instruments.host_renderer(Path('ymfm'))
                        self.assertEqual(sum('-o' in call.args[0] for call in run.call_args_list), 3)
                        run.reset_mock()
