"""Read back the Default Lit graph and open its master material for inspection."""
import json
from pathlib import Path
import unreal

handle = None

def show_material(_delta):
    global handle
    if unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets():
        return
    unreal.unregister_slate_post_tick_callback(handle)
    handle = None
    material = unreal.EditorAssetLibrary.load_asset('/Game/MazelineTests/M_Toon')
    instance = unreal.EditorAssetLibrary.load_asset('/Game/MazelineTests/MI_Toon')
    if material.get_editor_property('shading_model') != unreal.MaterialShadingModel.MSM_DEFAULT_LIT:
        raise RuntimeError('M_Toon is not Default Lit')
    nodes = unreal.MaterialEditingLibrary.get_material_expressions(material)
    outputs = [n for n in nodes if isinstance(n, unreal.MaterialExpressionMazelineGGXOutput)]
    if len(outputs) != 1:
        raise RuntimeError('Expected exactly one GGX output')
    inputs = {}
    for name in ['Sharpness', 'Threshold']:
        node = unreal.MazelineMaterialLibrary.get_ggx_parameter_input(material, name == 'Threshold')
        if not isinstance(node, unreal.MaterialExpressionScalarParameter) or str(node.get_editor_property('parameter_name')) != name:
            raise RuntimeError('Incorrect output input: ' + name)
        inputs[name] = node.get_path_name()
    report = {'shading_model': str(material.get_editor_property('shading_model')),
              'output': outputs[0].get_path_name(), 'inputs': inputs,
              'instance_parameters': {name: unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, name)
                                      for name in ['Roughness', 'Sharpness', 'Threshold']}}
    out = Path(unreal.Paths.project_saved_dir()) / 'DefaultLitGGX/editor-readback.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2), encoding='utf-8')
    unreal.EditorAssetLibrary.sync_browser_to_objects(['/Game/MazelineTests/M_Toon', '/Game/MazelineTests/MI_Toon'])
    if not unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([material]):
        raise RuntimeError('Could not open M_Toon')
    unreal.log('MAZELINE_DEFAULT_LIT_EDITOR_VERIFIED ' + str(out))

handle = unreal.register_slate_post_tick_callback(show_material)
