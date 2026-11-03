"""Explicit one-time migration of the first generated example curves; no user curve reset."""
import json
from pathlib import Path
import unreal

assets = unreal.EditorAssetLibrary
report = {}
for name in ['CR_ToonLinear', 'CR_ToonSoft', 'CR_ToonSteps']:
    curve = assets.load_asset('/Game/MazelineTests/Ramps/' + name)
    assert unreal.MazelineMaterialLibrary.remove_constructor_keys_from_ramp_example(curve), name
    assert assets.save_loaded_asset(curve)
    samples = {}
    for t in [0, .1, .24, .25, .5, .64, .65, .9, 1]:
        value = curve.get_unadjusted_linear_color_value(t)
        samples[str(t)] = [value.r, value.g, value.b, value.a]
    report[name] = samples
assert abs(report['CR_ToonSteps']['0.1'][0] - .12) < 1e-5
assert abs(report['CR_ToonSteps']['0.5'][0] - .5) < 1e-5
assert abs(report['CR_ToonSteps']['0.9'][0] - 1.) < 1e-5
atlas = assets.load_asset('/Game/MazelineTests/Ramps/CA_ToonDiffuse')
assert assets.save_loaded_asset(atlas, only_if_is_dirty=False)
out = Path(unreal.Paths.project_saved_dir()) / 'DirectionalRamp/curve-repair.json'
out.write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('MAZELINE_CURVE_KEYS_REPAIRED ' + str(out))
