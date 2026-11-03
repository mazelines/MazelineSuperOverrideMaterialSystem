"""Validate uniform parameter changes with an unchanged shader/patch in DX12 SM6."""
import datetime
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from PIL import Image, ImageChops

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--route', choices=['Deferred', 'Forward'])
parser.add_argument('--velocity-output-pass', type=int, choices=[0, 1, 2])
options = parser.parse_args()
exe = Path('C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe')
out = root / 'Saved/GGXParameters' / ('Validation-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir(parents=True)
(out.parent / 'latest-validation.txt').write_text(str(out), encoding='utf-8')
shader = root / 'Shaders/Private/Toon.ush'
engine_shader = exe.parents[2] / 'Shaders/Private/ShadingModels.ush'
def hashes():
    return [hashlib.sha256(p.read_bytes()).hexdigest() for p in [shader, engine_shader]]
before = hashes()
cases = [('Original', 0.0, 0.25), ('Middle', 0.5, 0.25), ('Hard', 1.0, 0.25), ('Threshold', 1.0, 0.6)]
report = {}
for route in ([options.route] if options.route else ['Deferred', 'Forward']):
    for name, sharpness, threshold in cases:
        tag = route + '-' + name
        log = out / (tag + '.log')
        args = [str(exe), str(root / 'MazeLineMaterialTest.uproject'),
                '/Game/MazelineTests/GGXValidation/L_GGXValidation', '-game', '-windowed',
                '-ResX=1280', '-ResY=720', '-unattended', '-DisablePython',
                '-benchmark', '-deterministic', '-fps=30',
                '-ExecCmds=r.BloomQuality 0,r.Lumen.DiffuseIndirect.Allow 0,r.Lumen.Reflections.Allow 0,r.SSR.Quality 0,r.Tonemapper.Quality 0',
                '-MazelineCapture=' + str(out / (tag + '.png')), '-abslog=' + str(log),
                '-MazelineSharpness=' + str(sharpness), '-MazelineThreshold=' + str(threshold)]
        if route == 'Forward':
            args.append('-ini:Engine:[/Script/Engine.RendererSettings]:r.ForwardShading=1')
        if options.velocity_output_pass is not None:
            args.append('-ini:Engine:[/Script/Engine.RendererSettings]:r.VelocityOutputPass=' + str(options.velocity_output_pass))
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        result = subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, startupinfo=startup, timeout=360)
        text = log.read_text(encoding='utf-8', errors='replace')
        if result.returncode or re.search(r'Failed to compile|LogShaders: Error:|LogShaderCompilers: Error:|Fatal error:|Assertion failed:', text):
            raise RuntimeError('UE shader/render failure: ' + str(log))
        for marker in ['MAZELINE_DYNAMIC_GGX count=1', 'MAZELINE_PACKAGED_CAPTURE_COMPLETE', 'rhiname="D3D12"', 'shaderplatform="PCD3D_SM6"']:
            if marker not in text:
                raise RuntimeError('Missing marker ' + marker + ': ' + str(log))
        print('GGX_PARAMETER_CAPTURE_PASS ' + tag, flush=True)
    images = {name: Image.open(out / (route + '-' + name + '.png')).convert('RGB') for name, _, _ in cases}
    for a, b in [('Original', 'Middle'), ('Middle', 'Hard'), ('Hard', 'Threshold')]:
        difference = ImageChops.difference(images[a], images[b])
        report[route + '_' + a + '_vs_' + b] = difference.getbbox() is not None
    if not all(report.values()):
        (out / 'verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        raise RuntimeError('Parameter changes did not affect ' + route + ': ' + str(out))
report['shader_and_patch_unchanged'] = before == hashes()
report['scope'] = 'DX12/SM6, runtime dynamic material instance setters, isolated direct light, UnrealEditor -game'
report['scope'] += ', velocity output pass=' + str(options.velocity_output_pass)
(out / 'verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
if not all(value for key, value in report.items() if key != 'scope'):
    raise RuntimeError('Parameter image comparison failed: ' + str(out))
print('GGX_PARAMETERS_PASS ' + str(out), flush=True)
