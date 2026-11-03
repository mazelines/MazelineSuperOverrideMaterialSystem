"""Connect an editable Curve Atlas through Custom inputs without changing PBR pins."""
import json
from pathlib import Path
import unreal

assets = unreal.EditorAssetLibrary
lib = unreal.MaterialEditingLibrary
material = assets.load_asset('/Game/MazelineTests/M_Toon')
assert isinstance(material, unreal.Material)
assert material.get_editor_property('shading_model') == unreal.MaterialShadingModel.MSM_DEFAULT_LIT
saved = Path(unreal.Paths.project_saved_dir()) / 'DirectionalRamp'
assert (saved / 'latest-backup.txt').exists(), 'Create a content/source backup first'
properties = [unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_METALLIC,
              unreal.MaterialProperty.MP_ROUGHNESS, unreal.MaterialProperty.MP_SPECULAR,
              unreal.MaterialProperty.MP_AMBIENT_OCCLUSION]
before = {str(p): lib.get_material_property_input_node(material, p) for p in properties}
nodes = list(lib.get_material_expressions(material))
customs = [n for n in nodes if isinstance(n, unreal.MaterialExpressionCustom)]
assert len(customs) == 1
custom = customs[0]
atlas = unreal.MazelineMaterialLibrary.create_toon_ramp_examples('/Game/MazelineTests/Ramps')
assert isinstance(atlas, unreal.CurveLinearColorAtlas)
for curve in atlas.get_editor_property('gradient_curves'):
    assert assets.save_loaded_asset(curve)
assert assets.save_loaded_asset(atlas)

inputs = list(custom.get_editor_property('inputs'))
for name in ['RampLUT', 'RampRow']:
    if not any(str(i.get_editor_property('input_name')) == name for i in inputs):
        value = unreal.CustomInput()
        value.set_editor_property('input_name', name)
        inputs.append(value)
custom.set_editor_property('inputs', inputs)
outputs = list(custom.get_editor_property('additional_outputs'))
if not any(str(o.get_editor_property('output_name')) == 'RampIndex' for o in outputs):
    output = unreal.CustomOutput()
    output.set_editor_property('output_name', 'RampIndex')
    output.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    outputs.append(output)
custom.set_editor_property('additional_outputs', outputs)
code = custom.get_editor_property('code')
if 'RampIndex = RampRow;' not in code:
    custom.set_editor_property('code', 'RampIndex = RampRow;\n' + code)
custom.set_editor_property('description', '2: Toon | RampLUT: Directional diffuse only | RampIndex -> Mazeline Ramp Output')

textures = [n for n in nodes if isinstance(n, unreal.MaterialExpressionTextureObject)
            and n.get_editor_property('texture') == atlas]
assert len(textures) <= 1
texture = textures[0] if textures else lib.create_material_expression(material, unreal.MaterialExpressionTextureObject, -1400, -550)
texture.set_editor_property('texture', atlas)
texture.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
texture.set_editor_property('desc', 'Shared Curve Atlas registered from RampLUT. Edit curve keys live; changing the atlas asset requires Apply / Update.')
params = [n for n in nodes if isinstance(n, unreal.MaterialExpressionScalarParameter)
          and str(n.get_editor_property('parameter_name')) == 'RampRow']
assert len(params) <= 1
row = params[0] if params else lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1400, -200)
if not params:
    row.set_editor_property('parameter_name', 'RampRow')
    row.set_editor_property('default_value', 2.0)
row.set_editor_property('group', 'Toon Diffuse')
row.set_editor_property('slider_min', 0.0)
row.set_editor_property('slider_max', 2.0)
row.set_editor_property('desc', 'Curve Atlas row: 0 Linear, 1 Soft, 2 Steps. Rounded to an integer; independent of function ID 2.')
ramp_outputs = [n for n in nodes if isinstance(n, unreal.MaterialExpressionMazelineRampOutput)]
assert len(ramp_outputs) <= 1
ramp_output = ramp_outputs[0] if ramp_outputs else lib.create_material_expression(material, unreal.MaterialExpressionMazelineRampOutput, -480, 840)
assert lib.connect_material_expressions(texture, '', custom, 'RampLUT')
assert lib.connect_material_expressions(row, '', custom, 'RampRow')
assert lib.connect_material_expressions(custom, 'RampIndex', ramp_output, 'RampRow')
lib.recompile_material(material)
for prop in properties:
    assert lib.get_material_property_input_node(material, prop) == before[str(prop)], str(prop)
assert assets.save_loaded_asset(material)
instance = assets.load_asset('/Game/MazelineTests/MI_Toon')
assert instance.get_editor_property('parent') == material
lib.update_material_instance(instance)
assert assets.save_loaded_asset(instance)
report = {'passed': True, 'material': material.get_path_name(), 'atlas': atlas.get_path_name(),
          'rows': [c.get_path_name() for c in atlas.get_editor_property('gradient_curves')],
          'default_row': row.get_editor_property('default_value'),
          'preserved_pins': {k: v.get_path_name() if v else None for k, v in before.items()},
          'custom_code': custom.get_editor_property('code'), 'shading_model': 'Default Lit'}
(saved / 'material.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('MAZELINE_DIRECTIONAL_RAMP_CONFIGURED ' + json.dumps(report))
