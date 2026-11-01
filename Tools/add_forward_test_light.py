import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.load_level("/Game/MazelineTests/L_ForwardComparison"):
    raise RuntimeError("Comparison level not found")
if any(a.get_actor_label() == "Mazeline Point" for a in actors.get_all_level_actors()):
    raise RuntimeError("Test point light already exists")
light = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(-250, 100, 120))
light.set_actor_label("Mazeline Point")
component = light.get_component_by_class(unreal.PointLightComponent)
component.set_editor_property("intensity", 30000.0)
component.set_editor_property("attenuation_radius", 700.0)
component.set_editor_property("light_color", unreal.Color(120, 160, 255, 255))
if not levels.save_current_level():
    raise RuntimeError("Could not save comparison level")
unreal.log("MAZELINE_SECOND_LIGHT_CREATED")
