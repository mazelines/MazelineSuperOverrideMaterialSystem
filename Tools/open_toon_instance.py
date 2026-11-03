"""Open the saved Toon instance once the normal editor finishes asset loading."""
import unreal

handle = None
def show_instance(_delta):
    global handle
    if unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets():
        return
    unreal.unregister_slate_post_tick_callback(handle)
    handle = None
    asset = unreal.EditorAssetLibrary.load_asset('/Game/MazelineTests/MI_Toon')
    unreal.EditorAssetLibrary.sync_browser_to_objects(['/Game/MazelineTests/M_Toon', '/Game/MazelineTests/MI_Toon'])
    if not unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([asset]):
        raise RuntimeError('Could not open Toon instance')
    unreal.log('MAZELINE_TOON_INSTANCE_EDITOR_OPENED')
handle = unreal.register_slate_post_tick_callback(show_instance)
