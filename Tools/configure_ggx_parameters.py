"""Expose GGX controls on Opaque Default Lit through Mazeline GGX Output."""
import json
from pathlib import Path
import unreal

lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
material = assets.load_asset('/Game/MazelineTests/M_Toon')
if not isinstance(material, unreal.Material):
    raise RuntimeError('M_Toon missing')
properties = [unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_METALLIC,
              unreal.MaterialProperty.MP_ROUGHNESS, unreal.MaterialProperty.MP_SPECULAR,
              unreal.MaterialProperty.MP_AMBIENT_OCCLUSION]
before = {str(p): lib.get_material_property_input_node(material, p) for p in properties}
nodes = list(lib.get_material_expressions(material))
params = {str(n.get_editor_property('parameter_name')): n for n in nodes
          if isinstance(n, unreal.MaterialExpressionScalarParameter)}
for name, default, y in [('Sharpness', 0.5, 450), ('Threshold', 0.25, 650)]:
    existing = unreal.MazelineMaterialLibrary.get_ggx_parameter_input(material, name == 'Threshold')
    if existing and existing != params.get(name):
        raise RuntimeError('Existing custom data connection must be inspected')
    node = params.get(name)
    if not node:
        node = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -300, y)
        node.set_editor_property('parameter_name', name)
        node.set_editor_property('default_value', default)
    node.set_editor_property('group', 'Toon Highlight')
    node.set_editor_property('slider_min', 0.0)
    node.set_editor_property('slider_max', 1.0)
    node.set_editor_property('desc', 'GGX ' + name + '. Mazeline GGX Output carries this independently of Specular and AO.')
    params[name] = node
if not unreal.MazelineMaterialLibrary.migrate_ggx_parameters_to_default_lit(material, params['Sharpness'], params['Threshold']):
    raise RuntimeError('Could not connect Default Lit GGX output; existing graph preserved')
if material.get_editor_property('shading_model') != unreal.MaterialShadingModel.MSM_DEFAULT_LIT:
    raise RuntimeError('Default Lit was not selected')
params['Roughness'].set_editor_property('desc', 'GGX lobe width. Sharpness and Threshold control its boundary independently.')
for node in nodes:
    if isinstance(node, unreal.MaterialExpressionCustom):
        node.set_editor_property('description', '2: Toon | Default Lit | Mazeline GGX Output carries Sharpness / Threshold')
lib.recompile_material(material)
for p in properties:
    if lib.get_material_property_input_node(material, p) != before[str(p)]:
        raise RuntimeError('Original pin changed: ' + str(p))
if not assets.save_loaded_asset(material):
    raise RuntimeError('Could not save M_Toon')

instance_path = '/Game/MazelineTests/MI_Toon'
instance = assets.load_asset(instance_path)
if instance and (not isinstance(instance, unreal.MaterialInstanceConstant) or instance.get_editor_property('parent') != material):
    raise RuntimeError('MI_Toon exists with a different parent/type')
if not instance:
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset('MI_Toon', '/Game/MazelineTests', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    lib.set_material_instance_parent(instance, material)
    for name in ['Roughness', 'Sharpness', 'Threshold']:
        lib.set_material_instance_scalar_parameter_value(instance, name, params[name].get_editor_property('default_value'))
    lib.update_material_instance(instance)
    if not assets.save_loaded_asset(instance):
        raise RuntimeError('Could not save MI_Toon')
report = {'preserved_pins': {k: v.get_path_name() if v else None for k, v in before.items()},
          'parameters': {n: params[n].get_editor_property('default_value') for n in ['Roughness', 'Sharpness', 'Threshold']},
          'instance': instance_path, 'carrier': 'Default Lit + Mazeline GGX Output -> CustomData.xy',
          'shading_model': str(material.get_editor_property('shading_model')), 'passed': True}
out = Path(unreal.Paths.project_saved_dir()) / 'DefaultLitGGX/material.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('MAZELINE_GGX_PARAMETERS_MATERIAL_PASS ' + str(out))
