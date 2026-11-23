# Particle: revize, opravy a nové funkce (23. 11. 2026)

Záznam práce z 23. 11. 2026 na pracovním prostředí Particle a na runtime efektů. Návod k používání je v [particle_editor.md](particle_editor.md); tento dokument říká, co se změnilo, proč a jak to bylo ověřeno.

## Commity

| Repozitář | Commit | Obsah |
| --- | --- | --- |
| GodotFpsStudio | `46594439` | opravy z revize, Spawn Effect, distorze, prolínání snímků |
| GodotFpsStudio | `dc6bdf16` | únik shaderu částic (klíč `ParticleProcessMaterial`) |
| GodotFpsStudio | `07bcfcec` | paprsky, decaly z kapek, záblesk obrazu, vítr, parametry, směr dopadu |
| GodotFpsStudio | `eaa71e4c` | sprška vodního výbuchu viditelná v náhledu |
| project-hl-2-inspiration | `a16f1dc` | zbraně předávají směr střely tabulce zásahů |
| project-hl-2-inspiration | `4c74cae` | rozpracovaný vodní výbuch (pěna s vlastní texturou) |

## Jak to bylo ověřeno

- Headless test runtime (45 kontrol) a headless test editoru (38 kontrol), oba bez chyb.
- Snímky z malého herního okna: distorze, prolínání snímků flipbooku, tři podoby paprsku, sprška s hladinou a bez ní.
- Hra na novém enginu naběhla bez chyb, které by souvisely s efekty.
- **V GUI editoru nebylo ověřeno nic.** Vzhled a ovládání v editoru zbývá projít ručně.

## 1. Revize a opravy

Kód prošli čtyři revizní agenti (dvě poloviny pluginu, časová osa se zásobníkem modulů, převodníkem a presety, runtime třídy). Nálezy byly před opravou ověřeny v kódu.

### Hra (`ParticleEffect3D`)

- **`stop()`**: události, které ještě nezačaly, už nenastanou (zvuk, otřes, zranění, spawn, světlo, decal). Trvalé světlo a trvalý decal zhasnou s posledními částicemi. Efekt čekající na kameru nebo na místo v rozpočtu `stop()` ukončí hned.
- **Rozpočet efektů**: smyčkový nebo nepřetržitý efekt (oheň, pára) se při plném rozpočtu jen uspí a rozjede se znovu, až se uvolní místo. Dřív zmizel natrvalo.
- **`seek()`** nespustí události z času 0 (ve hře by znovu udělil zranění výbuchu).
- **Kolize levelu**: `AnimatableBody3D` (výtah, dveře) se nezapéká.
- **`speed_scale`** efektu zrychluje i částice; emitor s vlastním Speed Scale má správně spočítaný konec.

### Editor

- Undo po zavření nebo výměně efektu nesahá do uvolněných uzlů (dřív možný pád).
- **Close Without Saving** opravdu zahodí úpravy materiálů a křivek (efekt se načítá s vlastními kopiemi).
- **Délka náhledu**: se všemi stopami ztlumenými se počítá jako na časové ose a zkrácení efektu náhled nepozastaví. Pravděpodobná příčina zaseknutých „0,1 / 0,1 s".
- Úprava za běhu znovu nespouští dohrané emitory; Speed Scale emitoru se projeví i v náhledu.
- **Časová osa**: koncová vlajka sleduje délku efektu, scrub se chytá hran i na mřížce, F podruhé ukáže celý efekt, prostřední tlačítko neruší rozdělané tažení.
- **Stopy**: mazání funguje i u vnořených stop a Undo vrátí stopu na místo, přejmenování zachová odkaz Sub Emitter, kopie mají vlastní křivky.
- **Ukládání**: Cmd+S nehýbe playheadem, nezdařené „Save and Close" nezavře efekt při příštím uložení.
- **Karta Render** u emitoru s vlastním shaderem ukáže jeho parametry a nenabízí Create Particle Material.
- **Karty Rotation a Sprite Sheet** upozorní, když s daným natočením nic nedělají; popisky „Sheet Plays per Life" a „Start Position (0–1)".
- **Convert to Node Graph**: záporné rozsahy zrychlení, flipbook s náhodnou životností, odraz.
- **Presety**: nábojnice, kusy a kosti se na konci života zmenší místo skokového zmizení.

### Únik shaderu částic

Hra při ukončení hlásila „ParticlesShaderRD were never freed". Klíč, podle kterého si `ParticleProcessMaterial` hledá svůj shader, využíval 46 ze 64 bitů a porovnává se po bajtech; v optimalizovaném buildu zůstávaly ve zbylých bitech náhodné hodnoty, takže materiál svůj shader nenašel a neuvolnil. Zbytek 64 bitů je teď vynulované pole `unused` a velikost hlídá `static_assert` (`engine/scene/resources/particle_process_material.h`). Při přidání bitu do klíče je potřeba `unused` zmenšit.

### Sprška vodního výbuchu v editoru

Emitor **Spray** nebyl v náhledu vidět, ve hře ano. Jeho shader ukazuje částice jen nad hladinou, kterou ve hře nastavuje bazén; v editoru ji nikdo nenastavil. Náhled teď dává spršce hladinu do počátku efektu a shader v presetech bez nastavené hladiny nic neořezává.

Omezení: stávající scény (`water_explosion.tscn`, `water_splash.tscn`) mají v sobě vlastní kopii starého shaderu. V Level editoru bez bazénu v nich sprška dál vidět není.

## 2. Nové funkce z revize

- **Spawn Effect** (Add → Events, `EffectSpawner3D`): přehraje jiný efekt nebo vytvoří scénu (gibs, trosky, druhý výbuch).
- **Distorze**: menu ⋮ karty Render → Distortion Material (Heat Haze / Shock Wave).
- **Blend Frames** v kartě Sprite Sheet: prolíná sousední snímky flipbooku.
- **3D Rotation a 3D Spin** v kartě Rotation pro mesh částice.
- **Stay in World when Attached** v kartě Emitter: záblesk drží na zbrani, kouř zůstane ve světě.
- Další řádky modulů: Amount Ratio, Orbit Speed, Align to Flight Direction, Drag as Friction, úhel kužele prstence, textury pro tvary Points, nastavení attractorů.
- **Kvalita a LOD**: `lod_distance`, `lod_amount` a nastavení projektu `rendering/particle_effects/amount_scale`.
- **Pauza**: `paused` na efektu.

## 3. Dvanáct funkcí podle UE5 a Source

1. **Paprsky** – stopa Beam (`EffectBeam3D`): pás mezi dvěma body natočený ke kameře, s vlněním, zúžením a rolující texturou. Ze hry `effect.set_target_position(bod)`.
2. **Směr dopadu** – tabulka zásahů bere směr střely a nakloní úlomky k odrazu (`reflect_amount`). Decal zůstává na normále. Hra už směr předává.
3. **Decaly z kapek** – stopa Splatter Decals (`SplatterDecals3D`): paprsky v kuželu, decal na zasaženém povrchu se zpožděním podle doby letu. Jen ve spuštěné hře, nejvýš 64 decalů.
4. **Osvětlení kouře ze směru** – zjednodušeně: normálová mapa z generátoru přes Apply to Emitter a řádky Normal Map, Normal Strength, Backlight. Skutečné „six-way" světelné mapy hotové nejsou.
5. **Vítr** – `ParticleLevelSetup.wind`, na emitor řádek Wind Influence (karta Forces).
6. **Varianty zásahů** – slovník `variants` v tabulce (povrch → pole scén).
7. **Parametry efektu** – `effect.set_parameter("power", 0.4)` a řádek Parameter Links (karta Emitter).
8. **Rychlost zdroje** – `effect.set_inherited_velocity(rychlost)` pro emitory s Inherit Velocity.
9. **Záblesk obrazu** – stopa Screen Flash (`ScreenFlash3D`).
10. **Pohyb v náhledu** – View → Move Effect (Circle).
11. **Overdraw** – View → Overdraw.
12. **Mizení u kamery** – řádek Camera Fade v kartě Render a preset Fade near Camera.

## Co zbývá

- Ruční ověření všeho výše v GUI editoru a ve hře.
- Parametry a vítr se v náhledu editoru neprojeví, jen ve hře.
- Varianty, vítr, parametry a Splatter Decals ve hře zatím nic nepoužívá.
- Neuděláno: skutečné „six-way" osvětlení kouře, emise z povrchu meshe, přepečení kolize po zničení statického tělesa, moduly Trails/Collision/Sub Emitter u emitoru s vlastním particle shaderem, přepsání starého shaderu ve stávajících vodních scénách.
