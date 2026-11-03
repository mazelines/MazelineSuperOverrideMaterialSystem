"""Historical constant-GGX setup. Use configure_ggx_parameters.py for current assets."""
import json
from pathlib import Path
import unreal

lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
material = assets.load_asset('/Game/MazelineTests/M_Toon')
if not isinstance(material, unreal.Material):
    raise RuntimeError('M_Toon material missing')
nodes = list(lib.get_material_expressions(material))
roughness = lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_ROUGHNESS)
if not isinstance(roughness, unreal.MaterialExpressionScalarParameter):
    raise RuntimeError('Roughness graph changed; inspect before modifying')
base = lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
metallic = lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_METALLIC)
customs = [n for n in nodes if isinstance(n, unreal.MaterialExpressionCustom)]
if len(customs) != 1:
    raise RuntimeError('Unexpected Custom expression count')
custom = customs[0]
defines = {d.get_editor_property('define_name'): d.get_editor_property('define_value')
           for d in custom.get_editor_property('additional_defines')}
if defines.get('MAZELINE_SHADER_OVERRIDE') != '2':
    raise RuntimeError('M_Toon must select function 2')
if '/Project/Private/Toon.ush' not in custom.get_editor_property('include_file_paths'):
    raise RuntimeError('M_Toon include changed')
before = {
    'parameter': str(roughness.get_editor_property('parameter_name')),
    'default': roughness.get_editor_property('default_value'),
    'base_color': base.get_path_name(),
    'metallic': metallic.get_path_name(),
    'node_count': len(nodes),
    'custom_code': custom.get_editor_property('code'),
    'textures': [n.get_editor_property('texture').get_path_name()
                 for n in nodes if isinstance(n, unreal.MaterialExpressionTextureSample)
                 and n.get_editor_property('texture')],
}
roughness.set_editor_property('group', 'Toon Highlight')
roughness.set_editor_property('desc', 'GGX roughness controls lobe width. Sharpness and Threshold reshape its distribution without blending.')
custom.set_editor_property('description', '2: Toon | GGX distribution shaping')
lib.recompile_material(material)
if lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR) != base:
    raise RuntimeError('Base Color connection changed')
if lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_ROUGHNESS) != roughness:
    raise RuntimeError('Roughness connection changed')
if len(lib.get_material_expressions(material)) != before['node_count']:
    raise RuntimeError('Graph node count changed')
if not assets.save_loaded_asset(material):
    raise RuntimeError('Failed to save M_Toon')
report = Path(unreal.Paths.project_saved_dir()) / 'GGXHighlight/material.json'
report.parent.mkdir(parents=True, exist_ok=True)
report.write_text(json.dumps({'preserved': before, 'passed': True}, indent=2), encoding='utf-8')
unreal.log('MAZELINE_TOON_HIGHLIGHT_MATERIAL_PASS ' + str(report))
