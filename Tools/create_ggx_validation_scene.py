"""Create an isolated, static lighting scene without modifying the user's map."""
import unreal

path = '/Game/MazelineTests/GGXValidation/L_GGXValidation'
assets = unreal.EditorAssetLibrary
if assets.does_asset_exist(path):
    raise RuntimeError('Validation map exists; refusing to overwrite')
material = assets.load_asset('/Game/MazelineTests/M_Toon')
mesh = assets.load_asset('/Engine/BasicShapes/Sphere')
if not material or not mesh:
    raise RuntimeError('Required material or mesh missing')
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.new_level(path):
    raise RuntimeError('Could not create validation map')
sphere = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
sphere.set_actor_label('GGX M_Toon')
sphere.set_actor_scale3d(unreal.Vector(3, 3, 3))
component = sphere.get_component_by_class(unreal.StaticMeshComponent)
component.set_static_mesh(mesh)
component.set_material(0, material)
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(-200, 0, 300), unreal.Rotator(-25, 20, 0))
light.set_actor_label('GGX Static Test Direction')
component = light.get_component_by_class(unreal.DirectionalLightComponent)
component.set_mobility(unreal.ComponentMobility.MOVABLE)
component.set_editor_property('intensity', 5.0)
component.set_editor_property('light_source_angle', 0.0)
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(-650, 0, 0), unreal.Rotator(0, 0, 0))
camera.set_actor_label('GGX Validation Camera')
camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(55.0)
if not levels.save_current_level():
    raise RuntimeError('Could not save validation map')
unreal.log('MAZELINE_GGX_VALIDATION_SCENE_CREATED ' + path)
