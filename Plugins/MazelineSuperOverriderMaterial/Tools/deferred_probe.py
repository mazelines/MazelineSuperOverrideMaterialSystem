"""Experimental three-file UE 5.8.4 deferred BxDF probe; not production support.

Reserves shading-model ID 14 and two per-object bits in selected materials.
Functions are supplied by a registry and compiled into the global light
shader. Selected pixels lose contact and dynamic-indirect shadow flags.
"""

import argparse
import json
from pathlib import Path
import re

import shader_patch

FILES = ("ShadingCommon.ush", "ShadingModelsMaterial.ush", "ShadingModels.ush")
MARKER = b"MAZELINE_DEFERRED_PROBE_V1"


def once(source, old, new, name):
    if source.count(old) != 1:
        raise ValueError(f"{name}: expected exactly one anchor")
    return source.replace(old, new, 1)


def load_registry(registry_path):
    data = json.loads(registry_path.read_text(encoding="utf-8"))
    functions = {}
    for item in data["functions"]:
        index = item["id"]
        if not isinstance(index, int) or isinstance(index, bool) or index not in (1, 2, 3) or index in functions:
            raise ValueError("Registry IDs must be unique integers from 1 to 3")
        path = (registry_path.parent / item["source"]).resolve(strict=True)
        code = path.read_text(encoding="utf-8-sig")
        if item.get("sha256") and shader_patch.digest(code.encode("utf-8")) != item["sha256"]:
            raise ValueError(f"{path}: HLSL changed since registry generation; prepare the registry again")
        if not re.search(r"\bvoid\s+MazelineEvaluateShaderOverride\s*\(", code):
            raise ValueError(f"{path}: expected MazelineEvaluateShaderOverride function")
        if re.search(r"(?m)^\s*#\s*include\b", code):
            raise ValueError(f"{path}: nested includes are not supported in the deferred prototype")
        functions[index] = code.encode("utf-8")
    if not functions:
        raise ValueError("Registry must contain at least one function")
    return dict(sorted(functions.items()))


def transform(name, original, functions):
    if MARKER in original:
        raise ValueError(f"{name}: deferred probe already present")
    newline = b"\r\n" if b"\r\n" in original else b"\n"
    if name == "ShadingCommon.ush":
        pattern = rb"(?m)^#define SHADINGMODELID_NUM[\t ]+14\r?$"
        matches = list(re.finditer(pattern, original))
        if len(matches) != 1:
            raise ValueError("Unexpected shading-model ID definition")
        old = matches[0].group(0).rstrip(b"\r")
        new = (b"#define SHADINGMODELID_MAZELINE 14 // " + MARKER + newline
               + b"#define SHADINGMODELID_NUM 15")
        return once(original, old, new, name)
    if name == "ShadingModelsMaterial.ush":
        old = b"\tGBuffer.ShadingModelID = ShadingModel;"
        lines = [old, b"\t// " + MARKER,
                 b"#if !FORWARD_SHADING && !SUBSTRATE_ENABLED && !SHADING_PATH_MOBILE && defined(MAZELINE_SHADER_OVERRIDE) && MAZELINE_SHADER_OVERRIDE",
                 b"\tif (ShadingModel == SHADINGMODELID_DEFAULT_LIT)", b"\t{",
                 b"\t\tGBuffer.ShadingModelID = SHADINGMODELID_MAZELINE;",
                 b"\t\tGBuffer.PerObjectGBufferData = min((float)MAZELINE_SHADER_OVERRIDE, 3.0f) / 3.0f;",
                 b"\t}", b"#endif"]
        return once(original, old, newline.join(lines), name)
    if name == "ShadingModels.ush":
        if shader_patch.FORWARD_MARKER not in original:
            raise ValueError("Forward probe must be present before the deferred probe")
        definitions = [b"// " + MARKER + b" generated function registry",
                       b"#if !FORWARD_SHADING && !SHADING_PATH_MOBILE"]
        for index, code in functions.items():
            definitions.extend([
                b"#define MazelineEvaluateShaderOverride MazelineDeferredFunction" + str(index).encode(),
                code.replace(b"\r\n", b"\n").replace(b"\n", newline),
                b"#undef MazelineEvaluateShaderOverride",
            ])
        definitions.append(b"#endif")
        original = once(original, shader_patch.ANCHOR,
                        newline.join(definitions) + newline + shader_patch.ANCHOR, name)
        old = shader_patch.ANCHOR + newline + b"{" + newline
        lines = [b"\t// " + MARKER,
                 b"#if !FORWARD_SHADING && !SHADING_PATH_MOBILE",
                 b"\tif (GBuffer.ShadingModelID == SHADINGMODELID_MAZELINE)", b"\t{",
                 b"\t\tuint FunctionId = (uint)round(GBuffer.PerObjectGBufferData * 3.0f);",
                 b"\t\tfloat3 OverrideDiffuse = GBuffer.DiffuseColor;",
                 b"\t\tfloat OverrideRoughness = GBuffer.Roughness;"]
        for index in functions:
            lines.extend([b"\t\tif (FunctionId == " + str(index).encode() + b")",
                          b"\t\t\tMazelineDeferredFunction" + str(index).encode()
                          + b"(GBuffer.BaseColor, GBuffer.Metallic, N, V, AreaLight.DiffuseL, OverrideDiffuse, OverrideRoughness);"])
        lines.extend([b"\t\tGBuffer.DiffuseColor = OverrideDiffuse;",
                      b"\t\tGBuffer.Roughness = saturate(OverrideRoughness);",
                      b"\t}", b"#endif"])
        updated = once(original, old, old + newline.join(lines) + newline, name)
        case = b"\t\tcase SHADINGMODELID_DEFAULT_LIT:"
        return once(updated, case, b"\t\tcase SHADINGMODELID_MAZELINE: // " + MARKER + newline + case, name)
    raise ValueError("Unknown shader")


def bundle_paths(engine, backups):
    shader, _ = shader_patch.paths(engine, backups)
    root = shader.parent
    targets = {name: (root / name).resolve(strict=True) for name in FILES}
    if any(path.parent != root for path in targets.values()):
        raise ValueError("Shader path resolves outside engine shader directory")
    return targets, backups.resolve()


def apply(engine, backups, registry):
    targets, backup_dir = bundle_paths(engine, backups)
    if backup_dir.exists():
        raise ValueError("Bundle backup already exists; inspect it before applying")
    originals = {name: path.read_bytes() for name, path in targets.items()}
    functions = load_registry(registry)
    patched = {name: transform(name, data, functions) for name, data in originals.items()}
    backup_dir.mkdir(parents=True, exist_ok=False)
    metadata = {}
    for name, path in targets.items():
        shader_patch.atomic_write(backup_dir / name, originals[name])
        metadata[name] = {"target": str(path), "original": shader_patch.digest(originals[name]),
                          "patched": shader_patch.digest(patched[name])}
    shader_patch.atomic_write(backup_dir / "metadata.json", json.dumps(metadata, indent=2).encode())
    if any(path.read_bytes() != originals[name] for name, path in targets.items()):
        raise ValueError("A shader changed during preparation; no shader was patched")
    applied = []
    try:
        for name, path in targets.items():
            shader_patch.atomic_write(path, patched[name])
            applied.append(name)
        if any(path.read_bytes() != patched[name] for name, path in targets.items()):
            raise OSError("Patch verification failed")
    except BaseException:
        for name in reversed(applied):
            if targets[name].read_bytes() == patched[name]:
                shader_patch.atomic_write(targets[name], originals[name])
        raise
    print("Deferred probe applied; backups preserved at", backup_dir)


def restore(engine, backups):
    targets, backup_dir = bundle_paths(engine, backups)
    metadata = json.loads((backup_dir / "metadata.json").read_text())
    originals = {name: (backup_dir / name).read_bytes() for name in FILES}
    for name, path in targets.items():
        row = metadata[name]
        if Path(row["target"]).resolve() != path or shader_patch.digest(originals[name]) != row["original"]:
            raise ValueError(f"{name}: backup mismatch; no files changed")
        if shader_patch.digest(path.read_bytes()) != row["patched"]:
            raise ValueError(f"{name}: shader changed; no files restored")
    for name, path in targets.items():
        shader_patch.atomic_write(path, originals[name])
    if any(path.read_bytes() != originals[name] for name, path in targets.items()):
        raise OSError("Restore verification failed; backups preserved")
    print("Deferred probe restored; backups preserved at", backup_dir)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("apply", "restore"))
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--backups", type=Path, required=True)
    parser.add_argument("--registry", type=Path, help="JSON mapping IDs to standalone HLSL files; required for apply")
    args = parser.parse_args()
    try:
        if args.action == "apply":
            if args.registry is None:
                raise ValueError("--registry is required for apply")
            apply(args.engine, args.backups, args.registry)
        else:
            restore(args.engine, args.backups)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        parser.exit(1, f"Deferred probe failed: {exc}\n")


if __name__ == "__main__":
    main()
