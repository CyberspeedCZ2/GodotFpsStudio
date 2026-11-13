# Pracovní prostředí Particle

Particle je samostatná obrazovka FPS Studia pro tvorbu a náhled nativních 3D particle uzlů Godotu. Upravuje vybraný uzel a jeho existující zdroje; nezavádí nový formát simulace ani assetů.

## Pracovní postup

1. V pracovní ploše **Particle** zvol **Add** a vytvoř GPU nebo CPU emitter přímo v aktivní scéně. V nabídce jsou také GPU předvolby **Burst**, **Fountain** a **Smoke**. Nový uzel se přidá přes Undo/Redo, vybere se a ihned otevře v náhledu. Případně můžeš vybrat existující `GPUParticles3D` nebo `CPUParticles3D` v aktivní scéně.
2. V seznamu **Emitters** můžeš přepínat mezi 3D particle uzly ve scéně.
3. V inspektoru **Emitter** upravuješ vlastnosti uzlu. U GPU uzlů je dostupný také materiál v inspektoru **Process Material**. Tlačítko **Create Node Graph** přepne procesní materiál na nativní `VisualShader` v režimu `particles` a otevře vestavěný editor grafu Godotu. Pokud už emitter procesní materiál používá, před jeho nahrazením se zobrazí potvrzení; změnu lze vrátit přes Undo. U existujícího particle grafu tlačítko nabídne jeho otevření.
4. Simulaci můžeš spustit, pozastavit, restartovat, změnit její rychlost a posunout časovou osu. **Loop** je ve výchozím stavu zapnutý; po vypnutí se přehrávání zastaví na konci životnosti emitteru. V náhledu lze tažením levým nebo pravým tlačítkem otáčet kameru, kolečkem přibližovat a tlačítkem **Reset View** vrátit kameru do výchozího pohledu. Mřížku lze přepnout přes **Grid**. Pomocí **Save Scene** uložíš vybraný emitter i jeho potomky jako samostatnou znovupoužitelnou scénu `.tscn`.

Náhled simuluje kopii vybraného emitteru a jeho potomků ve vlastním `SubViewport` s odděleným prostředím, světlem a kamerou. Změny vlastností emitteru i Process Material se promítají do náhledu. Pokud emitter nemá mesh nebo GPU process material, náhled doplní výchozí hodnoty pouze do této kopie; scéna se tím nemění. Úpravy vlastností používají vestavěné inspektory Godotu a jeho Undo/Redo. Node Graph je skutečný Godot `VisualShader`, nikoli vlastní formát grafu; editor nabízí nativní particle fáze a uzly Godotu.

## Aktuální rozsah

- Podporované jsou 3D GPU a CPU particle uzly. Prostředí vypisuje emittery aktivní scény, umožňuje vytvořit nový uzel a současně zobrazuje jeden vybraný uzel.
- GPU emitter může používat běžný `ParticleProcessMaterial` nebo nativní particle `VisualShader`. Graf je v této fázi uložen jako zdrojový materiál emitteru. **Save Scene** uloží emitter a potomky do `.tscn`; samostatný formát FPS Studio pro celý particle systém zatím není definován.
- Náhled kopíruje vybraný emitter a jeho potomky. Neinstancuje celou aktivní úroveň, takže kolizní nebo attractor uzly mimo jeho podstrom nejsou součástí simulace.
- Posunutí časové osy restartuje náhled a simuluje částice dopředu do zvoleného času. Nejde o přímý skok na libovolný stav.
- Náhled má referenční mřížku, orbitovací kameru a přiblížení kolečkem myši. Tvorba více emitterů jako jednoho systému, předvolby efektů, časová osa celého systému a editace 2D částic nejsou součástí této první verze.

## Implementace

Obrazovku přidává `FPSStudioParticleEditorPlugin` v `engine/editor/fps_studio/particle_editor_plugin.cpp`. Používá `EditorInspector` pro úpravy nativního uzlu a materiálu, vestavěný Godot VisualShader editor pro node graph a samostatnou kopii uzlu pro simulaci. Plugin registruje `EditorNode` bezprostředně za obrazovkou Model.
