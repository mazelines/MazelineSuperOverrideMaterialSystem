"""Capture control and override using identical geometry, camera and lights."""
import json
import os
from pathlib import Path
import time
import unreal

prefix = os.environ.get("MAZELINE_VALIDATION_PATH", "InputValidation")
mode = os.environ.get("MAZELINE_VALIDATION_MODE", "inputs")
control_only = os.environ.get("MAZELINE_CONTROL_ONLY", "0") == "1"
if mode not in ("inputs", "roughness"):
    raise RuntimeError("Unknown validation mode")
if not control_only:
    # The three-style demo retires the old registration. Never report a false
    # regression pass by comparing two ordinary Default Lit material graphs.
    expected_id, expected_file, parent_name = (
        ('3', 'MazelinePassthroughValidation.ush', 'M_Override') if mode == 'inputs'
        else ('2', 'MazelineSecondExample.ush', 'M_RoughnessOverride'))
    parent = unreal.EditorAssetLibrary.load_asset('/Game/MazelineTests/InputValidation/' + parent_name)
    registered = False
    for node in unreal.MaterialEditingLibrary.get_material_expressions(parent):
        if isinstance(node, unreal.MaterialExpressionCustom):
            defines = {d.get_editor_property('define_name'): d.get_editor_property('define_value')
                       for d in node.get_editor_property('additional_defines')}
            registered |= (defines.get('MAZELINE_SHADER_OVERRIDE') == expected_id and
                '/Project/Private/' + expected_file in list(node.get_editor_property('include_file_paths')))
    if not registered:
        raise RuntimeError('Legacy validation profile is inactive. Restore its backed-up materials, HLSL and native patch registry before running this regression.')
output_dir = Path(unreal.Paths.project_saved_dir()).resolve() / prefix
output_dir.mkdir(parents=True, exist_ok=False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
camera = next(a for a in actors if a.get_actor_label() == "Input Camera")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for command in ("r.AntiAliasingMethod 0", "r.ScreenPercentage 100", "r.EyeAdaptationQuality 0",
                "r.MotionBlurQuality 0", "r.BloomQuality 0", "r.DynamicGlobalIlluminationMethod 0",
                "r.ReflectionMethod 0"):
    unreal.SystemLibrary.execute_console_command(world, command)

spheres = [(a, a.get_actor_label().split("_")) for a in actors if a.get_actor_label().startswith("Input_")]
if len(spheres) != 9:
    raise RuntimeError("Expected nine validation spheres")
override_materials = {}
saved_materials = {}
for actor, (_, row, column) in spheres:
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    saved_materials[actor] = component.get_material(0)
    override_name = f"MI_Override_{row}_{column}" if mode == "inputs" else f"MI_RoughnessOverride_{column}"
    if mode == "roughness":
        control = unreal.EditorAssetLibrary.load_asset(f"/Game/MazelineTests/InputValidation/MI_RoughnessControl_{column}")
        if not control:
            raise RuntimeError("Missing roughness control material")
        component.set_material(0, control)
    material = unreal.EditorAssetLibrary.load_asset("/Game/MazelineTests/InputValidation/" + override_name)
    if not material:
        raise RuntimeError("Missing override material")
    override_materials[actor] = material
started = time.monotonic()
stage = "control"
requested = False
task = None
handle = None


def tick(delta):
    global started, stage, requested, task, handle
    output = output_dir / (stage + ".png")
    if not requested and time.monotonic() - started > 30:
        # This API pumps Slate while setting up the request, so guard re-entry
        # before invoking it, not after it returns.
        requested = True
        task = None
        task = unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, str(output), camera, delay=3.0)
        unreal.log("MAZELINE_INPUT_CAPTURE_REQUEST " + str(output))
    if requested and task is not None and output.is_file() and task.is_task_done():
        unreal.log("MAZELINE_INPUT_CAPTURE_DONE " + str(output))
        if stage == "control" and not control_only:
            for actor, _ in spheres:
                actor.get_component_by_class(unreal.StaticMeshComponent).set_material(0, override_materials[actor])
            stage = "override"
            requested = False
            started = time.monotonic()
        else:
            # Restore loaded editor scene to the saved control state; do not save test swaps.
            for actor, (_, row, column) in spheres:
                actor.get_component_by_class(unreal.StaticMeshComponent).set_material(0, saved_materials[actor])
            (output_dir / "complete.json").write_text(json.dumps({"captures": ["control.png"] if control_only else ["control.png", "override.png"],
                "mode": mode, "metallic_rows": [0.0, 0.5, 1.0] if mode == "inputs" else [1.0] * 3,
                "roughness_columns": [0.15, 0.5, 0.85] if mode == "inputs" else [0.85] * 3,
                "override_input_roughness": None if mode == "inputs" else 0.15, "lights": 2}))
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.log("MAZELINE_INPUT_VALIDATION_CAPTURE_COMPLETE")
            unreal.SystemLibrary.quit_editor()


handle = unreal.register_slate_post_tick_callback(tick)
