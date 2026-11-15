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
- `particle_presets.cpp` — 7 FPS presetů (Muzzle Flash, Explosion, Bullet Impact, Impact Sparks, Blood Hit, Smoke Plume, Fire), textury pečené do `res://vfx_textures/` s recepty `.vfx.cfg`.
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
- Známé: s particle `ShaderMaterial` (kód) na emitoru hlásí headless editor při ukončení úniky RID/ObjectDB. Je to i bez vloženého editoru kódu, nejde o regresi; zatím neřešeno.
- Po spuštění jsou Preview a Timeline vždy vidět (ukládá se jen stav bočních panelů). Posuvník času v transportu má minimální šířku 120 px. Zhuštěné UI se zapne i tehdy, když se široké rozložení nevejde.
- Uživatel potvrdil, že rozložení v GUI funguje dobře.
- Effect Library je přesunutá do menu **Library** v horní liště, panel knihovny je ve výchozím stavu skrytý (`show_library` v layoutu). Seznam emitorů má barvy stop (`FPSParticleTimeline::get_track_color`) a sloupec s typem.
- Inspirace od uživatele: Model workspace (záložky vlevo, svislé záložky vpravo) pro šetření místem.

## Co se právě řešilo
Poslední zadání: přepracovat rozložení Particle workspace (zavírání a otevírání panelů, graf bez vytlačení náhledu). Hotovo a pushnuto, čeká se na ruční test v GUI. Potom pokračovat zpětnou vazbou k funkcím Particle (časová osa, události, moduly, náhled).

## Ověření a jeho hranice
- Headless testy jsou editorový doplněk v dočasném projektu ve scratchpadu (`addons/fxtest/plugin.gd`, režimy `FXTEST=1|presets|timeline|level|events|modules|preview`) a skripty `rt.gd`/`ev.gd` pro runtime. Nejsou součástí repozitáře; při ztrátě scratchpadu je napiš znovu.
- Ručně v GUI zatím **neověřeno**: vzhled nového UI, zkratky časové osy, záblesky světla a otřes kamery v náhledu i ve hře, Convert to Shader Code (headless renderer vrací prázdný kód shaderu, převod je proto chráněný pojistkou).
- Herní projekt `/Users/davidsoudil/Projects/Godot/project-hl-2-inspiration` se s aktuálním buildem headless spustí bez chyb. V jeho `Levels/Test_levels/fps_level.tscn` může zůstat starý prázdný uzel `Explosion`; uživatel ho má smazat a přidat efekt znovu (ideálně uložený jako `.tscn`).

## Další na řadě
1. Zapracovat zpětnou vazbu uživatele z ručního testu nových funkcí v GUI.
2. Ručně ověřit Convert to Shader Code a události ve hře (případně opravit).
3. Možná rozšíření, pokud o ně uživatel požádá: převod modulů do Visual Shader grafu (dnes jen šablona Create Node Graph), oblast smyčky v časové ose, decal textury v generátoru (např. spálenina), další typy z EffectTextureMakeru (CellNoise, MarbleNoise).
