import json
from pathlib import Path
import tempfile
import unittest

import deferred_probe
import shader_patch


class DeferredProbeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.engine = self.root / "engine"
        version = self.engine / "Engine/Build/Build.version"
        version.parent.mkdir(parents=True)
        version.write_text(json.dumps({"MajorVersion": 5, "MinorVersion": 8, "PatchVersion": 4}))
        self.shader_dir = self.engine / "Engine/Shaders/Private"
        self.shader_dir.mkdir(parents=True)
        self.sources = {
            "ShadingCommon.ush": b"#define SHADINGMODELID_NUM\t14\r\n",
            "ShadingModelsMaterial.ush": b"\tGBuffer.ShadingModelID = ShadingModel;\r\n",
            "ShadingModels.ush": shader_patch.ANCHOR + b"\r\n{\r\n\t" + shader_patch.FORWARD_MARKER
                + b"\r\n}\r\n\t\tcase SHADINGMODELID_DEFAULT_LIT:\r\n",
        }
        for name, data in self.sources.items():
            (self.shader_dir / name).write_bytes(data)
        self.backups = self.root / "backups"
        self.function = self.root / "function.ush"
        self.function.write_text("void MazelineEvaluateShaderOverride() {}")
        self.registry = self.root / "registry.json"
        self.registry.write_text(json.dumps({"functions": [{"id": 1, "source": "function.ush"}]}))

    def tearDown(self):
        self.temp.cleanup()

    def test_bundle_round_trip(self):
        deferred_probe.apply(self.engine, self.backups, self.registry)
        for name in self.sources:
            self.assertIn(deferred_probe.MARKER, (self.shader_dir / name).read_bytes())
        deferred_probe.restore(self.engine, self.backups)
        for name, original in self.sources.items():
            self.assertEqual((self.shader_dir / name).read_bytes(), original)

    def test_restore_conflict_leaves_all_files_untouched(self):
        deferred_probe.apply(self.engine, self.backups, self.registry)
        target = self.shader_dir / "ShadingModelsMaterial.ush"
        target.write_bytes(target.read_bytes() + b"external change")
        before = {name: (self.shader_dir / name).read_bytes() for name in self.sources}
        with self.assertRaises(ValueError):
            deferred_probe.restore(self.engine, self.backups)
        self.assertEqual(before, {name: (self.shader_dir / name).read_bytes() for name in self.sources})

    def test_unknown_anchor_fails_before_backup(self):
        (self.shader_dir / "ShadingCommon.ush").write_bytes(b"unknown version")
        with self.assertRaises(ValueError):
            deferred_probe.apply(self.engine, self.backups, self.registry)
        self.assertFalse(self.backups.exists())

    def test_registry_accepts_utf8_bom_and_multiline_signature(self):
        code = "void\nMazelineEvaluateShaderOverride () {}"
        self.function.write_bytes(code.encode("utf-8-sig"))
        self.assertEqual(deferred_probe.load_registry(self.registry), {1: code.encode("utf-8")})


if __name__ == "__main__":
    unittest.main()
