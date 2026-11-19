# First-floor memory treasure room art

Only the first-floor memory treasure room uses these decorations. The exploration generator adds the vault separately.

## Asset recovery and materials

Install Medieval Dungeon from your Fab library into this Unreal project and run `Scripts/SetupFabCourtArt.ps1`. This builds local variants of the original masonry meshes using project textures. Follow the Fab setup instructions in [README_DEMO.md](../../README_DEMO.md).

Used models:

- `/Game/Dungeon/Meshes/Props/SM_Wall_00`
- `/Game/Dungeon/Meshes/Props/SM_Ground`
- `/Game/Dungeon/Meshes/Props/SM_Pillar`
- `/Game/Dungeon/Candles/Meshes/SM_Candles_Holder_00`
- `/Game/Dungeon/Candles/Meshes/SM_Candle_Melted_01`

The original stone texture directory was absent from the cache. Walls and floors therefore use dedicated project materials with CC0 Poly Haven textures, rather than claiming that the original downloaded material set is complete:

- https://polyhaven.com/a/stone_block_wall
- https://polyhaven.com/a/stone_tiles
- License: https://polyhaven.com/license

Textures are 1K, downloaded from the official Poly Haven API and verified against the supplied MD5 values. Warm candle lighting, charcoal backing, burgundy carpet and lavender emissive use the requested palette. Materials are saved under `/Game/Demo/MemoryRoomArt/`. No shared castle material was edited. The gold frame is a project-owned decorative assembly, and the portrait is a generated texture, not a Fab asset.

The faceless portrait source is `SourceArt/MemoryRoom/faceless_courtier.png`, generated with the built-in image tool. Prompt: flat front-on old oil portrait on cracked canvas, faceless medieval court figure, charcoal-purple background, burgundy mantle, antique gold clasp, softly lit erased face, no frame or text. It is applied only to no-collision portrait panels.

`create_materials.py` imports the source textures and creates missing assets without rebuilding the map. Run with the engine's PythonScriptPlugin commandlet; it does not require the plugin to be enabled permanently in the project.

## Collision contract

The first-floor memory room's original cube collision shell is rendered invisible but retains the same positions, dimensions, BlockAll profile and navigation influence:

- Floor: 1900 x 1900 x 70 cm, center Z=-35, walking plane Z=0.
- Wall centers: 900 cm from room center, 85 cm thick and 480 cm high.
- Doorway width: 480 cm; lintel underside Z=410.
- Corner columns: 135 x 135 x 520 cm, XY offsets +/-835, center Z=260.

Fitted meshes use their actual bounding-box centers and sizes, not their pivots. Every art primitive is NoCollision and does not affect navigation. The visual floor backing fills irregular mesh edges without changing the floor collision. Chest actor location, collision and rewards are unchanged; only its memory marker receives the new lavender material. The old interaction and lid-opening code remains in use.

## Verification on UE 5.8.4

Build completed successfully. Run either seed in standalone mode with `-CastleMemoryArtTest -CastleSeed=1337` (or `2026`) on `/Game/Demo/L_CastleDemo`.

| Seed | Door mask | Hidden shell boxes | Results |
| --- | --- | --- | --- |
| 1337 | 1, east | 11 | PASS: original box sizes/transforms, three capsule lanes, physical player movement, wall/jamb sweeps, real bolt blocked, real E opens once |
| 2026 | 9, east and south | 13 | PASS: same checks for both doors, real E opens once |

Logs: `D:/pcg/MemoryRoomArtReview/ArtSeed1337.log` and `ArtSeed2026.log`. Look for `CASTLE_MEMORY_ART_COLLISION_OK`, `CASTLE_MEMORY_ART_DOOR_WALK_OK`, `CASTLE_MEMORY_ART_CHEST_OK`, and `CASTLE_MEMORY_ART_OK`.

Final images: `Saved/Screenshots/MemoryRoomArt/MemoryRoom_Seed1337.png` and `MemoryRoom_Seed2026.png`. Closed-chest variants include `_Closed` in their names. Screenshots come from the UE renderer. Test mode temporarily stops enemy AI for repeatable inspection; normal play does not do this.

The existing RoguelitePrototype `FRogueliteTemplateMemberState::Mobility` startup diagnostic is unrelated and has not been modified in this narrowly scoped task.
