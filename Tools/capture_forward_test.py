import os
import time
import unreal

filename = os.environ.get("MAZELINE_CAPTURE_FILENAME", "MazelineForwardComparison.png")
output = os.path.abspath(os.path.join(unreal.Paths.project_saved_dir(), filename))
if os.path.exists(output):
    raise RuntimeError("Capture already exists; refusing to overwrite it: " + output)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
camera = next((a for a in actors if a.get_actor_label() == "Mazeline Comparison Camera"), None)
if camera is None:
    raise RuntimeError("Comparison camera is missing")
start = time.monotonic()
task = None
requested = False
handle = None


def tick(_delta):
    global task, handle, requested
    if not requested and time.monotonic() - start > 20:
        requested = True
        task = unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, output, camera, delay=3.0)
        unreal.log("MAZELINE_CAPTURE_REQUESTED " + output)
    if requested and os.path.isfile(output):
        unreal.log("MAZELINE_CAPTURE_COMPLETE " + output)
        unreal.unregister_slate_post_tick_callback(handle)


handle = unreal.register_slate_post_tick_callback(tick)
