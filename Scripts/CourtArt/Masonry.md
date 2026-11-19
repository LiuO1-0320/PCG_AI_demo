# Project masonry assets

The court and the memory room use three project variants of the locally imported Medieval Dungeon meshes. The vendor stone materials depend on nineteen texture packages absent from the local Fab cache. No substitute was saved under a vendor texture name. Instead, the derived meshes bind the existing project stone/tiles materials in every default slot, matching the material overrides that the memory room already used. The candle meshes keep their complete original materials.

Run `prepare_masonry.py` as a UE 5.8.4 PythonScriptPlugin commandlet. It enables the instancing and Nanite usage needed by the project palette and cloth, without altering their graphs, and enables instancing on the previous purple reward marker. On the first run it duplicates the three meshes, verifies the same vertex/section counts per LOD, bounds, collision mode, primitive counts and slot names, then swaps only the default material values. On later runs it checks the saved variants and material usage without loading any original stone mesh. The source packages and maps are never saved by the commandlet.

| Mesh under `/Game/Demo/CourtArt/Masonry/` | Defaults | LOD 0 vertices | Source collision |
| --- | --- | ---: | --- |
| SM_Wall_00 | 4 × M_MemoryStone | 33,126 | One convex hull |
| SM_Ground | 1 × M_MemoryTiles | 289 | Complex as simple |
| SM_Pillar | 3 × M_MemoryStone | 11,728 | One convex hull |

The material-slot identifier `WorldGridMaterial` is the floor model's original imported slot **name**. Its actual bound material is `M_MemoryTiles`. All decorative components remain query-free at runtime; gameplay collision is supplied by the existing room shell. `COURT_ART_PREPARED_OK` marks a successful validation in the commandlet log.
