# Godot FPS Studio — stav práce pro Claude Code

Trvalá pravidla projektu jsou v [AGENTS.md](AGENTS.md) (build jen `./tools/build_macos.sh`, GUI editoru nespouštět bez žádosti, žádné ZIP archivy, zachovat UniBuilder, po kontrole diffu commit + push). Tento soubor zachycuje, kde práce skončila.

Stav k 2026-11-15, větev `codex/black-default-fps-level` (vše pushnuto). Uživatelská dokumentace Particle je v `docs/particle_editor.md`, třídy v `engine/doc/classes/` (ParticleEffect3D, CameraShake3D, RadialDamage3D).

## Kde jsme skončili
Particle workspace je funkčně hotový pro FPS hru na motivy HL2. Poslední práce: kouř podle Source/UE5 (měkký flipbook, osvětlení, různé velikosti, odpor jen na začátku života). Uživatel chce kouř dál ladit sám v GUI („hodně bude záležet, jak to nastavím“), takže do kouře nesahat bez žádosti. Čeká se na další zpětnou vazbu z GUI.

## Hotovo

### VFX Texture Generator (`engine/editor/fps_studio/vfx_texture_generator.*`)
- Samostatný modul bez závislostí na editoru, vždy kompilovaný s `-O3` (`engine/editor/fps_studio/SCsub`; dev build je jinak `-O0`). Asynchronní náhled přes `WorkerThreadPool`, recepty `.cfg` v3. Uživatel potvrdil vzhled i rychlost.
- Typy: objemové (Fire, Smoke, Cloud, Explosion, Explosion Ring), energetické HDR, vzory (Caustics, Noise).
- **Soft Smoke (Particles)** TYPE 20 (`render_soft_smoke`, 2D): měkký obláček bez siluety, parametry `billows` (osvětlené chuchvalce uvnitř) a `lifecycle` (flipbook: mírně naroste, roztrhá se, zřídne).
- **Decals** TYPE 16–19 (`render_decal`, `write_flat`): Bullet Hole (surface 0 beton, 1 kov s lístky, 2 dřevo), Scorch / Burn Mark, Glass Crack, Splatter. Apply na stopu Decal nastaví `texture_albedo`/`texture_normal`; karta Decal má Generate Decal Texture...
- ID typů jsou uložená v receptech, nikdy je nepřečíslovat (nové jen na konec).

### Particle workspace (`engine/editor/fps_studio/particle_*`)
- Zásobník modulů (`particle_module_stack.cpp`), časová osa 2.0 (`particle_timeline.cpp`), náhled 2.0, Convert to Shader Code inline v Particle Logic. Undo jde do globální historie.
- Rozložení: panely zavíratelné, menu Layout, zkratky Alt+0–9 (fyzické klávesy kvůli české klávesnici), Library v horní liště, zhuštěné UI pod 1200 px, barvy z motivu editoru (`_apply_workspace_theme`, pozor na re-entry přes `applying_workspace_theme`). Uživatel potvrdil, že rozložení funguje.
- Náhled: View menu (prostředí, Glow, Emitter Bounds, Floor, Damage & Shake Radius, **Sound**, **Camera Shake**, Collision Test Walls). SubViewport náhledu je 3D posluchač zvuku.
- **17 presetů ve 4 kategoriích** (`particle_presets.cpp`, `get_preset_category`, `get_presets_in_display_order`; nové jen na konec enumu): Weapons, Impacts, Explosions & Fire, Environment. `fit_visibility_aabb` počítá hranice z pohybu, aditivní emitory bez řazení. Decaly: Explosion spálenina, zásahy díra podle materiálu, Blood Hit krev na zdi. Kolize zapnutá u jisker Explosion, úlomků/třísek/jisker zásahů, střepů, elektrických jisker a jisker světlice, kouře granátu.
- Kouř v presetech přes `make_soft_smoke(EmitterBuilder&)`: flipbook `smoke_soft_life.png` 4×4 přes život, per-vertex lit, široké Scale, rotace, ±30 % životnost. Výbuch: postupné vypouštění, křivka `PARAM_DAMPING` (odpor jen první třetinu života). **V Godotu damping brzdí i gravitaci**, takže stálý odpor větší než vztlak kouř nikdy nezvedne.
- Modul Collision: `collision_base_size` je poloměr kolize (částice se zastaví v té vzdálenosti); nastaví se z velikosti meshe (`get_suggested_collision_size`), karta varuje a má Fit Particle Size.
- Level editor: `ParticleEffectLevelPreviewPlugin` (`particle_level_preview_plugin.*`, registrace v `engine/editor/register_editor_types.cpp`) přidá při výběru efektu Preview Effect / Stop do lišty 3D. `editor_preview_play/stop`, kolize přes sdílené dlaždice (`_ensure_editor_level_collision`, přepeče jen při změně hashe geometrie), `_restore_editor_preview` vrací světla, decaly, pitch, `fixed_fps`, `emitting` (po dohrání, Stop a v `NOTIFICATION_EDITOR_PRE_SAVE`).

### Runtime (`engine/scene/3d`)
- `ParticleEffect3D`: časování emitorů v metadatech, `play/stop/seek`, události Light, Sound, CameraShake, Decal, Damage.
- **Kolize částic s levelem bez kolizních boxů**: první kolizní efekt spustí `prepare_level_collision` (StaticBody3D/CSG tvary → dlaždice SDF 16+2×2 m, 64³ na WorkerThreadPool; v úlohách sériový `bake_faces(..., false)`, jinak deadlock vnořených skupinových úloh). Než jsou hotové, efekt si napeče vlastní oblast (max 20 m, rozlišení 16/32/64, pás 0,6–1,5 m, vysoká priorita) s cache. Emitory s kolizí dostanou vyšší `fixed_fps` proti tunelování (`apply_collision_substeps`). Kolize i u shaderů s `COLLIDED`. `GPUParticlesCollisionSDF3D::bake_faces` je nové API, `gpu_particles_collision_3d.cpp` se kompiluje s `-O3` (`engine/scene/3d/SCsub`).
- **Pooling**: `spawn()` vrací dohrané efekty do zásobníku podle PackedScene (max 64), `prewarm`, `clear_pool`, `get_pooled_count`; odpojí `finished`.
- `CameraShake3D` 2.0 přes `Camera3D::add_shake_offset` (aditivní, neukládá se). `RadialDamage3D`: útlum, krytí 9 paprsky, `take_damage` s 1–3 argumenty, impulz RigidBody3D.

### Opravené chyby (výběr)
- Revize 2026-11-15 (4 oblasti, opraveno): Remove Emitter měl `add_do_reference` (po undo + nové akci smazal živý emitor), akce efektu jdou do `GLOBAL_HISTORY` s `effect_document_serial` (kroky starého dokumentu se ignorují), undo vynutí obnovu UI (`_set_edited_emitter(..., p_force)`), výběr emitoru už nepřestavuje náhled ani kameru (`preview_source_id`), úpravy se slučují na 1× za snímek (`_mark_document_dirty` / `_queue_preview_sync`), Open... se ptá na neuložené změny, uložení přebírá cache (`set_path(..., true)`) a přenačte instance v levelech, Add to Level nejdřív uloží. Runtime: čekání na úlohy pečení (`wait_for_task_completion`), stavy kolize podle space + kořene (víc scén), zrušení zastaralých dlaždic, pooly čistí mrtvá ID, cache podle kořene levelu, hodnoty emitorů/událostí se čtou při `play()`. Moduly: presety Color/Size a Paste převádí textury na Gradient/Curve pro CPU, karta nezapisuje výchozí metadata, schránka se maže v PREDELETE. RadialDamage3D drží ObjectID.
- Přidání prvního kolizního efektu do levelu: `LevelParticleCollision` se připojuje odloženě (`_attach_level_manager`), kořen bývá zaneprázdněný přidáváním efektu.
- Pád editoru při ukončení v zásobníku modulů (dvojité uvolnění): úklid ovládacích prvků vlastněných pluginem je v `NOTIFICATION_PREDELETE` přes ObjectID, ne v destruktoru (potomci jsou v destruktoru už uvolnění).
- Úniky při ukončení: náhledové slunce/prostředí `Node3DEditor` (ve forku vypnuté, nikdy nepřipojené) a prvky karet modulů u emitoru se shaderem.
- Pád `_build` u emitoru se shaderem (karta Collision neexistuje).
- Dřívější: smyčka prázdného efektu, Add to Level u neuloženého efektu, dvojitý reimport textur.

## Ověření a jeho hranice
- **Headless testy spouštěj s `HOME=<scratchpad>/home` a `--language en`.** Jinak zapisují do skutečného nastavení editoru uživatele (jednou tam omylem skončila zkratka ⌥Q, ručně odstraněno).
- Testovací projekt ve scratchpadu (`scratchpad/proj`, není v repozitáři): doplněk `addons/lt/plugin.gd` (zálohy `plugin_presets.gd.bak` = všech 17 presetů přes Library s potvrzením „Discard“, `plugin_leak.gd.bak` = úniky se shaderem, `plugin_extra.gd.bak` = presety + Remove/undo/Duplicate + Open při neuložených změnách), runtime skripty `col_test.gd`, `level_test.gd`, `pool_test.gd`, `sdf_probe.gd`, `damage_test.gd`, `shake_test*.gd`. Na herním levelu skripty v `scratchpad/gametest/` (spouštěné přes `-s`, herní repo se nemění). Vzhled textur: samostatný build `scratchpad/gen` (clang++ `main.cpp` + `vfx_texture_generator.cpp`, `sheet.py`), kouř `scratchpad/sim.py`. Při ztrátě scratchpadu je napiš znovu.
- Ručně v GUI zatím **neověřeno**: decaly a nový kouř ve hře, Preview Effect v Level editoru, tlačítko Fit Particle Size, přetažení zvuku na seznam emitorů, Convert to Shader Code, zkratky časové osy.
- Herní projekt `/Users/davidsoudil/Projects/Godot/project-hl-2-inspiration` (needitovat bez žádosti): level `Levels/Test_levels/fps_level.tscn` už nemá uzel Explosion, uživatel má efekt `Scenes/Effects/Explosion_01.tscn` (smyčka zapnutá, starý kouř a stará velikost kolize; nové presety mají opravené hodnoty). Efekty vytvořené před změnami je potřeba vytvořit znovu z Library.

## Další na řadě
1. Zpětná vazba uživatele z GUI (kouř ladí sám).
2. Možná rozšíření, pokud o ně požádá: normal mapy pro decaly z generátoru, převod modulů do Visual Shader grafu, oblast smyčky v časové ose, další typy z EffectTextureMakeru (CellNoise, MarbleNoise), automatické `prewarm` / `prepare_level_collision` při načtení levelu v herním projektu.
