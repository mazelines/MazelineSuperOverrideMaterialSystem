import unreal

asset_path = "/Game/MazelineTests"
asset_name = "M_ForwardOverride"
if unreal.EditorAssetLibrary.does_asset_exist(asset_path + "/" + asset_name):
    raise RuntimeError("Test material already exists; refusing to overwrite it")
material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    asset_name, asset_path, unreal.Material, unreal.MaterialFactoryNew())
if not material:
    raise RuntimeError("Could not create the test material")
library = unreal.MaterialEditingLibrary
custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -400, 0)
custom.set_editor_property("code", "return float3(0.8, 0.2, 0.1);")
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
define = unreal.CustomDefine()
define.set_editor_property("define_name", "MAZELINE_SHADER_OVERRIDE")
define.set_editor_property("define_value", "1")
custom.set_editor_property("additional_defines", [define])
custom.set_editor_property("include_file_paths", ["/Project/Private/MazelineForwardExample.ush"])
if not library.connect_material_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR):
    raise RuntimeError("Could not connect Base Color")
material.set_editor_property("two_sided", False)
library.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material):
    raise RuntimeError("Could not save test material")
unreal.log("MAZELINE_TEST_MATERIAL_CREATED " + material.get_path_name())
