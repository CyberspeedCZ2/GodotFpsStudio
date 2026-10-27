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

17. **Zrakový kužel NPC (Vision Frustum), pohled z očí [F4] a spolehlivý obousměrný obchvat překážek (Return Patrol Detour & Vision) — [HOTOVO]**:
    - **Diagnostika zasekávání při návratu (Return Patrol Freeze)**:
      - **Příčina 1 (Předčasný surrender v úzkých chodbách)**: V `_probe_obstacle_avoidance` při čelní detekci překážky (`HeavyObstacle`) v chodbě o šířce ~2.5 m levá i pravá vyhodnocovací koule ($\pm 38^\circ$) narážely do bočních zdí chodby (`safe_distance < 1.6 m`). Kód vyhodnotil situaci jako úplný slepý zával (`else: _forward_blocked = true; return base_dir`) a okamžitě vyskočil před voláním `_bake_detour_around_obstacle`. NPC se tak zaseklo čelem do bedny v nekonečné oscilaci vycouvání a nárazu.
      - **Příčina 2 (RigidBody3D v rovné NavMesh chodbě)**: Statický NavMesh v rovné chodbě obsahuje pouze rohy dveřních zárubní, ale neví o fyzikálních `RigidBody3D` bednách. Přímé spojení mezi waypointy proto protínalo model bedny.
    - **AI Vision Probing Fan a porovnávání hloubky koridoru**:
      - Dvě fixní boční koule nahrazeny zrakovým vějířem úhlů (vnitřní $\pm 18^\circ$ a vnější $\pm 36^\circ$).
      - Algoritmus měří skutečnou průchozí hloubku (`left_clearance` vs `right_clearance`). I v úzké chodbě automaticky vybere stranu s větším volným prostorem a nikdy se nevzdává se `_forward_blocked = true`, pokud je na jedné straně koridoru volno.
      - Adaptivní úhel vybočení je dynamicky omezen na $18^\circ$–$38^\circ$ podle vzdálenosti, takže NPC nebourá do bočních zdí.
    - **Segment-by-Segment Obstacle Verification (Garantovaný obchvat)**:
      - Zavedena geometrická kontrola úseček `_does_segment_intersect_cluster(p_from, p_to, cluster, clearance)` počítající přesnou vzdálenost od středu překážky k trajektorii.
      - Pokud jakýkoliv úsek trasy (i v rovném NavMesh koridoru) protíná těleso bedny, algoritmus automaticky vygeneruje a vloží 3 hladké bypass body (`pre_pos`, `cleared_side` s variabilním odstupem od zdi a `post_pos`).
      - Body jsou řazeny striktně ve vzestupném indexovém pořadí, takže trasa je matematicky konzistentní a hladká v obou směrech (tam i zpět).
    - **Zrakový kužel NPC (3D Vision Frustum Wireframe)**:
      - Export proměnné na NPC: `show_vision_frustum = true`, `vision_fov_horizontal = 75.0°`, `vision_fov_vertical = 40.0°`, `vision_range = 4.8 m`.
      - V `_draw_debug_vision_frustum` se v reálném čase před očima NPC vykresluje 3D perspektivní jehlan: 4 zorné hrany od očí k rohům, obdélníkový rámeček vzdálené roviny, středový osový paprsek, záměrný kříž (reticle) a vějíř 4 vnitřních zrakových paprsků.
      - Barevná indikace zraku: Azurová = volný výhled, Zlatožlutá / oranžová = aktivní vybočení / objížďka, Červená = zablokovaný výhled.
    - **Pohled z očí NPC AI ([F4] NPC Zrak)**:
      - Na NPC vytvořena `VisionCamera` (`Camera3D`) umístěná přímo v očích NPC (`origin = (0, 1.45, -0.38)`).
      - V `PlayerDevTools` implementována metoda `toggle_npc_camera()` a v `FPS_character_script.gd` namapována klávesa `[F4]`.
      - Stisknutím `[F4]` se kamera hráče okamžitě přepne do očí NPC AI s notifikací v HUD overlayi (`[F4] NPC Zrak`). Dalším stiskem se kamera plynule vrátí k hráči.
    - Sestaveno a podepsáno do `Godot FPS Studio.app`. Ověřeno 14000snímkovou headless simulací v `AI_Beehave_Test_Level.tscn` (NPC provedlo kompletní okruh tam, bez jediného zaváhání obešlo bednu na zpáteční cestě a pokračovalo do dalšího kola). Commitnuto a pushnuto na GitHub (`main`).

18. **Zastavení, rozhlížení se (Look-Around Scanning) a Beehave integrace při přemístění překážek — [HOTOVO]**:
    - **Diagnostika problému (Zaseknutí po posunutí beden)**:
      - **Příčina 1 (Trvalé označení v `_baked_detour_obstacles`)**: Jakmile byla pro bednu v určité pozici dříve napočtena objížďka, její ID bylo uloženo v poli `_baked_detour_obstacles`. Pokud hráč nebo fyzika bednu v levelu posunul (např. přímo do trasy ke zdi), kód považoval cluster za již zpracovaný (`already_baked = true`) a novou objížďku nikdy nezaložil.
      - **Příčina 2 (Beehave ActionMoveToPoint přepisoval cíl)**: V `ActionMoveToPoint` proměnná `target` držela starý cíl z blackboardu. Při uvíznutí po 2.5 s (`repath_after_seconds`) akce pouze na 0.5 s zastavila a pak vynutila původní zablokovaný cíl, čímž přepsala dynamický cíl objížďky.
    - **Sledování pozic překážek a automatická invalidace**:
      - V `_baked_obstacle_positions` se ukládá reálná 3D pozice každé zpracované překážky.
      - Pokud se jakákoliv překážka v clusteru pohne o $> 0.35\text{ m}$ (nebo je trasa před NPC zablokována), metoda `_invalidate_cluster_detours` okamžitě smaže staré záznamy a umožní přepočet nové trasy.
    - **Sekvence zastavení a rozhlížení (Look-Around State Machine)**:
      - Při čelní blokádě nebo po uvíznutí NPC okamžitě zabrzdí na nulu (`velocity = Vector3.ZERO`, `set_velocity(Vector3.ZERO)`).
      - **Fáze 0 (0.25 s)**: Zastavení na místě, detekce clusteru před sebou a invalidace zastaralých objížděk.
      - **Fáze 1 (0.45 s) — Pohled vlevo**: NPC plynule otočí hlavu a tělo o $+52^\circ$ doleva. Zrakový kužel a AI Vision Probes snímají volný prostor vlevo (`_look_around_left_clearance`).
      - **Fáze 2 (0.55 s) — Pohled vpravo**: NPC plynule otočí pohled o $-52^\circ$ doprava a měří volný prostor vpravo (`_look_around_right_clearance`).
      - **Fáze 3 — Vyhodnocení a pokračování**:
        - NPC porovná naměřené hloubky koridorů: pokud je např. pravá strana zablokována zdí a levá strana vede do otevřené místnosti ($> 1.3\text{ m}$), zvolí levou stranu (`_avoidance_side = 1.0`).
        - Okamžitě napočte a vloží novou hladkou Bézier objížďku (`_bake_detour_around_obstacle`) do `Curve3D`, otočí se do volného směru a plynule pokračuje v chůzi.
        - Pokud jsou obě strany zablokované ($< 0.8\text{ m}$), NPC nahlásí zával a otočí patrolu na reverzní směr.
    - **Plná integrace do Beehave (Behavior Tree)**:
      - **`ActionMoveToPoint`**: Během rozhlížení a řešení překážek hlásí `is_handling_obstacle() == true`, takže Beehave nespouští timeout. Cíl v blackboardu (`wander_target`) se automaticky synchronizuje s nově napočteným waypointem objížďky (`npc.get_current_npc_path_target()`).
      - **Nový Beehave uzel `ActionScanAndRepath` (`Scripts/NPC/BT/action_scan_and_repath.gd`)**: Samostatná Beehave akce pro stromeček chování — zastaví NPC, provede rozhlížení a vyhodnocení, napočte novou objížďku a po úspěchu předá nový cíl do blackboardu se statusem `SUCCESS`.
      - **Nový Beehave uzel `ConditionPathBlocked` (`Scripts/NPC/BT/condition_path_blocked.gd`)**: Podmínka pro reaktivní selektor v Beehave reagující na detekci blokující překážky před NPC.
    - Sestaveno a podepsáno v `Godot FPS Studio.app`. Commitnuto a pushnuto na GitHub (`main`).



19. **Dynamické překážky: ověřený lokální plánovač a paměť cest — [HOTOVO, 25. 10. 2026]**:
    - Nahrazuje destruktivní vkládání objížděk do `Curve3D` z bodů 14–18. Původní waypointy a jejich akce zůstávají zachované; objížďka má vlastní fyzikálně ověřený seznam bodů.
    - Původní chybu ze snímků se podařilo reprodukovat headless: NPC stálo u waypointu 23 přes čtyři minuty simulovaného času a původní kód změnil křivku z 25 na 32 bodů.
    - `VisionCamera` je debug pohled. Detekci pohybových překážek zajišťuje kapsle podle kolize NPC, včetně nízkých props a počátečních překryvů. Dotazy respektují skutečný `collision_mask`; speciální skupina ani `NavigationObstacle3D` nejsou pro prop nutné.
    - Nový postup: zastavit → fyzikálně se rozhlédnout → ověřit NavMesh koridor → ověřit naučené cesty → lokální A* s kontrolou každé hrany kapslí a navigací → bezpečné zkrácení a následování cesty.
    - Plánování má rozpočty: 0,5m buňky, lokální poloměr 12 m, nejvýše 2048 rozvinutých uzlů a 32 rozvinutí za fyzikální tick. Dlouhé úseky se opravují lokálně. Rampa se ověřuje svislou projekcí na NavMesh.
    - Za běhu se kontroluje další úsek a skutečné zkracování zbývající cesty. Přesunutá nebo nově vložená bedna vyvolá přepočet. Waypointy s Wait/Look/Interact a dalšími akcemi se nepřeskakují.
    - RVO pro props nesmí blokovat fyzicky ověřenou objížďku: při jejím následování se dočasně vyřadí avoidance vrstva props 1 z masky; avoidance mezi NPC na vrstvě 2 zůstává aktivní.
    - Dokončené a znovu ověřené objížďky se ukládají do `user://npc_navigation/`; `remember_navigation_detours` a `persist_learned_detours` jsou standardně zapnuté. Cache má podpis trasy/akcí/transformace/vrstev/rozměrů a před použitím se znovu fyzikálně ověřuje.
    - Opraveny `ActionMoveToPoint`, `ActionScanAndRepath` a `ConditionPathBlocked`. Skenování postupuje i po `stop()`, objížďka nepotlačuje detekci uvíznutí a pohyb proběhne nejvýše jednou za fyzikální snímek.
    - Controller synchronizován v herním projektu i `engine/modules/gdscript/editor/script_templates/CharacterBody3D/fps_npc_controller.gd`.
    - Regrese: `../project-hl-2-inspiration/Tests/NPC/dynamic_navigation_headless.gd`. Prošly nízké/otočené props, cluster u zdi, změna aktivní objížďky, úplný zával, obsazený akční cíl, překryvy, vrstvy a hranice NavMeshe, rampa, cache, odstranění závalu, návrat a samostatná scan akce.
    - V aktuálním uživatelském levelu dokončen průchod všemi 25 body tam i zpět za 88,95 s simulovaného času; s přesunem bedny do aktivní objížďky za 85,48 s. Křivka a metadata akcí zůstaly stejné.
    - Sestaveno pro macOS arm64 (`dev_build=yes`, `-j4`), aktualizována aplikace na disku a ověřen její ad-hoc podpis (`codesign --verify --deep --strict`). Kompletní regrese prošla také z této nainstalované aplikace. Podrobnosti a reprodukční příkaz: `docs/npc_dynamic_navigation.md`. GUI editor ani běžící uživatelská hra nebyly restartovány; ověření proběhlo v samostatných headless procesech. Změny tohoto bodu jsou zatím v pracovních stromech; nebyly commitnuty ani pushnuty.

20. **Trvalé pečení objížděk do trasy (`bake_detours_to_route`) a hladký návrat bez zkoumání — [HOTOVO, 25. 10. 2026]**:
    - **Požadavek uživatele**: NPC sice objížďku našlo a prošlo, ale nová cesta se neuložila do trasy, takže při zpáteční cestě muselo znovu zastavovat a zkoumat překážku.
    - **Vpečení do `Curve3D`**: Při nalezení platné objížďky (`bake_detours_to_route = true`) se ověřené body automaticky vloží přímo do `npc_path.curve`. Vpečené body i napojovací waypointy mají nastavené lineární tečny `Vector3.ZERO`, aby Bézier křivka nevyboulila do překážky.
    - **Metadata akcí**: Do metadat trasy (`_fps_npc_point_actions`) jsou vloženy akce pro nové body s příznakem `"is_detour": true` a odpovídající rychlostí (`move` / `run`).
    - **Lokální průchod a návrat s 0 replány**: Během prvního průchodu NPC následuje segmenty lokální objížďky až k cílovému waypointu za překážkou (`target_wp`), zatímco trasa je trvale aktualizována. Při návratu zpět prochází NPC vpečené body přímo po křivce trasy s **0 dodatečnými přepočty (0 return replans)**.
    - **Filtrování šikmých ramp**: V `_navigation_probe` se jako volný sklon vyhodnocují pouze pochozí plochy statického světa na Layer 1 (`collider.collision_layer == 1` a `normal.y > 0.4`). Těžké a nízké překážky na Layer 2 jsou spolehlivě detekovány nezávisle na sklonu horní plošky.
    - **Ověření**: Všech 10 headless testů v `Tests/NPC/dynamic_navigation_headless.gd` (včetně `test_baked_detour_real_level` a `test_cache`) prošlo bez jediné chyby (`failures: []`, exit kód 0). Celý level tam i zpět se zapečenými 29 body dokončen za 75,53 s (návratové replány = 0). Controller synchronizován v herním projektu i šabloně enginu.

21. **Aktivní zraková detekce kamerou (4 prostorové body), plynulé lidské zpomalování a zkracování trasy — [HOTOVO, 25. 10. 2026]**:
    - **Zraková kamera NPC (`Camera3D`) jako aktivní detektor**:
      - NPC využívá svou zrakovou kameru (`VisionCamera` nebo libovolný existující `Camera3D` uzel v hlavě / stromu potomků) pro aktivní předstihové skenování trasy do vzdálenosti `vision_range` (až 6.8 m).
      - Filtrování překážek se zaměřuje na dynamická tělesa (Layer 2 Heavy Obstacles, Layer 4 Player, Layer 5 NPC a skupiny `fps_obstacle`/`fps_heavy_obstacle`), takže běžné vzdálené zdi architektury na Layer 1 nezpůsobují falešný poplach.
    - **4 detekční body vpřed a volba největší mezery (přesně podle nákresu 2)**:
      - Jakmile kamera zaznamená překážku v dráze pohybu, okamžitě vyšle vějíř 4 prostorových paprsků (`-30°`, `-10°`, `+10°`, `+30°`) z očí na úroveň NavMeshe podlahy.
      - Každý bod otestuje přítomnost podlahy (`_navigation_floor`), fyzikální volnost průchozího koridoru kapsle a vzdálenost k překážce.
      - Algoritmus okamžitě vyhodnotí skóre a vybere největší a nejvýhodnější mezeru (`_vision_best_gap_idx`, `_vision_best_gap_dir`, `_vision_best_gap_pos`), ještě než NPC k překážce dorazí.
    - **Plynulé lidské zpomalování (Smooth Deceleration)**:
      - Místo aby NPC běželo plnou rychlostí a natvrdo narazilo/zastavilo těsně před překážkou, aktivuje se profil plynulého brzdění (`smoothstep`) v zóně od `vision_deceleration_distance` (5.2 m) do `vision_early_detour_distance` (3.6 m).
      - Rychlost hladce klesá ze sprintu/běhu na manévrovací dojezdovou rychlost (~1.1 m/s), což působí zcela přirozeně a lidsky.
    - **Včasné zahájení objížďky a zkracování zatáček (přesně podle nákresu 1)**:
      - Při dosažení 3.6 m od překážky NPC zahájí manévr okamžitě bez čekání na pomalé koukání do stran (fáze rozhlížení se přeskočí, protože zraková kamera již našla nejlepší mezeru).
      - V A* i corridor plánování probíhá string-pulling vyhlazení trasy (`_simplify_navigation_path`), takže NPC neobchází rohy po zbytečných mezibodech, ale přirozeně a plynule „řízne“ zatáčku po otevřeném NavMeshi (žlutá křivka z nákresu 1).
    - **3D Debug vizualizace**:
      - 4 zelené paprsky od očí k podlaze, 8hranné kroužky na zemi (zelená = volno, červená = blokováno).
      - Vybraná největší mezera je zvýrazněna zářivě zlatou barvou se svislou vodicí stopkou a vodicí linkou po podlaze od nohou NPC přímo k mezeře.
    - **Ověření a regrese**:
      - Všech 10 testů v `dynamic_navigation_headless.gd` prošlo bez jediné chyby (`failures: []`, exit kód 0, čas 27.7 s). Zpáteční cesta se zapečenou trasou proběhla za 75.45 s s 0 replány. Synchronizováno do `fps_npc_controller.gd` i `npc_controller.gd`.

22. **Kontinuální zrakový vějíř kamery, zvětšený dosah středového detektoru a zkracování rohů — [HOTOVO, 25. 10. 2026]**:
    - **Problém původní detekce**:
      - Kamera měla 4 detekční body vázané na podmínku `if not obstacle_hit: return` z pouhých 3 čelních paprsků pod nulovým úhlem. Pokud byla bedna mírně mimo osu (např. o 10°–20°), čelní paprsky ji minuly, vějíř 4 bodů se vůbec nespustil a NPC pokračovalo plnou rychlostí až do vzdálenosti 0.8 m.
      - Středový detektor (`AISphereProbeCenter`) měl dosah omezený na pouhých 0.8–1.05 m (byl svázán vzorcem brzdné dráhy), takže visel těsně před nohama NPC a k reakci docházelo až těsně před nárazem.
    - **Kontinuální zrakový vějíř kamery (Active Multi-Ray Camera Detector)**:
      - Vějíř 4 detekčních bodů (`-30°`, `-10°`, `+10°`, `+30°`) i vícevýškové paprsky z kamery (`y = 1.45 m`) běží kontinuálně v každém zrakovém snímku (~20 Hz) s dosahem `vision_range` (až 8.5 m).
      - Jakmile libovolný paprsek zorného pole zachytí překážku (Layer 2 Heavy Obstacle, Layer 16 NPC, skupina `fps_obstacle`/`fps_heavy_obstacle`), kamera ji ihned detekuje, spočte vzdálenost k překážce a vyhodnotí největší průchozí mezeru (`_vision_best_gap_idx`, `_vision_best_gap_dir`, `_vision_best_gap_pos`).
      - NPC plynule zpomaluje z `vision_deceleration_distance` (6.5 m) a již ve vzdálenosti `vision_early_detour_distance` (3.8–4.0 m) před překážkou zahajuje vybočení do největší mezery bez nutnosti zastavovat.
    - **Zvětšený dosah středového detektoru (`AISphereProbeCenter`)**:
      - `lookahead_dist` mrkve na křivce dynamicky škáluje s rychlostí až na 2.4 m.
      - Dosah středového detektoru v otevřeném koridoru zvětšen až na 2.4 m, přičemž v zapečených obloucích objížďky se adaptivně drží křivky trasy, aby nedocházelo k ořezávání rohů bedny.
    - **Zkracování zatáček přes otevřený prostor (Ulehčení trasy dle nákresu 1)**:
      - V `is_at_destination()` přidána kontrola volného průhledu k následujícímu waypointu (`_navigation_segment_clear`). Pokud má NPC volnou cestu, nemusí zacházet do rohu, ale plynule zkrátí roh až do vzdálenosti 2.8 m.
    - **Oprava vpečení objížděk a 0 replánů při návratu**:
      - `_install_local_detour` předává do `_bake_detour_to_curve` nezkrácený tvar trajektorie (`raw_candidate_path`), což zabraňuje předčasné redukci bodů a garantuje trvalé vpečení objížďky do `Curve3D`.
    - **Ověření a regrese**:
      - Všech 10 testů v `Tests/NPC/dynamic_navigation_headless.gd` prošlo bez jediné chyby (`failures: []`, exit kód 0, čas 38.3 s, `return_replans: 0`).
      - Soubory `engine/modules/gdscript/editor/script_templates/CharacterBody3D/fps_npc_controller.gd` a `Scripts/NPC/npc_controller.gd` jsou 100% synchronizované.

## Aktuální stav NPC — 26. 10. 2026 (Univerzální baseline)

Experimentální třída `DynamicNavigationAgent3D`, skenování kamerou, pečení objížděk a staré Beehave stromy zůstávají odstraněné. Náhradou je jednoduchý a testovaný základ:

- **Architektura**: Beehave v `basic_npc.tscn` vybírá prioritu hrozba → pozorování → trasa → bloudění. `NPCController` vykonává pohyb a reakce.
- **Civilian**: používá stres a povahy Steady, Cautious a Nervous. Klidně držená zbraň má nízký strop stresu a sama nevyvolá útěk; míření přímo na NPC stres zvyšuje až k útěku. Blízké výstřely a dopady vedle NPC vyvolají útěk od střelce, vzdálenost a zdi podnět tlumí. Přímá baterka vyvolá zakrytí očí a ústup, nikoli paniku. Po zranění civilista prchá okamžitě.
- **Hostile**: útočí a prohledává poslední známou pozici 5 s. Pod 30 % zdraví končí melee a hledá kryt nebo ustupuje.
- **Friendly**: náhrada Passive se zachovaným indexem 2; neútočí první, po zranění se brání a při nízkém zdraví ustupuje.
- **Poškození**: hitscan pistole má výchozí masku `1 | 2 | 4 | 16`, takže zasahuje NPC. Všichni ztrácejí zdraví podle své role.
- **Viditelná zbraň**: `PlayerWeaponInventory.has_visible_weapon()` a `FPSCharacter.has_visible_weapon()` odlišují vytasenou pistoli od holých rukou.
- **Editor trasy**: waypoint akce včetně Sit, billboard ikony, Bézier zaoblení a kontrola NavMeshe zůstávají zachované.
- **Synchronizace**: `engine/modules/gdscript/editor/script_templates/CharacterBody3D/fps_npc_controller.gd` je shodná s `Scripts/NPC/npc_controller.gd` v herním projektu.
- **Ověření**: 94 headless kontrol prošlo; testovací scéna proběhla bez skriptových chyb. Uživatel v GUI dříve potvrdil obnovu chůze po pozorování a útěk civilisty po zásahu; nové reakce na míření, baterku a výstřely mimo čekají na GUI potvrzení.
- **Předávací dokumentace**: `../project-hl-2-inspiration/Docs/npc_ai_handoff.md` obsahuje kompletní stav, reprodukční příkazy a další GUI testy.

Nejnovější šablona zatím nebyla zabudována do instalované aplikace; herní projekt používá controller přímo a testování nevyžaduje rebuild editoru.

