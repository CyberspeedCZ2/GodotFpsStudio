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

3. **Inteligentní vyhýbání překážkám (Avoidance & Physics filtering) — [HOTOVO]**:
   - **Nativní vrstvy**: V `register_scene_types.cpp` nastaveny výchozí názvy 3D fyzikálních vrstev (Layer 1: World, Layer 2: Heavy Obstacle, Layer 3: Debris, Layer 4: Player, Layer 5: NPC, Layer 6: Hitbox, Layer 7: Interactable) a Avoidance vrstev (Layer 1: Obstacles, Layer 2: NPCs, Layer 3: Player).
   - **Gameplay prvky a RMB menu**:
     - Do Level Viewport RMB menu (`Props and Obstacles`) přidána tvorba `Heavy Obstacle (Avoidance Crate)` na Layer 2 s připojeným `NavigationObstacle3D` (RVO avoidance enabled, radius 0.75m) a `Small Debris (Cup/Can)` na Layer 3 (Debris, mass 0.2kg, bez avoidance).
     - Do `NPCs and Routes` přidána tvorba `NPC Character (with Avoidance)` s `NavigationAgent3D` (avoidance enabled, radius 0.45m), `collision_layer = 16` (Layer 5: NPC) a `collision_mask = 27` (Layers 1, 2, 4, 5 — Layer 3 Debris je vyloučena, takže NPC nezasekává).
   - **NPC Controller Script**:
     - Do Content Browseru (`FPS +`) přidána rychlá akce `NPC Controller Script...`.
     - Vytvořena C++ vestavěná GDScript šablona `fps_npc_controller.gd` v `engine/modules/gdscript/editor/script_templates/CharacterBody3D/` s plnou podporou RVO avoidance (`NavigationAgent3D.velocity_computed`), jemným odtlačováním drobných fyzikálních těles (`apply_central_impulse` pro objekty s hmotností pod limit) a následováním patrol trasy s prováděním akcí včetně sprintu (`run`), čekání (`wait`) a otáčení (`look_at`).
   - Sestaveno, podepsáno a otestováno v `Godot FPS Studio.app`. Commitnuto a pushnuto na GitHub (`main`).

4. **Hladké 2-bodové Bézier oblouky a Lookahead („mrkev na prutu“) — [HOTOVO]**:
   - **C++ zaoblení rohů (`_round_route_corner`)**:
     - Namísto shluku 5–8 diskrétních lineárních bodů nahrazuje tlačítko `Round` ostrý roh přesně **2 body** (vstup a výstup oblouku) s optimálními kubickými Bézier tečnami (`point_out` a `point_in`), které v `Curve3D` vytváří hladký kruhový oblouk.
     - Kontrola průchodnosti NavMesh ověřuje vzorkování podél celé Bézier křivky.
   - **Lookahead sledování křivky (Mrkev na prutu)**:
     - V `npc_controller.gd` i šabloně `fps_npc_controller.gd` zavedeno Lookahead vzorkování křivky (`sample_baked` s předstihem 1.4 m).
     - Průchozí body (`move`, `run`, oblouky zatáček) nezpůsobují zastavení ani kolísání rychlosti; NPC projíždí zatáčky v plné rychlosti.
     - Zastavovací body (`wait`, `look_at`, konec trasy) zachovávají přesné okamžité zastavení přímo na bodě.
   - **Omezení předsunutí mrkve (Carrot clamp) a detekce postupu podél Curve3D**:
     - Mrkev je striktně omezena na `target_offset + 0.6 m` pro průchozí body a přesně `target_offset` pro koncové body, takže nemůže utéct dopředu po trase.
     - V `is_at_destination()` přidána kontrola postupu podél křivky (`progress_past`), takže minití bodu při projíždění oblouku (např. kvůli RVO avoidance) nezasekne waypoint cíl a NPC plynule přechází mezi body.
   - Sestaveno, podepsáno a otestováno v `Godot FPS Studio.app`. Commitnuto a pushnuto na GitHub (`main`).

5. **Plynulé zpomalování před cílem (Deceleration profiling) a perzistence běhu (`Run`) — [HOTOVO]**:
   - **Plynulé zpomalování (Deceleration)**:
     - Namísto okamžitého brzdění na 20–30 cm před cílem byl implementován přirozený brzdný profil na vzdálenost 1.6–4.2 m (podle aktuální rychlosti) využívající `smoothstep` křivku.
     - NPC při přiblížení k zastavovacímu bodu (`wait`, `look_at`, konec trasy) plynule zpomalí z plného sprintu/chůze na dojezdovou rychlost a hladce zastaví přímo na bodě bez trhání a klouzání.
     - Průchozí body (`move`, `run`, Bézier zaoblení) si zachovávají plnou rychlost.
   - **Perzistence běhu (`Run` vs `Move`)**:
     - Nastavení akce `Run` na waypointu nyní aktivuje stav `_active_route_locomotion = LOCOMOTION_RUN`, který zůstává aktivní i při přechodu na akční body (`wait`, `look_at`). NPC tedy sprintuje po celém úseku až k cílovému čekacímu bodu.
     - Akce `Move` přepíná zpět na standardní chůzi.
     - Nastavení `locomotion_style = Run` na samotném NPC vynucuje běh po celé trase.
     - `nav_agent.max_speed` automaticky škáluje s `run_speed * 1.5`, aby RVO avoidance neomezoval rychlost běhu.
   - Sestaveno, otestováno a ověřeno headless simulací. Commitnuto a pushnuto na GitHub (`main`).

6. **Spolehlivé obcházení pevných a dynamických překážek (Obstacle Avoidance & Probing) — [HOTOVO]**:
   - **Problém**: RVO simulace s pouhým `radius = 0.75` dělila vyhýbání 50/50 mezi agenty stejné priority; nehybná bedna neuhýbala a NPC se odklánělo jen napůl, což vedlo k zachytávání o roh a zaseknutí do fyziky. Na trase navíc bod křivky za bednou táhl vektor rychlosti v každém snímku zpět přímo do překážky.
   - **Těžké překážky (`HeavyObstacle`)**:
     - V `scene_tree_dock.cpp` a scéně doplněn polygonální obrys (`vertices`) se 4 body vymezujícími čtvercový koridor (1.3 × 1.3 m) s counter-clockwise vinutím, který v RVO vytváří tvrdou statickou bariéru vytlačující agenty ven.
     - `radius` zvětšen na 1.0 m a `height` na 1.2 m.
   - **Parametry agenta (`NavigationAgent3D`)**:
     - Nastaveno `avoidance_priority = 0.5` (překážka má 1.0, agent tedy přebírá 100% odpovědnost za vybočení), `time_horizon_agents = 1.5 s` a `time_horizon_obstacles = 1.5 s` (včasné vnímání překážek s předstihem).
   - **Aktivní detekce a vybočení (Obstacle Probing Whiskers)**:
     - Do šablony `fps_npc_controller.gd` i projektového `npc_controller.gd` implementována funkce `_probe_obstacle_avoidance()` s 3 prostorovými paprsky (střed, vlevo -28°, vpravo +28° na Layer 2 Heavy Obstacles s délkou až 2.4 m).
     - Při detekci překážky se požadovaný směr `desired_velocity` plynule stočí do volného koridoru vedle překážky, takže RVO server rychlost neudusí, ale agent plynule v plné rychlosti bednu v oblouku obejde a za ní se zařadí zpět na trasu.
     - Rotace NPC se při aktivním vyhýbání hladce orientuje podle `safe_velocity` reálného pohybu.
   - Sestaveno, podepsáno, ověřeno headless kontrolou a simulací v `project-hl-2-inspiration`. Commitnuto a pushnuto na GitHub (`main`).

