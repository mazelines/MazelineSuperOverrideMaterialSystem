"""One-time migration of the existing comparison materials using UE asset APIs.

Requires the pre-migration Content/Shaders backup recorded in latest-backup.txt.
Does not patch the engine; use the native MazelineShaderPatch commandlet next.
"""
import json
from pathlib import Path
import unreal

root = '/Game/MazelineTests'
lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
saved = Path(unreal.Paths.project_saved_dir()) / 'StyleMigration'
backup = Path((saved / 'latest-backup.txt').read_text(encoding='utf-8-sig').strip())
if not (backup / 'MazelineTests/L_ForwardComparison.umap').is_file():
    raise RuntimeError('Pre-migration backup missing')

mapping = [
    ('M_ForwardControl', 'M_Stylized', 1, 'Stylized'),
    ('M_ForwardOverride', 'M_Toon', 2, 'Toon'),
    ('M_SecondOverride', 'M_HybridToon', 3, 'HybridToon'),
]
for old, new, number, style in mapping:
    if not assets.does_asset_exist(root + '/' + old) or assets.does_asset_exist(root + '/' + new):
        raise RuntimeError('Unexpected migration state: ' + old + ' -> ' + new)
    if not (Path(unreal.Paths.project_dir()) / 'Shaders/Private' / (style + '.ush')).is_file():
        raise RuntimeError('Missing HLSL: ' + style)

if not levels.load_level(root + '/L_ForwardComparison'):
    raise RuntimeError('Cannot load comparison map')
report = {'backup': str(backup), 'materials': [], 'retired_validation': [], 'actors': []}
new_materials = {}
for old, new, number, style in mapping:
    material = assets.load_asset(root + '/' + old)
    customs = [n for n in lib.get_material_expressions(material) if isinstance(n, unreal.MaterialExpressionCustom)]
    if len(customs) > 1:
        raise RuntimeError('Unexpected custom nodes in ' + old)
    if customs:
        custom = customs[0]
    else:
        # Keep the original Base Color graph and insert a passthrough Custom.
        color = lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
        if not color:
            raise RuntimeError('Missing original Base Color in ' + old)
        custom = lib.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 0)
        custom.set_editor_property('code', 'return Tint;')
        custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property('input_name', 'Tint')
        custom.set_editor_property('inputs', [custom_input])
        if not lib.connect_material_expressions(color, '', custom, 'Tint'):
            raise RuntimeError('Cannot preserve Base Color graph')
        if not lib.connect_material_property(custom, '', unreal.MaterialProperty.MP_BASE_COLOR):
            raise RuntimeError('Cannot connect Custom to Base Color')
    define = unreal.CustomDefine()
    define.set_editor_property('define_name', 'MAZELINE_SHADER_OVERRIDE')
    define.set_editor_property('define_value', str(number))
    other_defines = [d for d in custom.get_editor_property('additional_defines')
                     if d.get_editor_property('define_name') != 'MAZELINE_SHADER_OVERRIDE']
    custom.set_editor_property('additional_defines', other_defines + [define])
    custom.set_editor_property('include_file_paths', ['/Project/Private/' + style + '.ush'])
    custom.set_editor_property('description', f'{number}: {style} | per-light HLSL override')
    # Existing color nodes and any connected user-authored inputs are preserved.
    for i, (name, prop, default) in enumerate([
        ('Metallic', unreal.MaterialProperty.MP_METALLIC, 0.0),
        ('Roughness', unreal.MaterialProperty.MP_ROUGHNESS, 0.5),
    ]):
        if not lib.get_material_property_input_node(material, prop):
            node = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -400, 220 + i * 160)
            node.set_editor_property('parameter_name', name)
            node.set_editor_property('default_value', default)
            if not lib.connect_material_property(node, '', prop):
                raise RuntimeError('Cannot connect ' + name)
    if not assets.rename_asset(root + '/' + old, root + '/' + new):
        raise RuntimeError('Asset rename failed: ' + old)
    lib.layout_material_expressions(material)
    lib.recompile_material(material)
    if not assets.save_loaded_asset(material):
        raise RuntimeError('Material save failed: ' + new)
    new_materials[style] = material
    report['materials'].append({'old': old, 'asset': material.get_path_name(), 'id': number,
                                'include': list(custom.get_editor_property('include_file_paths')),
                                'code': custom.get_editor_property('code')})

# Old regression fixtures use different HLSL under IDs 2/3. Retire only their
# marker/include, retaining the passthrough graph and their saved input values.
for name in ['M_Override', 'M_RoughnessOverride']:
    material = assets.load_asset(root + '/InputValidation/' + name)
    if not material:
        raise RuntimeError('Missing validation material: ' + name)
    for node in lib.get_material_expressions(material):
        if not isinstance(node, unreal.MaterialExpressionCustom):
            continue
        node.set_editor_property('additional_defines', [d for d in node.get_editor_property('additional_defines')
            if d.get_editor_property('define_name') != 'MAZELINE_SHADER_OVERRIDE'])
        node.set_editor_property('include_file_paths', [])
        node.set_editor_property('description', 'Archived 0.2 regression fixture: override disabled for style demo')
    lib.recompile_material(material)
    if not assets.save_loaded_asset(material):
        raise RuntimeError('Validation material save failed')
    report['retired_validation'].append(material.get_path_name())

# Keep the existing scene, transforms, lights and assignments. Update labels and
# explicitly save the renamed material references into the loaded map.
for actor in actors.get_all_level_actors():
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    if not component:
        continue
    for slot in range(component.get_num_materials()):
        material = component.get_material(slot)
        for style, expected in new_materials.items():
            if material == expected:
                component.set_material(slot, expected)
                actor.set_actor_label('Mazeline ' + style)
                report['actors'].append({'actor': actor.get_actor_label(), 'material': expected.get_path_name()})
if len(report['actors']) < 3:
    raise RuntimeError('Comparison scene did not retain all three materials')
if not levels.save_current_level():
    raise RuntimeError('Comparison map save failed')
(saved / 'migration-result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('MAZELINE_STYLE_MIGRATION_SUCCESS ' + json.dumps(report))
