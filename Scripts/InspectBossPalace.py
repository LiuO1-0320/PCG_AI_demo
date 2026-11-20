import unreal, json, os

world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/BossPalace/Maps/L_BossPalace')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
rows = []
for actor in actors:
    row = dict(name=actor.get_name(), label=actor.get_actor_label(), cls=actor.get_class().get_name(), location=list(actor.get_actor_location().to_tuple()), scale=list(actor.get_actor_scale3d().to_tuple()))
    meshes = actor.get_components_by_class(unreal.StaticMeshComponent)
    row['meshes'] = [c.static_mesh.get_path_name() if c.static_mesh else None for c in meshes]
    row['instances'] = [dict(name=c.get_name(), mesh=c.static_mesh.get_path_name() if c.static_mesh else None, count=c.get_instance_count(), transforms=[str(c.get_instance_transform(i, world_space=True)) for i in range(min(2,c.get_instance_count()))]) for c in meshes if isinstance(c, unreal.InstancedStaticMeshComponent)]
    rows.append(row)
path = os.path.join(unreal.Paths.project_saved_dir(), 'BossPalaceActors.json')
with open(path, 'w', encoding='utf-8') as f:
    json.dump(rows, f, ensure_ascii=False, indent=2)
unreal.log('PALACE_INSPECT_OK actors=' + str(len(rows)))
