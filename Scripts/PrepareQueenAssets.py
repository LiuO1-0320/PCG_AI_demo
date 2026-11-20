"""Bind migrated queen textures/material slots and rebuild their project shader data."""
import unreal as u

root='/Game/BossPrototype/QueenDemo/'
library=u.MaterialEditingLibrary
texture=u.load_asset(root+'T_QueenDemo_BaseColor')
assert texture
materials=[]
for name in ['M_QueenDemo','M_QueenFrameGlow']:
    material=u.load_asset(root+name)
    sample=library.get_material_property_input_node(material,u.MaterialProperty.MP_BASE_COLOR)
    assert isinstance(sample,u.MaterialExpressionTextureSample)
    sample.set_editor_property('texture',texture)
    material.set_editor_property('used_with_skeletal_mesh',True)
    library.recompile_material(material)
    assert u.EditorAssetLibrary.save_loaded_asset(material,False)
    materials.append(material)

skin=u.load_asset(root+'SK_QueenDemo')
slots=skin.get_editor_property('materials')
for slot in slots:
    slot.set_editor_property('material_interface',materials[1] if 'FrameGlow' in str(slot.material_slot_name) else materials[0])
skin.set_editor_property('materials',slots)
assert u.EditorAssetLibrary.save_loaded_asset(skin,False)
for name in ['M_QueenChargeCore','M_QueenChargeHalo','M_Warning']:
    material=u.load_asset('/Game/BossPrototype/Materials/'+name)
    library.recompile_material(material)
    assert u.EditorAssetLibrary.save_loaded_asset(material,False)
u.log('CASTLE_QUEEN_MATERIALS_OK slots={} texture={}'.format(len(slots),texture.get_path_name()))
