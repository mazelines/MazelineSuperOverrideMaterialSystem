"""Apply and restore a version-checked UE 5.8.4 shader patch.

The probe is a comment only. The forward patch is experimental and requires
the material shader to provide MazelineEvaluateShaderOverride.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile

ANCHOR = b"FDirectLighting DefaultLitBxDF( FGBufferData GBuffer, half3 N, half3 V, FAreaLight AreaLight, FShadowTerms Shadow )"
MARKER_PREFIX = b"// MAZELINE_SHADER_OVERRIDE_PATCH_"
PROBE_MARKER = MARKER_PREFIX + b"PROBE_V1"
FORWARD_MARKER = MARKER_PREFIX + b"FORWARD_V1"


def make_patch(original, kind):
    if MARKER_PREFIX in original or original.count(ANCHOR) != 1:
        raise ValueError("Unexpected existing marker or BxDF anchor count; no files changed")
    newline = b"\r\n" if b"\r\n" in original else b"\n"
    if kind == "probe":
        return original.replace(ANCHOR, PROBE_MARKER + newline + ANCHOR, 1)
    entry = ANCHOR + newline + b"{" + newline
    if original.count(entry) != 1:
        raise ValueError("Unexpected DefaultLitBxDF function entry; no files changed")
    lines = [
        b"\t" + FORWARD_MARKER,
        b"#if FORWARD_SHADING && defined(MAZELINE_SHADER_OVERRIDE) && MAZELINE_SHADER_OVERRIDE",
        b"\tif (GBuffer.ShadingModelID == SHADINGMODELID_DEFAULT_LIT)",
        b"\t{",
        b"\t\tfloat3 OverrideDiffuse = GBuffer.DiffuseColor;",
        b"\t\tfloat OverrideRoughness = GBuffer.Roughness;",
        b"\t\tMazelineEvaluateShaderOverride(GBuffer.BaseColor, GBuffer.Metallic, N, V, AreaLight.DiffuseL, OverrideDiffuse, OverrideRoughness);",
        b"\t\tGBuffer.DiffuseColor = OverrideDiffuse;",
        b"\t\tGBuffer.Roughness = saturate(OverrideRoughness);",
        b"\t}",
        b"#endif",
    ]
    return original.replace(entry, entry + newline.join(lines) + newline, 1)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def atomic_write(path, data):
    fd, tmp = tempfile.mkstemp(prefix=".mazeline-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def paths(engine, backups):
    engine = engine.resolve(strict=True)
    version = json.loads((engine / "Engine/Build/Build.version").read_text(encoding="utf-8"))
    if (version.get("MajorVersion"), version.get("MinorVersion"), version.get("PatchVersion")) != (5, 8, 4):
        raise ValueError("Only UE 5.8.4 is supported by this probe")
    target = (engine / "Engine/Shaders/Private/ShadingModels.ush").resolve(strict=True)
    if not target.is_relative_to(engine):
        raise ValueError("Shader path resolves outside the selected engine")
    backup_dir = backups.resolve() / hashlib.sha256(str(target).casefold().encode()).hexdigest()
    return target, backup_dir


def apply(engine, backups, kind="probe"):
    target, backup_dir = paths(engine, backups)
    original = target.read_bytes()
    if backup_dir.exists():
        metadata = json.loads((backup_dir / "metadata.json").read_text(encoding="utf-8"))
        saved = (backup_dir / "original.ush").read_bytes()
        if Path(metadata["target"]).resolve() != target or digest(saved) != metadata["original_sha256"]:
            raise ValueError("Backup identity or checksum mismatch; no files changed")
        if metadata.get("kind") != kind:
            raise ValueError("Backup belongs to a different patch kind; no files changed")
        if digest(original) == metadata["patched_sha256"]:
            print("Patch is already applied and matches the backup record")
            return
        if digest(original) != metadata["original_sha256"]:
            raise ValueError("Shader differs from both recorded states; refusing to overwrite it")
        patched = make_patch(saved, kind)
        if digest(patched) != metadata["patched_sha256"]:
            raise ValueError("Patch definition has changed; no files changed")
        if target.read_bytes() != original:
            raise ValueError("Shader changed during preparation; no files changed")
        atomic_write(target, patched)
        if target.read_bytes() != patched:
            raise OSError("Patch verification failed; backup preserved")
        print(f"{kind} patch reapplied; verified SHA-256:", digest(patched))
        return
    patched = make_patch(original, kind)
    backup_dir.mkdir(parents=True, exist_ok=False)
    atomic_write(backup_dir / "original.ush", original)
    metadata = {"target": str(target), "kind": kind, "original_sha256": digest(original), "patched_sha256": digest(patched)}
    atomic_write(backup_dir / "metadata.json", json.dumps(metadata, indent=2).encode("utf-8"))
    if target.read_bytes() != original:
        raise ValueError("Shader changed during preparation; backup preserved, target not modified")
    atomic_write(target, patched)
    if target.read_bytes() != patched:
        raise OSError("Patch verification failed; backup preserved")
    print(f"{kind} patch applied; verified SHA-256:", digest(patched))


def restore(engine, backups):
    target, backup_dir = paths(engine, backups)
    metadata = json.loads((backup_dir / "metadata.json").read_text(encoding="utf-8"))
    original = (backup_dir / "original.ush").read_bytes()
    current = target.read_bytes()
    if Path(metadata["target"]).resolve() != target or digest(original) != metadata["original_sha256"]:
        raise ValueError("Backup identity or checksum mismatch; no files changed")
    if digest(current) != metadata["patched_sha256"]:
        raise ValueError("Shader differs from the recorded patch; refusing to overwrite it")
    atomic_write(target, original)
    if digest(target.read_bytes()) != metadata["original_sha256"]:
        raise OSError("Restore verification failed; backup preserved")
    print("Original restored; verified SHA-256:", digest(original))
    # Keep the backup and metadata for audit and safe reapplication.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("apply", "restore"))
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--backups", type=Path, required=True)
    parser.add_argument("--kind", choices=("probe", "forward"), default="probe")
    args = parser.parse_args()
    try:
        if args.action == "apply":
            apply(args.engine, args.backups, args.kind)
        else:
            restore(args.engine, args.backups)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        parser.exit(1, f"Patch operation failed: {exc}\n")


if __name__ == "__main__":
    main()
