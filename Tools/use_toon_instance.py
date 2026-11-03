"""Assign the editable Toon instance only to existing M_Toon slots in Example."""
import json
from pathlib import Path
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets = unreal.EditorAssetLibrary
master = assets.load_asset('/Game/MazelineTests/M_Toon')
instance = assets.load_asset('/Game/MazelineTests/MI_Toon')
if not master or not instance or instance.get_editor_property('parent') != master:
    raise RuntimeError('Expected Toon material and its instance')
if not levels.load_level('/Game/MazelineTests/Example'):
    raise RuntimeError('Could not load Example')
changed = []
for actor in actors.get_all_level_actors():
    for component in actor.get_components_by_class(unreal.StaticMeshComponent):
        for index in range(component.get_num_materials()):
            if component.get_material(index) == master:
                component.set_material(index, instance)
                changed.append({'actor': actor.get_actor_label(), 'slot': index})
if changed and not levels.save_current_level():
    raise RuntimeError('Could not save Example')
out = Path(unreal.Paths.project_saved_dir()) / 'GGXParameters/instance-assignment.json'
out.write_text(json.dumps({'changed': changed}, indent=2), encoding='utf-8')
unreal.log('MAZELINE_TOON_INSTANCE_ASSIGNED ' + str(changed))
