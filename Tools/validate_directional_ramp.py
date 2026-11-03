"""DX12/SM6: ramp rows affect directional diffuse only; curve edits update live."""
import argparse
import datetime
import hashlib
import json
import re
import subprocess
from pathlib import Path
from PIL import Image, ImageChops

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--route', choices=['Deferred', 'Forward'])
parser.add_argument('--smoke', action='store_true', help='Only the first directional capture')
parser.add_argument('--light', choices=['Directional', 'Point', 'Spot', 'Rect'], help='Validate only the selected light')
options = parser.parse_args()
out = root / 'Saved/DirectionalRamp' / ('Validation-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir(parents=True)
(out.parent / 'latest-validation.txt').write_text(str(out), encoding='utf-8')
exe = Path('C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe')
files = [root / 'Shaders/Private/Toon.ush', root / 'Content/MazelineTests/M_Toon.uasset',
         root / 'Plugins/MazelineSuperOverriderMaterial/Shaders/Private/MazelineRampAtlas.usf',
         *sorted((root / 'Content/MazelineTests/Ramps').glob('*.uasset')),
         *[exe.parents[2] / 'Shaders/Private' / name for name in
           ['ShadingCommon.ush', 'ShadingModelsMaterial.ush', 'ShadingModels.ush', 'DeferredShadingCommon.ush', 'DeferredLightingCommon.ush']]]
def hashes():
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
before = hashes()
report = {'captures': [], 'checks': {}, 'excluded': [], 'scope': 'UnrealEditor -game, DX12/SM6, movable direct lights, Default Lit, no indirect/reflection/bloom'}
cases = [('Directional-' + str(row), 'Directional', row, 'Diffuse', False) for row in [0, 1, 2]]
cases += [(light + '-' + str(row), light, row, 'Diffuse', False) for light in ['Point', 'Spot', 'Rect'] for row in [0, 2]]
cases += [('Specular-' + str(row), 'Directional', row, 'Specular', False) for row in [0, 2]]
cases += [('LiveCurveEdit', 'Directional', 0, 'Diffuse', True)]
if options.smoke:
    cases = cases[:1]
if options.light:
    cases = [case for case in cases if case[1] == options.light]
for route in ([options.route] if options.route else ['Deferred', 'Forward']):
    if route == 'Forward':
        if options.light == 'Rect':
            raise RuntimeError('Opaque Forward Rect lighting is outside the validated engine support scope')
        report['excluded'].append('Forward Rect: unsupported opaque Rect lighting; black captures are not a passing isolation test')
    for tag, light, row, component, edit in cases:
        if route == 'Forward' and light == 'Rect':
            continue
        tag = route + '-' + tag
        log = out / (tag + '.log')
        args = [str(exe), str(root / 'MazeLineMaterialTest.uproject'), '/Game/MazelineTests/GGXValidation/L_GGXValidation',
                '-game', '-windowed', '-ResX=1280', '-ResY=720', '-unattended', '-DisablePython',
                '-benchmark', '-deterministic', '-fps=30',
                '-ExecCmds=r.BloomQuality 0,r.Lumen.DiffuseIndirect.Allow 0,r.Lumen.Reflections.Allow 0,r.SSR.Quality 0,r.Tonemapper.Quality 0',
                '-MazelineCapture=' + str(out / (tag + '.png')), '-abslog=' + str(log),
                '-MazelineSharpness=0.5', '-MazelineThreshold=0.25', '-MazelineRampRow=' + str(row),
                '-MazelineLightType=' + light, '-Mazeline' + component + 'Only']
        if route == 'Forward':
            args.append('-ini:Engine:[/Script/Engine.RendererSettings]:r.ForwardShading=1')
        if edit:
            args.append('-MazelineRampEdit')
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        result = subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, startupinfo=startup, timeout=1200)
        text = log.read_text(encoding='utf-8', errors='replace')
        if result.returncode or re.search(r'Failed to compile|LogShaders: Error:|LogShaderCompilers: Error:|Fatal error:|Assertion failed:|Ensure condition failed:|unbound parameters', text):
            raise RuntimeError('Shader/render failure: ' + str(log))
        markers = ['MAZELINE_DYNAMIC_RAMP count=1 row=' + str(row), 'MAZELINE_RAMP_LIGHT type=' + light,
                   'MAZELINE_RAMP_ATLAS function=2 texture=/Game/MazelineTests/Ramps/CA_ToonDiffuse',
                   'MAZELINE_PACKAGED_CAPTURE_COMPLETE', 'rhiname="D3D12"', 'shaderplatform="PCD3D_SM6"']
        if edit:
            markers.append('MAZELINE_RAMP_LIVE_UPDATE row=0 in_memory_only=yes')
        for marker in markers:
            if marker not in text:
                raise RuntimeError('Missing ' + marker + ': ' + str(log))
        report['captures'].append(tag)
        print('RAMP_CAPTURE_PASS ' + tag, flush=True)
        if not options.smoke and tag == route + '-Directional-2':
            first = Image.open(out / (route + '-Directional-0.png')).convert('RGB')
            last = Image.open(out / (route + '-Directional-2.png')).convert('RGB')
            if ImageChops.difference(first, last).getbbox() is None:
                raise RuntimeError('Directional rows produced identical images: ' + str(out))
    if not options.smoke:
        def image(tag):
            return Image.open(out / (route + '-' + tag + '.png')).convert('RGB')
        def differs(a, b):
            return ImageChops.difference(image(a), image(b)).getbbox() is not None
        for a, b in [('Directional-0', 'Directional-1'), ('Directional-1', 'Directional-2'), ('Directional-0', 'LiveCurveEdit')]:
            if (out / (route + '-' + a + '.png')).exists() and (out / (route + '-' + b + '.png')).exists():
                report['checks'][route + ':different:' + a + ':' + b] = differs(a, b)
        for light in ['Point', 'Spot', 'Rect', 'Specular']:
            if not (out / (route + '-' + light + '-0.png')).exists():
                continue
            report['checks'][route + ':unchanged:' + light] = not differs(light + '-0', light + '-2')
            # Exclude a trivially empty black image from the unchanged-light proof.
            extrema = image(light + '-0').getextrema()
            report['checks'][route + ':visible:' + light] = max(high - low for low, high in extrema) > 20
report['checks']['assets_and_shaders_unchanged'] = before == hashes()
report['passed'] = all(report['checks'].values())
(out / 'verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
if not report['passed']:
    raise RuntimeError('Ramp comparison failed: ' + str(out))
print('DIRECTIONAL_RAMP_PASS ' + str(out), flush=True)
