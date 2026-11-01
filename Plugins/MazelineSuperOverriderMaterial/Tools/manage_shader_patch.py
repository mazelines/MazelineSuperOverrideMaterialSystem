"""Manage a recoverable Forward + Deferred shader bundle for one UE installation.

Run with every editor/ShaderCompileWorker for this engine closed. Immutable
snapshots and a write-ahead journal live in Engine/Saved/MazelineShaderOverride.
An interrupted transaction must be recovered before any other mutation.
"""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import subprocess
import uuid

import deferred_probe
import shader_patch

FILES = deferred_probe.FILES
FORMAT = 1


def write_json(path, value):
    shader_patch.atomic_write(path, (json.dumps(value, indent=2) + "\n").encode("utf-8"))


def assert_engine_idle(engine):
    """Fail closed if process inspection fails; never stop another editor."""
    engine = Path(engine).resolve(strict=True)
    if os.name != "nt":
        raise RuntimeError("Process validation currently supports Windows only")
    script = ("$ErrorActionPreference='Stop'; $items=@(Get-CimInstance Win32_Process | "
              "Where-Object {$_.Name -like 'UnrealEditor*' -or $_.Name -eq 'ShaderCompileWorker.exe'} | "
              "Select-Object ProcessId,ExecutablePath); ConvertTo-Json -InputObject $items -Compress")
    result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
                            capture_output=True, text=True, check=True, timeout=30)
    processes = json.loads(result.stdout)
    for process in processes:
        executable = process.get("ExecutablePath")
        if not executable or Path(executable).resolve().is_relative_to(engine):
            raise RuntimeError(f"Close the engine editor/compiler before patching (PID {process['ProcessId']})")


class PatchManager:
    def __init__(self, engine, project):
        self.engine = Path(engine).resolve(strict=True)
        self.project = Path(project).resolve(strict=True)
        if not self.project.is_file() or self.project.suffix.lower() != ".uproject":
            raise ValueError("--project must identify an existing .uproject")
        self.store = self.engine / "Engine/Saved/MazelineShaderOverride"
        if not self.store.resolve().is_relative_to(self.engine):
            raise ValueError("Backup location resolves outside the engine")
        self.targets, _ = deferred_probe.bundle_paths(self.engine, self.store)
        self.state_path = self.store / "state.json"

    @contextmanager
    def locked(self):
        self.store.mkdir(parents=True, exist_ok=True)
        with (self.store / "operation.lock").open("a+b") as handle:
            if handle.seek(0, os.SEEK_END) == 0:
                handle.write(b"0")
                handle.flush()
            handle.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            try:
                yield
            finally:
                if os.name == "nt":
                    handle.seek(0)
                    msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
                else:
                    fcntl.flock(handle, fcntl.LOCK_UN)

    def current(self):
        return {name: path.read_bytes() for name, path in self.targets.items()}

    def state(self):
        if not self.state_path.exists():
            return None
        state = json.loads(self.state_path.read_text(encoding="utf-8"))
        if state["format"] != FORMAT or Path(state["engine"]).resolve() != self.engine:
            raise ValueError("Patch state format/engine identity mismatch")
        return state

    def snapshot(self, files, details):
        identifier = uuid.uuid4().hex
        directory = self.store / identifier
        directory.mkdir()
        for name in FILES:
            shader_patch.atomic_write(directory / name, files[name])
        write_json(directory / "manifest.json", {
            "files": {name: shader_patch.digest(files[name]) for name in FILES},
            "details": details,
        })
        return identifier

    def read_snapshot(self, identifier):
        if not isinstance(identifier, str) or not re.fullmatch(r"[0-9a-f]{32}", identifier):
            raise ValueError("Invalid snapshot identifier")
        directory = (self.store / identifier).resolve(strict=True)
        if directory.parent != self.store.resolve():
            raise ValueError("Snapshot resolves outside backup directory")
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
        files = {name: (directory / name).read_bytes() for name in FILES}
        if set(manifest["files"]) != set(FILES) or any(
                shader_patch.digest(files[name]) != manifest["files"][name] for name in FILES):
            raise ValueError("Snapshot checksum mismatch; no shader files changed")
        return files

    def require_owner(self, state):
        owner = state.get("pending", {}).get("owner") or state.get("owner")
        if owner and Path(owner).resolve() != self.project:
            raise ValueError(f"Engine patch belongs to {owner}; restore from that project first")

    def require_ready(self, state):
        self.require_owner(state)
        if state.get("pending"):
            raise ValueError("An interrupted transaction exists; use recover before continuing")
        expected = self.read_snapshot(state["active"])
        if self.current() != expected:
            raise ValueError("Engine shaders differ from the recorded snapshot; no files changed")
        return expected

    def transition(self, state, destination, owner):
        before = self.require_ready(state)
        after = self.read_snapshot(destination)
        pending = dict(state, pending={"destination": destination, "owner": str(self.project)})
        # The old active snapshot remains authoritative until every write verifies.
        write_json(self.state_path, pending)
        try:
            self.write_transition(before, after)
            write_json(self.state_path, dict(state, active=destination, owner=owner))
        except BaseException:
            # Leave the durable journal intact on process termination or I/O failure.
            # Explicit recover checks all files and rolls back to the old snapshot.
            raise

    def write_transition(self, before, after):
        if self.current() != before:
            raise ValueError("Engine shaders changed during preparation; transaction left for recover")
        for name, path in self.targets.items():
            if path.read_bytes() != before[name]:
                raise ValueError(f"{name}: external edit detected; transaction left for recover")
            if before[name] != after[name]:
                shader_patch.atomic_write(path, after[name])
        if self.current() != after:
            raise OSError("Bundle verification failed; transaction left for recover")

    def apply(self, registry):
        registry_data = json.loads(Path(registry).read_text(encoding="utf-8"))
        if registry_data.get("project") and Path(registry_data["project"]).resolve() != self.project:
            raise ValueError("Registry belongs to a different project")
        state = self.state()
        if state:
            self.require_ready(state)
            originals = self.read_snapshot(state["baseline"])
        else:
            originals = self.current()
        functions = deferred_probe.load_registry(Path(registry).resolve(strict=True))
        forward = dict(originals)
        forward["ShadingModels.ush"] = shader_patch.make_patch(originals["ShadingModels.ush"], "forward")
        patched = {name: deferred_probe.transform(name, forward[name], functions) for name in FILES}
        if state is None:
            baseline = self.snapshot(originals, {"type": "baseline", "engine": str(self.engine)})
            state = {"format": FORMAT, "engine": str(self.engine), "baseline": baseline,
                     "active": baseline, "owner": None}
            write_json(self.state_path, state)
        if self.read_snapshot(state["active"]) == patched:
            return "Already applied; every shader matches the requested registry"
        destination = self.snapshot(patched, {"type": "override", "project": str(self.project),
                                             "registry": str(Path(registry).resolve()),
                                             "function_hashes": {str(k): shader_patch.digest(v) for k, v in functions.items()}})
        self.transition(state, destination, str(self.project))
        return "Forward + Deferred applied and verified; restart the editor to compile shaders"

    def restore(self):
        state = self.state()
        if state is None:
            raise ValueError("No managed backup exists for this engine")
        self.require_ready(state)
        if state["active"] == state["baseline"]:
            return "Original shaders already restored and verified"
        self.transition(state, state["baseline"], None)
        return "Original shaders restored and verified; snapshots retained"

    def recover(self):
        state = self.state()
        if not state or not state.get("pending"):
            raise ValueError("No interrupted transaction to recover")
        self.require_owner(state)
        before = self.read_snapshot(state["active"])
        after = self.read_snapshot(state["pending"]["destination"])
        current = self.current()
        if any(current[name] not in (before[name], after[name]) for name in FILES):
            raise ValueError("External shader edits detected; no files recovered")
        self.write_transition(current, before)
        del state["pending"]
        write_json(self.state_path, state)
        return "Interrupted transaction rolled back and verified; retry the requested operation"

    def status(self):
        state = self.state()
        if state is None:
            return {"state": "unmanaged", "backup_directory": str(self.store)}
        baseline = self.read_snapshot(state["baseline"])
        active = self.read_snapshot(state["active"])
        return {"state": "interrupted" if state.get("pending") else (
                    "restored" if active == baseline else "applied"),
                "files_match_active_snapshot": self.current() == active,
                "owner": state["owner"], "backup_directory": str(self.store),
                "active_snapshot": state["active"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("apply", "restore", "recover", "status"))
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--registry", type=Path)
    args = parser.parse_args()
    try:
        manager = PatchManager(args.engine, args.project)
        with manager.locked():
            if args.action != "status":
                assert_engine_idle(manager.engine)
            if args.action == "apply":
                registry = args.registry or manager.project.parent / "Saved/Mazeline/deferred_registry.json"
                result = manager.apply(registry)
            else:
                result = getattr(manager, args.action)()
            print(json.dumps(result, indent=2) if isinstance(result, dict) else result)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"Mazeline patch operation failed: {exc}\n")


if __name__ == "__main__":
    main()
