"""Create saved input-regression assets through Unreal Editor APIs only."""
import unreal

root = "/Game/MazelineTests/InputValidation"
lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
if assets.does_asset_exist(root + "/L_InputValidation"):
    raise RuntimeError("Input-validation map already exists; refusing to overwrite")
for name in ("M_Control", "M_Override"):
    if assets.does_asset_exist(root + "/" + name):
        raise RuntimeError("Validation material already exists: " + name)

parents = {}
for kind in ("Control", "Override"):
    material = tools.create_asset("M_" + kind, root, unreal.Material, unreal.MaterialFactoryNew())
    color = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.65, 0.15, 0.06, 1))
    base = color
    if kind == "Override":
        base = lib.create_material_expression(material, unreal.MaterialExpressionCustom, -300, 0)
        base.set_editor_property("code", "return Color;")
        base.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", "Color")
        base.set_editor_property("inputs", [custom_input])
        define = unreal.CustomDefine()
        define.set_editor_property("define_name", "MAZELINE_SHADER_OVERRIDE")
        define.set_editor_property("define_value", "3")
        base.set_editor_property("additional_defines", [define])
        base.set_editor_property("include_file_paths", ["/Project/Private/MazelinePassthroughValidation.ush"])
        if not lib.connect_material_expressions(color, "", base, "Color"):
            raise RuntimeError("Failed to connect Custom input")
    if not lib.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR):
        raise RuntimeError("Failed to connect Base Color")
    for index, (name, prop) in enumerate((("Metallic", unreal.MaterialProperty.MP_METALLIC),
                                          ("Roughness", unreal.MaterialProperty.MP_ROUGHNESS))):
        parameter = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -300, 200 + index * 150)
        parameter.set_editor_property("parameter_name", name)
        parameter.set_editor_property("default_value", 0.5)
        if not lib.connect_material_property(parameter, "", prop):
            raise RuntimeError("Failed to connect " + name)
    lib.recompile_material(material)
    if not assets.save_loaded_asset(material):
        raise RuntimeError("Could not save material")
    parents[kind] = material

instances = {}
colors = (unreal.LinearColor(0.65, 0.15, 0.06, 1), unreal.LinearColor(0.08, 0.5, 0.2, 1),
          unreal.LinearColor(0.1, 0.2, 0.65, 1))
for row, metallic in enumerate((0.0, 0.5, 1.0)):
    for column, roughness in enumerate((0.15, 0.5, 0.85)):
        for kind in parents:
            name = f"MI_{kind}_{row}_{column}"
            instance = tools.create_asset(name, root, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            lib.set_material_instance_parent(instance, parents[kind])
            lib.set_material_instance_vector_parameter_value(instance, "Color", colors[column])
            lib.set_material_instance_scalar_parameter_value(instance, "Metallic", metallic)
            lib.set_material_instance_scalar_parameter_value(instance, "Roughness", roughness)
            lib.update_material_instance(instance)
            if not assets.save_loaded_asset(instance):
                raise RuntimeError("Could not save " + name)
            instances[kind, row, column] = instance

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.new_level(root + "/L_InputValidation"):
    raise RuntimeError("Could not create validation map")
mesh = assets.load_asset("/Engine/BasicShapes/Sphere")
for row in range(3):
    for column in range(3):
        sphere = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, (column - 1) * 180, 440 - row * 180))
        sphere.set_actor_label(f"Input_{row}_{column}")
        component = sphere.get_component_by_class(unreal.StaticMeshComponent)
        component.set_static_mesh(mesh)
        component.set_material(0, instances["Control", row, column])
        sphere.set_actor_scale3d(unreal.Vector(1.3, 1.3, 1.3))
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 600), unreal.Rotator(-35, -35, 0))
light.set_actor_label("Input Directional")
component = light.get_component_by_class(unreal.DirectionalLightComponent)
component.set_mobility(unreal.ComponentMobility.MOVABLE)
component.set_editor_property("intensity", 5.0)
point = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(-250, 180, 400))
point.set_actor_label("Input Point")
component = point.get_component_by_class(unreal.PointLightComponent)
component.set_mobility(unreal.ComponentMobility.MOVABLE)
component.set_editor_property("intensity", 1200.0)
component.set_editor_property("attenuation_radius", 700.0)
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(-850, 0, 260), unreal.Rotator(0, 0, 0))
camera.set_actor_label("Input Camera")
camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(60.0)
if not levels.save_current_level():
    raise RuntimeError("Could not save validation map")
unreal.log("MAZELINE_INPUT_VALIDATION_CREATED " + root)
