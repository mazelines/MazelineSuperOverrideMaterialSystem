import unreal

for name in ("MaterialExpressionCustom", "CustomDefine", "CustomInput", "MaterialEditingLibrary"):
    item = getattr(unreal, name, None)
    unreal.log("MAZELINE_API %s: %s" % (name, item))
    if item:
        unreal.log("MAZELINE_API_MEMBERS %s: %s" % (name, ", ".join(dir(item))))
unreal.log("MAZELINE_OUTPUT_ENUM " + str(dir(unreal.CustomMaterialOutputType)))
for name in ("EditorLevelLibrary", "LevelEditorSubsystem", "EditorActorSubsystem", "AutomationLibrary", "MaterialEditingLibrary"):
    item = getattr(unreal, name, None)
    unreal.log("MAZELINE_SCENE_API %s: %s" % (name, ", ".join(dir(item)) if item else "unavailable"))
for method in (unreal.AutomationLibrary.take_high_res_screenshot, unreal.MaterialEditingLibrary.get_statistics, unreal.MaterialEditingLibrary.get_inputs_for_material_expression, unreal.MaterialEditingLibrary.get_material_property_input_node, unreal.MaterialEditingLibrary.get_material_expressions):
    unreal.log("MAZELINE_METHOD_DOC " + str(method.__doc__))
