# Pracovní prostředí Particle

Particle workspace je samostatný editor opakovaně použitelných 3D efektů. Efekt upravuje mimo aktivní Level a do herní scény se přidává až příkazem **Add to Level**.

![Konceptuální podoba Particle workspace](images/particle_workspace_concept.png)

Render je vizuální reference pro rozložení UI. Levá část představuje knihovnu efektů a strom uzlů efektu, střed celý simulovaný efekt a pravý panel vlastnosti právě vybraného emitoru. Spodní graf je nativní Godot `VisualShader` v režimu Particle; není to graf vztahů mezi emitory. Vybraný GPU emitor lze upravovat přímo ve vloženém shaderovém editoru.

Při návrhu práce s efekty zohledňujeme také [tutoriál ohně a jisker od Le Lu](https://www.youtube.com/watch?v=Y1ZBzIiP-v4) a [EffectTextureMaker od Mebiusbox](https://mebiusbox.github.io/contents/EffectTextureMaker/). Oheň, kouř a jiskry se skládají z více emitterů; sprite a další VFX textury jsou samostatné obrazové zdroje. Z Particle workspace je nyní dostupný první nativní generátor: vytváří průhledné textury, ukládá je jako PNG a umí je přiřadit vybranému emitoru. Efektová scéna pak uchovává běžný odkaz na PNG asset.

## Vytvoření a ukládání efektu

- **New** vytvoří prázdný dokument efektu s kořenovým `Node3D` a připraveným GPU nebo CPU emitorem. Nabízí také předvolby Burst, Fountain a Smoke.
- **Open** načte efektovou scénu `.tscn` do Particle workspace. Panel **Effects** zobrazuje nedávno otevřené efekty a umí je filtrovat.
- **Save** nebo **Save As** ukládá celý dokument, včetně více emitorů, jejich potomků a navázaných zdrojů, jako `.tscn`. Běžné materiály a křivky zůstávají nativní Godot resources. Particle `VisualShader` je součástí zdrojů emitoru a lze ho také uložit samostatně jako `.tres`. Obrázková textura se ukládá samostatně (typicky `.png`) a emitter na ni odkazuje přes renderer materiál.
- Název rootu efektu změníš dvojklikem na jeho titulek v horním záhlaví. Dialog upraví název uzlu podle pravidel Godotu a změnu můžeš vrátit přes Undo/Redo.
- Starší scéna, jejímž kořenem je přímo `GPUParticles3D` nebo `CPUParticles3D`, se při otevření zachová jako efekt. Nový dokument používá `Node3D` kořen.
- **Add to Level** přidá instanci celého otevřeného efektu do právě upravované Level scény přes editorové Undo/Redo.

Neexistuje žádný pevně vybraný `BubbleParticles`. Při otevření workspace je efektový dokument prázdný; obsah aktuální Level scény se do něj automaticky nepřenáší.

## Emitory a vlastnosti

Levý panel rozděluje nedávné soubory efektů a strom emitorů. **Add** přidá GPU/CPU emitter nebo předvolbu, **Remove** odstraní vybraný uzel. Dvojklikem na název v hierarchii emitter přejmenuješ; změna podporuje Undo/Redo a při duplicitním názvu dostane uzel volný název. Efekt může obsahovat více particle uzlů; výběr v hierarchii určuje, který emitor se upravuje v pravých kartách **Emitter**, **Process** a **Renderer**.

Vlastnosti upravují Godotovy vestavěné `EditorInspector` panely. Pokud GPU emitor používá `ParticleProcessMaterial`, karta Process zobrazuje křivky Alpha over Life a Scale over Life ještě před úplným inspektorem, aby byly rychle dostupné. **Presets** nabízí pro Alpha Fade Out (Linear/Smooth), Fade In, Fade In and Out a Hold then Fade; pro Scale Grow, Shrink, Grow and Shrink, Hold then Grow a Steady. Preset vytvoří křivku, pokud zatím neexistuje. Křivky jsou nativní `CurveTexture` resources a změny presetů i bodů lze vrátit přes Undo/Redo. Ostatní vlastnosti zůstávají dostupné v kompletním inspektoru.

Tlačítko **Create Node Graph** změní procesní materiál vybraného GPU emitoru na nativní `VisualShader` v režimu `particles` a zobrazí jeho graf ve spodním panelu **Particle Logic**. Nový graf obsahuje připojený SphereEmitter pro tvar spawnu a ConeVelocity pro počáteční rychlost; při otevření se přizpůsobí dostupné ploše. Náhled shaderu je ve vloženém panelu skrytý, aby graf dostal více místa. Panel Particle Logic se bez vybraného emitoru skryje. Nahrazení existujícího materiálu se nejprve potvrdí a změnu lze vrátit přes Undo. Pokud emitor už particle `VisualShader` používá, spodní panel zobrazuje jeho graf a tlačítko **Open in Shader Editor** jej navíc otevře ve velkém Shader Editoru. Výběr jiného GPU emitoru přepne vložený graf na jeho materiál. CPU particles nemají particle shader graph.

`VisualShader` je graf výpočtu shaderu pro vybraný GPU emitor. Neřídí hierarchii, počet emitorů ani celý efekt. Vztahy a společné uložení emitorů řeší strom scény `.tscn`; shaderový graf je zdroj použitý jedním emitorem. Workspace proto tyto vrstvy rozlišuje.

## 3D náhled a simulace

Náhled simuluje izolovanou kopii celé hierarchie efektu ve vlastním `SubViewport`, s vlastním prostředím, světly, kamerou a referenční mřížkou. Chybějící draw mesh nebo GPU process material doplní pouze náhledová kopie; editovaný efekt se tím nemění.

- Levé nebo pravé tažení myší otáčí kameru, prostřední tažení náhled posouvá a kolečko přibližuje.
- **Reset View** znovu orámuje rozsah emitovaných particle uzlů. **Grid** zapíná referenční mřížku.
- **Play/Pause**, **Restart**, časová osa, **Speed** a **Loop** ovládají lokální simulaci. Smyčka je výchozím stavem zapnutá.
- Časová osa má délku nejdelší životnosti emitoru. Posun na jiné místo resetuje particle uzly a přehrává simulaci dopředu do zvoleného času; nejde o přímý skok na přesně rekonstruovaný stav.

## Současné hranice a další práce

- Šířky hlavních panelů se ukládají do rozvržení editoru a při dalším spuštění obnoví. Při úzkém okně se zkracuje document toolbar a časový popisek přehrávání je v pevně širokém ořezávacím kontejneru, aby jeho měnící se čísla nerozhazovala minimální šířku panelů. Uživatel potvrdil, že cukání pravé strany při pohybu časové osy už nepozoruje.
- Dvojklik na název emitoru ve stromu zahájí přejmenování; změna se objeví také v popisku náhledu a lze ji vrátit přes Undo/Redo.
- Křivky Alpha a Scale mají účelové presety i nativní CurveEditor. Další parametry procesu zatím nemají vlastní zjednodušené křivkové karty.
- **Texture Generator (VFX Texture Generator)** otevírá nativní nástroj pro procedurální VFX textury a animované sprite sheety. Podrobnosti jsou v části [VFX Texture Generator](#vfx-texture-generator).
- Náhled kopíruje podstrom uloženého efektu. Kolizní nebo attractor uzly, které jsou mimo tento podstrom ve scéně Level, se do simulace nepřidají.

## VFX Texture Generator

Generátor je samostatný modul `engine/editor/fps_studio/vfx_texture_generator.cpp` bez závislosti na editoru. Plugin ho obaluje do `Image` a vykresluje po řádcích přes `WorkerThreadPool`. Stejný zdroj lze přeložit i mimo engine jako malý nástroj a referenční obrázky tak lze vykreslit bez GUI.

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
