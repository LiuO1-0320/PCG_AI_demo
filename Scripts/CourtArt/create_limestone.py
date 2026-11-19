"""Build only M_CourtLimestone, using the existing project wall PBR textures.

Triplanar projection in fixed centimetres preserves grain on thin, scaled trim.
Both named colour endpoints are sRGB values converted to linear material values.
"""
import unreal as ue

ea=ue.EditorAssetLibrary; mel=ue.MaterialEditingLibrary
root='/Game/Demo/CourtArt'; name='M_CourtLimestone'
path=root+'/'+name
ea.make_directory(root)
mat=ea.load_asset(path) if ea.does_asset_exist(path) else ue.AssetToolsHelpers.get_asset_tools().create_asset(
    name,root,ue.Material,ue.MaterialFactoryNew())
assert mat
mel.delete_all_material_expressions(mat)
mat.set_editor_property('tangent_space_normal',True)


def node(kind,x,y): return mel.create_material_expression(mat,kind,x,y)


def connect(src,out,dest,pin):
    assert mel.connect_material_expressions(src,out,dest,pin),(src.get_name(),out,dest.get_name(),pin)


def prop(n,out,p): assert mel.connect_material_property(n,out,p),(n.get_name(),out,p)


def vector(vals,x,y):
    n=node(ue.MaterialExpressionConstant3Vector,x,y)
    n.set_editor_property('constant',ue.LinearColor(*vals,1.0)); return n


def srgb(rgb):
    return [((v/255+.055)/1.055)**2.4 if v>10.31475 else v/(255*12.92) for v in rgb]


size=vector([180,180,180],-1060,-380)
func_path='/Engine/Functions/Engine_MaterialFunctions01/Texturing/'
source='/Game/Demo/MemoryRoomArt/T_stone_block_wall_'
def world_sample(suffix,func,sampler,y):
    texture=ea.load_asset(source+suffix); assert texture
    obj=node(ue.MaterialExpressionTextureObject,-1060,y)
    obj.set_editor_property('texture',texture); obj.set_editor_property('sampler_type',sampler)
    f=node(ue.MaterialExpressionMaterialFunctionCall,-780,y)
    fn=ue.load_object(None,func_path+func+'.'+func); assert fn
    f.set_material_function(fn)
    connect(obj,'',f,'TextureObject');connect(size,'',f,'TextureSize')
    return f


diff=world_sample('Diffuse','WorldAlignedTexture',ue.MaterialSamplerType.SAMPLERTYPE_COLOR,-160)
normal=world_sample('nor_dx','WorldAlignedNormal',ue.MaterialSamplerType.SAMPLERTYPE_NORMAL,210)
rough=world_sample('Rough','WorldAlignedTexture',ue.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,420)
# Linear luminance of the source is about 0.026 at its median; remap the
# actual dark block texture rather than washing the grain out with a flat tint.
weights=vector([.2126,.7152,.0722],-760,-340)
lum=node(ue.MaterialExpressionDotProduct,-510,-175)
connect(diff,'XYZ Texture',lum,'A');connect(weights,'',lum,'B')
gain=node(ue.MaterialExpressionMultiply,-330,-175);gain.set_editor_property('const_b',15.)
connect(lum,'',gain,'A')
clamp=node(ue.MaterialExpressionSaturate,-160,-170);connect(gain,'',clamp,'')
darker=vector(srgb((83,75,66)),-160,-410)
lighter=vector(srgb((169,157,136)),-160,-290)
tone=node(ue.MaterialExpressionLinearInterpolate,70,-250)
connect(darker,'',tone,'A');connect(lighter,'',tone,'B');connect(clamp,'',tone,'Alpha')
prop(tone,'',ue.MaterialProperty.MP_BASE_COLOR)
normal_inputs=list(mel.get_material_expression_input_names(normal))
world_switch=next((pin for pin in normal_inputs if pin.replace(' ','').lower()=='worldspace'),None)
if world_switch:
    world=node(ue.MaterialExpressionStaticBool,-1020,300);world.set_editor_property('value',True)
    connect(world,'',normal,world_switch);mat.set_editor_property('tangent_space_normal',False)
prop(normal,'XYZ Texture',ue.MaterialProperty.MP_NORMAL)
mask=node(ue.MaterialExpressionComponentMask,-250,420);mask.set_editor_property('r',True)
mask.set_editor_property('g',False);mask.set_editor_property('b',False);mask.set_editor_property('a',False)
connect(rough,'XYZ Texture',mask,'');prop(mask,'',ue.MaterialProperty.MP_ROUGHNESS)
for usage in (ue.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES,ue.MaterialUsage.MATUSAGE_NANITE):
    mel.set_base_material_usage(mat,usage,True);assert mel.has_material_usage(mat,usage)
mel.recompile_material(mat)
assert ea.save_loaded_asset(mat,False)
assert mel.get_material_property_input_node(mat,ue.MaterialProperty.MP_BASE_COLOR)==tone
assert mel.get_material_property_input_node(mat,ue.MaterialProperty.MP_NORMAL)==normal
assert mel.get_material_property_input_node(mat,ue.MaterialProperty.MP_ROUGHNESS)==mask
ue.log('COURT_LIMESTONE_OK {} sRGB=#534B42..#A99D88 projection=180cm usages=ISM,Nanite'.format(mat.get_path_name()))
