"""UE 5.8 Python commandlet. Save only three court art packages, never a map."""
from pathlib import Path
import unreal as ue

ROOT='/Game/Demo/CourtArt'
src=Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir()))/'Saved/CourtArtSource'
ea=ue.EditorAssetLibrary
assets=ue.AssetToolsHelpers.get_asset_tools()
ea.make_directory(ROOT)


def get_or_import(name,extension,kind):
    path=ROOT+'/'+name
    if ea.does_asset_exist(path): return ea.load_asset(path)
    task=ue.AssetImportTask()
    for key,val in dict(filename=str(src/(name+extension)),destination_path=ROOT,destination_name=name,
                        automated=True,replace_existing=False,save=False).items():
        task.set_editor_property(key,val)
    assets.import_asset_tasks([task])
    # Interchange import may finish after import_asset_tasks.
    result=task.get_objects()
    item=next((x for x in result if isinstance(x,kind)),None)
    assert item and item.get_name()==name, (path,[x.get_path_name() for x in result])
    # glTF's scene translator places unsaved meshes in a StaticMeshes subfolder.
    # Duplicate only the mesh to its public asset path; never save the scene import.
    if item.get_path_name()!=path+'.'+name:
        item=assets.duplicate_asset(name,ROOT,item)
        assert item and item.get_path_name()==path+'.'+name
    return item


tex=get_or_import('T_TornStandardWeave','.png',ue.Texture2D)
tex.set_editor_property('srgb',True)
ea.save_loaded_asset(tex,False)

mat_name='M_TornStandardVelvet'
mat=ea.load_asset(ROOT+'/'+mat_name) if ea.does_asset_exist(ROOT+'/'+mat_name) else None
if not mat:
    mat=assets.create_asset(mat_name,ROOT,ue.Material,ue.MaterialFactoryNew())
m=ue.MaterialEditingLibrary
m.delete_all_material_expressions(mat)
mat.set_editor_property('two_sided',True)
mat.set_editor_property('used_with_instanced_static_meshes',True)
mat.set_editor_property('used_with_nanite',True)
t=m.create_material_expression(mat,ue.MaterialExpressionTextureSample,-440,-50)
t.set_editor_property('texture',tex)
m.connect_material_property(t,'RGB',ue.MaterialProperty.MP_BASE_COLOR)
r=m.create_material_expression(mat,ue.MaterialExpressionConstant,-440,130)
r.set_editor_property('r',.93)
m.connect_material_property(r,'',ue.MaterialProperty.MP_ROUGHNESS)
m.recompile_material(mat)
ea.save_loaded_asset(mat,False)

mesh=get_or_import('SM_TornStandard','.glb',ue.StaticMesh)
mesh.set_editor_property('has_navigation_data',False)
body=mesh.get_editor_property('body_setup')
agg=body.get_editor_property('agg_geom')
body.set_editor_property('agg_geom',type(agg)())
body.set_editor_property('collision_trace_flag',ue.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
body.set_editor_property('never_needs_cooked_collision_data',True)
instance=body.get_editor_property('default_instance')
instance.set_editor_property('collision_profile_name','NoCollision')
instance.set_editor_property('collision_enabled',ue.CollisionEnabled.NO_COLLISION)
body.set_editor_property('default_instance',instance)
mesh.set_material(0,mat)
ea.save_loaded_asset(mesh,False)

b=mesh.get_bounding_box(); dim=b.max-b.min
assert 3<dim.x<16 and 94<dim.y<103 and 190<dim.z<200,(b,dim)
assert not any(len(body.get_editor_property('agg_geom').get_editor_property(k)) for k in
               ('sphere_elems','box_elems','convex_elems','sphyl_elems','tapered_capsule_elems'))
assert not mesh.get_editor_property('has_navigation_data')
assert mesh.get_material(0)==mat
assert body.get_editor_property('default_instance').get_editor_property('collision_enabled')==ue.CollisionEnabled.NO_COLLISION
ue.log('COURT_STANDARD_VERIFIED mesh={} min={} max={} sizes={} simple=0 trace=SimpleAsComplex default=NoCollision nav=false mat={}'.format(
    mesh.get_path_name(),b.min,b.max,dim,mat.get_path_name()))
