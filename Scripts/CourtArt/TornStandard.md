# The Court Painters' Standard

Original project art; no downloaded mesh, image, font or texture. Two long brushes cross inside a pointed niche. The burgundy fabric has woven selvedges, dull gold embroidery and patches of lost gilding. The folds, irregular swallowtail hem and two small holes are mesh geometry. The material is opaque and double sided (one material slot, one texture).

| UE 5.8.4 asset | Purpose |
| --- | --- |
| `/Game/Demo/CourtArt/SM_TornStandard` | 820 source vertices, 1,516 triangles, one LOD |
| `/Game/Demo/CourtArt/M_TornStandardVelvet` | Two-sided, rough fabric, compatible with instanced static meshes |
| `/Game/Demo/CourtArt/T_TornStandardWeave` | Original 512 x 1024 sRGB colour texture with mipmaps |

**Local axes, centimetres:** X is depth (−4.453 to 4.504); Y is width (−50 to 50); Z is height (0 to 195). For a desired width and height, adjust Y and Z. Keep the thickness near its natural 9 cm or change X separately. Do not override material slot 0. The placed component should use `NoCollision` and have its navigation influence disabled. The saved mesh has no simple shapes, simple shapes serve complex queries, the body default is `NoCollision`, and navigation data is off.

**Reproduction from a clean project:** run `python Scripts/CourtArt/generate_standard.py` (requires Pillow and NumPy). It recreates deterministic glTF and cloth artwork under `Saved/CourtArtSource/`. Run the UE 5.8.4 PythonScriptPlugin commandlet with `-run=pythonscript -script=<absolute path to Scripts/CourtArt/import_standard.py> -EnablePlugins=PythonScriptPlugin -nullrhi -unattended -nop4`. The import is for a clean install and reuses existing named assets. It saves only the three listed packages. Source glTF intentionally has no material; the UE importer binds the finished cloth after importing the geometry. Its single material warning is expected. The final audit in the commandlet log must contain `COURT_STANDARD_VERIFIED`.
