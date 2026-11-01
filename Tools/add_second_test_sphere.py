import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.load_level("/Game/MazelineTests/L_ForwardComparison"):
    raise RuntimeError("Comparison level not found")
all_actors = actors.get_all_level_actors()
if any(a.get_actor_label() == "Mazeline Override 2" for a in all_actors):
    raise RuntimeError("Second override sphere already exists")
material = unreal.EditorAssetLibrary.load_asset("/Game/MazelineTests/M_SecondOverride")
mesh = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere.Sphere")
if not material or not mesh:
    raise RuntimeError("Test material or sphere mesh is missing")
sphere = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 300, 100))
sphere.set_actor_label("Mazeline Override 2")
component = sphere.get_component_by_class(unreal.StaticMeshComponent)
component.set_static_mesh(mesh)
component.set_material(0, material)
sphere.set_actor_scale3d(unreal.Vector(1.5, 1.5, 1.5))
camera = next(a for a in all_actors if a.get_actor_label() == "Mazeline Comparison Camera")
camera.set_actor_location(unreal.Vector(-850, 100, 140), False, True)
if not levels.save_current_level():
    raise RuntimeError("Could not save comparison level")
unreal.log("MAZELINE_SECOND_SPHERE_CREATED")
