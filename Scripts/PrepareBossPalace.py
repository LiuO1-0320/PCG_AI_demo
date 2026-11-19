"""Build a gameplay-free shell from the supplied reference; never modify the reference map."""
import unreal
import os
import runpy

assets = unreal.EditorAssetLibrary
for old, new in [('SM_InteriorFloor_1', 'SM_InteriorFloor'), ('SM_Statue_Base_1', 'SM_Statue_Base')]:
    root = '/Game/DarkFantasyCastle/Meshes/'
    if not assets.does_asset_exist(root + new):
        assets.duplicate_asset(root + old, root + new)
root = '/Game/DarkFantasyCastle/Materials/'
if not assets.does_asset_exist(root + 'MI_WallStatues'):
    assets.duplicate_asset(root + 'MI_WallStatues_1', root + 'MI_WallStatues')

world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/BossPalace/Maps/L_BossPalace')
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
kept = 0
for actor in sub.get_all_level_actors():
    cls = actor.get_class().get_name()
    if cls in ('CastleDirector', 'PCGWorldActor', 'PlayerStart', 'RecastNavMesh', 'NavMeshBoundsVolume', 'CameraActor', 'TargetPoint') or actor.get_actor_label().startswith('PCG '):
        sub.destroy_actor(actor)
        continue
    for component in actor.get_components_by_class(unreal.SceneComponent):
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
    kept += 1
floor_helpers = runpy.run_path(os.path.join(os.path.dirname(__file__), 'RepairBossPalaceFloor.py'), run_name='boss_floor_helpers')
floor_helpers['repair_floor'](world)
assert unreal.EditorLoadingAndSavingUtils.save_map(world, '/Game/BossPalace/Maps/L_BossPalaceShell')
assets.save_directory('/Game/DarkFantasyCastle')
unreal.log('PALACE_SHELL_OK actors=' + str(kept))
