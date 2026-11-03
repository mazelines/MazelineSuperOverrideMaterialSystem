"""Read back the saved ramp graph and instance without modifying assets."""
import json
from pathlib import Path
import unreal

lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
material = assets.load_asset('/Game/MazelineTests/M_Toon')
instance = assets.load_asset('/Game/MazelineTests/MI_Toon')
atlas = assets.load_asset('/Game/MazelineTests/Ramps/CA_ToonDiffuse')
assert material.get_editor_property('shading_model') == unreal.MaterialShadingModel.MSM_DEFAULT_LIT
assert instance.get_editor_property('parent') == material
nodes = list(lib.get_material_expressions(material))
customs = [n for n in nodes if isinstance(n, unreal.MaterialExpressionCustom)]
ramps = [n for n in nodes if isinstance(n, unreal.MaterialExpressionMazelineRampOutput)]
ggxs = [n for n in nodes if isinstance(n, unreal.MaterialExpressionMazelineGGXOutput)]
assert len(customs) == len(ramps) == len(ggxs) == 1
custom = customs[0]
names = [str(i.get_editor_property('input_name')) for i in custom.get_editor_property('inputs')]
connections = list(lib.get_inputs_for_material_expression(material, custom))
inputs = dict(zip(names, connections))
assert isinstance(inputs['RampLUT'], unreal.MaterialExpressionTextureObject)
assert inputs['RampLUT'].get_editor_property('texture') == atlas
assert str(inputs['RampRow'].get_editor_property('parameter_name')) == 'RampRow'
assert list(lib.get_inputs_for_material_expression(material, ramps[0])) == [custom]
assert 'RampIndex = RampRow;' in custom.get_editor_property('code')
for name in ['Sharpness', 'Threshold']:
    node = unreal.MazelineMaterialLibrary.get_ggx_parameter_input(material, name == 'Threshold')
    assert str(node.get_editor_property('parameter_name')) == name
rows = [c.get_path_name() for c in atlas.get_editor_property('gradient_curves')]
assert len(rows) == 3
assert not assets.does_asset_exist('/Game/MazelineTests/M_HybridToon')
parameters = {name: lib.get_material_instance_scalar_parameter_value(instance, name)
              for name in ['Roughness', 'Sharpness', 'Threshold', 'RampRow']}
assert parameters == {'Roughness': 0.5, 'Sharpness': 0.5, 'Threshold': 0.25, 'RampRow': 2.0}
used_textures = [t.get_path_name() for t in lib.get_used_textures(material)]
report = {'passed': True, 'shading_model': 'Default Lit', 'instance_parameters': parameters,
          'atlas': atlas.get_path_name(), 'curves': rows, 'used_textures': used_textures,
          'nodes': [{'type': n.get_class().get_name(), 'path': n.get_path_name(),
                     'x': n.get_editor_property('material_expression_editor_x'),
                     'y': n.get_editor_property('material_expression_editor_y')} for n in nodes]}
out = Path(unreal.Paths.project_saved_dir()) / 'DirectionalRamp/asset-verification.json'
out.write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('MAZELINE_RAMP_ASSETS_VERIFIED ' + str(out))
