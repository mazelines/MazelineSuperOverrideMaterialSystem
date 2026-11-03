"""Open the ramp controls and make room for the three newly added graph nodes."""
from pathlib import Path
import unreal

handle = None

def show_ramp(_delta):
    global handle
    if unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets():
        return
    unreal.unregister_slate_post_tick_callback(handle)
    handle = None
    assets = unreal.EditorAssetLibrary
    material = assets.load_asset('/Game/MazelineTests/M_Toon')
    for node in unreal.MaterialEditingLibrary.get_material_expressions(material):
        position = None
        if isinstance(node, unreal.MaterialExpressionTextureObject):
            if isinstance(node.get_editor_property('texture'), unreal.CurveLinearColorAtlas):
                position = (-1400, -550)
        elif isinstance(node, unreal.MaterialExpressionScalarParameter):
            if str(node.get_editor_property('parameter_name')) == 'RampRow':
                position = (-1400, -200)
        elif isinstance(node, unreal.MaterialExpressionMazelineRampOutput):
            position = (-480, 840)
        if position:
            node.set_editor_property('material_expression_editor_x', position[0])
            node.set_editor_property('material_expression_editor_y', position[1])
    assert assets.save_loaded_asset(material)
    script = Path(unreal.Paths.project_dir()) / 'Tools/verify_directional_ramp_assets.py'
    exec(compile(script.read_text(encoding='utf-8'), str(script), 'exec'), {})
    instance = assets.load_asset('/Game/MazelineTests/MI_Toon')
    curve = assets.load_asset('/Game/MazelineTests/Ramps/CR_ToonSteps')
    assets.sync_browser_to_objects(['/Game/MazelineTests/M_Toon', '/Game/MazelineTests/MI_Toon'])
    assert unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([curve, instance, material])
    unreal.log('MAZELINE_RAMP_EDITOR_READY')

handle = unreal.register_slate_post_tick_callback(show_ramp)
