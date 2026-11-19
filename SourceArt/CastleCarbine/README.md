# Archive carbine – first-person display model

The silhouette is **blaster-f** from **Kenney, Blaster Kit 2.1**. Source: https://kenney.nl/assets/blaster-kit. Its included notice, [Kenney_License.txt](Kenney_License.txt), identifies the pack as [Creative Commons Zero 1.0](https://creativecommons.org/publicdomain/zero/1.0/). The source permits use, modification and redistribution. Only this single model is included for the game.

The adaptation uses dark iron, oxblood panels, pewter and aged brass, with a little lavender light on the sight glass. The geometry is oriented and scaled for the existing first-person camera and muzzle marker; the model supplies no gameplay collision. No animation rig or external textures are needed.

- Author model in official ZIP: `Models/OBJ format/blaster-f.obj`, SHA-256 `01f043ca28ef17d168cf96b3a440fa6edd86fa4b2a767cb0a441c694b6d41b86`.
- Ready-to-import source: [SM_ArchiveCarbine.glb](SM_ArchiveCarbine.glb). 1,272 triangles; 62.88 × 8.16 × 21.36 cm in UE.
- Saved UE static mesh: `/Game/Demo/CastleCarbine/SM_ArchiveCarbine/StaticMeshes/SM_ArchiveCarbine`. Its five materials live alongside the StaticMeshes folder.
- To regenerate the GLB from a newly downloaded official ZIP, run `python Scripts/CastleCarbine/prepare_mesh.py <pack.zip>`. The geometry and five material slots are embedded in the GLB.
- On an empty destination, run `Scripts/CastleCarbine/import_asset.py` in the UE Python commandlet. With the asset already present, the same script validates its package, proportions and assigned materials without writing to the project.
