# CoCreate Modeling 2008 v16: workflowanalyse voor OpenDesigner

> Historische analyse van de hieronder genoemde v0.3-broncode. De afwijkende
> rotatiepivot van het menucommando is in v0.3.1 verholpen; zie
> [consistente rotatiepivot](ROTATION_PIVOT.md) voor het actuele gedrag en tests.


## Afbakening en werkwijze

Deze eigen analyse vergelijkt de lokaal aangeleverde Getting Started-documentatie
met OpenDesigner op `develop`, commit
`fcb94056f45f2d866fef88bc54fd853fcd92ec5b` (v0.3, inclusief Boolean Operations).
De werkmap was bij aanvang schoon. Er is geen applicatiecode aangepast en geen
commit of push gemaakt voor dit onderzoek.

De referentiemap is `/home/Jeroen/SolidDesignerReference/GettingStarted`.
Alle 117 HTML-bestanden zijn geïnventariseerd en op de relevante onderwerpen
doorzocht. De hieronder genoemde themapagina's zijn nader gelezen; enkele
illustraties bij browser, taakpanelen, Machine en vlakrotatie zijn uitsluitend
lokaal bekeken. De verwijzingen in dit document wijzen naar de oorspronkelijke
HTML-bestanden buiten de repository. Ze zijn dus machinegebonden.

Dit document bevat eigen beschrijvingen en aanbevelingen, geen overgenomen
handleidingteksten, screenshots, iconen, modellen of andere referentiebestanden.
Commandonamen dienen alleen om functies te identificeren.

De map wordt door de opdrachtgever als v16-referentie aangeleverd. Sommige
illustraties tonen de oudere productbenaming OneSpace Designer. Daaruit volgt
geen zelfstandige verificatie van een specifieke runtime/build. Het onderzoek
betreft het aangeleverde lesmateriaal, geen uitgevoerde vergelijking met een
geïnstalleerde originele applicatie.

De vijf historische hoofddocumenten `AGENTS.md`, `ARCHITECTURE.md`, `ROADMAP.md`,
`DECISIONS.md` en `CODEX_HANDOFF.md` ontbreken op deze `develop`. Hun beschikbare
versies op `feature/v0.20-create-cylinder` zijn gelezen als projectkader:
v16 als gedragsreferentie, GPL-compatibele eigen implementatie, geen kopiëren van
propriëtair materiaal en behoud van bestaande desktopbediening. Verouderde
technische aannames daarin zijn niet als huidige toestand gebruikt. Voor die
toestand zijn de broncode en de actuele [roadmap](ROADMAP.md),
[Sphere-documentatie](SPHERE_PULL.md), [Cone-documentatie](CONE_PULL.md) en
[Boolean-documentatie](BOOLEAN_OPERATIONS.md) leidend.

### Bewijsniveaus

- **Bevestigd door tekst:** expliciet beschreven gedrag in een genoemde HTML-pagina.
- **Zichtbaar in illustratie:** een lokaal bekeken afbeelding binnen die pagina;
  aanwezigheid/organisatie van een bedieningselement, niet automatisch zijn gedrag.
- **OpenDesigner-broncode:** statische analyse van de genoemde commit.
- **Aanbeveling:** een eigen ontwerpvoorstel, geen claim over ontbrekende v16-details.

`Unite_viewlet_swf.html` verwijst naar `Unite.swf`, maar er zijn geen SWF-bestanden
in de aangeleverde map aangetroffen. Die demonstratie is niet afgespeeld. Er is
voor dit document geen build, CTest of GUI-test uitgevoerd; bestaande testcode is
gelezen, niet opnieuw als bewijs van interactiegedrag uitgevoerd.

## Hoofdbevinding

OpenDesigner heeft inmiddels een bruikbare geometrische basis: parametrische
primitives, directe maatvelden, onderdeeltransformaties, volumezwaartepunt-gizmo's,
Booleans en een gedeelde undo/redo-geschiedenis. De grootste workflowafstand zit
in de overgang van **selectie naar commando naar referentie naar bevestiging**.

De handleiding gebruikt zowel een commando vóór selectie als voorselectie gevolgd
door een contextcommando. Manipulators horen bij die bewerking en kunnen aan
modelreferenties worden gekoppeld. OpenDesigner gebruikt meerdere blijvende
modusschakelaars, verschillende soorten modaliteit en grotendeels vaste lokale
assen. Daardoor kan een vergelijkbaar ogende pijl een andere bewerking of
bevestigingsbetekenis hebben.

Bronnen: [ma_3d_copilot.html][R03], [part_modify_intro.html][R04],
[part_modify_ex2a1.html][R05], [part_modify_ex2a2.html][R06],
[part_modify_ex2b3.html][R07].

## Vergelijking per onderwerp

| Onderwerp | Referentie: wat is aangetoond? | OpenDesigner op de onderzochte commit | Concrete richting |
| --- | --- | --- | --- |
| Structure Browser | Modelcomponenten, onderdelen en werkvlakken; detailweergave en zoeken op actief onderdeel. | Dock met één vaste assembly-root en vlakke onderdelenlijst; selectie, naam, kleur en zichtbaarheid. | Onderscheid bewerkingscontext, selectie en zichtbaarheid; voeg zoeken/filteren toe. |
| Selectielijsten | Expliciete opbouw van een lijst met randen, verwijderen uit die lijst en afronden van de selectie. | Ctrl voor additieve vlakken, Alt/Ctrl+Alt voor onderdelen; geen algemene randselectie. | Getypeerde selectiemodi en zichtbare selectielijst, met behoud van bestaande sneltoetsen. |
| 3D CoPilot | Pijl/ring en maatveld afhankelijk van het commando; referentie kan van vlak naar rand veranderen. | Oranje primitive-Pull en afzonderlijke Move/Rotate-gizmo; lokale onderdeelassen en vaste standaardpivot. | Eén referentie/pivot-model voor tekenen, hit-testen en geometrische uitvoering. |
| Move/Rotate | Move 3D kan een vlak verplaatsen en om een gekozen referentie kantelen. | Gehele onderdelen bewegen/roteren; Box-vlakken worden via grenzen herbouwd. | Maak onderdeeltransformatie en vlakvervorming expliciet verschillend. |
| Directe vlakbewerking | Verplaatsen, kantelen en uitlijnen; betrokken afrondingen worden in oefeningen herberekend. | Geen algemene Move/Rotate Face, Align of Blend voor willekeurige GeneralBRep. | Ontwikkel eerst betrouwbare planaire vlakbewerking, daarna ondersteuning van buurtopologie. |
| Booleans | Unite heeft Blank/Tools en optioneel behoud van tools; Subtract/Intersect staan zichtbaar in het Machine-paneel. | Alle drie aanwezig; alleen Subtract vraagt expliciet een target; altijd één nieuw resultaat. | Toon operandrollen en vervangingsbeleid expliciet; onderzoek behoud van tools en targetidentiteit. |
| Menu-organisatie | Taakbalk met uitklapbare, vastzetbare panelen; materiaal toevoegen/verwijderen als groepering. | Vrij vlak Modeling-menu, één toolbar en een Boolean-submenu. | Groepeer op doel en objecttype; behoud de huidige menutoegang. |
| Dialoog en bevestiging | Oefeningen sluiten af met OK; Align en Shell beschrijven Preview. | Slepen schrijft direct geometrie en bij loslaten een undo-stap; Esc behoudt meestal het sleepresultaat. | Voeg een consistente tijdelijke preview met afzonderlijk Toepassen en Annuleren toe. |

De referenties en beperkingen achter deze samenvatting staan per onderwerp hieronder.

## 1. Structure Browser en selectie

### Oorspronkelijke workflow

De browser is een modeloverzicht met onderdelen én werkvlakken. De introductie
maakt onderscheid tussen een eerste onderdeelplaats en een werkvlak. Er zijn
bedieningen voor detailweergave en zoeken, waaronder het terugvinden van het
actieve onderdeel. Dit wijst op een bewerkingscontext naast louter een lijst
van geselecteerde geometrie. De precieze regels voor alle assembly-, filter- of
zichtbaarheidsinteracties worden in deze pagina's niet uitgewerkt.

Bronnen: [browser_bar.html][R01], [browser_search.html][R02].

De randselectie-oefening bouwt eerst een verzameling op, biedt correctie van een
verkeerd gekozen element en sluit daarna de selectiefase af. Voor vlakbewerking
wordt ook eerst een vlak gekozen en vervolgens via een contextmenu de bewerking
opgeroepen. De zichtbare annuleerbediening na selectie is een aanwijzing voor een
expliciete selectietoestand; deze bron onderbouwt geen specifieke Esc-semantiek.

Bronnen: [part_modify_ex1a1.html][R08], [part_modify_ex1a2.html][R09],
[part_modify_ex1a3.html][R10], [part_modify_ex2a1.html][R05],
[part_modify_ex2a2.html][R06].

### Huidige implementatie

In [MainWindow.cpp](../src/MainWindow.cpp) wordt een `QTreeWidget` gevuld onder
`Assembly 1`. Die root vertegenwoordigt nog geen uitgewerkt assemblymodel.
Browserrijen verwijzen via `Qt::UserRole` naar stabiele posities in `boxes_`.
Boolean-bronnen blijven als inactieve slots bestaan; hun rijen worden verborgen.
`partsChanged` en `partSelectionChanged` synchroniseren de browser na wijzigingen.

Verborgen rijen mogen in de browser geselecteerd blijven voor Show/Hide, maar
worden niet in de viewportselectie opgenomen. Dat is een bruikbaar onderscheid
dat behouden moet blijven. Een verborgen browserselectie is momenteel ook geen
interactieve Boolean-operand.

`CadViewport::mousePressEvent`, `selectParts` en `selectedBooleanParts` verdelen
de selectie over OCCT-owners en `selectedPartIndices_`. Een normale klik selecteert
vlakken; Ctrl voegt vlakken toe of verwijdert ze; Alt en Ctrl+Alt richten zich op
hele onderdelen. In transformatiemodus kan een gewone klik op een GeneralBRep
al naar onderdeelselectie worden vertaald. Randselectie als volwaardige
modelleerinput ontbreekt.

Let op de naam `CadPart::active`: die betekent dat een slot nog tot het document
behoort. Het is **niet** het actieve bewerkingsonderdeel uit de referentie.
Die begrippen in toekomstige UI-code vermengen zou een ontwerpfout zijn.

### Aanbevelingen

1. Introduceer afzonderlijke begrippen voor browserselectie, geometrieselectie,
   bewerkingsonderdeel en zichtbaarheid. Gebruik een eigen `activePartId` voor
   de bewerkingscontext; hergebruik de huidige `active`-boolean daarvoor niet.
2. Toon selectieomvang en type: bijvoorbeeld onderdelen, vlakken of randen.
   Laat de gebruiker het type ook zichtbaar kiezen, zodat Alt niet de enige
   toegang tot onderdeelselectie is.
3. Voeg een bewerkbare selectielijst toe aan complexe opdrachten: toevoegen,
   verwijderen, leegmaken en expliciet afronden. Gebruik contextgevoelige
   filters; een Boolean accepteert solids, een toekomstige Blend randen.
4. Voeg zoeken op naam en filters voor zichtbaar/geselecteerd toe. Werkvlakken
   en echte assemblies horen pas in de browser wanneer het documentmodel ze kent.
5. Behoud stabiele sessie-IDs en de huidige bescherming tegen stale owners.
   Bij een later documentmodel moeten browserrijen op IDs blijven werken,
   niet op de actuele volgorde van zichtbare rijen.

## 2. 3D CoPilot, referenties en transformaties

### Oorspronkelijke workflow

De 3D CoPilot wordt als commandoafhankelijke bediening op het model beschreven:
lineaire en roterende bediening met maatfeedback. De vlakrotatie-oefening laat
specifiek zien dat een rand als andere referentie gekozen kan worden. Het gaat
daar dus om een geometrisch bepaalde referentie, niet uitsluitend om een gizmo
op het onderdeelcentrum.

Bronnen: [ma_3d_copilot.html][R03], [part_modify_ex2b3.html][R07],
[part_modify_ex2b4.html][R11].

Het lesmateriaal laat daarnaast geometrische vanginstellingen zien. De concrete
voorbeelden van middenpunten en cirkelelementen in `2d_customize.html` gaan over
**2D**. Ze bewijzen niet dat dezelfde volledige lijst ook als 3D-selectie bestaat.
`catch_settings.html` bevestigt wel dat vangcriteria voor precieze 2D- en
3D-bewerkingen een afzonderlijke instelling vormen.

Bronnen: [catch_settings.html][R12], [2d_customize.html][R13].

### Huidige implementatie en relevante afwijking

In [CadViewport.cpp](../src/CadViewport.cpp) gebruiken `updateMoveAxes`,
`updateRotationRings`, hun hit-tests en `applyRotationAngle` hetzelfde
`partLocalGizmoCenter`. Dit is gebaseerd op `BRepGProp::VolumeProperties`, met
één toepassing van de instantietransformatie. Voor groepen wordt het ongewogen
gemiddelde van de wereldcentra gebruikt. Die reeds geteste standaard is zinvol.

De standaardassen van de interactieve gizmo volgen de lokale onderdeeloriëntatie.
Een gekozen rand, vrij draaipunt of doelvlak kan het referentiekader nog niet
overnemen. De oranje Pull-pijlen gebruiken eigen lokale referenties; Cone heeft
aparte pijlen voor beide diameters en voor de hoogte vanaf beide uiteinden.
Die referenties moeten niet automatisch naar het volumezwaartepunt verhuizen.

**Statische bevinding:** het aparte menucommando `rotateSelectedPart()` gebruikt
nog het midden van `part.min`/`part.max`, getransformeerd naar wereldruimte.
Dat wijkt af van het zwaartepuntpad van de interactieve gizmo. Bij een kegel of
bewerkte GeneralBRep kunnen dezelfde hoek en wereldas dus een andere pivot
opleveren. Dit is een concrete verificatie- en harmonisatietaak, geen in dit
onderzoek uitgevoerde GUI-bugreproductie.

### Aanbevelingen

- Behoud het volumezwaartepunt als standaard voor **onderdelen**. Voeg expliciete
  keuzes toe voor lokale/wereldassen, een gekozen rand als as en een gekozen punt
  als pivot. Voor **vlakken** moet een afzonderlijke bewerkingsreferentie gelden.
- Laat een gedeelde referentieresolver de positie, as en pivot leveren aan zowel
  de gizmo als het dialoogcommando. Toon de huidige keuze naast de maatvelden.
- Voeg modelreferentie-snapping toe naast de bestaande vaste stappen in mm en
  graden. Specificeer de 3D-vangregels zelfstandig, met afstandstoleranties en
  feedback voor de werkelijk gebruikte referentie.
- De referentie moet kunnen wijzigen zonder de operandselectie opnieuw te maken.
  Verplaats een pivot alleen in een aparte, herkenbare referentiemodus.
- Houd groepgedrag voorspelbaar: behoud de huidige gemiddelde pivot als standaard
  en bied een gekozen gemeenschappelijke referentie als expliciete afwijking.

### Navigatie

[viewport2.html][R14] beschrijft Ctrl met respectievelijk de linker, middelste en
rechter muisknop voor pan, orbit en zoom. OpenDesigner gebruikt middenknop voor
orbit, Shift+midden voor pan en het wiel voor zoom. Dit is een aantoonbaar
verschil, geen reden om bestaande desktopbediening stilzwijgend te vervangen.
Een optioneel klassiek navigatieprofiel met dezelfde commando's is een betere
route; een wijziging van camerabediening mag modelgeometrie nooit veranderen.

## 3. Move, Rotate, Pull en directe vlakbewerkingen

### Wat de referentie werkelijk laat zien

`Extend_part.html` beschrijft Move 3D als een bewerking op **vlakken**, met
lineaire of roterende verplaatsing ten opzichte van referenties. Bij de
verlenging worden naburige afrondingen opnieuw opgebouwd. De volgende oefening
kantelt een vlak via een gewijzigde CoPilot-referentie. Dat is geometrische
vervorming van een onderdeel; niet hetzelfde als het hele onderdeel roteren.

Bronnen: [Extend_part.html][R15], [angle_face.html][R16],
[part_modify_ex2a3.html][R17], [part_modify_ex2b3.html][R07].

Align gebruikt een ander vlak of vlakreferentie om de doelgeometrie te wijzigen.
De oefening bevat selectie van doel en referentie, daarna Preview en OK. Ook
Blend wordt als bewerking van geselecteerde randen behandeld, met een numerieke
manipulator. Deze pagina's geven geen rechtvaardiging om willekeurige vrije-vorm-
vlakbewerking als eenvoudig bounds-rekenen te implementeren.

Bronnen: [align_face.html][R18], [part_modify_ex33.html][R19],
[part_modify_ex34.html][R20], [part_modify_ex1a4.html][R21],
[part_modify_ex1c2.html][R22].

### OpenDesigner: aanwezig, beperkt of ontbrekend

| Bewerking | Huidige broncode | Workflowafstand |
| --- | --- | --- |
| Onderdeel verplaatsen | `beginMoveAxisDrag`, `applyGroupMove`, instantietransformatie | Aanwezig, ook voor groepen en GeneralBRep; geen interactief gekozen modelreferentiekader. |
| Onderdeel roteren | `applyRotationAngle` en `rotateSelectedPart` | Aanwezig; referentie/pivot verschilt nog tussen de twee paden. |
| Box-vlakken trekken | `pullSelectedFace`, `prepareMultiFaceDrag`, `applyBoxBounds` | Lokale boxgrenzen wijzigen; geen algemene topologische vlakverplaatsing. |
| Cylinder/Sphere/Cone Pull | `applyPrimitivePull` | Parametrische herbouw; einddiameters/hoogtes blijven behouden als gebruikersmodel. |
| Vlak kantelen | Geen algemene Rotate Face-operatie | De huidige onderdeelrotatie kan deze oefening niet vervangen. |
| Align, Blend en blends onderhouden | Geen overeenkomstige modelleercommando's | Zowel interactie als geometriekernwerk nodig. |
| Nieuw detail op bestaand vlak | Geen werkvlak/profiel/Extrude-pijplijn | De primitieve-aanmaakroute dekt de referentie-oefeningen niet. |

`movePartAlongSelectedFace()` is ondanks zijn referentievlak een
**onderdeelverplaatsing**, momenteel beperkt tot Box. De naam moet niet als
gelijkwaardig aan het oorspronkelijke Move 3D voor vlakken worden uitgelegd.

De introductiecursus bouwt veel modellen via een werkvlak, een 2D-profiel en
Extrude/Mill. OpenDesigner begint juist met primitives. Dat is nuttig en hoeft
niet te verdwijnen, maar de oorspronkelijke oefenreeks kan daarom nog niet
volledig worden uitgevoerd.

Bronnen: [intro1.html][R23], [wp_on_face.html][R24],
[part_creation_intro.html][R25], [extrude_profile.html][R26],
[part_create_ex2b.html][R27], [part_create_ex2c.html][R28].

### Aanbevolen ontwikkelvolgorde

1. Maak in de interface onderscheid tussen Onderdeel verplaatsen/roteren,
   Vlak verplaatsen/kantelen en Primitive-afmetingen. Behoud absolute diameters
   en hoogtes; label een vlakverplaatsing expliciet als afstand/verandering.
2. Ontwikkel algemene planaire Move Face op een eenvoudige gesloten solid.
   Neem buuraansluitingen, oriëntatie en validiteit mee. Gebruik geen Box-herbouw
   op Boolean-resultaten om een vergelijkbare interactie te simuleren.
3. Voeg daarna een gekozen rand/as voor Rotate Face en een planaire Align toe.
   Een bewerkingspreview mag tijdelijk zijn; een ongeldige solid mag nooit de
   originele B-Rep vervangen.
4. Ontwikkel Blend apart, met expliciete regels voor aangrenzende geometrie.
   De les noemt onderhoud van blends, maar beschrijft geen herbruikbaar algoritme.
5. Voeg werkvlakken en profielen als eigen documentobjecten toe voordat de
   Extrude/Mill-lessen als gedekte workflows worden aangemerkt.

## 4. Boolean Unite, Subtract en Intersect

### Zekerheden en grenzen van de bron

[ma_add_unite_1.html][R29] beschrijft een rolverdeling: één Blank wordt aangepast
met één of meer Tools. De tools verdwijnen als zelfstandige onderdelen, tenzij
ze bewaard worden. Het hoofdonderdeel blijft als aangepast onderdeel bestaan.
Dit is een workflowverschil in identiteit en operandrollen, ook als de
wiskundige verenigingsgeometrie overeenkomt.

[ma_main_menu.html][R30] beschrijft materiaal toevoegen/verwijderen. In de
bijbehorende lokaal bekeken Machine-illustratie staan Unite bij toevoegen en
Subtract/Intersect bij verwijderen. Hun aanwezigheid en groepering zijn daarmee
zichtbaar. De onderzochte HTML-tekst bevat geen zelfstandige handleiding voor
de target/tool-keuze, bewaarkeuzes, lege resultaten of bevestiging van die twee
Booleans. Die details zijn dus **niet vastgesteld**, niet afwezig verklaard in
het originele product. Verwijzingen naar doorsnijdingen in de 2D-profiellessen
zijn geen bewijs voor Boolean Intersect.

De ontbrekende SWF-demonstratie achter [Unite_viewlet_swf.html][R31] beperkt ook
het bewijs voor de precieze Unite-dialooginteractie.

### Vergelijking met OpenDesigner

[BooleanOperations.cpp](../src/BooleanOperations.cpp) biedt alle drie operaties.
Operands krijgen hun wereldtransformaties; validiteit en positief solidvolume
worden gecontroleerd. Subtract vraagt een target; alle andere geselecteerde
onderdelen zijn tools. Unite en Intersect gebruiken een ongerichte operandset.

Een succes maakt altijd een nieuw GeneralBRep-onderdeel en maakt alle bronnen
inactief. Redo gebruikt het gecachete resultaat. Naam en kleur van de bronnen
zijn herstelbaar via `BooleanChange`; targetidentiteit blijft niet het actieve
resultaat-ID. Een optie om tools te behouden ontbreekt. Unite kan losse solids
in één onderdeel opleveren; de lesbron specificeert hiervoor geen foutbeleid.

### Aanbevelingen

- Voeg een consistente operandeditor toe: rollen kiezen, onderdelen vanuit
  browser of viewport toevoegen/verwijderen, gebruikte aantallen tonen en een
  preview uitvoeren. Behoud de expliciete targetkeuze van Subtract.
- Voeg behoud van tools eerst toe aan Unite, waarvoor het lesmateriaal dit
  expliciet onderbouwt. Eventuele gelijksoortige opties bij andere Booleans zijn
  eigen OpenDesigner-ontwerpkeuzes totdat aanvullende v16-documentatie is gelezen.
- Bepaal expliciet het identiteitsbeleid: behoud de goedgekeurde nieuwe-resultaat-
  werkwijze als voorspelbare optie; bied eventueel een modus die het hoofd-ID,
  de naam en kleur behoudt. Verander dit niet stilzwijgend naar Blank-semantiek.
- Laat de preview aangeven welke brononderdelen verdwijnen en welke blijven.
  Eigen accenten voor target, tools en resultaat zijn nuttiger dan alleen een
  generieke selectiehighlight; neem geen oorspronkelijke beeldmiddelen over.
- Behoud huidige validatie en alles-of-niets-vervanging. Intersect zonder volume,
  een lege Cut en een Cut zonder volumeverwijdering moeten een begrijpelijke
  melding geven vóór er iets wordt vervangen. Dit zijn eigen kwaliteitscriteria,
  geen uit deze tutorial bewezen v16-foutregels.

## 5. Modeling-menu's, dialoogvensters en bevestiging

### Referentiepatroon

[modeling_main_taskbar.html][R32] beschrijft een uitklapbaar taakpaneel dat kan
worden vastgezet. [ma_main_menu.html][R30] groepeert op materiaal toevoegen en
verwijderen. [part_modify_intro.html][R04] en de Blend-oefening tonen dat
contextmenu's en een 3D-modificatiegroep naar dezelfde opdrachten leiden.
De Application-pagina gaat over modules, niet over de gewone modelleerselectie.
Er is onvoldoende reden om nu al een modulesysteem te bouwen om deze bediening
na te bootsen.

Bronnen: [part_modify_ex1c2.html][R22], [application_menu.html][R33].

Prompt en status hebben verschillende rollen: hulp bij de volgende handeling
tegenover blijvend zichtbare instellingen, waaronder hoekeenheden. Sommige
opdrachten bieden expliciet een preview vóór OK. Dit ondersteunt een herkenbare
commandoafhandeling, zonder dat elke oorspronkelijke annuleringsvariant uit de
cursus kan worden afgeleid.

Bronnen: [promptbar.html][R34], [statusbar.html][R35],
[part_modify_ex34.html][R20], [ma_remove_shell2.html][R36].

### Huidige verschillen

- `MainWindow` bevat meerdere losse transformatiemodi naast Pull, primitieve
  creatie en Boolean. De toolbar bevat Create Box, maar niet de andere drie
  primitive-aanmaakacties. Er is geen viewport-contextmenu voor modelleren;
  het bestaande contextmenu hoort bij browserbeheer.
- De Combined Transform-schakelaar zet twee onderliggende acties aan/uit, maar
  wordt niet wederzijds bijgewerkt als die afzonderlijk veranderen. Statisch
  kan de gemarkeerde combinatie dus afwijken van de werkelijke deelmodi.
- De browser gebruikt een dock; een taakspecifiek parameterspaneel ontbreekt.
  Pull gebruikt een invoerveld in de viewport en enkele commando's gebruiken
  modale `QInputDialog`/`QDialog`-vensters. Unite en Intersect werken direct.
- Tijdens slepen wordt de werkelijke geometrie aangepast. Loslaten registreert
  de bewerking in de undo-stack. Esc sluit daarna het invoerveld en behoudt het
  sleepresultaat. Dat is geen volledige rollback van een preview.
- Statusmeldingen delen één statusbalk en verdwijnen vaak na vijf seconden.
  Er is geen afzonderlijke blijvende prompt met huidige opdracht, operandrol,
  eenheden, referentie en volgende stap.

### Voorstel voor eigen organisatie

| Groep | Voorgestelde inhoud | Gedrag |
| --- | --- | --- |
| Maken | Box, Cylinder, Sphere, Cone; later werkvlak en profiel | Alle primitives even goed bereikbaar. |
| Onderdeel transformeren | Move, Rotate, Duplicate | Eén actieve opdracht met lokale/wereldreferentie. |
| Geometrie wijzigen | Vlak verplaatsen/kantelen, Primitive Pull; later Align/Blend | Alleen ondersteunde geometrie aanbieden; type en maatbetekenis tonen. |
| Boolean | Unite, Subtract, Intersect | Gedeelde operandeditor, preview en vervangingsbeleid. |
| Hulpinstellingen | Vangen, stapgrootte, eenheden | Instellingen zichtbaar en los van het actuele commando. |

Dit is een eigen indeling die past bij de huidige mogelijkheden, geen kopie van
een v16-paneel. De bestaande menuroute blijft beschikbaar; contextmenu en
optioneel vastzetbaar taakpaneel moeten dezelfde acties aanroepen.

## 6. Aanbevolen architectuur en prioriteiten

De huidige scheiding is beperkt: `CadViewport` bevat zowel documentdata,
interactie, geometrie als undo-logica. `MainWindow` beheert browseracties en
modusschakelaars. `BooleanChange` laat al zien hoe een complexere bewerking
bronstatus en resultaat gezamenlijk kan bewaren. Bouw daarop voort.

### P0 — consistente opdrachtcyclus en betekenis

1. Introduceer een eigen commandosessie met fases: operands kiezen, referentie
   kiezen, parameters aanpassen, preview valideren, bevestigen of annuleren.
   Houd tijdelijke geometrie en definitieve documentstaat gescheiden.
2. Eén bevestiging maakt één undo-commando. Annuleren herstelt de beginsituatie;
   wisselen van gereedschap krijgt een expliciete bevestigingsregel. Verander
   de huidige Esc-betekenis pas samen met zichtbare uitleg en regressietests.
3. Harmoniseer `Rotate Part...` en interactieve rotatie rond één pivot-resolver.
   Standaard blijft volumezwaartepunt voor onderdelen; Pull houdt eigen ankers.
4. Scheid promptinformatie van blijvende instellingen. Laat de gebruiker steeds
   zien of een invoer een diameter, hoogte, verplaatsing of hoek betekent.

### P1 — operandrollen en geometrische referenties

5. Ontkoppel getypeerde selectie van renderowners, met stabiele onderdeel-ID,
   tijdelijke subshape-referentie en doelrol. Bewaar invalideringsregels bij
   topologievervanging; maak een stale face-owner nooit automatisch opnieuw geldig.
6. Deel de Boolean-operandeditor tussen alle drie opdrachten. Voeg behoud van
   tools en expliciet identiteitsbeleid toe zonder de bestaande snapshots en
   niet-overlappende Unite-resultaten te verliezen.
7. Voeg interactieve referentiekeuze toe aan transformaties. Voorzie daarbij
   consistente lokale/wereldassen en een gekozen punt/rand, ook voor groepen.
8. Introduceer een gemeenschappelijk commando-interface voor alle documentmutaties.
   De huidige `PullChange` met optioneel `BooleanChange` kan als tussenstap blijven;
   later kunnen creatie, duplicatie, naam, kleur en zichtbaarheid eveneens
   voorspelbare undo-acties krijgen. Een nieuw undo-systeem naast het bestaande
   zou juist de bewerkingsvolgorde versnipperen.

### P2 — geometrische dekking van de oorspronkelijke oefeningen

9. Algemene planaire Move Face, vervolgens Rotate Face en Align; elke stap met
   geldigheidscontrole, buuraansluiting en veilige rollback.
10. Werkvlakken, profielen en Extrude/Mill als afzonderlijke ontwikkellijn.
    Randselectie en Blend vragen een eigen kwaliteitsstap.

[delete.html][R37] behandelt Undo als teruggaan in de modelleerhandeling;
[menu1.html][R38] gebruikt het ook na werkvlakcreatie. Dat ondersteunt een
brede, consistente geschiedenis. Het bewijst geen exacte undo-dekking voor
alle oorspronkelijke instellingen. De prioriteiten hierboven zijn voorstellen,
geen afgeronde roadmaptaken.

## 7. Eigen acceptatiescenario's voor vervolgwerk

Deze scenario's zijn zelfstandig geformuleerd. Ze zijn niet uitgevoerd in dit
onderzoek en reproduceren geen originele oefenmodellen.

| Scenario | Verwacht bewijs |
| --- | --- |
| Browserselectie met twee zichtbare en één verborgen rij | Browserselectie blijft bruikbaar voor Show/Hide; modelleerinput volgt de expliciete rolkeuze; geen onverwacht gebruik van verborgen operands. |
| Voorselectie tegenover commando-eerst | Dezelfde geldige operandset levert dezelfde bewerking en dezelfde undo-stap; ongeldige typen blijven uitgesloten. |
| Cone via gizmo en via Rotate-dialoog | Bij dezelfde gekozen as/pivot en hoek zijn de wereldtransformaties gelijk, ook na Pull en duplicatie. |
| Referentie wisselen van vlak naar rand | De operandselectie blijft behouden; referentie en rotatieas veranderen zichtbaar zonder eerdere preview definitief te maken. |
| Vlakbewerking met een eigen offset van 12,25 mm | Alleen bedoelde geometrie verandert; Annuleren herstelt exact de B-Rep, transformatie en selectie van vóór de preview. |
| Unite met bewaren van tools | Tool-ID's, vormen en browserrijen blijven bestaan; hoofd/resultaatbeleid is zichtbaar; Undo/Redo herstelt alle rollen en zichtbaarheid. |
| Drie Booleans met ongeldige of lege preview | Geen gedeeltelijke bronverwijdering, geen vervuilde history en geen oude selectie-eigenaar na een mislukking. |
| Opdracht via menu, contextmenu en taakpaneel | Zelfde commandosessie, parameters, bevestiging en undo-gedrag; geen aparte implementaties per toegangspunt. |
| Primitive Pull versus Move Face | Einddiameter/hoogte blijft absolute maat; vlakoffset blijft als verplaatsing herkenbaar; onderkap houdt het juiste tegenoverliggende vlak vast. |
| Multipartrotatie rond standaard en gekozen pivot | Huidig ongewogen gemiddelde blijft standaard; expliciet gekozen pivot wordt consequent gebruikt voor feedback en uitvoering. |

Behoud de bestaande suites `primitive-pull`, `cone-pull`, `gizmo-center` en
`boolean-operations` als basis. Voeg nieuwe tests toe op de opdrachtcyclus en
referentieresolver; alleen geometrievolumes testen bewijst geen consistente
workflow of annulering.

## Bronverwijzingen

Alle onderstaande referenties blijven buiten de repository. De namen zijn
exacte lokale HTML-bestandsnamen; tekst en afbeeldingen zijn niet opgenomen.
Bij [ma_main_menu.html][R30] berust de aanwezigheid van Subtract/Intersect op de
illustratie, niet op een uitgewerkte tekstworkflow. Sommige historische
image-links gebruiken een andere letterkast dan de bestanden op Linux; de
relevante illustraties zijn lokaal op hun werkelijke bestandsnaam bekeken.

| Onderwerp | Relevante HTML-bestanden |
| --- | --- |
| Browser en actieve context | [browser_bar.html][R01], [browser_search.html][R02] |
| CoPilot en referentie | [ma_3d_copilot.html][R03], [part_modify_ex2b3.html][R07], [part_modify_ex2b4.html][R11] |
| Voorselectie en contextcommando | [part_modify_intro.html][R04], [part_modify_ex2a1.html][R05], [part_modify_ex2a2.html][R06] |
| Selectielijst met randen | [part_modify_ex1a1.html][R08], [part_modify_ex1a2.html][R09], [part_modify_ex1a3.html][R10] |
| Vangen en navigatie | [catch_settings.html][R12], [2d_customize.html][R13], [viewport2.html][R14] |
| Vlak verplaatsen/kantelen | [Extend_part.html][R15], [angle_face.html][R16], [part_modify_ex2a3.html][R17] |
| Align en preview | [align_face.html][R18], [part_modify_ex33.html][R19], [part_modify_ex34.html][R20] |
| Blends en herberekening | [part_modify_ex1a4.html][R21], [part_modify_ex1c2.html][R22] |
| Werkvlakken/profielen | [intro1.html][R23], [wp_on_face.html][R24], [part_creation_intro.html][R25] |
| Extrude en afronden | [extrude_profile.html][R26], [part_create_ex2b.html][R27], [part_create_ex2c.html][R28] |
| Unite en Boolean-menu | [ma_add_unite_1.html][R29], [ma_main_menu.html][R30], [Unite_viewlet_swf.html][R31] |
| Taakpanelen en modules | [modeling_main_taskbar.html][R32], [application_menu.html][R33] |
| Prompt, status en preview | [promptbar.html][R34], [statusbar.html][R35], [ma_remove_shell2.html][R36] |
| Undo en objecten | [delete.html][R37], [menu1.html][R38] |

[R01]: /home/Jeroen/SolidDesignerReference/GettingStarted/browser_bar.html
[R02]: /home/Jeroen/SolidDesignerReference/GettingStarted/browser_search.html
[R03]: /home/Jeroen/SolidDesignerReference/GettingStarted/ma_3d_copilot.html
[R04]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_intro.html
[R05]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_ex2a1.html
[R06]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_ex2a2.html
[R07]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_ex2b3.html
[R08]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_ex1a1.html
[R09]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_ex1a2.html
[R10]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_ex1a3.html
[R11]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_ex2b4.html
[R12]: /home/Jeroen/SolidDesignerReference/GettingStarted/catch_settings.html
[R13]: /home/Jeroen/SolidDesignerReference/GettingStarted/2d_customize.html
[R14]: /home/Jeroen/SolidDesignerReference/GettingStarted/viewport2.html
[R15]: /home/Jeroen/SolidDesignerReference/GettingStarted/Extend_part.html
[R16]: /home/Jeroen/SolidDesignerReference/GettingStarted/angle_face.html
[R17]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_ex2a3.html
[R18]: /home/Jeroen/SolidDesignerReference/GettingStarted/align_face.html
[R19]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_ex33.html
[R20]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_ex34.html
[R21]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_ex1a4.html
[R22]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_modify_ex1c2.html
[R23]: /home/Jeroen/SolidDesignerReference/GettingStarted/intro1.html
[R24]: /home/Jeroen/SolidDesignerReference/GettingStarted/wp_on_face.html
[R25]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_creation_intro.html
[R26]: /home/Jeroen/SolidDesignerReference/GettingStarted/extrude_profile.html
[R27]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_create_ex2b.html
[R28]: /home/Jeroen/SolidDesignerReference/GettingStarted/part_create_ex2c.html
[R29]: /home/Jeroen/SolidDesignerReference/GettingStarted/ma_add_unite_1.html
[R30]: /home/Jeroen/SolidDesignerReference/GettingStarted/ma_main_menu.html
[R31]: /home/Jeroen/SolidDesignerReference/GettingStarted/Unite_viewlet_swf.html
[R32]: /home/Jeroen/SolidDesignerReference/GettingStarted/modeling_main_taskbar.html
[R33]: /home/Jeroen/SolidDesignerReference/GettingStarted/application_menu.html
[R34]: /home/Jeroen/SolidDesignerReference/GettingStarted/promptbar.html
[R35]: /home/Jeroen/SolidDesignerReference/GettingStarted/statusbar.html
[R36]: /home/Jeroen/SolidDesignerReference/GettingStarted/ma_remove_shell2.html
[R37]: /home/Jeroen/SolidDesignerReference/GettingStarted/delete.html
[R38]: /home/Jeroen/SolidDesignerReference/GettingStarted/menu1.html
