"""Restore missing reference floor modules without changing gameplay collision."""
import unreal


def repair_floor(world):
    mesh = unreal.load_asset('/Game/DarkFantasyCastle/Meshes/SM_InteriorFloor_2')
    finishes = [unreal.load_asset('/Game/BossPalace/Desolation/Materials/MI_Soot_MI_Tiles_2'),
                unreal.load_asset('/Game/BossPalace/Desolation/Materials/MI_Soot_MI_Tiles3Int')]
    assert mesh and all(finishes), 'Required palace floor assets are missing'
    repaired = 0
    floor_count = 0
    foundation = 0
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if actor.get_world() != world:
            continue
        label = actor.get_actor_label()
        components = actor.get_components_by_class(unreal.StaticMeshComponent)
        if label.startswith('Stone floor '):
            for component in components:
                if not component.static_mesh:
                    component.set_static_mesh(mesh)
                    for slot in range(component.get_num_materials()):
                        component.set_material(slot, finishes[min(slot, len(finishes) - 1)])
                    repaired += 1
                assert component.static_mesh, label
                floor_count += 1
        elif label == 'Continuous walkable floor':
            for component in components:
                # Its existing top is at -2 cm, safely below the visible tile tops at 0 cm.
                component.set_visibility(True)
                component.set_hidden_in_game(False)
                component.set_material(0, finishes[0])
                foundation += 1
    assert floor_count >= 70 and foundation == 1, (floor_count, foundation)
    unreal.log('PALACE_FLOOR_REPAIRED restored={} tiles={} foundation={}'.format(repaired, floor_count, foundation))


if __name__ == '__main__':
    world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/BossPalace/Maps/L_BossPalaceShell')
    assert world
    repair_floor(world)
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, '/Game/BossPalace/Maps/L_BossPalaceShell')
