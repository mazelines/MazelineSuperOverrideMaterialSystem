"""Show the migrated materials after the normal editor has finished starting."""
import unreal

paths = ['/Game/MazelineTests/M_Stylized', '/Game/MazelineTests/M_Toon', '/Game/MazelineTests/M_HybridToon']
handle = None

def show_materials(_delta):
    global handle
    if unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets():
        return
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        handle = None
    unreal.EditorAssetLibrary.sync_browser_to_objects(paths)
    material = unreal.EditorAssetLibrary.load_asset(paths[1])
    if not unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([material]):
        raise RuntimeError('Could not open the Toon material editor')
    unreal.log('MAZELINE_STYLE_EDITOR_OPENED ' + material.get_path_name())

handle = unreal.register_slate_post_tick_callback(show_materials)
