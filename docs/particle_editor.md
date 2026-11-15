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
- **Menu Library** v horní liště vytvoří efekt z **FPS presetu** nebo znovu otevře nedávný efekt. Vyhledávací panel **Effect Library** pod seznamem stop je ve výchozím stavu skrytý, aby zbylo místo; zapne se v Library → Show Library Panel nebo v Layout.
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

**Damage** volá na zasaženém uzlu (nebo jeho rodiči) metodu `take_damage` s tolika parametry, kolik jich přijímá: `(amount)`, `(amount, hit_position)` nebo `(amount, hit_position, hit_direction)`. Funguje tak pro hráče, NPC i předměty bez úprav kódu. Postava s více kolizemi nebo hitboxy dostane zranění jen jednou. Krytí: z místa výbuchu jde 9 paprsků na střed a rohy cíle (vrstvy **Cover Layers**). Plně schovaný cíl nedostane nic, částečně krytý poměrnou část (např. 4 z 9 paprsků = 44 %). V editoru a v náhledu se nikomu zranění nedává. Preset Explosion obsahuje Damage 90 (plné do 2 m, nula v 7 m).

Dosah Damage (červeně) a Camera Shake (modře) je vidět jako kruhy: vnitřní plná síla, vnější útlum. V náhledu je zapíná **View → Damage & Shake Radius** (výchozí zapnuto, vybraná událost svítí víc), v Level editoru jsou vidět jako gizmo.

Otřes kamery funguje v náhledu i ve hře bez úprav herního kódu. Engine ho přidává ke kameře jako samostatný posun pohledu (`Camera3D.add_shake_offset`), takže nepřepisuje rotaci, kterou nastavuje skript hráče, a sčítá se s pohupováním hlavy nebo zpětným rázem. **Strength** je 0–1 (trauma), plnou sílu má kamera do **Full Strength Radius**, k **Fade Out Radius** plynule slábne. Pokud efekt startuje současně s levelem a kamera ještě není aktivní, otřes na ni až 0,5 s počká. Signál `shake_started(intensity)` lze napojit na další odezvu ve hře (vibrace ovladače).

Události mají v zásobníku vlastní kartu (čas, délka a vlastnosti) a v časové ose žlutou stopu. Světla a decaly efekt animuje přes intenzitu a průhlednost, takže viditelnost dál slouží jako přepínač ztlumení. Presety Explosion, Muzzle Flash a Impact Sparks obsahují záblesk světla, Explosion i otřes kamery a Fire trvalé mihotavé světlo.

Načasování se ukládá jako metadata emitoru (`particle_effect_delay`, `particle_effect_duration`).

## FPS presety

| Preset | Emitory |
| --- | --- |
| Muzzle Flash | záblesk, plamenné jazyky, jiskry a kouř (směr −Z) |
| Explosion | záblesk, objemová ohnivá koule jako sprite sheet 8×8 s životním cyklem, jiskry, rázová vlna, zpožděný kouř |
| Bullet Impact (Dust) | prachový obláček, úlomky a několik jisker (normála +Y) |
| Impact Sparks (Metal) | jiskry s krátkou září (normála +Y) |
| Blood Hit | krvavá mlha a kapky (směr +Z) |
| Smoke Plume | nepřetržitý stoupající sloup kouře s turbulencí |
| Fire | animované plameny (sheet 4×4), jiskry a kouř |

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

## Particle Logic

**Convert to Shader Code** převede moduly vybraného GPU emitoru na ekvivalentní particle shader (`ShaderMaterial`, vzhled zůstane stejný) a otevře jeho kód přímo v záložce **Particle Logic** (stejný editor kódu jako Godotův Shader Editor, se zvýrazněním a kontrolou chyb). Změny kódu se projeví v náhledu hned. Převod jde vrátit přes Undo; pokud renderer kód neposkytne, emitor zůstane beze změny.

Tlačítko **Create Node Graph** změní procesní materiál vybraného GPU emitoru na nativní `VisualShader` v režimu `particles` a zobrazí jeho graf v záložce **Particle Logic**. Nahrazení materiálu se nejprve potvrdí a jde vrátit přes Undo. Graf se upravuje přímo v této záložce (bez otevírání Godotova Shader Editoru); pro víc místa použij maximalizaci nebo samostatné okno. `VisualShader` je výpočet jednoho emitoru, neřídí hierarchii ani načasování efektu. CPU particles shaderový graf nemají.

## 3D náhled a simulace

Náhled simuluje izolovanou kopii celé hierarchie efektu ve vlastním `SubViewport` s vlastním prostředím, světly, kamerou a mřížkou. Chybějící draw mesh nebo process material doplní pouze náhledová kopie.

- Levé nebo pravé tažení myší otáčí kameru, prostřední tažení posouvá a kolečko přibližuje. **Reset View** efekt znovu orámuje.
- Menu **View**: prostředí **Studio**, **Daylight**, **Night**, **Interior** (pozadí a světla pro kontrolu efektu v různých podmínkách) a přepínače **Grid**, **Glow (Bloom)**, **Emitter Bounds** (žlutý obrys `visibility_aabb` vybraného emitoru), **Floor** (viditelná podlaha, výchozí vypnuto), **Damage & Shake Radius** (kruhy dosahu, výchozí zapnuto) a **Floor Collision** (kolizní podlaha pro modul Collision).
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
- Kód je napsán nově. Neobsahuje ani nepřekládá shadery EffectTextureMakeru ani shadery ze Shadertoy; EffectTextureMaker slouží pouze jako vizuální reference.

### Ovládání

- **Levý panel** obsahuje galerii typů s náhledy, seskupenou do kategorií Fire & Explosions, Smoke & Clouds, Energy & Light, Shapes a Noise & Patterns.
- **Střed** ukazuje čtvercový náhled s volbou pozadí (Checker, Black, Dark, Gray, White), přepínač **Show Sheet** pro celý sprite sheet a transport (Play/Pause, časová osa, čas a číslo snímku). Při přehrávání a tažení posuvníků se náhled počítá v rychlé draft kvalitě; po pozastavení se překreslí v plné kvalitě.
- **Pravý inspektor** má sbalovací sekce:
  - *Effect* — parametry specifické pro vybraný typ (např. u exploze Radius, Billows, Billow size, Detail, Fire, Smoke, Density, Light angle, Life cycle), Seed s tlačítkem Random a Reset to Defaults.
  - *Color & Tone* — dvě barvy s popisky podle typu (např. Flame color / Hot color, Shadow color / Lit color), Exposure a Opacity.
  - *Motion & Mapping* — Speed, Rotation, Polar Coordinates a Seamless Tiling (jen pro vzory).
  - *Output* — Resolution do 2048 px, Layout (samostatná textura, sheet 4×4 nebo 8×8), Frame rate a Duration.
  - *Post-processing* — Toon shading, prahování a rozmazání alfy, náhled alfa masky, export normal mapy a color balance.
- **Spodní lišta**: Load Recipe a Save Recipe vlevo, stav uprostřed, vpravo Close, Export PNG a Apply to Emitter. Apply to Emitter je aktivní až po exportu, protože emitor uchovává odkaz na projektový PNG. Nastaví `StandardMaterial3D`, `BILLBOARD_PARTICLES`, dělení atlasu a rychlost animace podle Frame rate.
- Recepty `.cfg` verze 3 ukládají typ, globální volby, postprocesy a parametry typu podle stabilních klíčů v sekci `[params]`. Recepty verze 1 a 2 se načtou se stejným typem, barvami, rychlostí a rotací; ostatní parametry dostanou výchozí hodnoty nového generátoru.

## Implementace

Workspace implementuje `FPSStudioParticleEditorPlugin` v `engine/editor/fps_studio/particle_editor_plugin.cpp`. Používá nativní uzly a resources Godotu, `EditorInspector`, vlastní náhledovou kopii a vestavěný VisualShader editor. Součástí zdokumentované designové reference je [particle_workspace_concept.png](images/particle_workspace_concept.png).
