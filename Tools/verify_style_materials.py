"""Read back saved assets and assert the three style registrations and graph paths."""
import json
from pathlib import Path
import unreal

assets = unreal.EditorAssetLibrary
lib = unreal.MaterialEditingLibrary
root = '/Game/MazelineTests'
rows = []
for number, style in [(1, 'Stylized'), (2, 'Toon'), (3, 'HybridToon')]:
    material = assets.load_asset(root + '/M_' + style)
    assert isinstance(material, unreal.Material), style
    customs = [n for n in lib.get_material_expressions(material) if isinstance(n, unreal.MaterialExpressionCustom)]
    assert len(customs) == 1, style
    custom = customs[0]
    defines = {d.get_editor_property('define_name'): d.get_editor_property('define_value')
               for d in custom.get_editor_property('additional_defines')}
    assert defines['MAZELINE_SHADER_OVERRIDE'] == str(number), style
    assert list(custom.get_editor_property('include_file_paths')) == ['/Project/Private/' + style + '.ush'], style
    inputs = {}
    queue = []
    for name, prop in [('BaseColor', unreal.MaterialProperty.MP_BASE_COLOR),
                       ('Metallic', unreal.MaterialProperty.MP_METALLIC),
                       ('Roughness', unreal.MaterialProperty.MP_ROUGHNESS)]:
        node = lib.get_material_property_input_node(material, prop)
        assert node, style + ': missing ' + name
        inputs[name] = node.get_class().get_name()
        queue.append(node)
    visited = set()
    while queue:
        node = queue.pop()
        if not node or node in visited:
            continue
        visited.add(node)
        queue.extend(lib.get_inputs_for_material_expression(material, node))
    assert custom in visited, style + ': Custom disconnected'
    if style == 'HybridToon':
        assert inputs['BaseColor'] == 'MaterialExpressionMultiply', 'User color multiply was lost'
    rows.append({'material': material.get_path_name(), 'id': number, 'style': style,
                 'include': list(custom.get_editor_property('include_file_paths')), 'pins': inputs,
                 'custom_reachable': True, 'node_count': len(lib.get_material_expressions(material))})

for name in ['M_Override', 'M_RoughnessOverride']:
    material = assets.load_asset(root + '/InputValidation/' + name)
    for node in lib.get_material_expressions(material):
        if isinstance(node, unreal.MaterialExpressionCustom):
            assert not any(d.get_editor_property('define_name') == 'MAZELINE_SHADER_OVERRIDE'
                           for d in node.get_editor_property('additional_defines')), name
            assert not list(node.get_editor_property('include_file_paths')), name

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(root + '/L_ForwardComparison')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
for row in rows:
    actor = next(a for a in actors if a.get_actor_label() == 'Mazeline ' + row['style'])
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    assert component.get_material(0).get_path_name() == row['material'], row['style']

output = Path(unreal.Paths.project_saved_dir()) / 'StyleMigration/verification.json'
output.write_text(json.dumps({'passed': True, 'materials': rows, 'scene_assignments': 3}, indent=2), encoding='utf-8')
unreal.log('MAZELINE_STYLE_VERIFIED ' + str(output))
