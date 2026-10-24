# Architektura Godot FPS Studio

## Hranice částí

- `engine/` je fork Godotu 4.7.2-stable. Patří sem název aplikace, izolace nastavení a editorové funkce dostupné bez zapnutí pluginu v jednotlivých projektech.
- `engine/editor/fps_studio/` je vlastní vrstva pracovního prostředí FPS Studio. Její první modul vlastní hlavní dockový split a pravý tab rail; další vlastní UI sem bude přibývat postupně, zatímco zralé služby enginu (scény, UndoRedo, import a načítání zdrojů) zůstanou dostupné.
- `templates/fps_vertical_slice/` je herní obsah pro FPS prototyp. Gameplay zůstane primárně v GDScriptu a projektová šablona nebude určovat vzhled ani funkce editoru.
- `engine/modules/fps_runtime/` je vyhrazeno pro nativní runtime API, které nebude vhodné implementovat v GDScriptu.
- `tools/` obsahuje reprodukovatelné build příkazy. `docs/` zachycuje strukturu, kompatibilitu a další etapy.

## Výchozí stav

1. Fork vychází z Godotu **4.7.2-stable**.
2. Vývojová aplikace se sestavuje nativně pro macOS Apple Silicon (`arm64`).
3. Aplikace, titulky oken a systémová složka nastavení používají pracovní název **Godot FPS Studio**. Godot logo zůstává.
4. **Content Browser** je Godotův vestavěný FileSystem panel upravený v editorovém C++; je dostupný v každém projektu bez pluginu. První nativní úprava přidává filtrování scén, skriptů, 3D zdrojů, materiálů, textur a zvuku i rychlé založení výchozích FPS skriptů pro hráče, zbraň, interakce a Action Director.
5. Výchozí pracovní plocha je nativně přestavěná: viewport uprostřed, World Outliner a Details v pravém sloupci, Content Browser dole, hlavní režimy Level/Code/Assets. Spuštěná hra se zobrazuje přímo přes Level viewport; samostatné herní okno se volí zvláštním cílem spuštění. Při prvním otevření projektu se jednorázově převede staré rozložení; další změny už respektují uživatelské uložení.
6. Herní šablona neaktivuje žádné editorové pluginy.

## Samostatné pracovní prostředí

Godot FPS Studio je samostatně pojmenovaná a balená aplikace s vlastním nastavením. Aktuální uživatelské rozhraní ale stále běží uvnitř upstream třídy `EditorNode`; nejde zatím o kompletně přepsaný editor od nuly. Cílem je oddělit vlastní FPS Studio pracovní plochu a panely do `editor/fps_studio/`, aniž bychom znovu implementovali potřebné engine služby. Nové rozhraní má řídit rozložení a interakce; samotné přejmenování či přebarvení výchozích obrazovek nestačí.

Prvním krokem extrakce je `FPSStudioWorkspace`, který skládá hlavní dockový split a pravý tab rail mimo `EditorNode`. Další kroky mají po jednom přesouvat workspace chrome, Content Browser a Level workflow do stejné vrstvy a zachovat ukládání rozložení i přístup k editorovým službám.

## Etapy

1. [Hotovo] Připnout stabilní verzi Godotu a vytvořit první editorový build pro macOS arm64.
2. [Hotovo: základ] Přesunout značku editoru a první panel z projektového doplňku do zdrojového forku enginu.
3. [Hotovo: první krok] Přenést Content Browser do forku a přidat nativní typové filtry a rychlé akce pro založení základních FPS skriptů.
4. [Hotovo: první průchod] Přestavět výchozí pracovní plochu inspirovanou UE5; další iterací je doladit vlastní vizuální styl a nástroje horní lišty.
5. Připravit FPS vertikální řez v GDScriptu: pohyb, kamera, interakce, zbraň, AI a ukládání.
6. Přidat Hammer-style level workflow: blokování geometrie, entity, spawn pointy a převod do herní scény.
7. Přidat Action Director a GDScript timeline pro dialogy, kamery a řízené sekvence.
8. Rozšířit Play in Editor ovládání a přidat vývojový God Mode s explicitním vypnutím při exportu hry.
9. Sestavit a ověřit herní exporty Windows, macOS a Linux.

## Zásady

- Začít změnami v editorovém enginu, když funkce patří do trvalého pracovního prostředí; herní pravidla zůstanou v GDScriptu.
- Držet změny přehledně oddělené od upstream Godotu, aby šel fork aktualizovat.
- Nevyžadovat speciální ukázkový projekt k použití editorových nástrojů.
- Mac je první vývojová platforma. Vydávané hry cílí na Windows, macOS a Linux.
