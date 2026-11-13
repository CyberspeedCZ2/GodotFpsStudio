# Particle Workspace — návrh a stav

## Cíl

Particle workspace je samostatné místo pro sestavení a ladění celých opakovaně použitelných 3D efektů. Má zjednodušit práci s částicemi, které Godot už nabízí jako uzly, materiály a shaderové resources, aniž by uživatel musel rozumět všem jejich interním třídám.

Vizuální předloha je [particle_workspace_concept.png](images/particle_workspace_concept.png). Ukazuje dvě oddělené vrstvy: levý strom je scéna efektu s emitory; nativní `VisualShader` dole je jen shader jednoho vybraného GPU emitoru. Konceptuální obrázek neurčuje vlastní formát ani nový systém efektových uzlů. Starší kompoziční draft je zachován jako [particle_workspace_concept-initial.png](images/particle_workspace_concept-initial.png).

## Jak je systém rozdělen

1. **Efektová scéna (`.tscn`)** — kořen `Node3D`, jeden či více `GPUParticles3D` / `CPUParticles3D` uzlů a podpůrné potomky. Ukládá hierarchii, transformace, draw passy, procesní materiály a jejich references. `Add to Level` přidá instanci celého efektu do právě otevřené Level scény.
2. **Procesní nastavení** — GPU emitor může použít nativní `ParticleProcessMaterial`, jehož spawn, pohyb, akcelerace, vykreslení a křivky se upravují přes existující inspektor Godotu.
3. **Particle shader graf (`VisualShader`)** — volitelná alternativa za `ParticleProcessMaterial` u GPU emitoru. Je to nativní shaderový graf pro výpočty particle kroků, ne graf celého efektu ani hierarchie emitorů. Lze ho uložit jako `.tres` nebo zahrnout jako subresource scény.
4. **Náhled** — workspace klonuje celý dokument do izolovaného `SubViewport`, takže Level scéna zůstává nedotčená. Náhled sdílí stejné efektové resources, ale zobrazuje vlastní simulovanou kopii uzlů.

Vlastní přípona `.particle` není zatím potřeba: uzly a vazby patří do `.tscn`, shader do `VisualShader` `.tres`/subresource, křivky do Godot `CurveTexture`. Vlastní kontejner má smysl až tehdy, kdyby dokument potřeboval data, která Godot scéna neumí uchovat.

## Cíl a podmínky dokončení

Referenční render vymezuje cílové rozložení: knihovna efektů a hierarchie emitorů vlevo, živý 3D náhled a časové ovládání uprostřed, skutečný particle shader graf ve spodní části a nativní inspektor vybraného uzlu vpravo. Hotový systém musí podporovat celý běžný cyklus tvorby bez přepínání do Level workspace.

- Efekt lze vytvořit, otevřít, přejmenovat, uložit, zavřít a znovu načíst jako scénu Godotu; změny a stav uložení jsou zřejmé.
- Efekt může mít více GPU/CPU emitorů; přidání, odebrání a přejmenování jsou bezpečné a lze je vrátit přes Undo/Redo.
- 3D náhled simuluje celou efektovou scénu, má stabilní perspektivu, mřížku, orbit, pan, zoom, reset a ovládání přehrávání.
- Vybraný GPU emitor může používat `ParticleProcessMaterial` nebo nativní particle `VisualShader`. Jeho skutečný graf lze upravovat přímo v dolním panelu; tlačítko ho může otevřít i ve velkém Shader Editoru. Výběr emitora určuje obsah grafu.
- Často používané křivky včetně Alpha over Life a Scale over Life jsou snadno dosažitelné, mají užitečné presety a změny se ihned projeví v náhledu.
- Generátor VFX textur musí pro základní workflow vytvořit statický obrázek, exportovat ho jako PNG a přiřadit ho emitoru. Přehrávání animace a export sprite sheetu jsou volitelná rozšíření; nejsou podmínkou pro vytvoření použitelného particle obrázku.
- Rozvržení zůstává použitelné při malé šířce okna i s otevřeným Output panelem. Testy probíhají v dočasném projektu, nikoli v samostatném herním repozitáři.

Hotový systém budu označovat za dokončený teprve po ručním ověření uvedeného workflow v běžném editoru Godotu a po aktualizaci této roadmapy skutečným výsledkem testů.

## Etapy, závislosti a ověřovací scénáře

| Etapa | Závisí na | Ověřovací scénáře | Stav |
| --- | --- | --- | --- |
| 1. Efektový dokument | — | Vytvořit efekt, uložit `.tscn`, zavřít workspace, znovu otevřít scénu a přidat instanci do Levelu. | V dočasném projektu ručně ověřeno uložení, opětovné načtení a přidání instance do Levelu s následným Undo. Přejmenování rootu přes titulek a jeho Undo/Redo je také ručně ověřené. |
| 2. Hierarchie emitterů | 1 | Přidat GPU/CPU emitter, odebrat vybraný uzel, ověřit Undo/Redo a ochranu posledního emitteru; přejmenovat emitter a ověřit Undo/Redo. | Přidání, odebrání i přejmenování jsou implementované. Přejmenování i Undo/Redo přidání a odebrání byly ručně ověřeny; oprava obnovuje strom výběru a vlastnictví uzlu bez varování v Outputu. |
| 3. Izolovaný náhled a rozvržení | 1, 2 | Ověřit více emitterů, orbit/pan/zoom, grid, přehrávání, restart, scrub a změnu materiálu; zopakovat při malém okně a otevřeném Output panelu. | V okně 1440×900 s otevřeným Output panelem zůstaly náhled, transport i vložený particle graf použitelné; při vytvoření grafu byly viditelné uzly SphereEmitter, ConeVelocity a Output. Prázdný stav bez vybraného emitoru spodní panel skrývá. Uživatel potvrdil, že cukání pravé strany při pohybu časové osy už nepozoruje. Samostatná kontrola bez Output panelu a při průběžné změně šířky okna zůstává otevřená. |
| 4. Křivky přes celý život částice | 2, 3 | Vytvořit Alpha/Scale křivku presetem, změnit body, ověřit okamžitý náhled, Undo/Redo a uložení/načtení efektu. | Presety a Undo/Redo byly ověřeny už dříve. Bod Alpha over Life jsem upravil, uložil do `.tscn` a po opětovném otevření ověřil jeho zachované souřadnice v CurveEditoru. |
| 5. Particle `VisualShader` | 2, 3 | Vytvořit graf, uložit/načíst scénu s grafem, přepnout emitter a ověřit vazbu grafu; ověřit Undo/Redo vytvoření. | Nativní editor grafu je vložený v Particle Logic. Vytvoření, uložení, znovunačtení i UndoRedo byly ručně ověřeny; při výběru dvou GPU emitterů se správně přepíná jejich samostatný graf. |
| 6. Generátor VFX textur | 2, 3 | Základ: vytvořit statický obrázek, exportovat PNG, importovat ho a přiřadit emitoru. Volitelně: projít animaci, FPS a sheet 4×4/8×8. | Statické PNG bylo v novém buildu exportováno, naimportováno a Apply to Emitter ho přiřadilo jako texturu PlaneMesh. Náhled ohně zůstal v Rendereru viditelný i při zapnutém přehrávání generátoru; další přepínání karet už nezpůsobilo pád a Output neměl chyby. V navazující kontrole se animoval náhled 4×4 sprite sheetu při 12 FPS a exportoval se PNG soubor; přiřazení sprite sheetu emitoru samostatně potvrzené není. Novější build přidává alfa a barevné postprocesy, export normal map a `.cfg` recepty; tato nová ovládání ještě čekají na ruční průchod v editoru. |
| 7. Dokončení a distribuce změn | 1–6 | Spustit build, projít dokumentaci, zopakovat hlavní workflow v dočasném projektu, zkontrolovat diff a stav Gitu. | `./tools/build_macos.sh` uspěl a zabalil UniBuilder. V dočasném projektu prošel celý cyklus efektu včetně Undo/Redo emitterů a vložení instance do Levelu. Uživatel potvrdil, že cukání pravého panelu už nepozoruje. Změny jsou v commitu `e1dcefe3` pushnutém na pracovní větev. Vizuální kontrola bez Output panelu a při změně šířky okna zůstává otevřená; nová ovládání generátoru rovněž potřebují ruční ověření. |

Závislosti určují pořadí implementace, ne blokování nezávislé dokumentace nebo jednotlivých oprav. Rozhraní považuj za ověřené až po ručním průchodu odpovídajícího scénáře v editoru.

## Zásady UI

- **Efekt je dokument, emitor je jeho část.** Otevření Particle workspace automaticky nevybírá uzly z Levelu. Při výběru emitteru se mění inspektor a shader graf pro tento uzel.
- **Godot zůstává zdrojem pravdy.** Vlastní UI sdružuje a zjednodušuje existující vlastnosti, ale neklonuje simulaci, shader graph ani materiálový formát.
- **Přehledné výchozí ovládání, dostupné pokročilé nastavení.** Časté ovládací prvky jsou u náhledu a tematicky rozdělené v pravém panelu; celý nativní inspektor zůstává dostupný.
- **Jedna hierarchie, jeden význam.** Levý panel ukazuje knihovnu efektových scén a emittery uvnitř otevřeného efektu. VisualShader zůstává samplerem výpočtu pro jeden emitor.
- **Náhled na první pohled.** Perspektivní 3D view s mřížkou, stabilním studiovým světlem a kamerou s orbit, pan, zoom; ovládání přehrávání, resetu, času, rychlosti a smyčky je pod ním.

## Implementace a stav

### 1. Samostatný efektový dokument — hotovo

- [x] Nahradit automatické prohledávání Levelu samostatným dokumentem Particle workspace.
- [x] Prázdný stav s vytvořením/otevřením efektu; žádný pevně přidaný `BubbleParticles`.
- [x] Nový `Node3D` efekt s připraveným GPU nebo CPU emitorem a předvolbami Burst, Fountain, Smoke.
- [x] Otevřít `.tscn`, rozpoznat starší scénu s particle uzlem jako kořenem.
- [x] Uložit celý efekt jako `.tscn`.
- [x] Přidat celý efekt do Levelu přes Undo/Redo.

### 2. Emitorová hierarchie a simulace — téměř hotovo

- [x] Oddělit nedávné soubory od emitterů v aktuálním efektu.
- [x] Přidat GPU/CPU emitter i předvolbu a odebrat vybraný emitter.
- [x] Bezpečně přejmenovat vybraný emitter dvojklikem v hierarchii; změna používá Undo/Redo.
- [x] Simulovat celý efekt včetně více emitterů.
- [x] Odvodit délku náhledu z nejdelší životnosti; přehrávání, restart, scrub, rychlost a Loop defaultně zapnutý.
- [x] Výběr uzlu přepíná odpovídající Emitter/Process/Renderer inspektory.

### 3. Náhled a UI — kostra hotová, vyžaduje další ladění

- [x] Samostatný dokumentový toolbar se stavem, Save a Add to Level.
- [x] Levý panel s nedávnými efekty a stromem emitterů.
- [x] Centrální preview s perspektivní kamerou, gridem, orbit, pan, zoom a Reset View.
- [x] Transport ovládání pod náhledem.
- [x] Pravé karty Emitter, Process a Renderer používají nativní Godot inspektory.
- [x] Upravit výchozí poměr vertikálního splitteru tak, aby 3D preview dostalo více plochy než spodní informační panel.
- [x] Uložit poměry splitterů do editorového rozvržení a obnovit je při spuštění.
- [x] Uživatel potvrdil, že při běhu časové osy už cukání pravé strany nepozoruje.
- [x] V okně 1440×900 s otevřeným Output panelem ověřit viditelnost preview, transportu a vloženého particle grafu; nový graf se otevře s připojeným spawn tvarem a rychlostí.
- [ ] Zopakovat vizuální kontrolu bez Output panelu a při změně šířky hlavního okna.

### 4. Křivky — základ hotov

- [x] Alpha over Life a Scale over Life s nativními `CurveTexture` a `CurveEditor`.
- [x] Přidat chybějící curve resource do Undo/Redo a promítnout změnu do preview.
- [x] Přidat účelové presety Fade/Grow, které lze aplikovat i před vytvořením křivky.
- [x] Přesunout křivky před kompletní inspektor v kartě Process.
- [x] Ručně ověřit Alpha/Scale presety a Undo/Redo v editoru.
- [x] V novém buildu upravit bod Alpha over Life, uložit efekt a ověřit zapsanou hodnotu v `.tscn`.
- [x] Znovu načíst právě uloženou scénu a zkontrolovat hodnotu bodu v CurveEditoru.
- [ ] Rozšířit nástroje na další parametry podle uživatelské hodnoty.

### 5. Nativní Particle `VisualShader` — vložený graf hotový

- [x] Zachovat a otevřít nativní particle `VisualShader` Godotu.
- [x] Vytvořit particle `VisualShader` pro GPU emitter s potvrzením před nahrazením materiálu a možností Undo.
- [x] Rozlišit shaderový graf emitoru od hierarchie celého efektu.
- [x] Vložit nativní `VisualShaderEditor` do spodního panelu a ponechat možnost otevřít graf ve velkém Shader Editoru.
- [x] Uložit a načíst efekt se shaderovým grafem; opravit historii UndoRedo při jeho vytvoření.
- [x] Ručně ověřit přepínání vloženého grafu mezi více GPU emitory.

### 6. Ověření, dokumentace, Git — část hotova

- [x] Přeložit editor přes `./tools/build_macos.sh`.
- [x] Aktualizovat uživatelskou dokumentaci a tuto roadmapu podle současného stavu.
- [x] Ověřit headless start editoru a opravit chybu `p_owner == this` v náhledu.
- [x] V dočasném projektu vytvořit a uložit efekt, znovu načíst scénu a přidat efekt do Levelu.
- [x] V novém buildu vytvořit particle `VisualShader`, uložit a znovu načíst scénu s grafem, otevřít další efekt bez pádu a ověřit čistý Output.
- [x] Projít export PNG a Apply to Emitter na novém buildu; ověřit přiřazení statické textury, Renderer náhled a stabilitu při přepínání karet.
- [x] Dovyzkoušet editaci a uložení bodu křivky i přepínání shader grafu mezi GPU emitory.
- [x] Potvrdit opětovné načtení právě upraveného bodu křivky.
- [x] Ověřit malé okno s otevřeným Output panelem při běhu a smyčce časové osy.
- [x] Projít celý create/add/remove/preview/save/open/Add to Level cyklus v jedné relaci; po uložení znovu načíst efekt, ověřit oba emitery a přidat instanci do Levelu přes Undo/Redo.
- [x] Zkontrolovat diff a stav Gitu, commitnout a pushnout změny na pracovní větev `codex/black-default-fps-level`.

### 7. Nativní VFX generátor textur — první verze hotová, pokračovat iterativně

- [x] Otevřít nativní okno generátoru přímo z Particle workspace; nejde o vloženou webovou stránku.
- [x] Procedurálně vytvořit základní textury pro oheň, kouř, explozi a jiskru s průhledným pozadím.
- [x] Nastavit seed, rozlišení a dvě barvy; zobrazit živý 2D náhled.
- [x] Přidat přehrávání a posuvník času; sprite sheet zobrazuje právě vybraný snímek, samostatná textura znovu vzorkuje procedurální animaci.
- [x] Exportovat PNG do projektu a importovat ho synchronně před přiřazením materiálu emitteru.
- Záměrně jde o nově napsaný nativní kód podle veřejně popsaného postupu. Konkrétní shaderové soubory EffectTextureMaker jsou minifikované a jejich licence není pro tento kód jednoznačně doložená. CC0 pro vygenerované textury automaticky nepovoluje kopírování generátoru.
- [x] Headless načíst statický PNG a přiřadit ho `StandardMaterial3D` na `QuadMesh` emitoru.
- [x] V editoru ručně ověřit Export PNG a Apply to Emitter po synchronním reimportu; statický obrázek stačí pro použitelný particle efekt.
- [x] Volitelně ověřit animaci náhledu a export 4×4 sprite sheetu v editoru; přiřazení animovaného sheetu emitoru zůstává samostatně neověřené.
- [x] Rozšířit katalog na devět skupinovaných typů a přidat rozlišení 2048×2048; preview při změně voleb vzorkuje max. 256×256.
- [x] Přidat prahování a měkké 3×3 rozmazání alfa kanálu, grayscale náhled masky a barevné tints pro shadows/midtones/highlights.
- [x] Přidat PNG export tangent-space normal mapy odvozené z alfa masky; generátor ji záměrně nepřiřazuje jako barvu částic.
- [x] Ukládat a načítat `.cfg` recepty včetně typu, barev, rozlišení, atlasu, FPS a postprocesů.
- [x] Rozšířit katalog na 15 typů ve skupinách (přidány WaveRing, Flash / Lens flare, Bubbles / Bokeh, Plasma fireball, Voronoi caustics, Electric arc) s procedurálním Voronoi generátorem.
- [x] Implementovat polární konverzi souřadnic (Polar conversion) pro kruhové/radiální VFX efekty inspirovanou EffectTextureMakerem.
- [x] Implementovat Toon shading s nastavitelnou 3-úrovňovou kvantizací prahů (dark/light thresholds).
- [x] Implementovat Seamless tiling s periodickým vzorkováním okrajů a nastavitelnou radiální maskou (radial mask).
- [x] Doplnit výškové měřítko normal mapy (cHeightScale 0.1–10.0) a tlačítko Reset pro vyvážení barev.
- [x] Aktualizovat formát `.cfg` receptů na verzi 2 s podporou nových parametrů a zpětnou kompatibilitou.
- [x] Automaticky nastavit billboard částic (`BILLBOARD_PARTICLES`) a rozdělení atlasu (`particles_anim_h_frames` / `v_frames`) na `StandardMaterial3D` emitoru při Apply to Emitter.
- [x] Přepracovat procedurální exploze: nahradit základní explozi realistickou vířivou ohnivou koulí (`Explosion (Fireball)`) s domain-warpingem a přidat prstencovou rázovou vlnu (`Explosion 2 (Shockwave ring)`) s ridged spiral šumem a žhavými filamenty odpovídajícími EffectTextureMakeru.
- [x] Přidat sekci Parameters podle EffectTextureMakeru: posuvníky Speed, Density, Emission, Turbulence / Detail, Contrast / Sharpness, Rotation (deg) a tlačítko Reset Parameters.
- [x] Přidat herní stylové předvolby (Game style / Quality presets): Realistic (Cinematic), Medium (Balanced), Low (Performance / Retro), Cartoon / Toon (Anime) a Custom pro rychlé přizpůsobení vizuální stylistiky a hardwarové náročnosti.
- [x] Rozšířit serializaci `.cfg` receptů o herní styl a parametry Speed, Density, Emission, Detail, Contrast a Angle.
- [ ] Ručně ověřit nová ovládání, uložení/načtení receptu v2, export normal mapy a úpravy alfa kanálu v editoru.
- [ ] Ověřit alfa export sprite sheetu a přiřazení animovaného sheetu emitoru.

## Analýza reference: EffectTextureMaker

Pro další rozvoj byl 2026-11-13 zkontrolován [anglický popis EffectTextureMakeru](https://mebiusbox.github.io/en/docs/lab/effect_texture_maker) a přiložené screenshoty jeho ovládání. Jde o širší procedurální nástroj než náš současný generátor. Dokumentace reference uvádí desítky předloh, normal mapu, sprite sheet, color balance, maximum 2048 px, časové efekty, polar conversion, toon shading, tiling, alpha PNG a ukládání/načítání nastavení v JSON.

| Oblast | EffectTextureMaker | Particle workspace dnes | Doporučení |
| --- | --- | --- | --- |
| Předlohy | Rozsáhlá galerie pro gradienty/geometrii, oheň/kouř/exploze, laser/flare/světla, ornamenty, částice, organické tvary a šum (např. MagicCircle, FlameLance, Bubbles, InkSplat, VoronoiNoise). Každý typ má vlastní parametry. | Devět procedurálních předloh ve skupinách: Fire, Smoke, Explosion, Spark streak, Lightning, Energy ring, Magic circle, Cloud a Noise field. Mají společný seed a dvě základní barvy; typově specifické parametry zatím chybí. Náhledy Lightning, Magic circle a Cloud byly zkontrolovány v editoru. | Nabídku dál rozšiřovat po skupinách a přidávat vlastní parametry vybraným předlohám; nekopírovat desítky položek do jediného dlouhého seznamu. |
| Rozlišení a reprodukce | Až 2048 px; Save/Load ukládá parametry v JSON. | 128–2048 px; preview vzorkuje nejvýše 256×256, plné rozlišení se počítá při exportu. Save/Load ukládá `.cfg` recept se seedem, barvami, rozlišením, layoutem, FPS a postprocesy. Export a načtení nového receptu čekají na ruční ověření. | Udržet recept editovatelný v projektu a později rozhodnout, zda má smysl převést `.cfg` na plnohodnotný Resource. |
| Animace | Čas a přehrávání; sprite-sheet dimension, start, délka animace a časový krok snímků. | Časový náhled a sheet 4×4/8×8 s FPS; export PNG. | Doplnit start, délku a frame step, aby uživatel řídil sampling nezávisle na výsledné FPS i počtu obsazených buněk atlasu. |
| Alfa | Prahování, tolerance, blur a náhled alfa kanálu. Přiložený screenshot ukazuje samostatné uložení PNG i sprite sheetu s alfou. | Alfa threshold, měkké 3×3 rozmazání a grayscale náhled masky; alfa zůstává součástí barevného PNG i atlasu. Export postprocesované alfy zbývá ručně ověřit. | Zvážit toleranci/rozšíření masky a ověřit PNG pro statický i animovaný výstup. Webový popis tvrdí, že alpha PNG nefunguje pro sprite sheet, ale changelog i přiložené UI ukazují export sprite sheetu s alfou; ověřit skutečně uložený soubor. |
| Barevné zpracování | Samostatné shadows/midtones/highlights a reset. | Dvě barvy určují základní gradient; volitelné tint barvy upravují stíny, střední tóny a světla. Hodnoty recept ukládá. Nové ovládací prvky ještě čekají na ruční kontrolu. | Později přidat přesnější offsety jednotlivých RGB kanálů a reset tonalit. |
| Odvozené výstupy | Normal map z výškové intenzity, toon shading se třemi tóny, polar conversion a tiling s radiální maskou. | Volitelná tangent-space normal mapa se odvozuje z upravené alfa výšky a exportuje se jako PNG. Není přiřazována jako barva neosvětleného particle materiálu. Toon, polar conversion a tiling zatím chybí. | Toon výstup a typově specifické parametry jsou další vhodné funkce. Tiling dát níže v pořadí, protože samotný autor reference ho označuje jako nekvalitní. |

Implementaci převzít jako nezávislý nativní Godot kód podle popsaného chování, nikoli překladem jejich minifikovaných shaderů. Reference výslovně licencuje vygenerované textury jako CC0, zatímco stránka uvádí copyright softwaru; licence obrazových výstupů proto není důvodem kopírovat implementaci generátoru.

## Rozsah mimo tuto etapu

2D částice, runtime efektové události, síťová replikace, víceúrovňové sub-emitter graphy a vlastní `.particle` formát. Cílem není implementovat Niagara-style multi-stage systém; editor sdružuje Godotovy emittery, procesní materiály a `VisualShader` do konzistentního prostředí.

## Historie

Plán zahájen 2026-11-13 po společné analýze Godotu, UE5 a Source enginu. Konceptuální render připraven jako UI reference a implementace pokračuje po etapách.
