import unreal

path = "/Game/MazelineTests"
name = "M_SecondOverride"
if unreal.EditorAssetLibrary.does_asset_exist(path + "/" + name):
    raise RuntimeError("Second test material already exists")
material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())
node = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionCustom, -400, 0)
node.set_editor_property("code", "return float3(0.1, 0.3, 0.8);")
node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
define = unreal.CustomDefine()
define.set_editor_property("define_name", "MAZELINE_SHADER_OVERRIDE")
define.set_editor_property("define_value", "2")
node.set_editor_property("additional_defines", [define])
node.set_editor_property("include_file_paths", ["/Project/Private/MazelineSecondExample.ush"])
if not unreal.MaterialEditingLibrary.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR):
    raise RuntimeError("Could not connect Base Color")
unreal.MaterialEditingLibrary.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material):
    raise RuntimeError("Could not save second test material")
unreal.log("MAZELINE_SECOND_MATERIAL_CREATED " + material.get_path_name())
