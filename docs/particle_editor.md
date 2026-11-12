# Pracovní prostředí Particle

Particle je samostatná obrazovka FPS Studia pro tvorbu a náhled nativních 3D particle uzlů Godotu. Upravuje vybraný uzel a jeho existující zdroje; nezavádí nový formát simulace ani assetů.

## Pracovní postup

1. V aktivní scéně přidej nebo vyber uzel `GPUParticles3D` či `CPUParticles3D`. Editor pro něj otevře pracovní prostředí Particle.
2. V seznamu **Emitters** můžeš přepínat mezi 3D particle uzly ve scéně.
3. V inspektoru **Emitter** upravuješ vlastnosti uzlu. U GPU uzlů je dostupný také materiál v inspektoru **Process Material**.
4. Simulaci můžeš spustit, pozastavit, restartovat, změnit její rychlost a posunout časovou osu. **Loop** je ve výchozím stavu zapnutý; po vypnutí se přehrávání zastaví na konci životnosti emitteru. V náhledu lze tažením levým nebo pravým tlačítkem otáčet kameru a kolečkem přibližovat.

Náhled simuluje kopii vybraného emitteru a jeho potomků ve vlastním `SubViewport` s odděleným prostředím, světlem a kamerou. Pokud emitter nemá mesh nebo GPU process material, náhled doplní výchozí hodnoty pouze do této kopie; scéna se tím nemění. Úpravy vlastností používají vestavěné inspektory Godotu a jeho Undo/Redo.

## Aktuální rozsah

- Podporované jsou 3D GPU a CPU particle uzly. Prostředí vypisuje emittery aktivní scény a současně zobrazuje jeden vybraný uzel.
- Náhled kopíruje vybraný emitter a jeho potomky. Neinstancuje celou aktivní úroveň, takže kolizní nebo attractor uzly mimo jeho podstrom nejsou součástí simulace.
- Posunutí časové osy restartuje náhled a simuluje částice dopředu do zvoleného času. Nejde o přímý skok na libovolný stav.
- Náhled má referenční mřížku, orbitovací kameru a přiblížení kolečkem myši. Tvorba více emitterů jako jednoho systému, předvolby efektů, časová osa celého systému a editace 2D částic nejsou součástí této první verze.

## Implementace

Obrazovku přidává `FPSStudioParticleEditorPlugin` v `engine/editor/fps_studio/particle_editor_plugin.cpp`. Používá `EditorInspector` pro úpravy nativního uzlu a materiálu a samostatnou kopii uzlu pro simulaci. Plugin registruje `EditorNode` bezprostředně za obrazovkou Model.
