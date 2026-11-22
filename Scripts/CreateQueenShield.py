import unreal as u

folder = "/Game/BossPrototype/Materials"
name = "M_QueenShield"
material = u.load_asset(folder + "/" + name) if u.EditorAssetLibrary.does_asset_exist(folder + "/" + name) else u.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, u.Material, u.MaterialFactoryNew())
lib = u.MaterialEditingLibrary
lib.delete_all_material_expressions(material)
material.set_editor_property("blend_mode", u.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
color = lib.create_material_expression(material, u.MaterialExpressionConstant3Vector, -350, -120)
color.set_editor_property("constant", u.LinearColor(0.19, 0.75, 1.4, 1))
edge = lib.create_material_expression(material, u.MaterialExpressionFresnel, -550, 120)
edge.set_editor_property("exponent", 2.4)
edge.set_editor_property("base_reflect_fraction", 0.055)
opacity = lib.create_material_expression(material, u.MaterialExpressionMultiply, -300, 120)
opacity.set_editor_property("const_b", 0.55)
assert lib.connect_material_expressions(edge, "", opacity, "A")
assert lib.connect_material_property(color, "", u.MaterialProperty.MP_EMISSIVE_COLOR)
assert lib.connect_material_property(opacity, "", u.MaterialProperty.MP_OPACITY)
lib.recompile_material(material)
assert u.EditorAssetLibrary.save_loaded_asset(material, False)
