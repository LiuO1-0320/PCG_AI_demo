"""UE 5.8.4 Python commandlet: preserve source packages and all material graphs.

Only enables material usage flags and creates the three project masonry assets.
On a subsequent run this verifies the saved assets without loading the Fab originals.
"""
import unreal as ue

ea=ue.EditorAssetLibrary
assets=ue.AssetToolsHelpers.get_asset_tools()
mel=ue.MaterialEditingLibrary
palette='/Game/Demo/MemoryRoomArt/'
root='/Game/Demo/CourtArt/Masonry'
source='/Game/Dungeon/Meshes/Props/'
ea.make_directory(root)


def flag(suffix):
    return next(getattr(ue.MaterialUsage,k) for k in dir(ue.MaterialUsage)
                if k.startswith('MATUSAGE') and k.replace('_','').endswith(suffix))


ism=flag('INSTANCEDSTATICMESHES')
nanite=flag('NANITE')
for path in [palette+k for k in ('M_Charcoal','M_Limestone','M_AntiqueGold','M_CandleFlame',
                                 'M_Velvet','M_MemoryStone','M_MemoryTiles')]+[
        '/Game/Demo/CourtArt/M_TornStandardVelvet','/Game/Demo/Materials/M_Purple']:
    mat=ea.load_asset(path)
    assert mat,path
    required=[ism,nanite] if not path.endswith('M_Purple') else [ism]
    changed=False
    for usage in required:
        if not mel.has_material_usage(mat,usage):
            mel.set_base_material_usage(mat,usage,True)
            changed=True
        assert mel.has_material_usage(mat,usage),(path,usage)
    if changed: assert ea.save_loaded_asset(mat,False),path
    ue.log('COURT_USAGE_OK {} required={} changed={}'.format(path,required,changed))


def signature(mesh):
    b=mesh.get_bounding_box()
    body=mesh.get_editor_property('body_setup')
    geo=body.get_editor_property('agg_geom')
    return ([(mesh.get_num_vertices(lod),mesh.get_num_sections(lod)) for lod in range(mesh.get_num_lods())],
            [round(x,4) for point in (b.min,b.max) for x in (point.x,point.y,point.z)],
            str(body.get_editor_property('collision_trace_flag')),
            [len(geo.get_editor_property(k)) for k in ('box_elems','convex_elems','sphyl_elems','sphere_elems')],
            [str(s.material_slot_name) for s in mesh.static_materials])


for name,finish,slots in [('SM_Wall_00','M_MemoryStone',4),('SM_Ground','M_MemoryTiles',1),('SM_Pillar','M_MemoryStone',3)]:
    path=root+'/'+name
    mat=ea.load_asset(palette+finish)
    if ea.does_asset_exist(path):
        mesh=ea.load_asset(path)
    else:
        original=ea.load_asset(source+name)
        assert original and mat,(name,finish)
        before=signature(original)
        mesh=assets.duplicate_asset(name,root,original)
        assert mesh and len(mesh.static_materials)==slots
        for slot in range(slots): mesh.set_material(slot,mat)
        assert signature(mesh)==before,(name,before,signature(mesh))
        assert ea.save_loaded_asset(mesh,False),path
    assert len(mesh.static_materials)==slots and all(mesh.get_material(i)==mat for i in range(slots)),path
    ue.log('COURT_MASONRY_OK {} slots={} default={} geometry={}'.format(path,slots,finish,signature(mesh)))

ue.log('COURT_ART_PREPARED_OK')
