# Project editor, asset workflow, and multiplayer testing

`meat2d_launcher` is a lightweight graphical workspace for creating and
shipping Meat2D games. It is intentionally compatible with ordinary CMake,
Git, and text files: using the editor never makes a project dependent on a
private database or proprietary project format.

## Create or open a project

Launch the editor:

```bash
meat2d_launcher
```

The welcome screen lets you choose a game-type starter:

- side-scroller, with platform movement and paintable simulation;
- top-down, with four-direction movement and a simulation arena;
- metroidvania, with side-view traversal and platform layout;
- falling-sand, with the focused deterministic elements laboratory.

Native folder dialogs are available, while every path remains directly
editable for keyboard-focused workflows. A generated project can also be
opened from the command line:

```bash
meat2d_launcher /path/to/game
```

## Code and asset browser

The Code & Assets tab scans only the selected project root. It provides:

- All, Code, and Assets filters plus case-insensitive path search;
- source, header, script, shader, CMake, TOML, JSON, documentation, image,
  audio, and font classification;
- bounded in-editor text editing with tabs and `Ctrl+S`;
- automatic project-tree refresh and selected-file change detection;
- create-file and native asset-import controls;
- PNG, JPEG, BMP, GIF, WebP, and TGA previews;
- external-app opening for formats that need specialized tools.

`.git`, IDE state, and generated build/package directories are hidden by
default. Generated folders can be shown explicitly. Symlinks are not followed,
paths are canonicalized, parent traversal is rejected, and the text editor
will not load files above 2 MiB. Asset imports are copied under `assets/` and
receive a non-destructive numbered filename when a name already exists.

The editor checks the project once per second so files created by an external
tool appear without reopening the project. A selected code file that changes
on disk reloads automatically when its editor buffer is clean. If both copies
changed, the editor preserves the local buffer and asks whether to reload the
disk version or keep the editor version. Deleted files can be recreated from a
preserved code buffer. No conflict is overwritten by Build, Package, Publish,
or `Ctrl+S` until it is resolved.

## Sprite manager

Selecting an image opens the sprite manager beside the asset browser. Set:

- frame width and height;
- outer margin and inter-frame spacing;
- named animation ranges;
- frames per second and loop behavior.

The preview overlays every detected frame rectangle and animates the selected
range. Save creates a sibling file such as:

```text
assets/player.png
assets/player.sprite.toml
```

The metadata is readable TOML:

```toml
# Meat2D sprite sheet
version = 1
image = "assets/player.png"
frame_width = 16
frame_height = 16
margin = 0
spacing = 0

[[animation]]
name = "run"
first_frame = 0
frame_count = 6
frames_per_second = 12
loop = true
```

Games can parse the file with `decode_sprite_sheet_toml()`, calculate a safe
frame count with `sprite_frame_count()`, and obtain source rectangles with
`sprite_frame()`. Paths, dimensions, animation counts, ranges, and integer
overflow are validated before use.

Sprite settings have the same dirty marker and `Ctrl+S` behavior as code.
Unsaved metadata is saved before build, package, or publish, blocks accidental
project closure, and can be reverted. Externally replaced images reload while
preserving unsaved frame settings; concurrent metadata edits use the same
explicit conflict controls as source files.

## Host and join developer sessions

The **Multiplayer** tab turns the bundled Elements Lab into a quick networking
test harness:

- **Start server** launches `meat2d_server` with the selected gameplay and LAN
  discovery ports.
- **Join local host** launches `meat2d_sandbox` against `127.0.0.1`.
- **Join direct** accepts any reachable hostname/IP and UDP gameplay port.
- **Refresh LAN** discovers compatible local sessions; each row has **Join**
  and **Copy** actions.
- **Refresh public list** queries the configured self-hosted directory; each
  listing can be joined through directory-assisted NAT introduction.

Enter the public directory host and port, enable public advertisement, and
then start the server to list an editor-hosted session publicly. The editor
uses SDL's cross-platform process API with argument arrays rather than a
command shell, so names and endpoints are not interpreted as shell commands.
It polls server/client lifetimes without freezing the UI. The server and all
clients launched from this tab are test processes owned by the editor and are
closed when the editor exits.

This tab launches the engine's living-lab client and server. Generated games
remain ordinary C++ projects and should expose their own player-facing
host/join screens through `Meat2D::Net`.

## Build, test, package, and publish

The Project tab runs long operations in the background and keeps the editor
responsive:

- **Build Debug** configures and compiles the development preset.
- **Build & Test** builds, launches the game, and captures its exit result.
- **Build Release** compiles the release preset.
- **Package** produces ZIP and TGZ game bundles with required licenses.
- **Publish** initializes Git when needed and uses an authenticated GitHub CLI
  session to create or push a public/private repository.

The intended inner loop is: select and edit a source or asset, choose **Build
& Test**, and close the running game to return its captured exit result to the
editor. Build, test, package, and publish save the current text or sprite
metadata first; `Ctrl+S` remains available for explicit saves. Debug and
Release share one multi-config dependency cache, so switching profiles does
not clone and configure SDL twice.

The editor finds CMake on `PATH`, in common Visual Studio/CMake/Scoop
locations, or through `MEAT2D_CMAKE`. Source-tree builds automatically point
new games at the same local engine checkout. Installed editors use the engine
tag recorded in the generated project.

The same workflow is scriptable:

```bash
meat2d new "My Game" --template side
meat2d new "My Game" --template top
meat2d new "My Game" --template metroidvania
meat2d new "My Game" --template falling-sand
meat2d build my-game
meat2d run my-game
meat2d package my-game
meat2d publish my-game --repo OWNER/my-game
```
