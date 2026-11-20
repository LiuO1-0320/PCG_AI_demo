"""Check that every referenced /Game package is present inside this project."""
import unreal, json, os

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
packages = set()
for root in ['/Game/BossPalace', '/Game/BossPrototype', '/Game/DarkFantasyCastle', '/Game/Demo/CourtArt', '/Game/Demo/MemoryRoomArt', '/Game/Dungeon']:
    packages.update(a.package_name for a in registry.get_assets_by_path(root, recursive=True))
missing = set()
for package in packages:
    for dependency in registry.get_dependencies(package, options):
        name = str(dependency)
        if name.startswith('/Game/') and not unreal.EditorAssetLibrary.does_asset_exist(name):
            missing.add(name)
report = dict(packages=len(packages), missing=sorted(missing))
with open(os.path.join(unreal.Paths.project_saved_dir(), 'BossPalaceDependencies.json'), 'w', encoding='utf-8') as f:
    json.dump(report, f, indent=2)
if missing:
    raise RuntimeError('Missing local packages: ' + str(sorted(missing)))
unreal.log('PALACE_DEPENDENCIES_OK packages=' + str(len(packages)))
