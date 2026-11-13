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
- [ ] Bezpečně přejmenovat vybraný emitter přímo v hierarchii.
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
- [ ] Uložit šířky panelů a jejich rozložení do editorových nastavení.
- [ ] Znovu proměřit responsivitu rozvržení s/bez spodního Output panelu a při malém okně.

### 4. Křivky — základ hotov

- [x] Alpha over Life a Scale over Life s nativními `CurveTexture` a `CurveEditor`.
- [x] Přidat chybějící curve resource do Undo/Redo a promítnout změnu do preview.
- [ ] Přidat volitelné presety a rozšířit nástroje na další parametry podle uživatelské hodnoty.
- [ ] Zlepšit umístění a dosažitelnost křivek v dlouhé kartě Process.

### 5. Nativní Particle `VisualShader` — základ hotov, graf zatím samostatně

- [x] Zachovat a otevřít nativní particle `VisualShader` Godotu.
- [x] Vytvořit particle `VisualShader` pro GPU emitter s potvrzením před nahrazením materiálu a možností Undo.
- [x] Rozlišit shaderový graf emitoru od hierarchie celého efektu.
- [x] Zatím otevřít graf ve vestavěném Shader Editoru Godotu; particle preview zůstává dostupný ve workspace.
- [ ] Prověřit, zda lze bezpečně vložit nativní GraphEdit view do spodního panelu bez kopírování editoru Shader.
- [ ] Ručně ověřit ukládání/načítání efektu s existujícím grafem i jeho napojením.

### 6. Ověření, dokumentace, Git — probíhá

- [x] Přeložit editor přes `./tools/build_macos.sh`.
- [x] Aktualizovat uživatelskou dokumentaci a tuto roadmapu podle současného stavu.
- [x] Ověřit headless start editoru a opravit chybu `p_owner == this` v náhledu.
- [ ] Ověřit hlavní pracovní postup v odděleném testovacím projektu: create, add/remove emitter, preview, Save/Open, Add to Level.
- [ ] Zkontrolovat diff, commitnout a pushnout změny na pracovní GitHub větev podle instrukcí repozitáře.

### 7. Nativní VFX generátor textur — první verze hotová, pokračovat iterativně

- [x] Otevřít nativní okno generátoru přímo z Particle workspace; nejde o vloženou webovou stránku.
- [x] Procedurálně vytvořit základní textury pro oheň, kouř, explozi a jiskru s průhledným pozadím.
- [x] Nastavit seed, rozlišení a dvě barvy; zobrazit živý 2D náhled.
- [x] Exportovat PNG do projektu a přiřadit ho materiálu vybraného GPU/CPU emitoru.
- Záměrně jde o nově napsaný nativní kód podle veřejně popsaného postupu. Konkrétní shaderové soubory EffectTextureMaker jsou minifikované a jejich licence není pro tento kód jednoznačně doložená. CC0 pro vygenerované textury automaticky nepovoluje kopírování generátoru.
- [ ] Vizuálně ověřit generování, uložení a přiřazení v editoru.
- [ ] Doplnit animované sprite sheety a nastavení snímků.
- [ ] Doplnit normal map, seamless tiling, color balance a pokročilejší šum.
- [ ] Ukládat parametry generátoru do samostatného Godot resource pro opětovnou editaci.

## Rozsah mimo tuto etapu

2D částice, runtime efektové události, síťová replikace, víceúrovňové sub-emitter graphy a vlastní `.particle` formát. Cílem není implementovat Niagara-style multi-stage systém; editor sdružuje Godotovy emittery, procesní materiály a `VisualShader` do konzistentního prostředí.

## Historie

Plán zahájen 2026-11-13 po společné analýze Godotu, UE5 a Source enginu. Konceptuální render připraven jako UI reference a implementace pokračuje po etapách.
