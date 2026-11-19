"""Run with UE PythonScriptPlugin. Writes only /Game/Demo/MemoryRoomArt assets."""
import unreal
from pathlib import Path

ROOT = '/Game/Demo/MemoryRoomArt'
unreal.EditorAssetLibrary.make_directory(ROOT)
tools = unreal.AssetToolsHelpers.get_asset_tools()

def material(name, color, metallic=0.0, roughness=0.7, emission=0.0):
    path = ROOT + '/' + name
    m = unreal.load_asset(path)
    if m:
        return m
    m = tools.create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('two_sided', True)
    def scalar(value):
        x = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionConstant)
        x.set_editor_property('r', value)
        return x
    c = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionConstant3Vector)
    c.set_editor_property('constant', unreal.LinearColor(*color))
    unreal.MaterialEditingLibrary.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(scalar(metallic), '', unreal.MaterialProperty.MP_METALLIC)
    unreal.MaterialEditingLibrary.connect_material_property(scalar(roughness), '', unreal.MaterialProperty.MP_ROUGHNESS)
    if emission:
        e = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionConstant3Vector)
        e.set_editor_property('constant', unreal.LinearColor(*[v*emission for v in color[:3]], 1.0))
        unreal.MaterialEditingLibrary.connect_material_property(e, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m

# sRGB hex palette converted to linear values.
def rgb(hexcode):
    def linear(v):
        v = v/255.0
        return v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4
    return (*[linear(int(hexcode[i:i+2], 16)) for i in (0,2,4)], 1.0)

material('M_Charcoal', rgb('211D28'))
material('M_Limestone', rgb('A99D88'))
material('M_Velvet', rgb('6F263C'), roughness=.93)
material('M_AntiqueGold', rgb('9E8150'), metallic=.65, roughness=.44)
material('M_MemoryGlow', rgb('A89BD4'), roughness=.4, emission=2.2)
material('M_CandleFlame', rgb('FFD79A'), emission=4.0)

def stone_material(name, source):
    path = ROOT + '/' + name
    if unreal.load_asset(path):
        return
    m = tools.create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('two_sided', True)
    uv = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate)
    uv.set_editor_property('u_tiling', 2.0)
    uv.set_editor_property('v_tiling', 2.0)
    for suffix, prop in [('Diffuse', unreal.MaterialProperty.MP_BASE_COLOR), ('nor_dx', unreal.MaterialProperty.MP_NORMAL), ('Rough', unreal.MaterialProperty.MP_ROUGHNESS)]:
        filename = str(Path(unreal.Paths.project_dir())/'SourceArt/MemoryRoom/Textures'/f'{source}_{suffix}.jpg')
        asset_name = 'T_' + source + '_' + suffix
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', filename)
        task.set_editor_property('destination_path', ROOT)
        task.set_editor_property('destination_name', asset_name)
        task.set_editor_property('automated', True)
        task.set_editor_property('save', True)
        tools.import_asset_tasks([task])
        texture = unreal.load_asset(ROOT + '/' + asset_name)
        assert texture, filename
        texture.set_editor_property('srgb', suffix == 'Diffuse')
        if suffix == 'nor_dx':
            texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        unreal.EditorAssetLibrary.save_loaded_asset(texture)
        tex = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionTextureSample)
        tex.set_editor_property('texture', texture)
        if suffix == 'nor_dx':
            tex.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        elif suffix == 'Rough':
            tex.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        unreal.MaterialEditingLibrary.connect_material_expressions(uv, '', tex, 'Coordinates')
        unreal.MaterialEditingLibrary.connect_material_property(tex, 'RGB' if suffix != 'Rough' else 'R', prop)
    unreal.MaterialEditingLibrary.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)

stone_material('M_MemoryStone', 'stone_block_wall')
stone_material('M_MemoryTiles', 'stone_tiles')

texture = unreal.load_asset(ROOT + '/T_FacelessCourtier')
if not texture:
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', str(Path(unreal.Paths.project_dir())/'SourceArt/MemoryRoom/faceless_courtier.png'))
    task.set_editor_property('destination_path', ROOT)
    task.set_editor_property('destination_name', 'T_FacelessCourtier')
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(ROOT + '/T_FacelessCourtier')
assert texture, 'Faceless portrait import failed'
m = unreal.load_asset(ROOT + '/M_FacelessCourtier')
if not m:
    m = tools.create_asset('M_FacelessCourtier', ROOT, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('two_sided', True)
    tex = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionTextureSample)
    tex.set_editor_property('texture', texture)
    unreal.MaterialEditingLibrary.connect_material_property(tex, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    # A little indirect contribution keeps the art legible in the game's dark lighting.
    mul = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionMultiply)
    mul.set_editor_property('const_b', .18)
    unreal.MaterialEditingLibrary.connect_material_expressions(tex, 'RGB', mul, 'A')
    unreal.MaterialEditingLibrary.connect_material_property(mul, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
unreal.log('CASTLE_MEMORY_MATERIALS_OK')

for name in ['SM_Wall_00','SM_Wall_01','SM_Ground','SM_Pillar']:
    mesh = unreal.load_asset('/Game/Dungeon/Meshes/Props/'+name)
    if mesh:
        unreal.log('CASTLE_ART_BOUNDS {} {} materials={}'.format(name, mesh.get_bounding_box(), [str(x.material_interface.get_path_name()) if x.material_interface else '' for x in mesh.static_materials]))
mesh = unreal.load_asset('/Game/Dungeon/Candles/Meshes/SM_Candles_Holder_00')
if mesh:
    unreal.log('CASTLE_ART_BOUNDS Candles {} materials={}'.format(mesh.get_bounding_box(), [str(x.material_interface.get_path_name()) if x.material_interface else '' for x in mesh.static_materials]))
