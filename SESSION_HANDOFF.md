# Session Handoff — FPS Studio Geometry Builder

## Current objective

Replace the former third-party modeling integration with an original native C++ geometry authoring tool inside Godot FPS Studio. Keep geometry creation reachable from the Level viewport right-click menu. Build feature parity in stages: viewport drawing, object/vertex/edge/face editing, topology tools, UV/material authoring, collision, bake, and export.

## Work started

- Removed the old modeling add-on source and license from `engine/editor/fps_studio/`, along with its resource-pack generator.
- Removed automatic add-on loading, packed-script scanning, resource-pack loading, build packaging, and viewport signal references.
- Deleted generated modeling resource packs and stale app backups whose binaries contained the former integration. The user chose to retain Git history, so older commits still contain the original files.
- Replaced the old RMB submenu with **FPS Studio Geometry** and added the same shape picker to the native 3D toolbar. It creates standard `MeshInstance3D` nodes from native Godot meshes, with Undo/Redo and scene ownership.
- Added native starter shapes for box, plane, cylinder, cone, prism, stairs, arch, poly shape, door, sphere, torus, pipe, curved stairs, trim, trim walls, and wall.
- Added **Object**, **Vertex**, **Edge**, and **Face** selection modes to the native toolbar. The modes use Godot's subgizmo selection and transform system; selected vertices, edges, and triangles can be translated, rotated, or scaled with Undo/Redo. Edge mode hides the internal diagonal between coplanar triangles.
- The implementation is still partial. Interactive parameterized drawing, loop/extrude/inset/bevel and other topology operations, UV/material authoring, automatic collision authoring, bake, and export still need implementation.

## Project constraints

- Never launch the GUI editor unless the user asks directly.
- Build the macOS arm64 editor with `./tools/build_macos.sh`.
- Do not create ZIP archives.
- The user previously authorized pushing project changes to GitHub.

## Repository and state

- Repository: `/Users/davidsoudil/Projects/Godot/GodotFpsStudio`
- Branch: `main`
- Main code: `engine/editor/scene/3d/node_3d_editor_plugin.cpp`, `engine/editor/docks/scene_tree_dock.cpp`, and matching headers.
- Geometry model/gizmo code: `engine/editor/fps_studio/fps_studio_geometry.cpp` and `fps_studio_geometry_gizmo_plugin.cpp`.
- Build script: `tools/build_macos.sh`
- Native editor workspace: `engine/editor/fps_studio/fps_studio_workspace.cpp/.h`

## Verification still required

- Continue with edge and face modes and the common topology operations, then UV/material authoring, collision, bake, and export.
- The source currently has a new uncommitted Vertex Mode implementation; run `git diff --check`, build with `./tools/build_macos.sh`, verify the installed app signature, and then commit/push normally. Do not rewrite Git history or force-push; the user explicitly chose to keep history unchanged.
- Do not open the GUI editor without an explicit request.
