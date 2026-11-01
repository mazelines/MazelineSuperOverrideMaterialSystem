"""Isolate function ID 2's Roughness output using fully metallic surfaces."""
import unreal

root = "/Game/MazelineTests/InputValidation"
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
if assets.does_asset_exist(root + "/M_RoughnessOverride"):
    raise RuntimeError("Roughness validation assets already exist; refusing to overwrite")
source = assets.load_asset(root + "/M_Override")
material = tools.duplicate_asset("M_RoughnessOverride", root, source)
customs = [node for node in lib.get_material_expressions(material) if isinstance(node, unreal.MaterialExpressionCustom)]
if len(customs) != 1:
    raise RuntimeError("Expected exactly one Custom Expression")
define = unreal.CustomDefine()
define.set_editor_property("define_name", "MAZELINE_SHADER_OVERRIDE")
define.set_editor_property("define_value", "2")
customs[0].set_editor_property("additional_defines", [define])
customs[0].set_editor_property("include_file_paths", ["/Project/Private/MazelineSecondExample.ush"])
lib.recompile_material(material)
if not assets.save_loaded_asset(material):
    raise RuntimeError("Could not save roughness override material")
control = assets.load_asset(root + "/M_Control")
colors = (unreal.LinearColor(0.65, 0.15, 0.06, 1), unreal.LinearColor(0.08, 0.5, 0.2, 1),
          unreal.LinearColor(0.1, 0.2, 0.65, 1))
for column, color in enumerate(colors):
    for kind, parent, roughness in (("Control", control, 0.85), ("Override", material, 0.15)):
        name = f"MI_Roughness{kind}_{column}"
        instance = tools.create_asset(name, root, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        lib.set_material_instance_parent(instance, parent)
        lib.set_material_instance_vector_parameter_value(instance, "Color", color)
        lib.set_material_instance_scalar_parameter_value(instance, "Metallic", 1.0)
        lib.set_material_instance_scalar_parameter_value(instance, "Roughness", roughness)
        lib.update_material_instance(instance)
        if not assets.save_loaded_asset(instance):
            raise RuntimeError("Could not save " + name)
        if abs(lib.get_material_instance_scalar_parameter_value(instance, "Roughness") - roughness) > 0.0001:
            raise RuntimeError("Roughness readback mismatch")
unreal.log("MAZELINE_ROUGHNESS_VALIDATION_CREATED: control=0.85, override input=0.15, HLSL output=0.85, metallic=1")
