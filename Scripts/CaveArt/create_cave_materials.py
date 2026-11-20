"""Run with UE PythonScriptPlugin. Writes only /Game/Demo/CaveArt assets.

Builds two parameterised material variants for the treasure cave (PCG2):

  M_CaveStone  - wall masonry, tunable tint / roughness / macro grime
  M_CaveTile   - floor slabs, same knobs

They reuse the textures that already live in /Game/Demo/MemoryRoomArt, so no
new texture payload enters the repository. The point of the parameters is that
CASTLE runtime code can grade one room per "Memory Depth" step through a
Material Instance Dynamic instead of shipping several static materials.

Run:
  UnrealEditor-Cmd.exe <proj> -run=pythonscript -script=<this file> -unattended
"""
import unreal

ROOT = '/Game/Demo/CaveArt'
TEX = '/Game/Demo/MemoryRoomArt'
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
unreal.EditorAssetLibrary.make_directory(ROOT)


def tex(name):
    asset = unreal.load_asset(TEX + '/' + name)
    assert asset, 'missing texture ' + name
    return asset


def sample(material, texture, uv, sampler, srgb):
    texture.set_editor_property('srgb', srgb)
    node = mel.create_material_expression(material, unreal.MaterialExpressionTextureSample)
    node.set_editor_property('texture', texture)
    node.set_editor_property('sampler_type', sampler)
    mel.connect_material_expressions(uv, '', node, 'Coordinates')
    return node


def build(name, prefix):
    path = ROOT + '/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log('CASTLE_CAVE_MATERIAL skip ' + path)
        return
    m = tools.create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('two_sided', True)

    uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate)
    uv.set_editor_property('u_tiling', 2.0)
    uv.set_editor_property('v_tiling', 2.0)
    # A second, much coarser UV set reuses the same diffuse as a macro mask.
    # Multiplying the fine tiling by the coarse one breaks the visible grid
    # repetition without importing an extra texture.
    uv_macro = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate)
    uv_macro.set_editor_property('u_tiling', 0.37)
    uv_macro.set_editor_property('v_tiling', 0.37)

    base = sample(m, tex('T_' + prefix + '_Diffuse'), uv,
                  unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, True)
    normal = sample(m, tex('T_' + prefix + '_nor_dx'), uv,
                    unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, False)
    rough = sample(m, tex('T_' + prefix + '_Rough'), uv,
                   unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, False)
    macro = sample(m, tex('T_' + prefix + '_Diffuse'), uv_macro,
                   unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, True)

    white = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector)
    white.set_editor_property('constant', unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    grime = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter)
    grime.set_editor_property('parameter_name', 'MacroGrime')
    grime.set_editor_property('default_value', 0.5)

    tint = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter)
    tint.set_editor_property('parameter_name', 'Tint')
    tint.set_editor_property('default_value', unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    rough_scale = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter)
    rough_scale.set_editor_property('parameter_name', 'RoughnessScale')
    rough_scale.set_editor_property('default_value', 1.0)

    blend = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate)
    # The coarse sample averages well below 1.0, so it is boosted back to roughly
    # neutral first. That keeps MacroGrime as pure mottling instead of a global
    # darkening knob.
    boost = mel.create_material_expression(m, unreal.MaterialExpressionConstant)
    boost.set_editor_property('r', 1.0)
    lifted = mel.create_material_expression(m, unreal.MaterialExpressionMultiply)
    mel.connect_material_expressions(macro, 'RGB', lifted, 'A')
    mel.connect_material_expressions(boost, '', lifted, 'B')
    mel.connect_material_expressions(white, '', blend, 'A')
    mel.connect_material_expressions(lifted, '', blend, 'B')
    mel.connect_material_expressions(grime, '', blend, 'Alpha')

    lit = mel.create_material_expression(m, unreal.MaterialExpressionMultiply)
    mel.connect_material_expressions(base, 'RGB', lit, 'A')
    mel.connect_material_expressions(blend, '', lit, 'B')

    graded = mel.create_material_expression(m, unreal.MaterialExpressionMultiply)
    mel.connect_material_expressions(lit, '', graded, 'A')
    mel.connect_material_expressions(tint, '', graded, 'B')

    roughness = mel.create_material_expression(m, unreal.MaterialExpressionMultiply)
    mel.connect_material_expressions(rough, 'R', roughness, 'A')
    mel.connect_material_expressions(rough_scale, '', roughness, 'B')

    mel.connect_material_property(graded, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(normal, 'RGB', unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m, only_if_is_dirty=False)
    unreal.log('CASTLE_CAVE_MATERIAL ok ' + path)


build('M_CaveStone', 'stone_block_wall')
build('M_CaveTile', 'stone_tiles')


def flat(name, hexcode, metallic=0.0, roughness=0.7, emission=0.0):
    """Single-colour material, used for the chandelier and its inlay."""
    path = ROOT + '/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log('CASTLE_CAVE_MATERIAL skip ' + path)
        return
    m = tools.create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('two_sided', True)

    def linear(v):
        v = v / 255.0
        return v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4

    colour = [linear(int(hexcode[i:i + 2], 16)) for i in (0, 2, 4)]

    def constant(value, prop):
        node = mel.create_material_expression(m, unreal.MaterialExpressionConstant)
        node.set_editor_property('r', value)
        mel.connect_material_property(node, '', prop)

    base = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector)
    base.set_editor_property('constant', unreal.LinearColor(*colour, 1.0))
    mel.connect_material_property(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
    constant(metallic, unreal.MaterialProperty.MP_METALLIC)
    constant(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    if emission:
        glow = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector)
        glow.set_editor_property('constant', unreal.LinearColor(*[c * emission for c in colour], 1.0))
        mel.connect_material_property(glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m, only_if_is_dirty=False)
    unreal.log('CASTLE_CAVE_MATERIAL ok ' + path)


# Warm struck gold for the chandelier frame and the floor inlay. The emissive
# term keeps it readable in the darker rooms; it is not meant to light anything.
flat('M_CaveGold', 'E8B96A', metallic=.9, roughness=.22, emission=.30)

for name in ('M_CaveStone', 'M_CaveTile'):
    asset = unreal.load_asset(ROOT + '/' + name)
    params = []
    if asset:
        for collection in mel.get_scalar_parameter_names(asset):
            params.append(collection)
        for collection in mel.get_vector_parameter_names(asset):
            params.append(collection)
    unreal.log('CASTLE_CAVE_MATERIAL params {} = {}'.format(name, params))

unreal.log('CASTLE_CAVE_MATERIALS_OK')
