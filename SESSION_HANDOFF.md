# Session Handoff — FPS Studio Geometry Builder

## Current objective

Build a native C++ geometry authoring tool inside Godot FPS Studio and keep geometry creation reachable from the Level viewport right-click menu. The full target includes viewport drawing, object/vertex/edge/face editing, topology tools, UV/material authoring, collision, bake, and export.

## Work started

- Removed the external modeling add-on source, license, resource-pack generator, automatic loader, pack scanning/loading, build packaging, and viewport signal references from the current checkout.
- Deleted generated modeling resource packs and stale app backups containing the removed integration. The user chose to retain Git history, so older commits still contain the deleted files.
- Added **FPS Studio Geometry** to the Level viewport RMB menu and added the shape picker to the native 3D toolbar. It creates standard `MeshInstance3D` nodes from native Godot meshes, with Undo/Redo and scene ownership.
- Added native starter shapes for box, plane, cylinder, cone, prism, stairs, arch, poly shape, door, sphere, torus, pipe, curved stairs, trim, trim walls, and wall.
- Added **Object**, **Vertex**, **Edge**, and **Face** selection modes to the native toolbar. The modes use Godot's subgizmo selection and transform system; selected vertices, edges, and triangles can be translated, rotated, or scaled with Undo/Redo. Edge mode hides the internal diagonal between coplanar triangles.
- Added native **Mirror X/Y/Z**, **Subdivide**, and **Conform Normals** operations to the 3D toolbar with Undo/Redo.
- The implementation remains partial. The previous tool inventory included primitive drawing, soft selection, topology selection and editing operations, cut/insert tools, material assignment and vertex paint, UV editing, mesh merging and booleans, plus OBJ/glTF export. These native features remain to be built alongside parameters for interactive shapes, automatic collision authoring, and baking.

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

- Continue with interactive parameterized drawing and topology operations (extrude, inset, bevel, loop/cut, bridge, merge, weld, delete/fill, and selection helpers), then soft selection/pivot tools, UV/material authoring, collision, bake, and export.
- The current work adds mesh operations in `fps_studio_geometry.cpp` and the native 3D toolbar, with Undo/Redo integration in `scene_tree_dock.cpp`.
- The user authorized normal pushes and chose to keep Git history unchanged. Never rewrite history or force-push.
- Latest verification for this work: `git diff --check`, `./tools/build_macos.sh`, and the app signature check all passed after the documentation refresh.
- Do not open the GUI editor without an explicit request.
