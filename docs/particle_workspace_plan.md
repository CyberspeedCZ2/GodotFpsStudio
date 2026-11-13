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
| 1. Efektový dokument | — | Vytvořit efekt, uložit `.tscn`, zavřít workspace, znovu otevřít scénu a přidat instanci do Levelu. | V dočasném projektu ručně ověřeno uložení, znovunačtení efektu i přidání do Levelu. |
| 2. Hierarchie emitterů | 1 | Přidat GPU/CPU emitter, odebrat vybraný uzel, ověřit Undo/Redo a ochranu posledního emitteru; přejmenovat emitter a ověřit Undo/Redo. | Přidání, odebrání i přejmenování jsou implementované; rename a jeho Undo/Redo ručně ověřeny. |
| 3. Izolovaný náhled a rozvržení | 1, 2 | Ověřit více emitterů, orbit/pan/zoom, grid, přehrávání, restart, scrub a změnu materiálu; zopakovat při malém okně a otevřeném Output panelu. | Rozměry splitterů se ukládají a změnu šířky levého panelu jsem ověřil v dočasném projektu. Pro úzké okno se zkracuje toolbar a časový popisek přehrávání má pevnou šířku, aby se měnící text nepřepočítával do minima layoutu. Aktuální build prošel; vizuální ověření této opravy se nepodařilo dokončit, protože automatizace při restartu editoru vypršela. |
| 4. Křivky přes celý život částice | 2, 3 | Vytvořit Alpha/Scale křivku presetem, změnit body, ověřit okamžitý náhled, Undo/Redo a uložení/načtení efektu. | Presety a Undo/Redo byly ověřeny už dříve. Bod Alpha over Life jsem upravil, uložil do `.tscn` a po opětovném otevření ověřil jeho zachované souřadnice v CurveEditoru. |
| 5. Particle `VisualShader` | 2, 3 | Vytvořit graf, uložit/načíst scénu s grafem, přepnout emitter a ověřit vazbu grafu; ověřit Undo/Redo vytvoření. | Nativní editor grafu je vložený v Particle Logic. Vytvoření, uložení, znovunačtení i UndoRedo byly ručně ověřeny; při výběru dvou GPU emitterů se správně přepíná jejich samostatný graf. |
| 6. Generátor VFX textur | 2, 3 | Základ: vytvořit statický obrázek, exportovat PNG, importovat ho a přiřadit emitoru. Volitelně: projít animaci, FPS a sheet 4×4/8×8. | Statické PNG bylo v novém buildu exportováno, naimportováno a Apply to Emitter ho přiřadilo jako texturu PlaneMesh. Náhled ohně zůstal v Rendereru viditelný i při zapnutém přehrávání generátoru; další přepínání karet už nezpůsobilo pád a Output neměl chyby. Sprite sheet je volitelný a neověřený. |
| 7. Dokončení a distribuce změn | 1–6 | Spustit build, projít dokumentaci, zopakovat hlavní workflow v dočasném projektu, zkontrolovat diff a stav Gitu. | Build i diff jsou zkontrolované; commity `7d4ea917` a `d791cf8b` jsou pushnuté. Opětovné načtení upravené křivky je ověřené; malé okno a stabilita pravého panelu při přehrávání ještě čekají na vizuální kontrolu. |

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
- [ ] V editoru vizuálně ověřit kompaktní toolbar a stabilní pravý panel při běhu časové osy, s/bez spodního Output panelu a při malém okně.

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
- [ ] Ověřit malé okno s otevřeným Output panelem.
- [ ] Projít celý create/add/remove/preview/save/open/Add to Level cyklus v jedné relaci.
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
- [ ] Volitelně ověřit animaci náhledu a export animovaného sprite sheetu v editoru.
- [ ] Doplnit normal map, seamless tiling, color balance a pokročilejší šum.
- [ ] Ukládat parametry generátoru do samostatného Godot resource pro opětovnou editaci.

## Rozsah mimo tuto etapu

2D částice, runtime efektové události, síťová replikace, víceúrovňové sub-emitter graphy a vlastní `.particle` formát. Cílem není implementovat Niagara-style multi-stage systém; editor sdružuje Godotovy emittery, procesní materiály a `VisualShader` do konzistentního prostředí.

## Historie

Plán zahájen 2026-11-13 po společné analýze Godotu, UE5 a Source enginu. Konceptuální render připraven jako UI reference a implementace pokračuje po etapách.
