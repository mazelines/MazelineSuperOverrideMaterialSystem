"""Update the remaining scene captions after HybridToon retirement."""
import json
import re
from pathlib import Path
import unreal

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
changes = []
for asset in registry.get_assets_by_path('/Game/MazelineTests', recursive=True):
    if str(asset.asset_class_path.asset_name) != 'World':
        continue
    assert levels.load_level(str(asset.package_name))
    changed = False
    for actor in actors.get_all_level_actors():
        label = actor.get_actor_label()
        if 'HybridToon' in label:
            actor.set_actor_label(label.replace('HybridToon', 'Toon Alternate'))
            changes.append({'actor': actor.get_path_name(), 'label': label})
            changed = True
        for component in actor.get_components_by_class(unreal.TextRenderComponent):
            caption = str(component.get_editor_property('text'))
            if re.search(r'hybrid\s*toon', caption, re.IGNORECASE):
                component.set_text(re.sub(r'hybrid\s*toon', 'TOON ALT', caption, flags=re.IGNORECASE))
                changes.append({'actor': actor.get_path_name(), 'caption': caption})
                changed = True
    if changed:
        assert levels.save_current_level()
out = Path(unreal.Paths.project_saved_dir()) / 'HybridRetirement/labels.json'
out.write_text(json.dumps(changes, indent=2), encoding='utf-8')
unreal.log('MAZELINE_HYBRID_LABELS ' + json.dumps(changes))
