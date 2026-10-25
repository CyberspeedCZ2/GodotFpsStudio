# Dynamická navigace NPC — V1

## Cíl

Přidat samostatný dynamický plánovač vedle původní navigace Godotu. `Path3D` dál určuje patrolní cíle a jejich akce; nový plánovač mění pouze aktuální cestu NPC k jednomu cíli. Nemaže ani nepřepisuje waypointy.

## Vrstvy

- `DynamicNavigationAgent3D` dědí z vestavěného `NavigationAgent3D` a přidává stav cesty, fyzikální kontrolu průchodnosti a jednoduchou objížďku.
- Beehave `ActionMoveToPoint` předá cíl agentovi a čeká na stav `FOLLOWING`, `PLANNING`, `BLOCKED` nebo `ARRIVED`. Při závalu zůstává akce aktivní a agent pravidelně zkouší plánovat znovu.
- `NPCController` odpovídá za akceleraci, brzdění a natočení postavy. Při plánování a změně směru zpomaluje plynule.
- Avoidance Godotu se dál používá pro souběh NPC. Fyzikální dotazy vlastního agenta kontrolují statické i dynamické překážky podle collision masky NPC.

## V1: jedna překážka

Agent kontroluje fyzikální kapslí koridor před NPC, nejvýše do nastaveného lookaheadu. Pokud původní NavMesh cesta narazí na překážku, V1 zkusí obejít jednu překážku ze stran: odvodí přibližný půdorys boxu, promítne body před a za překážku na NavMesh a každý úsek kandidátní trasy znovu ověří kapslí. Vybere kratší průchodnou variantu. Když ověřenou objížďku nenajde, oznámí `BLOCKED` a s odstupem plánování zopakuje.

Kvůli Jolt Physics se `ShapeRestInfo` nepředává jako výstup `cast_motion()`. Po nalezení zásahu agent samostatným `rest_info()` dotazem na nebezpečné pozici určí zasažený collider; při neúspěchu použije `intersect_shape()`.

Tato verze neumí ještě plánovat kolem shluku beden, více navazujících překážek ani ukládat naučené cesty. Výsledek je proto základní experiment pro ověření agentu na skutečném NPC. Další práce má navazovat na pozorované selhání a rozšiřovat plánovač po malých krocích.

## Záměrně odstraněný starý detektor

Z controlleru byly odstraněny paprskové sondy pro plánování, lokální A*, dočasné stavy rozhlížení, cache objížděk a vkládání objížděk do `Curve3D`. Z Beehave stromu byly odstraněny detekční uzly `ActionScanAndRepath` a `ConditionPathBlocked`. Pasivní `VisionCamera` pro F4 pohled z očí NPC zůstává pouze jako ladicí nástroj; o překážkách nerozhoduje. Reakce na hráče, patrola, waypointové akce, pohyb, zdraví, strach a interakce s překážkami zůstávají zachované.

## Soubory

- Nativní agent: `engine/modules/navigation_3d/dynamic_navigation_agent_3d.h/.cpp`
- Registrace třídy: `engine/modules/navigation_3d/register_types.cpp`
- NPC scéna a controller: `Scenes/NPC/basic_npc.tscn`, `Scripts/NPC/npc_controller.gd`
- Beehave pohyb: `Scripts/NPC/BT/action_move_to_point.gd`
- Vestavěná šablona controlleru: `engine/modules/gdscript/editor/script_templates/CharacterBody3D/fps_npc_controller.gd`

## Ověřování

Pro tuto iteraci proběhla kompilace macOS arm64 dev buildu a headless načtení projektu kvůli kontrole parseru GDScript. Editor ani hra se nespouštěly. Skutečné chování při pohybu kolem beden, více překážkách a v různých geometriích NavMeshe ještě vyžaduje uživatelské ověření v běžící hře.

## Zobrazení plánované trasy

Za běhu hry klávesa F4 přepíná 3D zobrazení trasy všech NPC ve skupině `fps_npc`. Modrá tenká linka kreslí celou baked křivku patrolního `Path3D`; silnější linka ukazuje aktuální vypočtený úsek `DynamicNavigationAgent3D`: zelená znamená následovanou cestu, žlutá plánování a červená blokovanou cestu. Celá patrolní křivka se vykreslí jednou při zapnutí F4 a zůstává v souřadnicích světa; pohyblivý úsek se obnovuje 10× za sekundu, aby debug overlay nezatěžoval každým fyzikálním snímkem. Pokud agent vrátí bod přímo pod NPC a nemá další použitelný bod, ovladač po krátké prodlevě požádá o přepočítání cesty. Pasivní pohled z očí NPC zůstává dostupný přes `get_vision_camera()`; F4 je vyhrazené trase.
