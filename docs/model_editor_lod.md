# Model Editor: LOD a náhled modelu

Tento dokument popisuje aktuální chování pracovního prostoru Model v Godot FPS Studio a ukládání nastavení úrovní detailu (LOD).

## Rozložení a náhled

- Vlevo je strom scény a seznamy meshů a materiálů. Výběr meshe omezuje statistiky i LOD úpravy na tento mesh.
- Uprostřed je 3D náhled. Ovládací prvky náhledu umožňují model orámovat, přepínat drátěný režim, zapínat automatický výběr LOD nebo ručně prohlížet dostupné úrovně.
- Tlačítko **Helpers** zobrazí a orámuje prázdné transformové uzly mimo meze meshe, které mohou roztahovat výběrový rámeček v Levelu. Jejich polohy a názvy se vykreslí v náhledu. Vyber uzel ve stromu a v kartě **Details** ho můžeš převést na jiný typ přes **Node Type** nebo ho vynechat z importu přes **Skip Import**; změna se použije při opětovném importu.
- Pod modelem je barevně tlumená perspektivní mřížka a měkký stín. Mřížka se s dálkou ztrácí do barevného obzoru hloubkovou mlhou, která začíná přibližně 60 m od kamery. Model View má vlastní procedurální oblohu s mraky, slunce, ambientní osvětlení i odrazy; nastavení otevřené Level scény se do náhledu nepřenášejí. Otáčení kamery nemění polohu světla ani stínu. Pomocná podlaha patří pouze náhledu a nepřidává se do importovaného modelu.
- Přes náhled se zobrazují statistiky právě vykreslovaného LOD: počet trojúhelníků a vrcholů. Podle dostupných údajů může být zobrazen také počet původních polygonů.
- Vpravo je obsah nastavení a vedle něj svislá navigace v pořadí **LOD, Mesh, Physics, Hitboxes, Lighting, Details, Material, Environment**. Popisky navigace jsou otočené o 90°; tlačítka reagují na najetí myší, začínají od horního okraje a vybraná karta zůstává zvýrazněná. Obsah záložek zůstává vodorovný a při nízkém okně lze navigaci posouvat. Kliknutí na právě otevřenou záložku panel nastavení schová (zůstane jen svislá navigace a náhled se roztáhne), kliknutí na libovolnou záložku ho zase ukáže; stav se pamatuje pro projekt. Panel nejde zúžit pod šířku, kterou potřebuje nejširší záložka. Mraky se posouvají jen když jsou zapnuté a mají nenulovou rychlost (obloha se jinak nepřekresluje každý snímek), slunce se při animaci posouvá 30× za sekundu. Environment řídí pouze vzhled náhledu Model View: čas a směr slunce, sklon jeho denní dráhy, intenzitu světla, animaci den/noc a zapnutí, hustotu, velikost i rychlost mraků. Hodnoty a vybraná karta se ukládají pro projekt editoru a nijak nemění Level ani importovaný model. Výběr ve stromu kartu nepřepíná, pokud se hodí i pro nový výběr (Hitboxes, Lighting, Details, Environment); z karet pro mesh (LOD, Mesh, Physics) se po výběru uzlu přejde na Details, výběr materiálu otevře Material. Šířka levého stromu a pravého panelu se ukládá do nastavení editoru.

## Hitboxy modelu

Karta **Hitboxes** zobrazuje seznam se samostatnými sloupci **Name** a **Hitgroup**, takže název boxu zůstává oddělený od jeho skupiny zásahu. Menu předvoleb přidá přibližnou sadu sedmi oblastí humanoidního těla nebo jednotlivou část. Rozměry se odhadnou z mezí modelu a pokud se najde odpovídající běžně pojmenovaná kost, box se na ni připojí; výsledek lze ručně upravit. Předvolby se přidávají k existujícím hitboxům. Hitgroupy mají v seznamu i náhledu odlišné barvy a vybraný box je zvýrazněný. Panel vybraného boxu umožňuje upravit název, skupinu, pevné poškození **Damage (HP)**, připojenou kost a rohy **Minimum** a **Maximum** v lokálním prostoru kosti. Volba **Model Root** místo kosti použije lokální prostor kořene scény. Kompaktní ikonová tlačítka přidávají, duplikují nebo odebírají box. Vedle nich jsou **Undo** a **Redo** (také Ctrl/Cmd+Z a Shift+Ctrl/Cmd+Z, dokud je fokus v kartě Hitboxes) a **Revert to Imported**, které vrátí hitboxy zapsané posledním Apply Changes. Historie platí pro otevřený model (až 100 kroků); psaní názvu nebo tažení jedné hodnoty je jeden krok. Duplikace kopíruje jeho rozměry, hitgroup, poškození i připojení a vytvoří jedinečný název. Název se po potvrzení (Enter nebo opuštění pole) upraví na jedinečný a neprázdný, protože z něj vzniká jméno uzlu a overridy poškození v levelu se ukládají podle cesty k uzlu. Změna kosti (nebo Model Root) převede rohy do prostoru nové kosti, takže box zůstane na místě.

Skupiny zásahu odpovídají běžným Source hitgroup číslům: 0 Generic, 1 Head, 2 Chest, 3 Stomach, 4 Left Arm, 5 Right Arm, 6 Left Leg a 7 Right Leg. Výchozí poškození pro model s NPC o 100 HP je 100 HP pro hlavu, 40 HP pro hrudník, 30 HP pro břicho a 15 HP pro ostatní skupiny; každou hodnotu lze změnit. Rozpracované nastavení se ukládá vedle zdrojového modelu do souboru `.hitboxcfg`; model bez hitboxů prázdný soubor nevytváří. Pokud při importu chybí kostra nebo kost hitboxu (třeba ji přejmenovala bone mapa), import hitbox vynechá s chybou ve výstupu místo toho, aby ho položil ke kořeni s rohy v prostoru kosti. Tlačítko **Apply Changes** při importu `PackedScene` vytvoří pro každý záznam `Area3D` s `CollisionShape3D` ve tvaru kvádru; hitgroup, pevné poškození v HP a rohy jsou dostupné jako metadata oblasti. Tyto uzly používají samostatnou fyzikální vrstvu 20, aby je hitscan mohl vyhodnotit odděleně od kapsle NPC a běžných triggerů. V otevřeném Levelu lze hodnotu změnit pro konkrétní instanci: v Scene/World Outlineru zapnout na instanci importovaného modelu **Editable Children**, vybrat příslušný uzel hitboxu `Area3D` a v Inspectoru upravit **Metadata → damage**. Tím se uloží override do scény Levelu, zatímco výchozí hodnota zdrojového modelu zůstane stejná. Připojení ke kosti je realizováno přes `BoneAttachment3D`. Hitscan zbraň, která tuto metadata podporuje, použije pevné poškození hitboxu; při zásahu modelu bez FPS hitboxu použije vlastní `damage_per_shot`. V Level viewportu UniBuilder tyto importované FPS hitboxy vykresluje jako průhledné barevné kvádry se zřetelnými hranami, stejnými barvami podle hitgroup jako v Model View. Trigger ikony jejich uzlů `Area3D` se nezobrazují, aby nekolidovaly s náhledem hitboxů. Tlačítko **Hitboxes** vedle **Grid** v UniBuilder liště přepíná celé toto zobrazení; běžných kolizních tvarů ani ikon ostatních `Area3D` se nedotýká. Volba se ukládá pro projekt. Toto je Godot import, nikoli generátor Valve QC `$hbox`/`$hboxset` příkazů.
- Kamera se při prvním otevření přizpůsobí mezím modelu. Výpočet používá poloměr obalující koule, takže samotné otáčení modelu nemění vzdálenost kamery. Tlačítko Frame znovu vystředí a přiblíží aktuální obsah; zoom a posun zůstávají ručním ovládáním.

## Nastavení LOD

Na kartě **LOD** lze zapnout nebo vypnout generování LOD, zvolit předvolbu (Automatic, Quality, Balanced nebo Performance) a upravovat jednotlivé úrovně. LOD 0 je původní mesh a nelze jej odstranit. Další úrovně lze přidávat a odstraňovat; podporováno je nejvýše 10 generovaných úrovní.

U vybrané úrovně jsou dostupné:

- **Retain triangles (%)** – cílový podíl trojúhelníků zachovaný oproti předchozí úrovni. Zjednodušovač nemusí přesného cíle dosáhnout.
- **Reference target (m)** – přibližný cíl vzdálenosti přechodu pro referenční kameru. Ovládá načasování přechodu a aktualizuje živý náhled.

Odhad vzdálenosti používá referenční kameru 1920 × 1080, vertikální zorné pole 75°, měřítko instance 1, LOD bias 1 a aktuální projektový práh LOD. Jde o orientační hodnotu: skutečné přepnutí závisí také na kameře, rozlišení, měřítku instance, projektových nastaveních a jednotlivých površích meshe.

Seznam úrovní zobrazuje počty trojúhelníků a vrcholů. Během přepočtu náhledu jsou neaktuální hodnoty označené „Updating…“. Po dokončení se nedostupné úrovně zobrazí jako „Unavailable“; čekání na tlačítko Apply Changes se od právě probíhající aktualizace rozlišuje. Po odstranění úrovně se výběr zachová u stejného zbývajícího LOD, případně přejde na nejbližší úroveň, pokud byla odstraněna právě vybraná.

## Náhled bez opakovaného importu

Změna LOD nejprve vytvoří náhled z uloženého zdrojového `ImporterMesh`. Po krátké prodlevě se přepočítají geometrie, statistiky a automatické přepínání v náhledu. Běžné úpravy LOD proto nespouštějí úplný import modelu. Zdrojem náhledu je samotný `ImporterMesh` (generování LOD jeho data nemění), takže se při otevření nekopírují všechny meshe. U modelu nad 250 000 trojúhelníků se LOD náhledu negenerují hned při otevření, ale až když mesh poprvé vybereš; do té doby ukazuje náhled celé scény jen LOD 0. (Škola i policista mají kolem 41 000 trojúhelníků, generování trvá řádově 20 ms.)

Rozpracované nastavení se průběžně ukládá do doprovodného souboru:

```text
<cesta-k-modelu>.lodcfg
```

Soubor vzniká, až když se LOD nějakého meshe upraví (otevření modelu ho nevytváří). Volba automatických úrovní se do něj zapisuje výslovně, aby ji po znovuotevření nepřebily vlastní úrovně z `.import`. Rozpracovaná úprava se dokončí i při přepnutí výběru na jiný mesh dřív, než doběhne prodleva, a během tažení posuvníku se náhled nepřepočítává (posuvník by se uvolnil). Soubor používá formát `ConfigFile`, obsahuje verzi konfigurace a nastavení podle stabilního ID každého meshe. Uchovává zapnutí generování, úhel slučování normál, předvolbu nebo vlastní počet úrovní a nastavení jednotlivých úrovní. Uložení proběhne také při zavření nebo opětovném otevření panelu.

Když jsou rozpracované změny nastavení importu nebo materiálů a otevře se jiný model nebo se Model View zavře, editor se zeptá: **Apply Changes** je zapíše, **Discard** je zahodí, zavření dialogu nechá model otevřený. Úpravy LOD a hitboxů se neptají, protože se ukládají do `.lodcfg` a `.hitboxcfg`. Tlačítko **Apply Changes** je okamžik, kdy se uložená nastavení použijí při generování importovaného projektového assetu. Úpravy se tak mohou průběžně ukládat a kontrolovat v náhledu, zatímco finální import proběhne až po dokončení práce. Pokud se má konfigurace používat na dalších počítačích nebo sdílet s týmem, verzujte soubor `.lodcfg` společně se zdrojovým modelem.

Per-mesh konfigurace vyžaduje stabilní import ID v původním souboru. Pokud je mesh bez něj, editor tuto skutečnost zobrazí a nastavení pro něj nelze spolehlivě ukládat.

## Build

macOS arm64 aplikaci sestaví a nainstaluje skript:

```sh
./tools/build_macos.sh
```

Výsledkem je `Godot FPS Studio.app` v kořeni repozitáře. Build skript nevytváří ZIP archiv.
