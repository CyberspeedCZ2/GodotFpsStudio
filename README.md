# Godot FPS Studio

Samostatná aplikace postavená ze zdrojového forku Godotu 4.7.2-stable. Zachovává Godot logo a značku v hlavičce, ale aplikace, titulky oken a systémová složka nastavení používají název Godot FPS Studio. Editor je součástí upraveného enginu a funguje nad libovolným Godot projektem.

## Struktura

```text
godot-fps-fork/
├── engine/                         # upravený zdrojový kód Godotu 4.7.2
│   └── editor/fps_studio/          # vlastní vrstva pracovního prostředí, postupně vyčleňovaná
├── templates/fps_vertical_slice/   # volitelná šablona hry, bez editorových pluginů
├── docs/architecture.md            # hranice modulů a další etapy
└── tools/build_macos.sh            # sestavení a balení aplikace pro Apple Silicon
```

## První změny editoru

- Projekt Manager i editor mají název **Godot FPS Studio**; Godot logo zůstává.
- Nastavení enginu používá vlastní systémovou složku `godotfpsstudio`, oddělenou od běžné instalace Godotu.
- Výchozí pracovní plocha je přestavěná pro tvorbu levelů: velký viewport uprostřed, **World Outliner** vpravo nahoře, **Details** vpravo dole a **Content Browser** ve spodní části viewportu. Hlavní pracovní záložky jsou Level, Code a Assets; spuštěná hra se při volbě **Run in Level View** zobrazí přímo přes 3D viewport Level. Vedle tlačítka spuštění lze zvolit **Run in Separate Window**. Nabídka Editor se jmenuje FPS Studio.
- Při prvním otevření projektu fork jednorázově převede staré rozložení editoru na FPS pracovní plochu a pak už respektuje uložené uživatelské rozložení.
- Vestavěný souborový panel je pojmenovaný **Content Browser** přímo ve zdrojovém kódu enginu. Zobrazuje se s každým projektem a není závislý na ukázkové hře.
- Content Browser má typové filtry pro scény, skripty, 3D zdroje, materiály, textury a zvuk. Nabídka **FPS +** založí GDScript výchozí bod pro hráčský controller, zbraň, interakce nebo Action Director.
- FPS šablona je samostatná herní ukázka. Nepřidává editorové pluginy do nastavení projektu.

Tohle je první kompletní průchod novou pracovní plochou inspirovanou UE5. Content Browser zatím vychází z vestavěného souborového panelu Godotu. Základní Play in Editor režim teď používá Level viewport; další kroky jsou Hammer-style level nástroje, Action Director timeline v GDScriptu a God Mode.

Aplikace je samostatně pojmenovaná a balená, ale její editorové UI zatím stále běží uvnitř Godot `EditorNode`. Nejde tedy ještě o editor přepsaný od nuly. Vlastní vrstvu pracovního prostředí začíná oddělovat `engine/editor/fps_studio/FPSStudioWorkspace`; další části rozhraní se mají přesouvat postupně, při zachování potřebných engine služeb.

## Sestavení na macOS Apple Silicon

Před sestavením je potřeba alespoň 25 GiB volného místa. Sestavení využívá Xcode, Python 3.9+ a SCons 4.4+. Čisté sestavení potřebuje Vulkan SDK/MoltenVK; stávající místní SCons build cache umožňuje znovu sestavit upravené zdroje bez nové instalace SDK. Skript nic neinstaluje.

Engine binárky a výchozí Intel exporty nyní cílí na macOS 12+, aby odpovídaly MoltenVK 1.4.2 z Vulkan SDK 1.4.363.0. Aplikace FPS Studio vyžaduje podle metadat balíčku macOS 13+ na Apple Silicon a macOS 12+ na Intelu; výchozí Apple Silicon exporty her také zůstávají na macOS 13+.

```sh
./tools/build_macos.sh
```

Výsledkem je ověřený archiv `Godot FPS Studio macOS arm64.zip` s podepsanou lokálním ad-hoc podpisem aplikací `Godot FPS Studio.app`. Spuštění aplikace bez argumentů zobrazí Projekt Manager; po otevření nebo vytvoření projektu se načte upravený editor s Content Browser panelem. Šablonu hry lze importovat z `templates/fps_vertical_slice/project.godot`.

Hry budou cílit na Windows, macOS a Linux. Exportní šablony pro tyto systémy zatím nejsou sestavené.
