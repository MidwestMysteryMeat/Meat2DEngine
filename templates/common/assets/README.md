# Game assets

Import sprites, textures, audio, and fonts here with the Meat2D editor or your
normal filesystem tools.

Selecting a PNG/JPEG/BMP/GIF/WebP/TGA image in the editor opens the sprite
manager. Frame grids and named animations are saved beside the image as
human-readable `*.sprite.toml` files.

Keep source art in a separate subfolder if it should ship differently from
runtime-ready assets, and update the install rule in the project
`CMakeLists.txt` when needed.
