"""Exercise the C++ restore/apply commands and verify all five original hashes."""
import hashlib
import json
import re
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
engine = Path('C:/Program Files/Epic Games/UE_5.8/Engine')
store = engine / 'Saved/MazelineShaderOverride'
files = ['ShadingCommon.ush', 'ShadingModelsMaterial.ush', 'ShadingModels.ush',
         'DeferredShadingCommon.ush', 'DeferredLightingCommon.ush']
out = root / 'Saved/DirectionalRamp'
state = json.loads((store / 'state.json').read_text(encoding='utf-8-sig'))
assert not state.get('pending')
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def snapshot(identifier):
    assert re.fullmatch(r'[0-9a-f]{32}', identifier)
    return store / identifier
baseline = snapshot(state['baseline'])
baseline_manifest = json.loads((baseline / 'manifest.json').read_text(encoding='utf-8-sig'))
original = {}
protected = {}
for name in files:
    directory = baseline
    if name not in baseline_manifest['files']:
        extension = 'legacy-extension.json' if name == 'DeferredShadingCommon.ush' else 'ramp-extension.json'
        directory = snapshot(json.loads((store / extension).read_text(encoding='utf-8-sig'))['snapshot'])
        protected[str(store / extension)] = digest(store / extension)
    manifest_path = directory / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    original[name] = manifest['files'][name]
    assert digest(directory / name) == original[name]
    protected[str(manifest_path)] = digest(manifest_path)
    protected[str(directory / name)] = digest(directory / name)
applied = {name: digest(engine / 'Shaders/Private' / name) for name in files}
def command(action):
    log = root / 'Saved/Logs' / ('DirectionalRamp-' + action + '.log')
    result = subprocess.run([str(engine / 'Binaries/Win64/UnrealEditor-Cmd.exe'), str(root / 'MazeLineMaterialTest.uproject'),
        '-run=MazelineShaderPatch', '-Action=' + action, '-nullrhi', '-unattended', '-DisablePython', '-abslog=' + str(log)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=180)
    text = log.read_text(encoding='utf-8', errors='replace')
    assert result.returncode == 0 and 'MAZELINE_NATIVE_RESULT ' + action + ' success' in text, str(log)
    return text
try:
    command('restore')
    restored = {name: digest(engine / 'Shaders/Private' / name) for name in files}
    assert restored == original, 'Restored files differ from immutable originals'
finally:
    command('apply')
assert {name: digest(engine / 'Shaders/Private' / name) for name in files} == applied
assert all(digest(Path(path)) == value for path, value in protected.items())
assert 'state=applied matches=yes' in command('status')
report = {'passed': True, 'original_hashes': original, 'applied_hashes': applied,
          'original_snapshots_unchanged': True, 'final_state': 'applied matches=yes'}
(out / 'restore-verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('MAZELINE_FIVE_FILE_RESTORE_PASS', flush=True)
