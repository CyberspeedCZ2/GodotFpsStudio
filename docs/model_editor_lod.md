# Model Editor: LOD a náhled modelu

Tento dokument popisuje aktuální chování pracovního prostoru Model v Godot FPS Studio a ukládání nastavení úrovní detailu (LOD).

## Rozložení a náhled

- Vlevo je strom scény a seznamy meshů a materiálů. Výběr meshe omezuje statistiky i LOD úpravy na tento mesh.
- Uprostřed je 3D náhled. Ovládací prvky náhledu umožňují model orámovat, přepínat drátěný režim, zapínat automatický výběr LOD nebo ručně prohlížet dostupné úrovně.
- Pod modelem je barevně tlumená perspektivní mřížka a měkký stín. Mřížka se s dálkou ztrácí do barevného obzoru hloubkovou mlhou, která začíná přibližně 60 m od kamery. Model View má vlastní procedurální oblohu s mraky, slunce, ambientní osvětlení i odrazy; nastavení otevřené Level scény se do náhledu nepřenášejí. Otáčení kamery nemění polohu světla ani stínu. Pomocná podlaha patří pouze náhledu a nepřidává se do importovaného modelu.
- Přes náhled se zobrazují statistiky právě vykreslovaného LOD: počet trojúhelníků a vrcholů. Podle dostupných údajů může být zobrazen také počet původních polygonů.
- Vpravo je obsah nastavení a vedle něj svislá navigace v pořadí **LOD, Mesh, Physics, Hitboxes, Lighting, Details, Material, Environment**. Popisky navigace jsou otočené o 90°; tlačítka reagují na najetí myší, začínají od horního okraje a vybraná karta zůstává zvýrazněná. Obsah záložek zůstává vodorovný a při nízkém okně lze navigaci posouvat. Environment řídí pouze vzhled náhledu Model View: čas a směr slunce, sklon jeho denní dráhy, intenzitu světla, animaci den/noc a zapnutí, hustotu, velikost i rychlost mraků. Hodnoty a vybraná karta se ukládají pro projekt editoru a nijak nemění Level ani importovaný model. Šířka levého stromu a pravého panelu se ukládá do nastavení editoru.

## Hitboxy modelu

Karta **Hitboxes** umožňuje přidávat a odebírat pojmenované boxy. Každý box má skupinu zásahu, kost, ke které je připojen, a rohy **Minimum** a **Maximum** v lokálním prostoru zvolené kosti. Volba **Model Root** místo kosti použije lokální prostor kořene scény. Náhled zobrazuje všechny boxy; vybraný box je zvýrazněný. Přidání a odebrání používá kompaktní ikonová tlačítka.

Skupiny zásahu odpovídají běžným Source hitgroup číslům: 0 Generic, 1 Head, 2 Chest, 3 Stomach, 4 Left Arm, 5 Right Arm, 6 Left Leg a 7 Right Leg. Rozpracované nastavení se ukládá vedle zdrojového modelu do souboru `.hitboxcfg`. Tlačítko **Apply Changes** při importu `PackedScene` vytvoří pro každý záznam `Area3D` s `CollisionShape3D` ve tvaru kvádru; číslo hitgroup a rohy jsou dostupné jako metadata oblasti. Připojení ke kosti je realizováno přes `BoneAttachment3D`. Toto je Godot import, nikoli generátor Valve QC `$hbox`/`$hboxset` příkazů.
- Kamera se při prvním otevření přizpůsobí mezím modelu. Výpočet používá poloměr obalující koule, takže samotné otáčení modelu nemění vzdálenost kamery. Tlačítko Frame znovu vystředí a přiblíží aktuální obsah; zoom a posun zůstávají ručním ovládáním.

## Nastavení LOD

Na kartě **LOD** lze zapnout nebo vypnout generování LOD, zvolit předvolbu (Automatic, Quality, Balanced nebo Performance) a upravovat jednotlivé úrovně. LOD 0 je původní mesh a nelze jej odstranit. Další úrovně lze přidávat a odstraňovat; podporováno je nejvýše 10 generovaných úrovní.

U vybrané úrovně jsou dostupné:

- **Retain triangles (%)** – cílový podíl trojúhelníků zachovaný oproti předchozí úrovni. Zjednodušovač nemusí přesného cíle dosáhnout.
- **Reference target (m)** – přibližný cíl vzdálenosti přechodu pro referenční kameru. Ovládá načasování přechodu a aktualizuje živý náhled.

Odhad vzdálenosti používá referenční kameru 1920 × 1080, vertikální zorné pole 75°, měřítko instance 1, LOD bias 1 a aktuální projektový práh LOD. Jde o orientační hodnotu: skutečné přepnutí závisí také na kameře, rozlišení, měřítku instance, projektových nastaveních a jednotlivých površích meshe.

Seznam úrovní zobrazuje počty trojúhelníků a vrcholů. Během přepočtu náhledu jsou neaktuální hodnoty označené „Updating…“. Po dokončení se nedostupné úrovně zobrazí jako „Unavailable“; čekání na tlačítko Apply Changes se od právě probíhající aktualizace rozlišuje. Po odstranění úrovně se výběr zachová u stejného zbývajícího LOD, případně přejde na nejbližší úroveň, pokud byla odstraněna právě vybraná.

## Náhled bez opakovaného importu

Změna LOD nejprve vytvoří náhled z uloženého zdrojového `ImporterMesh`. Po krátké prodlevě se přepočítají geometrie, statistiky a automatické přepínání v náhledu. Běžné úpravy LOD proto nespouštějí úplný import modelu.

Rozpracované nastavení se průběžně ukládá do doprovodného souboru:

```text
<cesta-k-modelu>.lodcfg
```

Soubor používá formát `ConfigFile`, obsahuje verzi konfigurace a nastavení podle stabilního ID každého meshe. Uchovává zapnutí generování, úhel slučování normál, předvolbu nebo vlastní počet úrovní a nastavení jednotlivých úrovní. Uložení proběhne také při zavření nebo opětovném otevření panelu.

Tlačítko **Apply Changes** je okamžik, kdy se uložená nastavení použijí při generování importovaného projektového assetu. Úpravy se tak mohou průběžně ukládat a kontrolovat v náhledu, zatímco finální import proběhne až po dokončení práce. Pokud se má konfigurace používat na dalších počítačích nebo sdílet s týmem, verzujte soubor `.lodcfg` společně se zdrojovým modelem.

Per-mesh konfigurace vyžaduje stabilní import ID v původním souboru. Pokud je mesh bez něj, editor tuto skutečnost zobrazí a nastavení pro něj nelze spolehlivě ukládat.

## Build

macOS arm64 aplikaci sestaví a nainstaluje skript:

```sh
./tools/build_macos.sh
```

Výsledkem je `Godot FPS Studio.app` v kořeni repozitáře. Build skript nevytváří ZIP archiv.
