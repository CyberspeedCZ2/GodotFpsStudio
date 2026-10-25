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

10. **Fyzikální odtlačení lehkých a středních beden — Fáze 2 (Solo Push) — [HOTOVO]**:
    - **Fyzikální tlačení překážek (`_handle_physics_push` / `_push_light_debris`)**:
      - Rozšíření parametrů NPC: `can_push_obstacles = true`, `obstacle_push_mass_limit = 65.0 kg` (zahrnuje 45kg těžké bedny), `obstacle_push_force = 95.0 N`, `obstacle_push_duration_max = 2.5 s`.
      - Rozlišení lehké suti (`body.mass <= 5.0 kg` -> okamžitý impuls) a těžších beden (`body.mass <= 65.0 kg` -> souvislá tlačná síla `apply_central_force`).
      - Automatické probuzení uspaných fyzikálních těles (`body.sleeping = false`).
    - **Časový limit tlačení & automatický přechod do objížďky**:
      - Pokud se bedna nepohne do 2.5 sekund (např. je vzpříčená o zeď nebo zablokovaná jinou těžkou překážkou), NPC pokus o tlačení ukončí a automaticky přejde do `STUCK_RECOVERY` a `DETOUR` pro obejití závalu.
    - **Vizuální indikátor**:
      - Během tlačení 3D mrkvička / stavový label svítí oranžově s textem `📦 Pushing Obstacle`.
    - Sestaveno, podepsáno, otestováno v headless režimu. Commitnuto a pushnuto na GitHub (`main`).

11. **Reverzní patrola při trvalém závalu — Fáze 3 (Fallback Reverse Patrol) — [HOTOVO]**:
    - **Automatické otočení patroly (`_trigger_fallback_reverse_patrol`)**:
      - Pokud je trasa trvale zablokovaná (`is_target_reachable() == false` nebo selže odtlačení i objížďka), NPC nezůstane bezradně stát na místě.
      - Po 1s vyhodnocení situace NPC zahlásí stav (`Zaval: Cesta zablokovana, obracim patrolu!`), otočí směr patroly (`_npc_path_direction = -_npc_path_direction`) a začne procházet dostupné waypointy v obráceném pořadí.
    - **Cyklické testování uvolnění překážky**:
      - Když NPC při reverzní patrole obejde otevřenou část mapy a přiblíží se zpět k zablokovanému waypointu, automaticky otestuje průchodnost.
      - Pokud hráč mezitím překážku odstranil, NPC plynule naváže na původní plnou trasu. Pokud zával trvá, otočí se a pokračuje v patrole po bezpečné zóně.
    - **Vizuální indikátor**:
      - 3D značka mrkvičky v reálném čase zobrazuje `🔄 Fallback Patrol` (fialový indikátor).
    - Sestaveno, podepsáno, otestováno v headless režimu. Commitnuto a pushnuto na GitHub (`main`).

12. **Kooperativní odtlačení těžkých překážek — Fáze 4 (Co-op Push & Companions) — [HOTOVO]**:
    - **Detekce nadlimitních překážek a volání posily (`_request_coop_help`)**:
      - Pokud má překážka hmotnost přesahující limit pro jednoho (`mass > 65 kg`), ale do kooperativního limitu (`mass <= 140 kg`), nebo selže sólo pokus, NPC automaticky vyhledá nejbližšího volného spolubojovníka ze skupiny `"fps_npc"` v okruhu 24 metrů (`coop_call_radius`).
      - Vyvolávající NPC přejde do role `COOP_ROLE_CALLER`, zastaví se a čeká na příchod parťáka (`📢 Waiting for Partner`).
    - **Reakce spolubojovníka (`respond_to_coop_request` a `finish_coop_help`)**:
      - Oslovené NPC přejde do role `COOP_ROLE_HELPER`, uloží si svůj aktuální navigační cíl a sprintem (`run_speed`) přispěchá na pomoc k boku volajícího parťáka (`🏃 Assisting Partner`).
      - Po přiblížení k překážce (< 2 m) se zapojí do tlačení směrem od svého těla k překážce se znásobenou tlačnou silou (`1.5× obstacle_push_force`).
      - Společná tlačná síla obou NPC dosahuje až 3.4násobku základní síly, což spolehlivě uvede do pohybu i masivní 140kg závaly.
    - **Koordinované ukončení a návrat k hlídce**:
      - Jakmile se překážka posune a uvolní průchod, nebo vyprší časový limit kooperace (`coop_push_duration_max = 3.5 s`), vyvolávající NPC automaticky uvědomí pomocníka (`finish_coop_help()`).
      - Pomocník se plynule vrátí na svou původní patrolu a k uloženému waypointu, zatímco primární NPC pokračuje po uvolněné trase.
    - **Vizuální indikátory**:
      - Dynamické 3D stavové popisky: `🤝 Co-op Pushing` (žlutý indikátor), `📢 Waiting for Partner` (oranžový indikátor) a `🏃 Assisting Partner` (azurový indikátor).
    - Sestaveno, podepsáno, otestováno v headless režimu. Commitnuto a pushnuto na GitHub (`main`).

13. **Dvoustupňový prostorový senzor (2-Tier Scout & Detour Probing) — [HOTOVO]**:
    - **Problém**: Plně rozvinuté vyhodnocovací koule (radius 0.42 m) narážely do bočních zdí běžných chodeb, což způsobovalo škubání a falešné hlášení zablokování cesty v rovných úsecích. Zároveň v action_move_to_point.gd časový limit repath_after_seconds (2.5 s) chybně spouštěl reverzní patrolu uprostřed dlouhých přesunů.
    - **1. Stupeň — Dopředná kontrolní hlídka (Scout Probe)**:
      - V klidovém stavu (při volném pohybu v chodbách a místnostech) je aktivní **pouze jediná malá kontrolní koule** přímo vpřed (radius 0.18 m, délka 2.5 m).
      - Boční koule a prstence jsou zcela neaktivní (dormant). NPC prochází chodbami s naprosto hladkou rychlostí a nulovou kolizí se stěnami.
    - **2. Stupeň — Vyhodnocovací koule při překážce (Detour Evaluation Probes)**:
      - Teprve při detekci překážky přímo před NPC (scout_res.safe_distance < 2.0 m) se aktivují prostorové koule o velikosti těla (radius 0.38 m) pro otestování obejití vlevo (+28°) a vpravo (-28°) a průchodnosti úžin na 1.2 m.
      - Pokud se tělo nevejde do mezery mezi bednou a zdí, NPC se vyhne do volného prostoru na opačnou stranu.
    - **Oprava v Behavior Tree (action_move_to_point.gd)**:
      - Odstraněno nechtěné otáčení patroly po 2.5 sekundách. Reverzní patrola se spouští výhradně při skutečné nedosažitelnosti cíle (unreachable).
    - **Dynamická 3D vizualizace**:
      - V klidu se před NPC vznáší pouze malá zelená kontrolní koule (radius 0.18 m).
      - Při detekci překážky koule zčervená a rozsvítí se prstence vyhodnocování (červený pro neprůchozí štěrbinu, zelený pro volnou trasu).
    - Sestaveno, podepsáno, otestováno 5000snímkovou headless simulací. Commitnuto a pushnuto na GitHub (main).

14. **Tří-koulvý prostorový senzor a dynamické založení objízdné trasy (3-Spheres & Route Detour Baking) — [HOTOVO]**:
    - **3-koulvý systém senzorů**:
      - **Hlavní středová koule (Center Scout)**: V klidu (rovné chodby, volné místnosti) je zelená, malá (radius 0.20 m) a kontroluje směr pohybu s lookahead předstihem 3.6 m.
      - **Aktivace pomocných koulí (Left & Right)**: Jakmile hlavní koule detekuje překážku v dráze před cílovým waypointem (< 3.2 m), zčervená a okamžitě aktivuje dvě boční koule (vlevo +38°, vpravo -38°).
      - **Rozpoznání volného prostoru**: Zablokovaná strana (stěna / bedna) se zbarví červeně, volná strana do otevřeného prostoru zeleně.
      - **Natočení a vyčištění výhledu**: NPC se plynule natočí do volného směru (k zelené kouli). Po natočení středová koule míří do volného prostoru, nic nedetekuje a zezelená, pomocné koule se skryjí a NPC pokračuje rovně plnou rychlostí.
      - **Filtrování šikmých ramp**: Šikmé pochozí povrchy a rampy (`normal.y > 0.4`) jsou automaticky filtrovány, takže nezpůsobují falešné objížďky.
    - **Trvalé dynamické založení nové trasy (Route Detour Baking)**:
      - Aby NPC nemuselo překážku při dalších kolech znovu detekovat ani se zastavovat, `_bake_detour_around_obstacle` vypočítá bezpečný průchozí koridor okolo překážky s odstupem 1.8–3.2 m do volného prostoru.
      - Přímo do patrol křivky `Curve3D` v `npc_path.curve` vloží 2 nové navigační body a sesynchronizuje metadata `_fps_npc_point_actions` s akcí `"move"`.
      - NPC se plynule napojí na tyto body a po jejich průjezdu se vrátí na původní trasu. Při všech dalších průchodech patroly NPC automaticky prochází touto novou objízdnou křivkou bez jakéhokoliv zasekávání.
    - Sestaveno, podepsáno, otestováno a ověřeno 5500snímkovou headless simulací. Commitnuto a pushnuto na GitHub (`main`).

15. **Doladění objízdných tras: Model offset 1.0 m, hladké Bézier oblouky, filtrování ramp a odstranění 2s pauzy — [HOTOVO]**:
    - **Odstranění 2s pauz na dlouhých chodbách (`action_move_to_point.gd`)**:
      - **Příčina**: V `ActionMoveToPoint` se zbývající vzdálenost `remaining` měřila k diskrétním rohům NavMeshe (`navigation_path[path_index]`). Při patrolu po `Curve3D` se však NPC naviguje pomocí mrkve, nikoliv po NavMesh rozích, takže `path_index` se neaktualizoval a vzdálenost k rohu po jeho minutí rostla. Po 2.5 sekundách (`repath_after_seconds`) akce vyvolala `npc.stop()` a `_recovery_pause = 0.5 s` (obnova cesty).
      - **Řešení**: Zavedena kontrola `is_advancing`: pokud je NPC v pohybu (`velocity.length() > 0.35 m/s` nebo se otáčí), plynule postupuje a časovač uvíznutí se nuluje (`_elapsed_time = 0.0`). Na rovných úsecích a dlouhých chodbách (WP 18->19 a WP 26->27) NPC sprintuje plynule bez jakéhokoliv zastavování.
    - **Dynamický offset 1.0 m od modelu překážky**:
      - Funkce `_get_obstacle_horizontal_radius(collider)` dynamicky zjistí skutečné rozměry modelu/kolize z `CollisionShape3D` (`BoxShape3D`, `CylinderShape3D`, `SphereShape3D`), `NavigationObstacle3D` nebo `MeshInstance3D` AABB.
      - Výsledný odstup `desired_clearance = obs_radius + 1.0 m` (namísto dřívějších přehnaných 3.2 m) vede trasu přirozeně a těsně okolo bedny s čistým 1m odstupem.
      - Inteligentní detekce prostoru: pokud je mezi bednou a stěnou méně než `obs_radius + 0.75 m`, algoritmus automaticky zvolí druhou stranu do volného prostoru místnosti.
    - **Hladké kubické Bézier oblouky (odstranění kostrbatosti)**:
      - Detour body `pt_a` a `pt_b` již nejsou vkládány s nulovými tečnami (`Vector3.ZERO`).
      - Do `Curve3D` se počítají hladké C1-spojité kubické Bézier tečny (`tangent_a` a `tangent_b`), které v patrol křivce vytvářejí elegantní plynulý oblouk okolo překážky.
    - **Striktní filtrování architektury levelu a šikmých ramp**:
      - `_is_valid_detour_obstacle(collider)` povoluje zakládání tras výhradně pro skutečné překážky (`RigidBody3D`, skupina `fps_heavy_obstacle` / `fps_obstacle`, vrstva 2 `Heavy Obstacle` nebo uzel s `NavigationObstacle3D`).
      - Statická architektura levelu (stěny, dveře, šikmé rampy UniBuilderu a pochozí povrchy na NavMeshi) je v `_cast_sphere_probe` detekována jako pochozí terén a nikdy nespouští falešné objízdné body na hranách ramp.
    - Sestaveno, otestováno 5500snímkovou headless simulací bez jediné chyby. Commitnuto a pushnuto na GitHub (`main`).

16. **Adaptivní variabilní offset, odstranění zablokovaných waypointů a NavMesh String-Pulling (Zero-Loop Funnel Bypass) — [HOTOVO]**:
    - **Analýza chování v herních enginech (UE5, Source Engine / Valve, Unity)**:
      - **Unreal Engine 5 (Recast & Detour)**: Využívá Funnel algoritmus (string-pulling) přes koridory NavMesh polygonů. Polygony jsou automaticky odsazeny od zdí o poloměr agenta (`AgentRadius`) a dynamické překážky vyřezávají navmesh (`carve`). Při výpočtu obchvatu Funnel napne „provázek“ od vstupu k výstupu – pokud existuje přímá viditelnost, eliminuje všechny mezilehlé vrcholy a spojí trasu rovně („na přímo rovně“). Pokud překážky tvoří shluk (např. 2 bedny u sebe), Recast je sloučí do jediného souvislého ostrova, kolem kterého vede jediný hladký obchvat bez vnitřních kliček.
      - **Valve Source Engine (Half-Life 2 / `CNavMesh`)**: AI při zablokování úseku patrol trasy (`path_track` / `path_corner`) physics bednami provede „Corridor Bypassing“: najde poslední čistý uzel před závalem a první čistý uzel za ním. Všechny zablokované uzly uvnitř závalu jsou z trasy odstraněny / přeskočeny.
    - **Odstranění zablokovaných bodů z trasy („Smazat cestu“)**:
      - **Příčina smyček a uzlů**: Starý kód pouze vložil 2 body před překážku, ale ponechal původní waypointy, které ležely mezi bednami nebo přímo v nich. Trasa se pak z objízdného bodu vracela zpět mezi bedny a u druhé bedny vytvořila další objížďku, což vytvořilo křížící se smyčky ve tvaru osmičky (viditelné na screenech uživatele). Při návratu v reverzní patrole NPC v tomto uzlu rotovalo na místě.
      - **Řešení**: Implementována funkce `_collect_obstacle_cluster`, která detekuje celý shluk překážek do 4.5 m (obě bedny). Funkce `_find_clean_exit_waypoint` vyhledá první skutečně bezpečný a volný waypoint za celým shlukem. Všechny staré zablokované body mezi vstupem a výstupem jsou z `Curve3D` i metadat `_fps_npc_point_actions` kompletně smazány (`remove_point`).
    - **Adaptivní variabilní offset (Ochrana proti vkládání bodů do zdi)**:
      - **Příčina bodů ve zdi**: Fixní matematický offset (`obs_radius + 1.0 m`) v úzkých chodbách přesahoval dostupnou šířku a umístil waypoint do zdi.
      - **Řešení**: `_calculate_variable_clearance_point` změří skutečný odstup k protější zdi prostorovým paprskem (`Layer 1 World`). Pokud je chodba užší než 2.2 m, bod je přesně vycentrován do středu volného prostoru (`clampf(total_corridor * 0.5, ...)`). Pozice je navíc zarovnána na legální NavMesh polygon pomocí `NavigationServer3D.map_get_closest_point`. Bod nikdy nemůže skončit ve zdi!
    - **String-Pulling a spojení přímo rovně („Na přímo rovně“)**:
      - Pomocí `NavigationServer3D.map_get_path(nav_map, entry_pos, exit_pos, true)` se spustí Funnel algoritmus.
      - Pokud je průchod volný nebo existuje přímá linie, server vrátí pouze start a cíl (`size <= 2`). Trasa je spojena přímo rovně v jediné čisté přímce bez zbytečných bodů.
      - Pokud je nutné bedny obejít, vloží se přesné lomové body NavMeshe (typicky 1–2).
    - **Symetrické Bézier tečny (Zaručený reverzní návrat bez točení ve smyčce)**:
      - Každý vložený bod má striktně symetrické $C^1$ tečny: `t_out = chord_dir * handle_len` a `t_in = -t_out`.
      - Křivka je striktně planární a monotónní, bez jakýchkoliv kliček, ostrých záhybů či překřížení.
      - Při otočení patroly (`_npc_path_direction = -1`) NPC prochází tou samou hladkou křivkou zpět bez jakéhokoliv zasekávání nebo točení na místě.
    - Sestaveno, podepsáno do `Godot FPS Studio.app`, ověřeno 6500snímkovou headless simulací v `AI_Beehave_Test_Level.tscn` (průchod tam i zpět přes upravenou trasu proběhl naprosto hladce). Commitnuto a pushnuto na GitHub (`main`).

