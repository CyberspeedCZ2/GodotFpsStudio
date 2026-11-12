# Architektura Godot FPS Studio

## Hranice projektu

- `engine/` je kompletní fork Godotu 4.7.2. Obsahuje engine, editorové funkce FPS Studio i upstream moduly, testy a knihovny třetích stran.
- `engine/editor/fps_studio/` obsahuje vlastní pracovní prostředí FPS Studio a zdroj UniBuilderu.
- Základní editorové služby, například správa scén, import, Undo/Redo a ukládání rozložení, zůstávají součástí Godot editoru.
- `tools/build_macos.sh` sestavuje aplikaci pro macOS arm64. `tools/pack_fps_studio_unibuilder.gd` připravuje UniBuilder jako vestavěný PCK balíček.
- `docs/` popisuje aktuální strukturu a konkrétní funkce.

V repozitáři momentálně není samostatná herní šablona ani ukázkový FPS projekt.

## Pracovní prostředí

FPS Studio nabízí obrazovky Level, Model, Code, Assets a Particle. Vlastní třída `FPSStudioWorkspace` skládá hlavní rozložení, pracovní plochy a docky. Stále ji vytváří a propojuje Godotova třída `EditorNode`; editor tedy používá nativní služby Godotu a není přepsaný od nuly.

Content Browser vychází z vestavěného filesystem panelu. UniBuilder je součástí sestavené aplikace, takže jeho funkce nejsou závislé na doplňku v projektu uživatele.

## Současné oblasti funkcí

- **Level:** úpravy scény a viewportu, vytváření FPS uzlů, NPC tras a waypoint akcí, práce s NavigationRegion3D, NavigationObstacle3D a NavigationLink3D.
- **Play:** spuštění hry v Level viewportu nebo v samostatném okně.
- **Model:** náhled importovaných modelů, LOD nastavení a jejich průběžné ukládání do souboru `.lodcfg`. Podrobnosti jsou v [Model Editor: LOD a náhled modelu](model_editor_lod.md).
- **Particle:** samostatná pracovní obrazovka pro náhled `GPUParticles3D` a `CPUParticles3D`, přehrávání simulace a úpravu nativních uzlů a materiálů. Aktuální rozsah popisuje [Particle workspace](particle_editor.md).
- **UniBuilder:** vestavěné nástroje pro tvorbu a úpravu brush/CSG geometrie.

## Směr dalšího vývoje

Další změny mají postupně oddělovat vlastní pracovní prostředí od `EditorNode`, ale zachovat zavedené engine služby. Herní logika zůstává v projektech uživatelů; nativní engine rozšíření patří do forku jen tehdy, když mají být dostupná napříč projekty.

Před odstraněním upstream složek z `engine/` je nutné ověřit jejich vazby na sestavení enginu. To, že modul není používán konkrétní FPS scénou, samo o sobě neznamená, že je bezpečné ho z forku odstranit.
