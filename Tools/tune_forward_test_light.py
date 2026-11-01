import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.load_level("/Game/MazelineTests/L_ForwardComparison"):
    raise RuntimeError("Comparison level not found")
matches = [a for a in actors.get_all_level_actors() if a.get_actor_label() == "Mazeline Point"]
if len(matches) != 1:
    raise RuntimeError("Expected exactly one test point light")
matches[0].get_component_by_class(unreal.PointLightComponent).set_editor_property("intensity", 25.0)
if not levels.save_current_level():
    raise RuntimeError("Could not save comparison level")
unreal.log("MAZELINE_SECOND_LIGHT_TUNED")
