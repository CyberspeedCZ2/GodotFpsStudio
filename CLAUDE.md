# Godot FPS Studio — stav práce pro Claude Code

Trvalá pravidla projektu jsou v [AGENTS.md](AGENTS.md) (build jen `./tools/build_macos.sh`, GUI editoru nespouštět bez žádosti, žádné ZIP archivy, zachovat UniBuilder, po kontrole diffu commit + push). Tento soubor zachycuje, kde práce skončila.

Stav k 2026-11-15, větev `codex/black-default-fps-level` (pushnuto, poslední commit `f4813a02`).

## Hotovo

### VFX Texture Generator
- Samostatný modul `engine/editor/fps_studio/vfx_texture_generator.cpp/.h` bez závislostí na editoru, vždy kompilovaný s `-O3` (viz `engine/editor/fps_studio/SCsub`; dev build je jinak `-O0`).
- Vlastní objemový renderer (Fire, Smoke, Cloud, Explosion, Explosion Ring), HDR emise pro energetické efekty, periodické vzory (Caustics, Noise). Žádný přepis cizích shaderů (EffectTextureMaker ani Shadertoy).
- Asynchronní náhled přes `WorkerThreadPool` s adaptivním rozlišením, bez per-pixel jitteru (sdílené hloubkové řezy + podkroky podle optické hustoty).
- Nové UI (galerie typů, náhled s pozadím, inspektor ve sbalovacích sekcích), parametry podle typu, recepty `.cfg` v3.
- Uživatel potvrdil, že generátor vypadá dobře a je dost rychlý.

### Particle workspace (inspirace UE5 Niagara a HL2)
- `particle_module_stack.cpp` — zásobník modulů s nativními editory vlastností, presety modulů (menu ⋮), Copy/Paste/Reset, křivky přes život, moduly Trails a Attractors, karty událostí. Undo jde do globální historie (`create_action_for_history` + `force_fixed_history`).
- `particle_timeline.cpp` — časová osa 2.0: zoom/posun, ztlumení a solo stop, kontextové menu, zkratky, duplikace emitoru s vlastními zdroji.
- `particle_presets.cpp` — 17 FPS presetů ve 4 kategoriích (`get_preset_category`, `get_presets_in_display_order`; nové jsou přidané na konec enumu kvůli stabilním id): Weapons, Impacts, Explosions & Fire, Environment. `fit_visibility_aabb` počítá hranice z pohybu, aditivní emitory bez řazení, textury pečené do `res://vfx_textures/` s recepty `.vfx.cfg`.
- Generátor napojený v modulu Render (Apply uloží PNG + recept vedle efektu, zapíše `.import` s 3D nastavením, přiřadí emitoru s Undo).
- Náhled 2.0: menu View (Studio/Daylight/Night/Interior, Glow, hranice emitoru, kolizní podlaha), statistiky, Convert to Shader Code.

### Runtime (engine/scene/3d)
- `ParticleEffect3D` — kořen efektů: zpoždění/délka emitorů v metadatech, `play`/`stop`/`seek`, `finished`, `looping`, `auto_free`, statický `spawn()`; časované události Light3D, AudioStreamPlayer3D, CameraShake3D, Decal; skryté stopy jsou ztlumené; varování bez emitorů.
- `CameraShake3D` — otřes aktivní kamery přes `h_offset`/`v_offset`.
- Docs: `engine/doc/classes/ParticleEffect3D.xml`, `CameraShake3D.xml`; uživatelská dokumentace `docs/particle_editor.md`, roadmapa `docs/particle_workspace_plan.md`.

### Opravené chyby
- Pád hry: smyčka prázdného `ParticleEffect3D` (nekonečná rekurze).
- Add to Level u neuloženého efektu zahazoval emitory při uložení levelu.
- Dvojitý reimport vygenerovaných textur („Task 'reimport' already exists“).

### Rozložení Particle workspace (2026-11-15)
- Panely Emitters, Preview, Timeline & Logic a Properties jdou zavřít křížkem a znovu otevřít přes menu **Layout**. Spodní panel jde maximalizovat (skryje náhled) nebo oddělit do vlastního okna (`WindowWrapper`). Viditelnost panelů se ukládá do `get_window_layout`.
- Graf VisualShader se edituje jen inline v Particle Logic, tlačítko „Open in Shader Editor“ je pryč. Náhled má minimální výšku 140 px.
- Křížek v odděleném okně vrací panel zpět (dřív ho skryl). Pod 1200 px šířky se UI zhušťuje (`_apply_compact_layout`), záhlaví Particle Logic je `HFlowContainer`.
- Convert to Shader Code otevírá kód inline v Particle Logic (`TextShaderEditor`, tlačítko Toggle Files Panel je skryté). Godotův Script editor shadery neumí, proto ne záložka Script.
- Úniky při ukončení opraveny (2026-11-15): náhledové slunce a prostředí 3D editoru (`~Node3DEditor`, ve forku výchozí vypnuté, nikdy nepřipojené) a ovládací prvky karet modulů, které vlastní plugin a u emitoru se shaderem zůstanou bez rodiče (`~FPSParticleModuleStack`). Opraven i pád `_build` u emitoru se shaderem (karta Collision neexistuje).
- Po spuštění jsou Preview a Timeline vždy vidět (ukládá se jen stav bočních panelů). Posuvník času v transportu má minimální šířku 120 px. Zhuštěné UI se zapne i tehdy, když se široké rozložení nevejde.
- Uživatel potvrdil, že rozložení v GUI funguje dobře.
- Effect Library je přesunutá do menu **Library** v horní liště, panel knihovny je ve výchozím stavu skrytý (`show_library` v layoutu). Seznam emitorů má barvy stop (`FPSParticleTimeline::get_track_color`) a sloupec s typem.
- Zkratky Layout: `particle_workspace/*` přes `ED_SHORTCUT` (Alt+0–9 jako **fyzické** klávesy kvůli české klávesnici, kde je na „1“ znak „+“; přenastavitelné v Editor Settings → Shortcuts), přeposílá je MenuButton Layout, takže platí jen v Particle.
- Vzhled: `_apply_workspace_theme` (barvy z motivu editoru: `background` pro pozadí, `base_color` pro panely, `accent_color` pro výběr a vybranou záložku). Pozor na re-entry: override na kořeni vyvolá theme_changed → hlídá `applying_workspace_theme` (bez něj se editor zasekl).
- CameraShake3D 2.0: rotační trauma otřes přes nový `Camera3D::add_shake_offset` (aditivní, neukládá se), `inner_radius`, `max_rotation_degrees`, `max_offset`, čekání až 0,5 s na kameru, signál `shake_started`. Ověřeno headless runtime testem (`shake_test*.gd` ve scratchpadu).
- Ve hře: uzel `Explosion` ve `fps_level.tscn` je stará vložená kopie bez LightFlash/CameraShake; uživatel ho má nahradit (efekt uložit jako .tscn a znovu Add to Level). Herní repo needitovat bez žádosti.
- RadialDamage3D (`engine/scene/3d/radial_damage_3d.*`, událost EVENT_DAMAGE): zranění s útlumem, krytí 9 paprsky (poměrně), `take_damage` voláno s počtem argumentů podle metody (hráč má 1, NPC 3), impulz pro RigidBody3D. Ověřeno headless (`damage_test.gd`). Kruhy dosahu: `particle_event_gizmos.*` (gizmo v Level + náhled, View → Damage & Shake Radius). View → Floor (výchozí vypnuto).
- Zvuk: preset Explosion má prázdnou stopu Sound; audio soubor jde přetáhnout na seznam emitorů (`_drop_on_emitter_tree`, headless neověřeno). Kolize: tlačítko Add Collision to Level v modulu Collision peče `ParticleCollision` (SDF) z meshů levelu bez CharacterBody/RigidBody/efektů (ověřeno headless); View → Collision Test Walls v náhledu; výchozí collision_base_size 0,25.
- Zvuk v náhledu: SubViewport náhledu je 3D posluchač (`set_as_audio_listener_3d`), bez toho AudioStreamPlayer3D mlčel. View → Sound a View → Camera Shake vypínají zvuk a otřes jen v kopii náhledu (`_apply_preview_event_toggles`, ověřeno headless).
- Kolize se stěnami bez kolizních boxů: `ParticleEffect3D` (skupina Level Collision, `collide_with_level`) při `play()` za běhu hry dotazem na fyziku najde StaticBody3D/CSG tvary v `visibility_aabb` emitorů s kolizí, převede je na trojúhelníky a hned (úzký pás 1,5 m, ~15 ms; nad 20k trojúhelníků na pozadí) napeče přechodný `GPUParticlesCollisionSDF3D` (`bake_faces`, nový v `gpu_particles_collision_3d`, soubor kompilován s -O3). Sdílená LRU cache 16 záznamů, mazaná v `unregister_scene_types`. Ověřeno headless (`col_test.gd`, ~130 ms na pozadí, 0,3 ms hlavní vlákno) i na `fps_level.tscn` (56 trojúhelníků). Emitory s kolizí dostanou vyšší `fixed_fps` proti tunelování (`apply_collision_substeps`, i v náhledu); kolize se pozná i u shaderů s `COLLIDED`. `collision_base_size` je poloměr kolize (částice se zastaví v té vzdálenosti); modul ho nastaví z velikosti meshe (`get_suggested_collision_size`), karta varuje a má Fit Particle Size. SDF je ověřené vzorkováním (`sdf_probe.gd`, meta `_debug_sdf_image` jen s --verbose).
- Kolizní oblast je nejvýš 20 m kolem efektu, rozlišení SDF 16/32/64 podle velikosti (buňka ~0,15–0,31 m), pás `clamp(6 buněk, 0,6, 1,5 m)`.
- Test všech presetů: doplněk `addons/lt` vytvoří přes menu Library všech 17 a potvrzuje dialog „Discard“.
- Sdílená kolize levelu: `ParticleEffect3D::prepare_level_collision` (strom → tvary StaticBody3D/CSG → dlaždice 16+2×2 m, 64³, úlohy WorkerThreadPool se sériovým `bake_faces(..., false)`, jinak deadlock vnořených skupinových úloh). Spustí ji první kolizní efekt; po `is_level_collision_ready` efekty nepečou. Okamžité pečení má vysokou prioritu. Test `level_test.gd`, na herním levelu `gametest/game_level_test.gd` (skript mimo herní repo).
- Pooling: `spawn()` vrací dohrané efekty do zásobníku podle PackedScene (max 64), bez skrývání (skrývání zpomalovalo), `prewarm`, `clear_pool`, odpojí `finished`. Test `pool_test.gd`, měření na `Explosion_01.tscn` uživatele v `gametest/pool_game.gd` (jen čtení).
- Generátor: kategorie Decals (TYPE_BULLET_HOLE 16, TYPE_SCORCH 17, TYPE_GLASS_CRACK 18, TYPE_SPLATTER 19, `render_decal`, `write_flat`). Apply na stopu Decal nastaví `texture_albedo`/`texture_normal`; presety Explosion, zásahy a Blood Hit mají decal události. Rychlé ladění vzhledu: samostatný build `scratchpad/gen` (clang++ main.cpp + vfx_texture_generator.cpp, `sheet.py`).
- Level editor: `ParticleEffectLevelPreviewPlugin` (`particle_level_preview_plugin.*`, registrace v `register_editor_types.cpp`) přidá při výběru efektu do lišty 3D Preview Effect/Stop → `editor_preview_play/stop`. Kolize přes `_ensure_editor_level_collision` (hash trojúhelníků, přepeče jen při změně). `_restore_editor_preview` vrací světla, decaly, pitch, `fixed_fps`, `emitting` (po dohrání, Stop, `NOTIFICATION_EDITOR_PRE_SAVE`).
- Headless editor testy spouštěj s `--language en` (izolované nastavení jinak přepne editor do češtiny).
- Inspirace od uživatele: Model workspace (záložky vlevo, svislé záložky vpravo) pro šetření místem.

## Co se právě řešilo
Poslední zadání: přepracovat rozložení Particle workspace (zavírání a otevírání panelů, graf bez vytlačení náhledu). Hotovo a pushnuto, čeká se na ruční test v GUI. Potom pokračovat zpětnou vazbou k funkcím Particle (časová osa, události, moduly, náhled).

## Ověření a jeho hranice
- **Headless testy spouštěj s `HOME=<scratchpad>/home`.** Jinak zapisují do skutečného nastavení editoru uživatele (`~/Library/Application Support/godotfpsstudio/editor_settings-4.7.tres`). Test přenastavení zkratky tam jednou omylem uložil ⌥Q; záznam byl ručně odstraněn.
- Headless testy jsou editorový doplněk v dočasném projektu ve scratchpadu (`addons/fxtest/plugin.gd`, režimy `FXTEST=1|presets|timeline|level|events|modules|preview`) a skripty `rt.gd`/`ev.gd` pro runtime. Nejsou součástí repozitáře; při ztrátě scratchpadu je napiš znovu.
- Ručně v GUI zatím **neověřeno**: vzhled nového UI, zkratky časové osy, záblesky světla a otřes kamery v náhledu i ve hře, Convert to Shader Code (headless renderer vrací prázdný kód shaderu, převod je proto chráněný pojistkou).
- Herní projekt `/Users/davidsoudil/Projects/Godot/project-hl-2-inspiration` se s aktuálním buildem headless spustí bez chyb. V jeho `Levels/Test_levels/fps_level.tscn` může zůstat starý prázdný uzel `Explosion`; uživatel ho má smazat a přidat efekt znovu (ideálně uložený jako `.tscn`).

## Další na řadě
1. Zapracovat zpětnou vazbu uživatele z ručního testu nových funkcí v GUI.
2. Ručně ověřit Convert to Shader Code a události ve hře (případně opravit).
3. Možná rozšíření, pokud o ně uživatel požádá: převod modulů do Visual Shader grafu (dnes jen šablona Create Node Graph), oblast smyčky v časové ose, decal textury v generátoru (např. spálenina), další typy z EffectTextureMakeru (CellNoise, MarbleNoise).
