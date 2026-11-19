"""Run with UE's PythonScriptPlugin. Creates only the first-person art package."""
from pathlib import Path
import unreal


root = '/Game/Demo/CastleCarbine'
name = 'SM_ArchiveCarbine'
path = f'{root}/{name}/StaticMeshes/{name}'
lib = unreal.EditorAssetLibrary
if not lib.does_asset_exist(path):
    lib.make_directory(root)
    task = unreal.AssetImportTask()
    file = Path(unreal.Paths.project_dir()).resolve()/'SourceArt'/'CastleCarbine'/(name + '.glb')
    task.filename = str(file)
    task.destination_path = root
    task.automated = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    unreal.log('CARBINE_IMPORT_ASSETS ' + str(task.imported_object_paths))
mesh = lib.load_asset(path)
assert isinstance(mesh, unreal.StaticMesh), path
materials = [mesh.get_material(i).get_name() for i in range(len(mesh.static_materials))]
assert len(materials) == 5 and all('Carbine' in m for m in materials), materials
box = mesh.get_bounding_box()
extent = box.max - box.min
assert abs(extent.x-62.88) < .2 and abs(extent.y-8.16) < .2 and abs(extent.z-21.36) < .2, str(box)
assert 30 < box.max.x < 32, str(box)
unreal.log(f'CARBINE_ART_VALID path={path} size_cm={extent} muzzle_x={box.max.x:.2f} materials={materials}')
