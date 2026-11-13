# Pracovní prostředí Particle

Particle je samostatná obrazovka FPS Studia pro tvorbu a náhled nativních 3D particle uzlů Godotu. Upravuje vybraný uzel a jeho existující zdroje; nezavádí nový formát simulace ani assetů.

## Pracovní postup

1. Seznam **Emitters** vlevo zobrazuje uzly z právě otevřené scény; Particle workspace do ní žádný výchozí emitor nevkládá. V nabídce **File** vytvoříš GPU nebo CPU emitor, případně GPU předvolbu **Burst**, **Fountain** nebo **Smoke**. Nový uzel se přidá přes Undo/Redo a ihned se otevře v náhledu. Stejná nabídka umí načíst particle scénu do otevřené scény a uložit vybraný emitor s potomky jako znovupoužitelnou scénu.
2. V seznamu **Emitters** můžeš přepínat mezi 3D particle uzly v otevřené scéně.
3. V inspektoru **Emitter** upravuješ vlastnosti uzlu. U GPU uzlů je dostupný také materiál v inspektoru **Process Material**. Tlačítko **Create Node Graph** přepne procesní materiál na nativní `VisualShader` v režimu `particles` a otevře vestavěný editor grafu Godotu. Pokud už emitter procesní materiál používá, před jeho nahrazením se zobrazí potvrzení; změnu lze vrátit přes Undo. U existujícího particle grafu tlačítko nabídne jeho otevření.
4. Simulaci můžeš spustit, pozastavit, restartovat, změnit její rychlost a posunout časovou osu. **Loop** je ve výchozím stavu zapnutý; po vypnutí se přehrávání zastaví na konci životnosti emitteru. Tažením levým nebo pravým tlačítkem otáčíš kameru, prostředním tlačítkem náhled posouváš a kolečkem přibližuješ. **Reset View** vrátí kameru do výchozího pohledu. Kamera používá perspektivu s FOV 55° a pevnou světovou osou Y nahoru. Mřížku lze přepnout přes **Grid**.

Náhled simuluje kopii vybraného emitteru a jeho potomků ve vlastním `SubViewport` s odděleným prostředím, světlem a kamerou. Změny vlastností emitteru i Process Material se promítají do náhledu. Pokud emitter nemá mesh nebo GPU process material, náhled doplní výchozí hodnoty pouze do této kopie; scéna se tím nemění. Úpravy vlastností používají vestavěné inspektory Godotu a jeho Undo/Redo. Node Graph je skutečný Godot `VisualShader`, nikoli vlastní formát grafu; editor nabízí nativní particle fáze a uzly Godotu.

Godotův panel náhledu uvnitř VisualShader editoru neumí zobrazit režim `particles` — hlášení na screenshotu je omezení shaderového náhledu, ne particle simulace. Náhled Particle workspace zobrazuje celou simulaci emitteru a jeho draw passů, takže doplňuje graf na úrovni celého efektu; nejde o náhled jednotlivého uzlu grafu.

## Aktuální rozsah

- Podporované jsou 3D GPU a CPU particle uzly. Prostředí vypisuje emittery aktivní scény, umožňuje vytvořit nový uzel a současně zobrazuje jeden vybraný uzel.
- GPU emitter může používat běžný `ParticleProcessMaterial` nebo nativní particle `VisualShader`. Kompletní emitter je Godot `Node`, a proto se ukládá jako PackedScene `.tscn`; nabídka **File > Load Particle Scene** jej vloží do otevřené scény přes Undo/Redo a **File > Save Particle Scene As** uloží vybraný emitter i potomky. Samotný uzlový graf je `VisualShader` zdroj ukládaný jako `.tres`. Tyto formáty řeší odlišné vrstvy: `.tscn` uchovává celý efekt, `.tres` shaderový graf. Vlastní přípona FPS Studia by nepřinesla výhodu, dokud nebude definován další obsah particle assetu.
- Náhled kopíruje vybraný emitter a jeho potomky. Neinstancuje celou aktivní úroveň, takže kolizní nebo attractor uzly mimo jeho podstrom nejsou součástí simulace.
- Posunutí časové osy restartuje náhled a simuluje částice dopředu do zvoleného času. Nejde o přímý skok na libovolný stav.
- Náhled má referenční mřížku, orbitovací a posouvací kameru a přiblížení kolečkem myši. Tvorba více emitterů jako jednoho systému, časová osa celého systému a editace 2D částic nejsou součástí této verze.

## Implementace

Obrazovku přidává `FPSStudioParticleEditorPlugin` v `engine/editor/fps_studio/particle_editor_plugin.cpp`. Používá `EditorInspector` pro úpravy nativního uzlu a materiálu, vestavěný Godot VisualShader editor pro node graph a samostatnou kopii uzlu pro simulaci. Plugin registruje `EditorNode` bezprostředně za obrazovkou Model.
