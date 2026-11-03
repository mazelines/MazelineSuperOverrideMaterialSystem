"""Retire the HybridToon example through UE APIs after a full content backup.

Replace mesh material slots with MI_Toon, retain scene actors and transforms,
then refuse deletion if any other asset still references the old material.
"""
import json
from pathlib import Path
import unreal

assets = unreal.EditorAssetLibrary
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
output = Path(unreal.Paths.project_saved_dir()) / 'HybridRetirement'
backup = Path((output / 'latest-backup.txt').read_text(encoding='utf-8-sig').strip())
assert (backup / 'MazelineTests/M_HybridToon.uasset').is_file(), 'Backup missing'
old_path = '/Game/MazelineTests/M_HybridToon'
old = assets.load_asset(old_path)
replacement = assets.load_asset('/Game/MazelineTests/MI_Toon')
assert isinstance(old, unreal.Material)
assert isinstance(replacement, unreal.MaterialInstanceConstant)
changed = []

# Inspect all project test maps, including maps with stale registry metadata.
for asset in registry.get_assets_by_path('/Game/MazelineTests', recursive=True):
    if str(asset.asset_class_path.asset_name) != 'World':
        continue
    path = str(asset.package_name)
    assert levels.load_level(path), path
    map_changes = []
    for actor in actors.get_all_level_actors():
        for component in actor.get_components_by_class(unreal.MeshComponent):
            for slot in range(component.get_num_materials()):
                if component.get_material(slot) != old:
                    continue
                label = actor.get_actor_label()
                component.set_material(slot, replacement)
                if 'HybridToon' in label:
                    actor.set_actor_label(label.replace('HybridToon', 'Toon Alternate'))
                map_changes.append({'actor': actor.get_path_name(), 'slot': slot,
                                    'old_label': label, 'new_label': actor.get_actor_label(),
                                    'material': replacement.get_path_name()})
    if map_changes:
        assert levels.save_current_level(), path
        changed.append({'map': path, 'slots': map_changes})

registry.scan_paths_synchronous(['/Game/MazelineTests'], force_rescan=True)
refs = list(assets.find_package_referencers_for_asset(old_path, True))
assert not refs, 'Unconverted references prevent retirement: ' + str(refs)
assert assets.delete_asset(old_path), 'Material deletion failed'
assert not assets.does_asset_exist(old_path), 'Deleted asset still present'
report = {'backup': str(backup), 'replacement': replacement.get_path_name(),
          'changed_maps': changed, 'remaining_references': refs, 'deleted': old_path}
(output / 'retirement.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('MAZELINE_HYBRID_RETIRED ' + json.dumps(report))
