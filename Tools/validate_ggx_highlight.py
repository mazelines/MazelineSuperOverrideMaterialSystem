"""Render GGX endpoints through UE and restore the current Toon shader in finally.

Run only with all editors closed. This uses the native transactional patcher.
It temporarily tests the diffuse-only baseline and Sharpness 0/0.5/1, then
restores the author's source and patch. It never edits material or map assets.
"""
import json
import re
import subprocess
import datetime
from pathlib import Path
from PIL import Image, ImageChops

root = Path(__file__).resolve().parents[1]
engine = Path('C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64')
project = root / 'MazeLineMaterialTest.uproject'
shader = root / 'Shaders/Private/Toon.ush'
original = shader.read_text(encoding='utf-8')
if original.count('const float Sharpness = 0.5;') != 1:
    raise RuntimeError('Expected current default Sharpness 0.5; inspect before validating')
stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
out = root / 'Saved/GGXHighlight' / ('Validation-' + stamp)
out.mkdir(parents=True, exist_ok=False)
(out / 'Toon.final.ush').write_text(original, encoding='utf-8')
(out.parent / 'latest-validation.txt').write_text(str(out), encoding='utf-8')
last_written = original

def write_source(value):
    global last_written
    if shader.read_text(encoding='utf-8') != last_written:
        raise RuntimeError('Toon.ush changed externally; refusing to overwrite')
    shader.write_text(value, encoding='utf-8', newline='\n')
    last_written = value

def execute(exe, args, log, timeout):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    result = subprocess.run([str(engine / exe), str(project), *args,
                             '-abslog=' + str(log)], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, timeout=timeout,
                            startupinfo=startup)
    text = log.read_text(encoding='utf-8', errors='replace')
    if result.returncode or re.search(r'Failed to compile|LogShaderCompilers: Error:|LogShaders: Error:|Fatal error:|Assertion failed:', text):
        raise RuntimeError('UE failed: ' + str(log))
    return text

def apply(name):
    text = execute('UnrealEditor-Cmd.exe', ['-run=MazelineShaderPatch', '-Action=apply',
                    '-nullrhi', '-unattended', '-DisablePython'], out / (name + '-apply.log'), 120)
    if 'MAZELINE_NATIVE_RESULT apply success' not in text:
        raise RuntimeError('Native patch did not confirm success')

def capture(name, forward=False):
    args = ['/Game/MazelineTests/GGXValidation/L_GGXValidation', '-game', '-windowed',
            '-ResX=1280', '-ResY=720', '-unattended', '-DisablePython',
            '-benchmark', '-deterministic', '-fps=30',
            '-ExecCmds=r.BloomQuality 0,r.Lumen.DiffuseIndirect.Allow 0,r.Lumen.Reflections.Allow 0,r.SSR.Quality 0,r.Tonemapper.Quality 0',
            '-MazelineCapture=' + str(out / (name + '.png'))]
    if forward:
        args.append('-ini:Engine:[/Script/Engine.RendererSettings]:r.ForwardShading=1')
    text = execute('UnrealEditor.exe', args, out / (name + '.log'), 360)
    if 'MAZELINE_PACKAGED_CAPTURE_COMPLETE' not in text or 'shaderplatform="PCD3D_SM6"' not in text or 'rhiname="D3D12"' not in text:
        raise RuntimeError('Missing DX12/SM6 capture confirmation: ' + name)
    print('GGX_CAPTURE_PASS ' + name, flush=True)

try:
    variants = [
        ('Baseline', original.split('// Reshape the actual GGX')[0].replace('#define MAZELINE_HAS_GGX_DISTRIBUTION_OVERRIDE 1', '')),
        ('Sharpness0', original.replace('const float Sharpness = 0.5;', 'const float Sharpness = 0.0;')),
        ('Sharpness1', original.replace('const float Sharpness = 0.5;', 'const float Sharpness = 1.0;')),
        ('Sharpness05', original),
    ]
    for name, code in variants:
        write_source(code)
        apply(name)
        capture(name)
    capture('Sharpness05Forward', forward=True)
    images = {name: Image.open(out / (name + '.png')).convert('RGB') for name, _ in variants}
    identical = ImageChops.difference(images['Baseline'], images['Sharpness0']).getbbox() is None
    middle_changed = ImageChops.difference(images['Sharpness0'], images['Sharpness05']).getbbox() is not None
    hard_changed = ImageChops.difference(images['Sharpness05'], images['Sharpness1']).getbbox() is not None
    report = {'sharpness_zero_pixel_identical': identical, 'middle_changes_image': middle_changed,
              'hard_differs_from_middle': hard_changed, 'dx12_sm6_forward_capture': True,
              'map': '/Game/MazelineTests/GGXValidation/L_GGXValidation', 'scope': 'UnrealEditor -game; isolated direct light, not packaged'}
    (out / 'verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report), flush=True)
    if not all((identical, middle_changed, hard_changed)):
        raise RuntimeError('GGX image comparison failed; inspect verification and captures')
finally:
    write_source(original)
    apply('Final')
    print('GGX_FINAL_SOURCE_AND_PATCH_RESTORED ' + str(out), flush=True)
