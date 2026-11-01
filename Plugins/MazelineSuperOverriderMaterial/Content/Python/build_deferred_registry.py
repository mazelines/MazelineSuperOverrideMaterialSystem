"""Build an experimental registry from connected Custom Expression nodes.

Run inside Unreal Editor Python. Each participating material uses define value
1, 2 or 3 and one standalone /Project shader include declaring
MazelineEvaluateShaderOverride. Material-function traversal and texture
binding in deferred HLSL are not supported by this prototype.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

import unreal


def main():
    project = Path(unreal.Paths.project_dir()).resolve()
    shader_root = (project / "Shaders").resolve()
    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    asset_registry.wait_for_completion()
    assets = asset_registry.get_assets_by_path("/Game", recursive=True)
    library = unreal.MaterialEditingLibrary
    roots = (unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_ROUGHNESS,
             unreal.MaterialProperty.MP_METALLIC)
    functions = {}
    for asset_data in assets:
        if str(asset_data.asset_class_path.asset_name) != "Material":
            continue
        material = asset_data.get_asset()
        queue = [library.get_material_property_input_node(material, prop) for prop in roots]
        visited = set()
        material_ids = set()
        while queue:
            node = queue.pop()
            if node is None or node.get_path_name() in visited:
                continue
            visited.add(node.get_path_name())
            if isinstance(node, unreal.MaterialExpressionMaterialFunctionCall):
                unreal.log_warning("Mazeline registry does not traverse material functions: " + node.get_path_name())
            if isinstance(node, unreal.MaterialExpressionCustom):
                matching = [d for d in node.get_editor_property("additional_defines")
                            if d.get_editor_property("define_name") == "MAZELINE_SHADER_OVERRIDE"]
                if matching:
                    if (material.get_editor_property("material_domain") != unreal.MaterialDomain.MD_SURFACE
                            or material.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_OPAQUE
                            or material.get_editor_property("shading_model") != unreal.MaterialShadingModel.MSM_DEFAULT_LIT):
                        raise RuntimeError("Override currently requires an Opaque Default Lit surface material: " + material.get_path_name())
                    if len(matching) != 1:
                        raise RuntimeError("Duplicate override defines: " + node.get_path_name())
                    value = matching[0].get_editor_property("define_value")
                    if value not in ("1", "2", "3"):
                        raise RuntimeError("Override define must be 1, 2 or 3: " + node.get_path_name())
                    candidates = []
                    for include in node.get_editor_property("include_file_paths"):
                        if not include.startswith("/Project/"):
                            continue
                        file_path = (shader_root / include[len("/Project/"):]).resolve()
                        if not file_path.is_relative_to(shader_root) or not file_path.is_file():
                            raise RuntimeError("Invalid project shader path: " + include)
                        code = file_path.read_text(encoding="utf-8-sig")
                        if re.search(r"\bvoid\s+MazelineEvaluateShaderOverride\s*\(", code):
                            candidates.append((file_path, hashlib.sha256(code.encode("utf-8")).hexdigest()))
                    if len(candidates) != 1:
                        raise RuntimeError("Exactly one override function include is required: " + node.get_path_name())
                    index = int(value)
                    material_ids.add(index)
                    if len(material_ids) != 1:
                        raise RuntimeError("A material must select exactly one override function ID: " + material.get_path_name())
                    file_path, source_hash = candidates[0]
                    if index in functions and functions[index][1] != source_hash:
                        raise RuntimeError("Different functions use the same override ID: " + str(index))
                    functions[index] = (file_path, source_hash)
            queue.extend(library.get_inputs_for_material_expression(material, node))
    if not functions:
        raise RuntimeError("No connected override functions found")
    project_files = list(project.glob("*.uproject"))
    if len(project_files) != 1:
        raise RuntimeError("Expected one .uproject in the current project directory")
    result = {"schema": 1, "project": str(project_files[0]),
              "functions": [{"id": index, "source": str(functions[index][0]),
                             "sha256": functions[index][1]}
                            for index in sorted(functions)]}
    output_dir = project / "Saved" / "Mazeline"
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / "deferred_registry.json"
    fd, temporary = tempfile.mkstemp(prefix=".registry-", dir=output_dir)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(result, stream, indent=2)
            stream.write("\n")
        os.replace(temporary, output)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    unreal.log("MAZELINE_REGISTRY_CREATED " + str(output))
    tools_dir = Path(__file__).resolve().parents[2] / "Tools"
    sys.path.insert(0, str(tools_dir))
    try:
        from patch_launcher import write_launcher
        launcher = write_launcher(Path(unreal.Paths.engine_dir()).resolve().parent,
                                  project_files[0], output_dir)
    finally:
        sys.path.remove(str(tools_dir))
    unreal.log("MAZELINE_PATCH_LAUNCHER_CREATED " + str(launcher))


main()
