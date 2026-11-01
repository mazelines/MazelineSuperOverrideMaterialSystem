import json
from pathlib import Path
import tempfile
import unittest

import shader_patch


class ShaderPatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.engine = self.root / "engine"
        version = self.engine / "Engine/Build/Build.version"
        version.parent.mkdir(parents=True)
        version.write_text(json.dumps({"MajorVersion": 5, "MinorVersion": 8, "PatchVersion": 4}))
        self.target = self.engine / "Engine/Shaders/Private/ShadingModels.ush"
        self.target.parent.mkdir(parents=True)
        self.original = b"header\r\n" + shader_patch.ANCHOR + b"\r\n{\r\nbody\r\n}\r\n"
        self.target.write_bytes(self.original)
        self.backups = self.root / "backups"

    def tearDown(self):
        self.temp.cleanup()

    def test_round_trip_duplicate_and_reapply(self):
        shader_patch.apply(self.engine, self.backups)
        self.assertIn(shader_patch.PROBE_MARKER + b"\r\n", self.target.read_bytes())
        first_patch = self.target.read_bytes()
        shader_patch.apply(self.engine, self.backups)
        self.assertEqual(self.target.read_bytes(), first_patch)
        shader_patch.restore(self.engine, self.backups)
        self.assertEqual(self.target.read_bytes(), self.original)
        shader_patch.apply(self.engine, self.backups)
        self.assertEqual(self.target.read_bytes(), first_patch)
        shader_patch.restore(self.engine, self.backups)

    def test_conflicting_edits_are_not_overwritten(self):
        shader_patch.apply(self.engine, self.backups)
        modified = self.target.read_bytes() + b"user edit"
        self.target.write_bytes(modified)
        with self.assertRaises(ValueError):
            shader_patch.restore(self.engine, self.backups)
        self.assertEqual(self.target.read_bytes(), modified)

    def test_unknown_input_rejected_without_backup(self):
        self.target.write_bytes(b"unexpected shader")
        with self.assertRaises(ValueError):
            shader_patch.apply(self.engine, self.backups)
        self.assertFalse(self.backups.exists())
        self.assertEqual(self.target.read_bytes(), b"unexpected shader")

    def test_forward_patch_is_conditional_and_reversible(self):
        shader_patch.apply(self.engine, self.backups, "forward")
        patched = self.target.read_bytes()
        self.assertIn(shader_patch.FORWARD_MARKER, patched)
        self.assertIn(b"#if FORWARD_SHADING && defined(MAZELINE_SHADER_OVERRIDE)", patched)
        shader_patch.restore(self.engine, self.backups)
        self.assertEqual(self.target.read_bytes(), self.original)


if __name__ == "__main__":
    unittest.main()
