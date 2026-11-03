"""Read saved HybridToon references before retiring the example."""
import json
from pathlib import Path
import unreal

assets = unreal.EditorAssetLibrary
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
path = '/Game/MazelineTests/M_HybridToon'
refs = list(assets.find_package_referencers_for_asset(path, True))
report = {'asset': path, 'referencers': refs, 'exists': assets.does_asset_exist(path)}
output = Path(unreal.Paths.project_saved_dir()) / 'HybridRetirement'
output.mkdir(parents=True, exist_ok=True)
(output / 'references-before.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('MAZELINE_HYBRID_REFERENCES ' + json.dumps(report))
