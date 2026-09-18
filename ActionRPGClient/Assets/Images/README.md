# Images

Store textures, sprite sheets, map backgrounds and visual-effect images here. Register each runtime file under `[Images]` in `Data/assets.ini` and refer to it by logical ID from gameplay data.

Player animation sheets are configured in `Data/animations.ini`. Frames are read left-to-right and then top-to-bottom; `first_row_ratio` can move the split between two unevenly padded rows without changing C++ code.
