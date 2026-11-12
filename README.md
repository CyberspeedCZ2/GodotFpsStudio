# Godot FPS Studio

Godot FPS Studio is a source fork of Godot 4.7.2 with a native editor workspace for FPS level and asset authoring. It is built from the complete Godot engine source and currently targets a macOS Apple Silicon editor build.

## Repository contents

- `engine/` — Godot engine source, including FPS Studio editor changes and upstream modules.
- `engine/editor/fps_studio/` — FPS Studio workspace code and the bundled UniBuilder source.
- `docs/` — current architecture and feature documentation.
- `tools/build_macos.sh` — builds and installs the macOS arm64 application.
- `tools/pack_fps_studio_unibuilder.gd` — packs UniBuilder resources into the editor bundle.

This repository does not currently include a standalone game sample or a reusable FPS project template. The build script can package the editor without either.

## Current editor

The editor is built into the engine, so its workspace and tools are available when opening any Godot project. The custom UI is still hosted by Godot's `EditorNode`; FPS Studio is not a separately rewritten editor.

The workspace includes Level, Model, Code, Assets, and Particle screens, a Content Browser, Level viewport authoring tools, NPC route editing, NavigationMesh tools, model import settings with LOD preview and configuration, and a particle preview workspace for native 3D particle nodes. The engine also provides a `PostProcessVolume3D` node for camera-position based Environment overrides with a box gizmo, priority, weight, and smooth blend radius. Play can run in the Level viewport or in a separate window.

UniBuilder is bundled with the editor as a built-in resource pack. It does not need to be installed as a project add-on.

See [PostProcessVolume3D](docs/post_process_volume_3d.md) for setup and limitations.

See [Particle workspace](docs/particle_editor.md) for its workflow and current scope.

## Build on macOS Apple Silicon

Requirements:

- macOS on Apple Silicon
- Xcode command-line tools
- Python 3.9 or newer
- SCons 4.4 or newer
- Vulkan SDK with MoltenVK for a clean build

An already configured local SCons build can be rebuilt without installing the Vulkan SDK again. Keep the existing build cache if you rely on that setup.

Run:

```sh
./tools/build_macos.sh
```

The script installs the signed application at `Godot FPS Studio.app` in the repository root. It does not create a ZIP archive. The app bundle and compiler outputs are local build artifacts and are ignored by Git.
