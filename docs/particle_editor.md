# Pracovní prostředí Particle

Particle workspace je samostatný editor opakovaně použitelných 3D efektů. Efekt upravuje mimo aktivní Level a do herní scény se přidává příkazem **Add to Level**, nebo se spouští z kódu přes `ParticleEffect3D`.

![Konceptuální podoba Particle workspace](images/particle_workspace_concept.png)

Render je vizuální reference pro rozložení UI. Levá část představuje knihovnu efektů a strom uzlů efektu, střed celý simulovaný efekt a pravý panel vlastnosti právě vybraného emitoru. Spodní graf je nativní Godot `VisualShader` v režimu Particle; není to graf vztahů mezi emitory. Vybraný GPU emitor lze upravovat přímo ve vloženém shaderovém editoru.

Při návrhu práce s efekty zohledňujeme také [tutoriál ohně a jisker od Le Lu](https://www.youtube.com/watch?v=Y1ZBzIiP-v4) a [EffectTextureMaker od Mebiusbox](https://mebiusbox.github.io/contents/EffectTextureMaker/). Oheň, kouř a jiskry se skládají z více emitterů; sprite a další VFX textury jsou samostatné obrazové zdroje. Z Particle workspace je nyní dostupný první nativní generátor: vytváří průhledné textury, ukládá je jako PNG a umí je přiřadit vybranému emitoru. Efektová scéna pak uchovává běžný odkaz na PNG asset.

## Rozložení

Vzhled používá barvy aktuálního motivu Godotu: pozadí workspace má barvu pozadí editoru a každý panel (horní lišta, Emitters, náhled, Timeline & Logic, Properties) je samostatná plocha v barvě panelů s mezerou mezi nimi. Vybraná položka seznamu má akcentní proužek jako v Model workspace.

Rozložení vychází z UE5 Niagara a z particle editoru Source enginu (HL2):

- **Horní lišta dokumentu:** název efektu (dvojklik přejmenuje), stav uložení, **New**, **Open...**, **Save** a **Add to Level**.
- **Vlevo:** seznam stop (emitory i události) s tlačítky **Add**, **Duplicate** a **Remove**. Každá stopa má barevný proužek ve stejné barvě jako v časové ose a vpravo typ s ikonou (např. `GPU · 14` = GPU emitor se 14 částicemi, `Light`, `Shake`, `Sound`, `Decal`).
- **Menu Library** v horní liště vytvoří efekt z **FPS presetu** nebo znovu otevře nedávný efekt. Vyhledávací panel **Effect Library** pod seznamem stop je ve výchozím stavu skrytý, aby zbylo místo; zapne se v Library → Show Library Panel nebo v Layout. Náhledové obrázky presetů se vykreslí na pozadí hned po otevření pracovní obrazovky Particle a uloží se do `.godot/editor/fps_particle_thumbnails/` (podle buildu editoru), takže při dalším spuštění jsou v menu hned. Snímek se fotí na začátku života částic, ne podle dlouhých událostí jako 12s decal. Když myš chvíli (výchozí 1 s) zůstane na položce v menu Library nebo v panelu Effect Library, vedle ní se otevře větší náhled (280 px) s názvem a popisem, který efekt přehrává dokola (tooltip s popisem se pak nezobrazuje); zavře se, jakmile myš položku opustí. Zpoždění se mění v Editor Settings → FPS Studio → Particles → Library Hover Preview Delay (0 = vypnuto).
- **Uprostřed:** 3D náhled s menu **View** a transportem (Play/Pause, Restart, čas, statistiky, Speed, Loop). Pod ním jsou záložky **Timeline** a **Particle Logic**.
- **Vpravo:** vybraný emitor jako **zásobník modulů**. Přepínač **All Properties** zobrazí kompletní nativní inspektory (Emitter, Process, Renderer).

Panely jde zavřít a znovu otevřít. Každý panel má v záhlaví křížek a menu **Layout** v horní liště obsahuje:

- přepínače **Emitters**, **Effect Library**, **Preview**, **Timeline & Logic** a **Properties**,
- **Open Timeline** / **Open Particle Logic** (otevře spodní panel na dané záložce, i když byl zavřený),
- **Maximize (Hide Preview)** (spodní panel zabere celý střed; totéž dělá tlačítko vpravo nahoře ve spodním panelu),
- **Separate Window** (přesune Timeline & Logic do samostatného okna; zavřením okna nebo křížkem v něm se panel vrátí na původní místo),
- **Reset Layout** (vše zobrazí a vrátí výchozí rozměry).

Všechny položky menu Layout mají editorové zkratky. Fungují, jen když je zobrazené Particle, a dají se přenastavit v **Editor → Editor Settings → Shortcuts** (skupina **Particle Workspace**, hledej „particle“). Výchozí zkratky jsou Option/Alt + číslo. Používají fyzické klávesy číselné řady, takže fungují stejně i na české klávesnici:

| Zkratka | Akce |
| --- | --- |
| Alt+1 | Emitters |
| Alt+2 | Effect Library |
| Alt+3 | 3D náhled (Preview) |
| Alt+4 | Timeline & Logic |
| Alt+5 | Properties |
| Alt+6 | Otevřít Timeline |
| Alt+7 | Otevřít Particle Logic |
| Alt+8 | Maximalizovat Timeline & Logic |
| Alt+9 | Timeline & Logic v samostatném okně |
| Alt+0 | Reset Layout |

Ve středu je vždy aspoň náhled nebo spodní panel. Náhled má minimální výšku a graf ho už nevytlačí; při přepnutí na Particle Logic s grafem dostane spodní panel aspoň polovinu výšky. Viditelnost bočních panelů (Emitters, Properties) se ukládá do rozložení editoru. Náhled a Timeline jsou po spuštění editoru vždy zobrazené. V užším okně (pod 1200 px) se lišty zhustí: Layout, Duplicate, Remove, Reset View, Pause a Restart jsou jen ikony, statistiky a popisek Speed se skryjí a postranní panely se zúží, aby se celé prostředí vešlo.

## Vytvoření a ukládání efektu

- **New** nabízí prázdný GPU/CPU efekt, šablony Burst, Fountain, Smoke a skupinu **FPS Presets**. Kořenem nového efektu je `ParticleEffect3D`.
- **Open** načte efektovou scénu `.tscn`. Starší efekt s kořenem `Node3D` se při otevření převede na `ParticleEffect3D` (potomci, transformace a metadata zůstanou); změna se zapíše při dalším uložení. Scéna, jejímž kořenem je přímo `GPUParticles3D` nebo `CPUParticles3D`, se zachová.
- **Save** ukládá celý dokument včetně emitorů, načasování a navázaných zdrojů jako `.tscn`. Textury jsou samostatné PNG soubory, na které odkazuje materiál emitoru.
- **Add to Level** přidá instanci efektu do právě upravované Level scény přes Undo/Redo.

## Zásobník modulů

Každý emitor se upravuje jako svislý zásobník karet. Karty obsahují vybrané nativní vlastnosti `GPUParticles3D` a `ParticleProcessMaterial` (nebo `CPUParticles3D`) s Godotovými editory vlastností, včetně min/max posuvníků pro rozsahy. Každá změna jde přes Undo/Redo a hned se projeví v náhledu.

| Modul | Obsah |
| --- | --- |
| Emitter | Emitting, Max Particles, Lifetime, náhodnost životnosti, One Shot, Burst (explosiveness), Randomness, Prewarm, Speed Scale, Local Space, Fixed FPS |
| Spawn Shape | tvar emise; zobrazuje jen parametry zvoleného tvaru (poloměr, rozměr boxu, prstenec), offset a měřítko |
| Initial Velocity | směr, rozptyl, plochost, rychlost min/max, dědění rychlosti |
| Forces | gravitace, lineární, radiální a tangenciální zrychlení, odpor |
| Color over Life | základní barva, gradient přes život, náhodná barva, záře přes život, variace odstínu a karta Alpha over Life s presety |
| Size over Life | velikost min/max a karta Scale over Life s presety |
| Render | náhled textury, **Generate Texture...**, textura, tint, blend (Mix/Add…), shading, natočení (billboard), barva částic, soft particles, velikost quadu, řazení, zarovnání |

Karta Emitter obsahuje i **Start Delay** a **Emission Length** (stejné hodnoty jako tažení v časové ose). Moduly mají řádky „… over Life“ s křivkami (`CurveTexture`) pro radiální rychlost, limit rychlosti, zrychlení, odpor, odstín, velikost podle rychlosti, úhel, rotaci a rychlost sprite sheetu. Render nabízí **Mesh** pro mesh částice (např. úlomky).

Menu **⋮** v záhlaví každé karty nabízí presety modulu (např. Fountain, Explosion (Radial), Gentle Rise, Forward Cone; Earth Gravity, Buoyant Smoke, Zero Gravity; barvy Fire, Smoke, Sparks, Magic; velikost Grow, Shrink, Pop In and Out; Additive Glow, Soft Alpha, Lit by Scene), **Copy Module**, **Paste Module** (do stejného modulu jiného emitoru) a **Reset to Defaults**. Vše jde vrátit přes Undo.

Volitelné moduly přidá **+ Module** a odebere křížek v záhlaví karty: **Rotation**, **Sprite Sheet Animation** (rychlost, počáteční snímek, sloupce, řádky, smyčka), **Turbulence**, **Collision**, **Sub Emitter**, **Trails** (ribbon stopa, délka, šířka, segmenty, šířka podél stopy; materiál se přepne do trail módu) a **Attractors** (přidá `GPUParticlesAttractorSphere3D` k emitoru, tlačítkem Add Attractor další). Turbulence, Collision, Sub Emitter, Trails a Attractors jsou jen pro GPU emitory. Odebrání vrátí hodnoty modulu do vypnutého stavu. Pokud GPU emitor používá vlastní particle `VisualShader`, procesní moduly se skryjí a logika se upravuje v Particle Logic.

Emitor bez particle materiálu nabídne v modulu Render **Create Particle Material** (unshaded, billboard, barva z částic).

## Generátor textur v modulu Render

**Generate Texture...** otevře VFX Texture Generator pro vybraný emitor. Pokud jeho současná textura má vedle sebe recept `.vfx.cfg`, generátor ho načte a pokračuje v úpravách. **Apply to Emitter** texturu vykreslí v plném rozlišení a uloží ji vedle efektu do složky `textures/` (u neuloženého efektu do `res://vfx_textures/`) jako `<efekt>_<emitor>.png` spolu s receptem. Pak ji přiřadí materiálu emitoru: zachová blend, tint i soft particles, nastaví billboard a dělení sprite sheetu. Krok jde vrátit přes Undo.

Rychlost animace sprite sheetu odpovídá tomu, jak Godot počítá snímky (`anim_speed` 1 přehraje celý sheet jednou za život částice): sheet s životním cyklem (exploze) se přehraje jednou za život, ostatní sheety ve své délce Duration.

## Časová osa

Záložka **Timeline** zobrazuje stopu pro každý emitor a událost: plný pruh je doba aktivity (emise), slabší pokračování doba, kdy částice ještě dožívají. Nekonečná stopa končí symbolem ∞. Přerušovaná čára **End** označuje konec efektu, červený playhead aktuální čas. Pruhy emitorů ukazují počet částic a životnost.

- **Lišta:** Play/Pause, Restart, Loop, čas, přichytávání (Snap), zoom a **Fit**.
- **Zoom a posun:** Ctrl/Cmd + kolečko nebo gesto zoomuje kolem kurzoru, Shift + kolečko, prostřední tlačítko nebo trackpad posouvá.
- **Scrub:** kliknutí nebo tažení v pravítku.
- **Načasování:** tažením pruhu se mění start, tažením pravého okraje délka. Krok 0,05 s, Shift přichytávání dočasně vypne. Dvojklik na pruh vrátí délku na výchozí chování.
- **Oko** stopu ztlumí (skryje; ukládá se a respektuje ji i hra), **S** ji sólově přehraje jen v náhledu.
- **Pravé tlačítko:** Duplicate, Rename, Visible, Solo in Preview, Reset Timing, Delete.
- **Zkratky** (s fokusem na časové ose): mezerník Play/Pause, Home Restart, F Fit, Delete smazat, Ctrl/Cmd+D duplikovat, H ztlumit, S solo. Mezerník, Home a F fungují i v 3D náhledu.
- Duplikát emitoru dostane vlastní process material, mesh i materiál (textury zůstávají sdílené).
- Změny jdou přes Undo/Redo a náhled se řídí stejným načasováním jako hra. Délka smyčky náhledu je celá délka efektu.

## Události

**Add → Events** přidá k efektu časované události, které `ParticleEffect3D` přehrává spolu s emitory (v náhledu i ve hře):

| Událost | Uzel | Chování |
| --- | --- | --- |
| Light Flash | `OmniLight3D` (funguje i `SpotLight3D`) | intenzita sleduje křivku **Intensity over Time** během délky; délka 0 = trvale svítí; **Flicker** přidá mihotání |
| Sound | `AudioStreamPlayer3D` | přehraje se jednou ve svém čase; **Pitch Random** náhodně mění výšku |
| Camera Shake | `CameraShake3D` | otřese aktuální kamerou: náklon/otočení/převrácení a jemný posun (síla, délka, frekvence, max. rotace, max. posun, poloměr plné síly a útlumu) |
| Damage | `RadialDamage3D` | zraní vše, co má `take_damage()`: plné zranění do vnitřního poloměru, k vnějšímu klesá na nulu; krytí za překážkou zranění sníží nebo zastaví; fyzikální objekty odhodí |
| Decal | `Decal` | objeví se ve svém čase a během **Fade Out Part** zmizí; délka 0 = zůstane |

**Zvuk exploze:** preset Explosion má prázdnou stopu **Sound**. Vyber ji a v kartě zvol audio soubor, nebo přetáhni audio soubor z FileSystemu přímo na stopu v seznamu emitorů. Přetažení mimo existující Sound stopu přidá novou Sound událost se jménem souboru.

**Damage** volá na zasaženém uzlu (nebo jeho rodiči) metodu `take_damage` s tolika parametry, kolik jich přijímá: `(amount)`, `(amount, hit_position)` nebo `(amount, hit_position, hit_direction)`. Funguje tak pro hráče, NPC i předměty bez úprav kódu. Postava s více kolizemi nebo hitboxy dostane zranění jen jednou. Krytí: z místa výbuchu jde 9 paprsků na střed a rohy cíle (vrstvy **Cover Layers**). Plně schovaný cíl nedostane nic, částečně krytý poměrnou část (např. 4 z 9 paprsků = 44 %). V editoru a v náhledu se nikomu zranění nedává. Preset Explosion obsahuje Damage 90 (plné do 2 m, nula v 7 m).

Dosah Damage (červeně) a Camera Shake (modře) je vidět jako kruhy: vnitřní plná síla, vnější útlum. V náhledu je zapíná **View → Damage & Shake Radius** (výchozí zapnuto, vybraná událost svítí víc), v Level editoru jsou vidět jako gizmo.

Otřes kamery funguje v náhledu i ve hře bez úprav herního kódu. Engine ho přidává ke kameře jako samostatný posun pohledu (`Camera3D.add_shake_offset`), takže nepřepisuje rotaci, kterou nastavuje skript hráče, a sčítá se s pohupováním hlavy nebo zpětným rázem. **Strength** je 0–1 (trauma), plnou sílu má kamera do **Full Strength Radius**, k **Fade Out Radius** plynule slábne. Pokud efekt startuje současně s levelem a kamera ještě není aktivní, otřes na ni až 0,5 s počká. Signál `shake_started(intensity)` lze napojit na další odezvu ve hře (vibrace ovladače).

Události mají v zásobníku vlastní kartu (čas, délka a vlastnosti) a v časové ose žlutou stopu. Světla a decaly efekt animuje přes intenzitu a průhlednost, takže viditelnost dál slouží jako přepínač ztlumení. Presety Explosion, Muzzle Flash a Impact Sparks obsahují záblesk světla, Explosion i otřes kamery a Fire trvalé mihotavé světlo.

Načasování se ukládá jako metadata emitoru (`particle_effect_delay`, `particle_effect_duration`).

## FPS presety

Presety jsou v menu **Library**, v panelu knihovny a v **New → FPS Presets**, seskupené do kategorií. „Kolize“ znamená zapnutý modul Collision (ve hře se částice odrážejí od stěn levelu), „zvuk“ prázdnou stopu Sound pro vlastní soubor.

| Kategorie | Preset | Obsah |
| --- | --- | --- |
| Weapons | Muzzle Flash | záblesk, plamenné jazyky, jiskry a kouř (směr −Z) |
| Weapons | Bullet Tracer | jeden rychlý zářící pruh podél −Z (spawn u ústí hlavně) |
| Impacts | Bullet Impact (Dust) | prachový obláček, úlomky a jiskry s kolizí, decal díry v betonu (normála +Y) |
| Impacts | Bullet Impact (Wood) | hnědý prach, třísky s kolizí a decal díry ve dřevě (normála +Y) |
| Impacts | Impact Sparks (Metal) | jiskry s kolizí, krátká záře a decal díry v kovu (normála +Y) |
| Impacts | Blood Hit | krvavá mlha, kapky (směr +Z) a krvavý decal na zdi za zásahem (do 2 m) |
| Impacts | Glass Shatter | střepy, které se točí a odrážejí od podlahy (kolize), záblesky, zvuk; tabule čelem k +Z |
| Impacts | Water Splash | sloupec kapek, mlha a plochý kruh na hladině (+Y), zvuk |
| Explosions & Fire | Explosion | záblesk, ohnivá koule (sheet 8×8), jiskry s kolizí, rázová vlna, kouř, decal spáleniny (20 s), světlo, otřes, zvuk, zranění |
| Explosions & Fire | Fire | animované plameny (sheet 4×4), jiskry, kouř a mihotavé světlo |
| Explosions & Fire | Smoke Grenade | hustá kouřová clona na 8 s, která vyplní místnost a drží se od stěn (kolize), zvuk |
| Explosions & Fire | Signal Flare | červené jádro, jiskry s kolizí, červený kouř, mihotavé světlo, zvuk |
| Environment | Smoke Plume | nepřetržitý stoupající sloup kouře s turbulencí |
| Environment | Steam Leak | proud páry podél +Z s turbulencí, zvuk |
| Environment | Electric Sparks | opakované modré jiskry s kolizí, elektrický oblouk, blikající světlo, zvuk |
| Environment | Dust Puff (Landing) | plochý prstenec prachu po zemi (dopad, těžký krok) |
| Environment | Ambient Dust | pomalu poletující prach v prostoru 6 × 3 × 6 m |

Decaly v presetech promítají podél své osy −Y (u zásahů do povrchu s normálou +Y), díry po kulkách zůstanou 12 s, spálenina 20 s, krev 15 s a pak se rozplynou. Efekt žije, dokud decal nezmizí, takže se do zásobníku (pooling) vrátí až potom.

Presety si spočítají `visibility_aabb` podle rychlosti, gravitace a doby života (pád jen ve směru gravitace), místo jednotné krychle 16 m. Aditivní emitory se neřadí podle hloubky (`DRAW_ORDER_INDEX`), řazení platí jen pro průhledné.

Textury presetů vytvoří generátor do `res://vfx_textures/` a vedle nich uloží recepty. Existující soubor se znovu použije, takže presety sdílí např. `smoke_puff.png` a další vytvoření je rychlé. Vlastní úprava takové textury se projeví ve všech efektech, které ji používají.

## Spuštění efektu ze hry

Kořen efektu `ParticleEffect3D` je runtime uzel enginu:

- `autoplay` přehraje efekt při vstupu do scény ve spuštěné hře, `looping` konečný efekt opakuje, `auto_free` ho po dokončení uvolní.
- `play()`, `stop()` (nové částice přestanou vznikat, existující dožijí) a signál `finished`.
- `ParticleEffect3D.spawn(scene, parent, transform)` vytvoří, umístí a přehraje jednorázový efekt a po dokončení ho uvolní:

```gdscript
const IMPACT = preload("res://effects/bullet_impact.tscn")

func _on_hit(position: Vector3, normal: Vector3) -> void:
    # Presety dopadu míří podél +Y; otoč +Y do normály povrchu.
    var basis := Basis(Quaternion(Vector3.UP, normal.normalized()))
    ParticleEffect3D.spawn(IMPACT, get_tree().current_scene, Transform3D(basis, position))
```


### Recyklace efektů (pooling)

`ParticleEffect3D.spawn()` dohrané efekty neruší, ale vrací je do zásobníku podle scény a další `spawn()` stejné scény je použije znovu (nejvýš 64 čekajících na scénu). Rychlá palba tak nevytváří novou instanci scény a nové buffery částic pro každý výstřel. Na efektu Explosion je znovupoužití ~0,09 ms oproti ~0,14 ms za nový efekt (bez GPU části, kterou headless test nezměří).

- `ParticleEffect3D.prewarm(IMPACT, get_tree().current_scene, 16)` při načtení levelu vytvoří efekty dopředu, takže ani první výstřely nic nevytvářejí.
- Připojení na signál `finished` platí jen pro jedno použití; při návratu do zásobníku se odpojí (kromě `CONNECT_PERSIST`).
- `ParticleEffect3D.clear_pool()` zásobník uvolní (např. při změně levelu; efekty pod uvolněnou scénou zmizí s ní).

### Tabulka zásahů podle materiálu (`ParticleImpactTable`)

Resource, který přiřadí povrchu efekt: v Inspectoru slovník `effects` (název povrchu → scéna efektu) a `default_effect` pro ostatní povrchy. Zbraň pak jen předá výsledek raycastu:

```gdscript
@export var impacts: ParticleImpactTable

func _fire(query: PhysicsRayQueryParameters3D) -> void:
    var hit := get_world_3d().direct_space_state.intersect_ray(query)
    if hit:
        impacts.spawn_from_hit(get_tree().current_scene, hit)
```

Povrch se hledá v tomto pořadí: metadata `surface` na kolideru nebo na jednom z jeho 3 nejbližších předků → metadata `surface` na jeho `PhysicsMaterial`, jinak název (`resource_name`) materiálu → skupina uzlu pojmenovaná jako klíč tabulky → `default_effect`. Velikost písmen nerozhoduje. Efekt míří +Y do normály povrchu (tak jsou postavené presety zásahů), `random_roll` ho náhodně pootočí kolem normály a `surface_offset` ho odsune od povrchu.

### Rozpočet a vzdálenost

- **Project Settings → `rendering/particle_effects/max_active_effects`** (výchozí 96, 0 = bez limitu): kolik efektů hraje zároveň. Když je plno, nový efekt ukončí nejstarší hrající efekt s nejnižší prioritou, která není vyšší než jeho vlastní (`priority`). Mají-li všechny hrající efekty vyšší prioritu, nový se nepřehraje.
- **`cull_distance`** na efektu (0 = vždy): dál od kamery se efekt nepřehraje. Nepřehraný efekt vyšle `finished` v dalším snímku a vrátí se do zásobníku. Smyčkový efekt kontroluje vzdálenost každou půl sekundu a rozjede se, až se kamera přiblíží.
- Efekty s událostí poškození (`RadialDamage3D`) hrají vždy, protože jde o hratelnost.
- `ParticleEffect3D.get_active_effect_count()` vrátí počet hrajících efektů.

### Příprava levelu (`ParticleLevelSetup`)

Uzel, který se vloží do levelu. Při startu hry (ne v editoru) napeče kolizi částic celého levelu (`prepare_collision`, `collision_mask`) a naplní zásobníky efektů z `prewarm_effects` a ze všech efektů přiřazené `impact_table` (`prewarm_count` instancí na scénu, `effects_per_frame` scén za snímek). Po naplnění vyšle `setup_finished`.

### Efekty připojené ke zbrani

`ParticleEffect3D.spawn_attached(MUZZLE, $Muzzle, Transform3D())` přehraje efekt jako potomka uzlu (např. `Marker3D` na hlavni) s lokální transformací a zapne `local_space`, takže emitory simulují v prostoru efektu (`local_coords`) a záblesk i jiskry se při otáčení kamery neopožďují. `local_space` jde zapnout i ručně na kořeni efektu.

### Barva a velikost při spuštění

Jeden efekt může posloužit pro víc situací: `ParticleEffect3D.spawn(scene, parent, transform, tint, scale)` (a stejně `spawn_attached`) přehraje efekt s barvou a velikostí jen pro toto přehrání.

```gdscript
ParticleEffect3D.spawn(BLOOD, level, xform, Color(0.9, 0.85, 0.2))   # žlutá krev
ParticleEffect3D.spawn(EXPLOSION, level, xform, Color.WHITE, 2.0)     # velký výbuch (barel)
```

- **`tint`** násobí barvu emitorů (`color` procesního materiálu nebo CPU emitoru, tedy i barevné přechody), `light_color` světel a barvu decalů. Průhlednost animují události, ta se nemění.
- **`effect_scale`** násobí velikost částic, počáteční rychlost, gravitaci a zrychlení, rozměry tvaru vypouštění, `visibility_aabb` a velikost kolize, dosah světel, velikost decalů a poloměry `CameraShake3D` a `RadialDamage3D`. Životnost a časování zůstávají.
- Stejné vlastnosti `tint` a `effect_scale` má i kořen efektu; argumenty `spawn()` je pro dané přehrání násobí.
- Hodnoty se nastaví v `play()` jen ve spuštěné hře, a to na lokálních kopiích procesních materiálů, takže sdílené materiály ani ostatní instance se nezmění. Původní hodnoty si uzly pamatují a efekt z poolu přehraný znovu bez argumentů vypadá jako autorský. Editor ukazuje vždy autorský vzhled.
- `ParticleImpactTable` má volitelné slovníky `tints` a `scales` (povrch → barva / násobek), např. jeden efekt krve se žlutou barvou pro povrch `insect`; `spawn()` a `spawn_from_hit()` berou navíc vlastní `tint` a `scale`.

## Particle Logic

**Convert to Shader Code** převede moduly vybraného GPU emitoru na ekvivalentní particle shader (`ShaderMaterial`, vzhled zůstane stejný) a otevře jeho kód přímo v záložce **Particle Logic** (stejný editor kódu jako Godotův Shader Editor, se zvýrazněním a kontrolou chyb). Změny kódu se projeví v náhledu hned. Převod jde vrátit přes Undo; pokud renderer kód neposkytne, emitor zůstane beze změny.

Tlačítko **Create Node Graph** změní procesní materiál vybraného GPU emitoru na nativní `VisualShader` v režimu `particles` a zobrazí jeho graf v záložce **Particle Logic**. Nahrazení materiálu se nejprve potvrdí a jde vrátit přes Undo. Graf se upravuje přímo v této záložce (bez otevírání Godotova Shader Editoru); pro víc místa použij maximalizaci nebo samostatné okno. `VisualShader` je výpočet jednoho emitoru, neřídí hierarchii ani načasování efektu. CPU particles shaderový graf nemají.

## Kolize částic se stěnami

GPU částice samy o sobě nenarážejí na fyzikální tělesa, jen na kolizní uzly částic (`GPUParticlesCollision*`). `ParticleEffect3D` to ve hře řeší automaticky: když se přehraje, najde statickou fyzikální kolizi kolem sebe (`StaticBody3D`, včetně kolizí UniBuilderu, a CSG) a na pozadí z ní napeče dočasný `GPUParticlesCollisionSDF3D`. Žádné kolizní boxy do místností přidávat nemusíš.

1. Emitoru, který se má odrážet (např. **Smoke**), zapni modul **Collision** (+ Module → Collision). Kolize platí jen pro emitory s tímto modulem. **Mode** Rigid, **Bounce** u kouře nízko (0–0,2), **Friction** 0,1–0,5. **Particle Size** (`collision_base_size`) je poloměr kolizní koule: částice se zastaví právě v této vzdálenosti od povrchu. Při zapnutí modulu se nastaví na čtvrtinu velikosti vykreslené částice (mesh × průměrné Scale), např. jiskra 0,18 m → 0,05 m, obláček kouře 2,4 m → 0,6 m. Když je hodnota moc velká (částice „visí ve vzduchu“) nebo moc malá (zapadají do stěn), karta to oranžově hlásí a tlačítko **Fit Particle Size** ji srovná.
2. Odraz vyladíš v náhledu přes **View → Collision Test Walls**.
3. Ve hře to funguje samo. Kořen efektu má skupinu **Level Collision**: `collide_with_level` (výchozí zapnuto) a `level_collision_mask` (fyzikální vrstvy stěn, výchozí 1).

Jak to funguje:
- **Sdílená kolize celého levelu.** První efekt s kolizí ve hře spustí na pozadí pečení celého levelu (všechna StaticBody3D a CSG s kolizí ve vrstvách `level_collision_mask`) do dlaždic 20 m (jádro 16 m + 2 m překryv, 128³, buňka ~0,16 m, takže i podlaha nebo stěna silná 0,2 m má uvnitř aspoň jednu buňku). Dlaždice jsou uzly `LevelParticleCollision/Tile` pod scénou levelu a platí pro všechny efekty najednou. Jakmile jsou hotové (testovací level ~230 ms na pozadí), další efekty, včetně zásahů kulek, žádnou vlastní kolizi nepočítají (~0,03 ms na efekt). Aby byla hotová už před první střelbou, zavolej při načtení levelu `ParticleEffect3D.prepare_level_collision(level_root)`.
- **Dokud se level peče**, efekt si spočítá vlastní kolizi jen pro svou oblast (nejvýš 20 m kolem efektu, rozlišení podle velikosti, ~10–20 ms), takže platí i pro úplně první výbuch. Efekt přehraný znovu na stejném místě ji vezme z cache.
- Hráč, postavy a RigidBody3D se nepečou. Když se geometrie levelu změní za běhu, zavolej `ParticleEffect3D.clear_level_collision_cache()` (další efekt level napeče znovu).

Kolize platí pro každý GPU emitor, který ji používá: modul Collision (`ParticleProcessMaterial.collision_mode`) i vlastní shader nebo Visual Shader, který čte `COLLIDED` (např. po Convert to Shader Code). CPUParticles3D kolize v Godotu neumí.

Pevná slupka za plochou se počítá jen od plochy nejbližší dané buňce SDF. Dřív měla slupku každá plocha zvlášť a slupka spodní strany tenké podlahy (nebo vnější strany tenké stěny) prorazila do místnosti, takže úlomky zůstávaly viset ve vzduchu nad podlahou a před stěnami. Geometrie tenčí než jedna buňka (u dlaždic levelu ~0,16 m) nemá uvnitř žádnou buňku a částice jí mohou projít.

Reakce na kolizi (Collision Rigid) brzdí jen částice, které se do povrchu pohybují. V původním Godotu přišla částice, která se povrchu jen dotýkala, o rychlost i tehdy, když od něj odlétala, a tření ji brzdilo v každém kroku. Úlomky, které vznikají přímo na stěně, proto pomalu klouzaly po stěně dolů jako papír.

Rychlé částice (jiskry) by mezi dvěma kroky simulace přeskočily tenkou stěnu. Efekt proto emitorům s kolizí zvýší `fixed_fps` tak, aby se částice za krok posunula méně než o tloušťku kolize plus svou velikost (nejvýš 240 kroků za sekundu); totéž dělá náhled. Rychlost se odhaduje z Initial Velocity, gravitace a zrychlení.

**Náhled v Level editoru:** když v levelu vybereš uzel `ParticleEffect3D`, v liště 3D viewportu se objeví **Preview Effect** (▶) a Stop. Efekt se přehraje přímo v levelu včetně světel, zvuku a otřesu a částice narážejí do geometrie levelu přes stejné sdílené dlaždice jako ve hře. Dlaždice se přepečou jen tehdy, když se změnila statická kolize. Náhled scénu nemění: po dohrání, po Stop i před uložením vrátí energii světel, průhlednost decalů, `fixed_fps` a `emitting` emitorů. Zranění se v editoru nepočítá.

Volitelně: tlačítko **Add Collision to Level** v kartě Collision přidá do levelu trvalý uzel `ParticleCollision` napečený z meshů, trvalou kolizi uloženou ve scéně (např. pro samostatné GPUParticles3D mimo `ParticleEffect3D`); pro efekty už není potřeba. Data se uloží vedle levelu (`<level>.ParticleCollision_data.exr`), level pak ulož.

## 3D náhled a simulace

Náhled simuluje izolovanou kopii celé hierarchie efektu ve vlastním `SubViewport` s vlastním prostředím, světly, kamerou a mřížkou. Chybějící draw mesh nebo process material doplní pouze náhledová kopie.

- Levé nebo pravé tažení myší otáčí kameru, prostřední tažení posouvá a kolečko přibližuje. **Reset View** (nebo klávesa F) efekt znovu orámuje. Rámuje se podle toho, kde částice opravdu jsou (oblast emise, kužel Direction/Spread, dolet průměrnou rychlostí za ~0,3 s, gravitace a velikost částice), ne podle Visibility AABB, který je kvůli ořezávání mnohem větší. Zásah kulky je tak ~2 m od kamery místo ~13 m.
- Menu **View**: prostředí **Studio**, **Daylight**, **Night**, **Interior** (pozadí a světla pro kontrolu efektu v různých podmínkách) a přepínače **Grid**, **Glow (Bloom)**, **Emitter Bounds** (žlutý obrys `visibility_aabb` vybraného emitoru), **Floor** (viditelná podlaha, výchozí vypnuto), **Damage & Shake Radius** (kruhy dosahu, výchozí zapnuto), **Sound** a **Camera Shake** (přehrávání zvuku a otřes kamery v náhledu, výchozí zapnuto; vypnutí platí jen pro náhled, efekt se nemění) a **Collision Test Walls** (kolizní podlaha a tři průhledné stěny pro ladění modulu Collision).
- Statistiky v transportu: viditelné stopy, maximum částic, draw cally a trojúhelníky náhledu.
- **Play/Pause**, **Restart**, časový posuvník, **Speed** a **Loop** ovládají simulaci. Posun na jiný čas restartuje efekt a přehraje ho dopředu (`ParticleEffect3D.seek`); jde o přiblížení, ne přesnou rekonstrukci stavu.
- Změna načasování na časové ose zachová polohu kamery.

## Současné hranice a další práce

- Šířky hlavních panelů se ukládají do rozvržení editoru.
- Nové UI (zásobník modulů, časová osa 2.0, události, presety modulů, náhled 2.0) je ověřeno headless testy v dočasném projektu, ne zatím ručně v GUI. Převod na shader kód headless renderer neumí (vrací prázdný kód); v GUI je potřeba ho ověřit ručně.
- Náhled kopíruje podstrom efektu; kolizní nebo attractor uzly mimo efekt se do simulace nepřidají.
- Presety dopadu míří podél +Y (normála povrchu), krev podél +Z a muzzle flash podél −Z.

## VFX Texture Generator

Generátor je samostatný modul `engine/editor/fps_studio/vfx_texture_generator.cpp` bez závislosti na editoru. Plugin ho obaluje do `Image` a vykresluje po řádcích přes `WorkerThreadPool`. `SCsub` tento jediný soubor kompiluje vždy s `-O3`, protože vývojový build jinak používá `-O0` a generátor by byl 6–8× pomalejší. Náhled se počítá asynchronně: editor spustí skupinu řádků a v `NOTIFICATION_PROCESS` jen zjišťuje, zda je hotová; mezitím čeká pouze nejnovější požadavek. Při přehrávání se rozlišení náhledu přizpůsobuje času výpočtu (128–256 px), po zastavení nebo úpravě hodnoty se nejprve ukáže draft a poté se překreslí v plné kvalitě až do 512 px. Vypnutý post-processing se přeskočí. Stejný zdroj lze přeložit i mimo engine jako malý nástroj a referenční obrázky tak lze vykreslit bez GUI.

### Vykreslování

- **Fire, Smoke, Cloud, Explosion a Explosion Ring** jsou objemová média. Paprsek prochází 3D polem hustoty a teploty. Útlum je počítán podle Beer–Lambertova zákona, emise podle teploty (rampa saze → barva A → barva B → bílé jádro), rozptyl s krátkým stínovým paprskem ke klíčovému světlu a výsledek prochází exponenciálním tone mapperem. Tvar vytváří vlastní 3D gradientní šum, billow turbulence (zaoblené „květákové“ chuchvalce) a domain warping. Doména šumu je pootočená mimo osy mřížky, aby obrysy nepůsobily hranatě.
- Exploze mají parametr **Life cycle**. Ve sprite sheetu exploze během animace expanduje, chladne a mění se v kouř; u samostatné textury tento průběh řídí posuvník času.
- Energetické efekty (Spark, Lightning, Electric Burst, Muzzle Flash / Flare, Energy Ring, Shockwave, Magic Circle, Bokeh, Plasma Sun) se počítají analyticky jako HDR emise a prochází stejným tone mapperem. Proto mají měkký fotografický přechod místo plochých barevných pásů. Blesky jsou fraktální polyline z midpoint displacement s větvemi.
- Caustics a Noise jsou vzory podporující skutečně bezešvé dlaždicování (periodický šum a periodické Voronoi buňky).
- Emisivní světlo zvyšuje alfu, takže textura s běžným alpha blendingem vypadá na černém pozadí stejně jako aditivní výsledek.
- **Debris Chunk** (kategorie Shapes) je osvětlený úlomek kamene, betonu nebo cihly: nepravidelný mnohoúhelník se zkosenými hranami, hřbetem, který rozdělí horní plochu na dvě fasety, a zrnitým povrchem. Ve sprite sheetu je každý snímek jiný úlomek; preset Bullet Impact používá list 4×4 `debris_chunk.png` s náhodným snímkem pro každou částici (Anim Offset 0–1, Anim Speed 0). Barvu úlomků mění barva částic (Color).
- **Soft Smoke (Particles)** je rychlý 2D typ pro kouř z částic: nemá ostrou siluetu, průhlednost plynule slábne od středu, šum ho trhá na cáry a **Inner billows** přidávají osvětlené chuchvalce uvnitř, takže nepůsobí jako rozmazaná skvrna. Parametr **Life cycle** ve sprite sheetu udělá flipbook přes život částice (obláček mírně naroste, rozpadne se na cáry a zřídne), jak to dělají kouřové flipbooky v UE5. Objemový typ Smoke má tvrdý okraj a silné vlastní stíny, proto se na částice hodí méně.
- Kouř v presetech je nastavený podle Source (HL2) a UE5 Niagara (`make_soft_smoke` v `particle_presets.cpp`):
  - sdílený flipbook `smoke_soft_life.png` 4×4 (1024 px) přehrávaný jednou přes život částice, neutrální barva obarvená barvou částice,
  - osvětlení scénou per-vertex (levné), takže kouř v tmavém levelu nesvítí; kdo chce svítící kouř, přepne v modulu Render stínování na Unshaded,
  - různé velikosti (Scale min/max široko), náhodná rotace s pomalým otáčením, náhodná délka života (±30 %), start menší a růst (Scale over Life),
  - u výbuchu: postupné vypouštění (Burst 0,55), rozlet do stran se silným odporem jen na začátku života (**křivka Damping**), potom vztlak. Pozor: v Godotu odpor brzdí i gravitaci/vztlak, takže kouř s odporem větším než vztlak nikdy nestoupne; proto křivka odporu, která po 30 % života klesne na nulu.
  - méně a větších částic, protože hlavní cenou kouře je překreslování průhledných ploch (overdraw).
- **Decals** (Bullet Hole, Scorch / Burn Mark, Glass Crack, Splatter) jsou neemisivní textury s barvou a alfou pro uzly `Decal`; mimo stopu je alfa 0.
  - *Bullet Hole*: tmavá díra se stínovanou stěnou, odštípnutý povrch, radiální praskliny a saze. Parametr **Surface**: 0 beton/omítka, 1 kov (ohnuté osvětlené lístky kolem díry, bez prasklin), 2 dřevo (odštěpek protažený podél vláken, třísky). Barva B je barva povrchu (beton šedá, dřevo hnědá).
  - *Scorch / Burn Mark*: tmavé saze přecházející do spáleného okraje, roztřepený obrys, radiální pruhy výbuchu a ohořelý kruh. **Blast streaks = 0** dá spáleninu po ohni bez pruhů.
  - *Glass Crack*: pavučina radiálních a kruhových prasklin, rozdrcená matná zóna a průhledná díra.
  - *Splatter*: krev, olej nebo barva; roztřepená skvrna, kapky a protažené kapky ve směru +X (otoč přes Rotation).
- Kód je napsán nově. Neobsahuje ani nepřekládá shadery EffectTextureMakeru ani shadery ze Shadertoy; EffectTextureMaker slouží pouze jako vizuální reference.

### Ovládání

- **Levý panel** obsahuje galerii typů s náhledy, seskupenou do kategorií Fire & Explosions, Smoke & Clouds, Energy & Light, Shapes, Noise & Patterns a Decals.
- **Střed** ukazuje čtvercový náhled s volbou pozadí (Checker, Black, Dark, Gray, White), přepínač **Show Sheet** pro celý sprite sheet a transport (Play/Pause, časová osa, čas a číslo snímku). Při přehrávání a tažení posuvníků se náhled počítá v rychlé draft kvalitě; po pozastavení se překreslí v plné kvalitě.
- **Pravý inspektor** má sbalovací sekce:
  - *Effect* — parametry specifické pro vybraný typ (např. u exploze Radius, Billows, Billow size, Detail, Fire, Smoke, Density, Light angle, Life cycle), Seed s tlačítkem Random a Reset to Defaults.
  - *Color & Tone* — dvě barvy s popisky podle typu (např. Flame color / Hot color, Shadow color / Lit color), Exposure a Opacity.
  - *Motion & Mapping* — Speed, Rotation, Polar Coordinates a Seamless Tiling (jen pro vzory).
  - *Output* — Resolution do 2048 px, Layout (samostatná textura, sheet 4×4 nebo 8×8), Frame rate a Duration.
  - *Post-processing* — Toon shading, prahování a rozmazání alfy, náhled alfa masky, export normal mapy a color balance.
- **Spodní lišta**: Load Recipe a Save Recipe vlevo, stav uprostřed, vpravo Close, Export PNG a Apply to Emitter. Apply to Emitter je aktivní až po exportu, protože emitor uchovává odkaz na projektový PNG. Nastaví `StandardMaterial3D`, `BILLBOARD_PARTICLES`, dělení atlasu a rychlost animace podle Frame rate. U vybrané stopy **Decal** nastaví přímo její texturu (`texture_albedo`), se zapnutým exportem normal mapy její `texture_normal`. Karta Decal má tlačítko **Generate Decal Texture...**, které otevře generátor na kategorii Decals.
- Recepty `.cfg` verze 3 ukládají typ, globální volby, postprocesy a parametry typu podle stabilních klíčů v sekci `[params]`. Recepty verze 1 a 2 se načtou se stejným typem, barvami, rychlostí a rotací; ostatní parametry dostanou výchozí hodnoty nového generátoru.

## Implementace

Workspace implementuje `FPSStudioParticleEditorPlugin` v `engine/editor/fps_studio/particle_editor_plugin.cpp`. Používá nativní uzly a resources Godotu, `EditorInspector`, vlastní náhledovou kopii a vestavěný VisualShader editor. Součástí zdokumentované designové reference je [particle_workspace_concept.png](images/particle_workspace_concept.png).
