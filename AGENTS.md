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
   - **Aktivní detekce a vybočení (Obstacle Probing Whiskers & Sticky Avoidance)**:
     - **Odstranění nestability a kmitání (Sticky Avoidance)**: Čelní náraz do rohu bedny dříve způsoboval kmitání směru vybočení zleva doprava (skok o 110°), což spouštělo `_turning_in_place = true`. Při něm navíc NavigationServer bez posílání nové rychlosti opakoval starou `safe_velocity`, takže agent byl nekontrolovaně unášen pryč ze scény.
     - **Paměť strany obcházení (`_avoidance_side`)**: Jakmile NPC začne překážku obcházet vpravo (-1.0) nebo vlevo (+1.0), drží tuto stranu po celou dobu, dokud jsou paprsky zablokované.
     - **Ohraničení úhlu (Bounded Steer Angle)**: Úhel vybočení je striktně omezen na 18°–40° a plynule dotažen (`move_toward`), takže nikdy nepřekročí práh pro otáčení na místě a NPC neztrácí dopřednou rychlost ani se neotáčí za záda.
     - **Vícebodové prostorové paprsky**: Centrální paprsek (2.6 m), 2 rovnoběžné paprsky na šířku těla (0.7 m koridor) a 2 úhlové vousy (30° out).
     - **Ochrana proti zombie driftu**: V `_turning_in_place` i v `_on_safe_velocity_computed` se striktně vynucuje `velocity = Vector3.ZERO` a do RVO serveru se odesílá nulová rychlost.
   - Sestaveno, podepsáno, ověřeno 30s headless simulací v `project-hl-2-inspiration` (NPC hladce obešlo bednu s odstupem 1.4 m a pokračovalo až na konec trasy). Commitnuto a pushnuto na GitHub (`main`).

7. **Živá 3D vizualizace chování NPC (Debug Visuals: Paprsky, Mrkev, Vektor řízení) — [HOTOVO]**:
   - **3D model mrkvičky na prutu (`Carrot on a stick`)**:
     - Dynamicky vytvořený 3D objekt `AICarrotMarker` (kónické oranžové tělo mrkve se zelenou natí a 3D štítkem `🥕 Carrot Target`), který se v reálném čase pohybuje po křivce trasy před NPC na vypočteném lookahead offsetu, vznáší se a rotuje.
     - Z hrudi NPC k mrkvi vede zářivě oranžovo-žlutá vodicí linka („vlasec“).
   - **Prostorové paprsky senzorů (`ImmediateMesh` s `no_depth_test`)**:
     - Zelené čáry pro volné koridory (centrální paprsek, levý a pravý pruh šířky těla, 30° úhlové vousy).
     - Jasně červené čáry při detekci překážky s červeným 3D křížkem přímo v bodě dopadu paprsku na těleso překážky.
     - Azurová šipka znázorňující vypočtený vektor řízení a vybočení.
   - **Integrace s Godmode a Dev režimem**:
     - Automatická aktivace, jakmile má hráč zapnutý Godmode (`F1` / `is_godmode`).
     - Rychlé manuální přepínání klávesou `F3` (`[F3] AI Paprsky & Mrkev`) se stavovou notifikací v HUD overlayi.
     - Export proměnná `@export var debug_visuals: bool = false` na NPC pro zapnutí přímo v inspektoru.
   - Sestaveno, podepsáno, otestováno v headless režimu. Commitnuto a pushnuto na GitHub (`main`).

8. **Zobrazení trasy a NavMesh cesty NPC (Route Curve & NavMesh Path Visuals) — [HOTOVO]**:
   - **Křivka patrol trasy (`Curve3D` / Path3D)**:
     - Vzorkování křivky trasy po 0.35 m s vykreslením plynulé azurové linie podél celé trasy (`show_route_curve = true`).
     - 8hranné kroužky a vertikální piny na všech bodech waypointů.
     - Barevné odlišení akcí: oranžová pro `Run`, zelená pro `Wait`, fialová pro `Look At`, světle modrá pro `Move`.
     - Zvýraznění aktivního cílového waypointu: zářivě žlutý zvětšený kroužek s vysokým 3D pinem.
   - **Živá NavMesh trajektorie (`NavigationAgent3D.get_current_navigation_path()`)**:
     - Žlutá vodicí čára spojující NPC s jednotlivými uzly NavMeshe v reálném čase (`show_nav_path = true`).
     - Na každém lomovém bodě NavMeshe se vykresluje žlutý 3D diamant.
     - Při vyhýbání se překážce hráč přesně vidí, kudy NavMesh a RVO algoritmus agenta směrují.
   - **Integrace a zobrazení**:
     - Vše je součástí debug vrstvy (`debug_visuals`, klávesa `F3` nebo Godmode `F1`).
     - Všechny linie mají `no_depth_test = true` a jemný výškový offset nad podlahou proti Z-fightingu, takže jsou plně čitelné i přes modely a zdi.
   - Sestaveno, podepsáno, otestováno v headless režimu. Commitnuto a pushnuto na GitHub (`main`).

9. **Inteligentní obcházení překážek a NavMesh vyřezávání — Fáze 1 (Dynamic Carving & Detour State) — [HOTOVO]**:
   - **Dynamické vyřezávání NavMeshe (`affect_navigation_mesh` a `carve_navigation_mesh`)**:
     - V `scene_tree_dock.cpp` i v testovací scéně u `NavigationObstacle3D` (Heavy Obstacle) zapnuto vyřezávání NavMeshe. Překážky v reálném čase přerušují průchodnost NavMeshe pod sebou.
     - Detekční paprsky na NPC (`_probe_obstacle_avoidance`) nyní kontrolují i Layer 1 (World), takže senzory vnímají pevné zdi i těžké překážky.
   - **Stavový automat navigace a detekce uvíznutí**:
     - Detekce uvíznutí (`_stuck_timer` > 0.6 s nebo čelní blokáda všech paprsků):
     - **`STUCK_RECOVERY`**: Krátký úkrok vzad (0.55 s) pro uvolnění tření o geometrii rohu/bedny.
     - **`DETOUR`**: Dočasné opuštění křivky `Curve3D` a přechod na čistou NavMesh navigaci (`nav_agent.get_next_path_position()`) k cílovému waypointu okolo překážky/zdi.
     - **Zpětné napojení**: Jakmile se NPC dostane zpět do blízkosti křivky trasy (< 0.85 m) a čelní koridor je volný, plynule naváže zpět na patrol křivku.
     - **`BLOCKED`**: Pokud je cíl zcela nedostupný (`is_target_reachable() == false`), NPC plynule zabrzdí a čeká bez kmitání a tření.
   - **Vizuální zpětná vazba**:
     - Značka mrkve v reálném čase indikuje navigační stav: `🥕 Carrot Target` (Patrola), `↩️ Backtracking` (Vycouvání), `↪️ Detour (NavMesh)` (Objížďka po NavMeshi), `🛑 Path Blocked` (Zablokováno).
   - Sestaveno, podepsáno, otestováno v headless režimu. Commitnuto a pushnuto na GitHub (`main`).
