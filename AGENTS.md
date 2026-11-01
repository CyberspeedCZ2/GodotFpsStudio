# Godot FPS Studio — trvalý kontext

## Projekt

- Repozitář: `GodotFpsStudio`, hlavní větev `main`, GitHub: `git@github.com:CyberspeedCZ2/GodotFpsStudio.git`.
- Základem je kompletní zdrojový fork Godotu 4.7.2.
- Vývojový build cílí na macOS Apple Silicon. Hry mohou cílit na další platformy, ale exportní šablony nejsou součástí běžného editorového buildu.
- Repo obsahuje zdrojový kód editoru a enginu, nikoli aktuální samostatný FPS herní projekt.
- Herní projekt je samostatný repozitář `/Users/davidsoudil/Projects/Godot/project-hl-2-inspiration` na GitHubu `CyberspeedCZ2/project-hl-2-inspiration`. Jeho scény a herní obsah nepatří do tohoto repozitáře.

## Pokyny pro práci

1. **Nespouštěj GUI editoru**, pokud o to uživatel výslovně nepožádá. Kód kontroluj staticky nebo headless nástroji.
2. Build na macOS prováděj skriptem `./tools/build_macos.sh`. Výsledkem je `Godot FPS Studio.app` v kořeni repozitáře.
3. **Nevytvářej distribuční ZIP archivy.**
4. **UniBuilder zachovej.** Jeho zdroj je v `engine/editor/fps_studio/unibuilder/` a build jej balí jako vestavěný PCK.
5. Změny určené do projektu commitni a pushni na GitHub po kontrole diffu a stavu Gitu.

## Aktuální stav

- Editor nabízí pracovní obrazovky Level, Model, Code a Assets. Vlastní `FPSStudioWorkspace` stále běží uvnitř Godotova `EditorNode`.
- Content Browser a FPS Studio nástroje jsou součástí enginu a dostupné v libovolném Godot projektu.
- UniBuilder je vestavěný do aplikace; uživatelé jej nemusí instalovat jako projektový doplněk.
- Level nástroje zahrnují tvorbu FPS uzlů, editaci NPC tras a waypoint akcí, práci s NavMesh a spuštění hry v Level viewportu nebo samostatném okně.
- Model workspace obsahuje náhled LOD a ukládá rozpracovaná nastavení do `.lodcfg`. Hitboxy ukládá vedle zdrojového modelu jako `.hitboxcfg`; při Apply Changes přidá do importované PackedScene kostmi propojené boxové `Area3D` s metadaty hitgroup. Podrobnosti jsou v `docs/model_editor_lod.md`.
- Bake NavigationMesh zarovnává výšku agenta a maximální stoupání k voxelovým krokům. Kruhový NavigationObstacle3D používá poloměr pro půdorys a nastavenou výšku směrem vzhůru od pozice uzlu.

## Známá omezení a otevřené zprávy

- Generování vrcholů NavigationObstacle3D z modelu zatím není implementováno. Bake používá konvexní obrys překážky; jeden obrys nedokáže popsat otvor nebo vnitřní dvůr.
- Uživatel dříve nahlásil možné problémy s materiály `.tres` při náhledu/vložení modelu, obnovováním Content Browseru po uložení modelu a UniBuilder exportem GLB s prázdným bufferem. Oprava těchto hlášení není v tomto repozitáři potvrzena; před změnou je reprodukuj v příslušném herním projektu.
- Vizuální chování editoru považuj za ověřené až po potvrzení uživatelem; editor neotevírej bez výslovné žádosti.

## Důležitá místa

- `engine/editor/fps_studio/` — pracovní prostředí a vestavěný UniBuilder.
- `engine/editor/import/3d/scene_import_settings.cpp` — náhled a nastavení importu modelů včetně LOD.
- `engine/modules/navigation_3d/` a `engine/scene/3d/navigation/` — NavigationMesh a NavigationObstacle3D.
- `tools/build_macos.sh` — build, balení aplikace a podpis.
- Aktuální dokumentace: `README.md`, `docs/architecture.md` a `docs/model_editor_lod.md`.
