# Assets

Runtime content is stored outside C++ source code.

- `Data/assets.ini` maps logical IDs to data, image and audio paths.
- `Data/animations.ini` defines sprite-sheet layout, playback speed and render size.
- `Data/projectiles.ini` defines straight and arc projectile motion, lifetime and primitive visuals.
- `Data/skills.ini` defines ordered input commands and timing limits.
- `Data/effects.ini` links skills to visual/audio effect definitions.
- `Images/` is reserved for textures, sprite sheets and effect images.
- `Audio/` is reserved for music and sound effects.

The build copies this directory next to `ActionRPGClient.exe`. Runtime paths are resolved from the executable directory, not the process working directory.

`anchor_y` is the normalized vertical point in a sprite cell that is placed on the character's ground position. Use the shared foot baseline of every frame so animation does not drift along the map depth axis.
For a two-row sheet with different padding per row, `second_row_anchor_y` overrides that point for the second row.
`anchor_xs` optionally supplies one normalized horizontal body anchor per frame, preventing unevenly packed sheets from moving the character on screen.
