import unreal

path = "/Game/MazelineTests"
level_path = path + "/L_ForwardComparison"
control_path = path + "/M_ForwardControl"
test = unreal.EditorAssetLibrary.load_asset(path + "/M_ForwardOverride")
if not test:
    raise RuntimeError("Create the forward override test material first")
if unreal.EditorAssetLibrary.does_asset_exist(level_path) or unreal.EditorAssetLibrary.does_asset_exist(control_path):
    raise RuntimeError("A comparison asset already exists; refusing to overwrite it")

control = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    "M_ForwardControl", path, unreal.Material, unreal.MaterialFactoryNew())
constant = unreal.MaterialEditingLibrary.create_material_expression(
    control, unreal.MaterialExpressionConstant3Vector, -300, 0)
constant.set_editor_property("constant", unreal.LinearColor(0.8, 0.2, 0.1, 1.0))
if not unreal.MaterialEditingLibrary.connect_material_property(constant, "", unreal.MaterialProperty.MP_BASE_COLOR):
    raise RuntimeError("Could not connect the control Base Color")
unreal.MaterialEditingLibrary.recompile_material(control)
if not unreal.EditorAssetLibrary.save_loaded_asset(control):
    raise RuntimeError("Could not save control material")

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.new_level(level_path):
    raise RuntimeError("Could not create comparison level")
sphere_mesh = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere.Sphere")
if not sphere_mesh:
    raise RuntimeError("Engine sphere mesh unavailable")
for label, y, material in (("Control", -100, control), ("Override", 100, test)):
    sphere = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, y, 100))
    sphere.set_actor_label("Mazeline " + label)
    component = sphere.get_component_by_class(unreal.StaticMeshComponent)
    component.set_static_mesh(sphere_mesh)
    component.set_material(0, material)
    sphere.set_actor_scale3d(unreal.Vector(1.5, 1.5, 1.5))

light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 200), unreal.Rotator(-40, -40, 0))
light.set_actor_label("Mazeline Directional")
light.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("intensity", 5.0)
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(-650, 0, 140), unreal.Rotator(0, 0, 0))
camera.set_actor_label("Mazeline Comparison Camera")
if not levels.save_current_level():
    raise RuntimeError("Could not save comparison level")
unreal.log("MAZELINE_COMPARISON_LEVEL_CREATED " + level_path)
