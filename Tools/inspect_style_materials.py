import json
from pathlib import Path
import unreal

lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
result = []
for path in assets.list_assets('/Game/MazelineTests', recursive=True, include_folder=False):
    asset = assets.load_asset(path)
    if not isinstance(asset, unreal.Material):
        continue
    nodes = []
    for node in lib.get_material_expressions(asset):
        row = {'type': node.get_class().get_name(), 'name': node.get_name()}
        if isinstance(node, unreal.MaterialExpressionCustom):
            row.update(code=node.get_editor_property('code'),
                       includes=list(node.get_editor_property('include_file_paths')),
                       defines=[{'name': d.get_editor_property('define_name'), 'value': d.get_editor_property('define_value')} for d in node.get_editor_property('additional_defines')])
        nodes.append(row)
    result.append({'material': asset.get_path_name(), 'nodes': nodes})
output = Path(unreal.Paths.project_saved_dir()) / 'StyleMigration/materials-before.json'
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
unreal.log('MAZELINE_STYLE_INSPECTED ' + str(output))
