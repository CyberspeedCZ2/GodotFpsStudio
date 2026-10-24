# AGENTS.md — Godot FPS Studio

Tento soubor slouží jako primární instrukce a kontext pro asistenta Antigravity / Gemini při zahájení jakékoliv nové konverzace v tomto projektu.

---

## 1. O projektu a identita
- **Projekt**: **Godot FPS Studio**
- **Základ**: Samostatný fork **Godotu 4.7.2-stable** zaměřený na FPS hry (inspirované Half-Life, Valve Hammer workflow a moderním UE5 rozhraním).
- **Značka**: Aplikace se jmenuje Godot FPS Studio, zachovává Godot logo, systémová složka nastavení je `godotfpsstudio`.
- **Cílová platforma editoru**: macOS Apple Silicon (`arm64`). Exportované hry cílí na macOS, Windows a Linux.
- **GitHub repozitář**: `git@github.com:CyberspeedCZ2/GodotFpsStudio.git` (hlavní větev `main`, záchytný tag `v4.7.2-fps-studio-stable`).

---

## 2. Zásadní pravidla a omezení pro asistenta (STRICT RULES)
1. **EDITOR NEOTVÍREJ**: Editor (`Godot FPS Studio.app`) **nikdy nespouštěj**, pokud o to uživatel výslovně a přímo nepožádá. Všechny kontroly prováděj staticky, přes git diff, syntax checks nebo headless nástroje.
2. **ŽÁDNÉ ZIP ARCHIVY**: Nevytvářej distribuční `.zip` archivy. Sestavená aplikace se instaluje a podepisuje přímo v kořeni projektu jako `Godot FPS Studio.app`.
3. **STRUKTURA REPOZITÁŘE**:
   - Kořen projektu je hlavní Git repozitář.
   - `engine/`: Kompletní zdrojový C++ kód forku Godotu.
   - `tools/build_macos.sh`: Výchozí skript pro sestavení, balení a ad-hoc podpis aplikace pro Apple Silicon.
   - `docs/`: Architektura a specifikace.
   - `.gitignore` filtruje sestavené `.app` balíčky (i zálohy), mezipaměti a výstupy kompilace (`engine/bin`, `.sconsign` atd.).

---

## 3. Co je hotové a implementované v C++ forku

### A. Pracovní prostředí (UE5-style)
- `engine/editor/fps_studio/fps_studio_workspace.h/.cpp`:
  - Extrahovaná vrstva pracovního prostředí z `EditorNode`.
  - Středový 3D viewport, vpravo nahoře World Outliner, vpravo dole Details, dole Content Browser.
  - Pravý vertikální dock tab rail s perzistencí šířek (`dock_panel_width_1..4`).
- **Play in Level View (Embedded Play)**: Hra běží přímo v Level viewportu; boční doky a pravý rail se automaticky složí a po vypnutí obnoví. Tmavý Game Output overlay panel.
- **Měření 3D FPS a RAM**: Samostatné počítadlo vyrenderovaných snímků 3D viewportu v reálném čase přes `RenderingServer` se zobrazením ve status baru.

### B. Viewport & Gameplay authoring nástroje
- **RMB kontextové menu**: Rychlý klik otevře FPS menu, podržení/tažení zapne freelook kameru.
- **FPS Gameplay prvky**: Player Start, Checkpoint, NPC Spawn, NPC Waypoint, NPC Action Point, NPC Cover Point, Objective Marker, Interaction Area, Navigation Region, Navigation Obstacle, Navigation Link.
- **Vysoce kontrastní gizmo ikony**: Vlastní SVG gizma pro gameplay uzly čitelná i skrz modely levelu (`GizmoTrigger.svg`, `FPSPlayerStart`, `FPSCheckpoint`, atd.).
- **UniBuilder CSG/brush modeling**: Integrovaný plný balíček (`DEMO = false`) bez demo limitů, interaktivní click-by-click vkládání z viewport menu.
- **Editor tras pro NPC (`Path3D` / NPC Route)**:
  - C++ vrstva v `editor/scene/3d/path_3d_editor_plugin.cpp` a `npc_route_geometry.h`.
  - Podpora akcí na waypointech (Move, Wait, Look At, Animation, Interact, Attack, Cover).
  - Vykreslování koridoru i při odznačení cesty.
  - Živá kontrola propojení s NavMesh (`NavigationServer3D`) — zelená průchozí, červená blokovaná.

---

## 4. Aktuální úkoly a specifikace (Current Focus)

Uživatel zadal následující požadavky na waypoint systém a chování NPC:

1. **Akce `Run` (Běh)** — [HOTOVO]:
   - Do seznamu akcí waypointů v `Path3DEditorPlugin` doplněna akce pro běh/sprint (`Run`).
   - Přidána barevná paleta pro `Run` (oranžová `Color(1.0, 0.45, 0.10)`).
2. **Ikony akcí na waypointech ve 3D**: — [HOTOVO]:
   - Vytvořeno 7 vysoce kontrastních SVG ikon v `engine/editor/icons/FPSRouteAction*.svg`.
   - Vykreslování billboard quadů s vypnutým depth testem a maximální prioritou přímo u bodů trasy ve 3D viewportu s vertikálním offsetem a vodicí stopkou k bodu.
   - Sestaveno, podepsáno a otestováno v `Godot FPS Studio.app`. Commitnuto a pushnuto na GitHub (`main`).

3. **Inteligentní vyhýbání překážkám (Avoidance & Physics filtering) — [NÁSLEDUJÍCÍ KROK / FÁZE 2]**:
   - NPC se musí dokázat dynamicky vyhnout velkým předmětům na zemi (bedny, sudy) pomocí navigace / dynamic obstacles (RVO avoidance).
   - Malé předměty (kelímek, drobný nepořádek) nesmí NPC zastavit: NPC fyzika musí malé předměty ignorovat (pomocí oddělených kolizních vrstev a masek nebo váhového prahu / push síly), takže NPC do nich nenaráží jako do zdi.
