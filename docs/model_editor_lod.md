# Model Editor: LOD a náhled modelu

Tento dokument popisuje aktuální chování pracovního prostoru Model v Godot FPS Studio a ukládání nastavení úrovní detailu (LOD).

## Rozložení a náhled

- Vlevo je strom scény a seznamy meshů a materiálů. Výběr meshe omezuje statistiky i LOD úpravy na tento mesh.
- Uprostřed je 3D náhled. Ovládací prvky náhledu umožňují model orámovat, přepínat drátěný režim, zapínat automatický výběr LOD nebo ručně prohlížet dostupné úrovně.
- Přes náhled se zobrazují statistiky právě vykreslovaného LOD: počet trojúhelníků a vrcholů. Podle dostupných údajů může být zobrazen také počet původních polygonů.
- Vpravo jsou oddělené karty LOD, Mesh, Physics, Lighting a Details.
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

macOS arm64 build projektu se vytváří skriptem:

```sh
./tools/build_macos.sh
```

V rámci této změny build doběhl úspěšně a výsledná aplikace prošla ověřením ad-hoc podpisu. Editor nebyl při této dokumentační změně spuštěn.
