import json
from pathlib import Path
from unittest import mock

import test_deferred_probe
import manage_shader_patch
import shader_patch


class ManagedPatchTests(test_deferred_probe.DeferredProbeTests):
    def setUp(self):
        super().setUp()
        self.sources["ShadingModels.ush"] = (shader_patch.ANCHOR + b"\r\n{\r\n}\r\n"
                                              b"\t\tcase SHADINGMODELID_DEFAULT_LIT:\r\n")
        for name, source in self.sources.items():
            (self.shader_dir / name).write_bytes(source)
        self.project = self.root / "Example.uproject"
        self.project.write_text("{}")
        self.manager = manage_shader_patch.PatchManager(self.engine, self.project)
        self.manager.store.mkdir(parents=True)

    # The legacy tests require an already-patched Forward shader; this fixture
    # instead supplies clean files for the integrated manager.
    def test_bundle_round_trip(self):
        self.manager.apply(self.registry)
        first = self.manager.current()
        self.assertEqual(self.manager.status()["state"], "applied")
        self.assertIn("Already applied", self.manager.apply(self.registry))
        self.function.write_text("void MazelineEvaluateShaderOverride() { /* new body */ }")
        self.manager.apply(self.registry)
        self.assertNotEqual(first, self.manager.current())
        self.manager.restore()
        self.assertEqual(self.manager.current(), self.sources)
        self.assertIn("already restored", self.manager.restore())

    def test_restore_conflict_leaves_all_files_untouched(self):
        self.manager.apply(self.registry)
        target = self.shader_dir / "ShadingModelsMaterial.ush"
        target.write_bytes(target.read_bytes() + b"external edit")
        before = self.manager.current()
        with self.assertRaisesRegex(ValueError, "differ"):
            self.manager.restore()
        self.assertEqual(self.manager.current(), before)

    def test_unknown_anchor_fails_before_backup(self):
        (self.shader_dir / "ShadingCommon.ush").write_bytes(b"unknown")
        with self.assertRaises(ValueError):
            self.manager.apply(self.registry)
        self.assertIsNone(self.manager.state())

    def fail_target_write(self, count):
        original_write = shader_patch.atomic_write
        counter = 0

        def interrupted_write(path, data):
            nonlocal counter
            if path.parent == self.shader_dir.resolve():
                counter += 1
                if counter == count:
                    raise OSError("simulated disk failure")
            original_write(path, data)
        return mock.patch.object(shader_patch, "atomic_write", side_effect=interrupted_write)

    def test_interrupted_first_apply_recovers_across_new_manager(self):
        with self.fail_target_write(2), self.assertRaises(OSError):
            self.manager.apply(self.registry)
        self.assertEqual(self.manager.status()["state"], "interrupted")
        with self.assertRaisesRegex(ValueError, "recover"):
            self.manager.apply(self.registry)
        reopened = manage_shader_patch.PatchManager(self.engine, self.project)
        reopened.recover()
        self.assertEqual(reopened.current(), self.sources)
        reopened.apply(self.registry)
        self.assertTrue(reopened.status()["files_match_active_snapshot"])

    def test_interrupted_restore_recovers_to_previous_patch(self):
        self.manager.apply(self.registry)
        patched = self.manager.current()
        with self.fail_target_write(2), self.assertRaises(OSError):
            self.manager.restore()
        self.manager.recover()
        self.assertEqual(self.manager.current(), patched)
        self.manager.restore()
        self.assertEqual(self.manager.current(), self.sources)

    def test_interrupted_update_preserves_previous_registry(self):
        self.manager.apply(self.registry)
        patched = self.manager.current()
        self.function.write_text("void MazelineEvaluateShaderOverride() { /* revision */ }")
        # Only ShadingModels changes during a registry update.
        with self.fail_target_write(1), self.assertRaises(OSError):
            self.manager.apply(self.registry)
        self.manager.recover()
        self.assertEqual(self.manager.current(), patched)
        self.manager.apply(self.registry)
        self.assertNotEqual(self.manager.current(), patched)

    def test_recovery_refuses_external_edit_before_any_write(self):
        with self.fail_target_write(2), self.assertRaises(OSError):
            self.manager.apply(self.registry)
        target = self.shader_dir / "ShadingModels.ush"
        target.write_bytes(target.read_bytes() + b"external edit")
        before = self.manager.current()
        with self.assertRaisesRegex(ValueError, "External"):
            self.manager.recover()
        self.assertEqual(self.manager.current(), before)

    def test_recovery_refuses_corrupt_backup(self):
        with self.fail_target_write(2), self.assertRaises(OSError):
            self.manager.apply(self.registry)
        baseline = self.manager.store / self.manager.state()["baseline"] / "ShadingCommon.ush"
        baseline.write_bytes(b"corrupt")
        before = self.manager.current()
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.manager.recover()
        self.assertEqual(self.manager.current(), before)

    def test_other_project_cannot_replace_or_restore_active_patch(self):
        self.manager.apply(self.registry)
        project = self.root / "Other.uproject"
        project.write_text("{}")
        other = manage_shader_patch.PatchManager(self.engine, project)
        for operation in (lambda: other.apply(self.registry), other.restore):
            with self.assertRaisesRegex(ValueError, "belongs to"):
                operation()
        self.manager.restore()
        other.apply(self.registry)
        self.assertEqual(other.status()["owner"], str(project.resolve()))

    def test_engine_lock_prevents_overlapping_operations(self):
        other = manage_shader_patch.PatchManager(self.engine, self.project)
        with self.manager.locked():
            with self.assertRaises(OSError):
                with other.locked():
                    self.fail("Second manager must not acquire this engine lock")

    def test_corrupt_active_backup_prevents_mutation(self):
        self.manager.apply(self.registry)
        active = self.manager.store / self.manager.state()["active"] / "ShadingModels.ush"
        active.write_bytes(b"corrupt")
        before = self.manager.current()
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.manager.restore()
        self.assertEqual(self.manager.current(), before)

    def test_process_guard_rejects_editor_and_unknown_executable(self):
        for executable in (str(self.engine / "Engine/Binaries/Win64/UnrealEditor.exe"), None):
            response = mock.Mock(stdout=json.dumps([{"ProcessId": 99, "ExecutablePath": executable}]))
            with mock.patch.object(manage_shader_patch.subprocess, "run", return_value=response):
                with self.assertRaisesRegex(RuntimeError, "99"):
                    manage_shader_patch.assert_engine_idle(self.engine)

    def test_stale_registry_source_is_rejected_before_shader_changes(self):
        self.registry.write_text(json.dumps({"functions": [{"id": 1, "source": "function.ush",
            "sha256": shader_patch.digest(self.function.read_bytes())}]}))
        self.function.write_text("void MazelineEvaluateShaderOverride() { /* changed */ }")
        with self.assertRaisesRegex(ValueError, "changed since"):
            self.manager.apply(self.registry)
        self.assertIsNone(self.manager.state())
        self.assertEqual(self.manager.current(), self.sources)

    def test_registry_project_mismatch_is_rejected(self):
        self.registry.write_text(json.dumps({"project": str(self.root / "Other.uproject"),
            "functions": [{"id": 1, "source": "function.ush"}]}))
        with self.assertRaisesRegex(ValueError, "different project"):
            self.manager.apply(self.registry)
        self.assertIsNone(self.manager.state())


if __name__ == "__main__":
    import unittest
    unittest.main()
