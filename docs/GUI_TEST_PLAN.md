# Fedora KDE GUI-testplan — v0.4.2 Project 3D Geometry

Status: **handmatige uitvoering nog niet bevestigd**. Automatische/Xvfb-tests
vervangen deze controle van KDE-focus, decoraties, HiDPI en interactie niet.
Noteer per test A–H PASS/FAIL, gebruikte schaalfactor en eventuele afwijking.
Merge pas na groene CI, opgeloste fouten en expliciete toestemming van de gebruiker.

## Voorbereiding

1. Start de Release-build van de featurebranch. Bewaar eventueel bestaand werk.
2. Maak een eigen testdocument; gebruik File → New waar hieronder gevraagd.
   Sla het vooraf op als u wilt controleren dat preview de dirty-indicator niet wijzigt.
3. Maak een Box via Modeling → Create Box. Maak een XY-Workplane via Modeling →
   Construction Geometry → Workplane → XY (of de overeenkomstige XY-preset).
   Gebruik desgewenst een kleine normaal-offset zodat 2D en 3D bij orbit uiteenliggen.
4. Activeer dit Workplane met **Edit Sketch** of de Workplane-keuzelijst in de
   **2D Modeling**-toolbar. Het menucommando staat onder **Modeling → 2D Modeling →
   Project 3D Geometry**; dezelfde actie staat in de toolbar (eventueel onder `»`).
5. Klik vervangt bronselectie; **Ctrl+klik** voegt toe/verwijdert. De paneellijst
   heeft **Remove selected sources** en **Clear sources**. Beweeg met middelste
   muisknop, pan met Shift+middelste knop en zoom met het wiel.

## Test A — Vertex en geldig doel

1. Controleer vóór Edit Sketch dat Project 3D Geometry uitgeschakeld is.
2. Activeer WP1, start het commando en kies **Vertex**.
3. Klik op een zichtbare hoek van de Box; roteer zo nodig voor een vrije hoek.
4. Verwacht: WP1 blijft actief; één bron en één Construction Entity; zichtbaar
   geselecteerd 3D-punt en blauwe puntmarker op de loodrechte projectie in WP1.
5. Zoom: de marker blijft een herkenbare schermmarker. Preview maakt het document
   niet gewijzigd wanneer het vooraf opgeslagen was.
6. Kies OK. Verwacht één Construction Point onder WP1 in de Structure Browser,
   normale roze constructiekleur en herstelde 2D-selectie. Undo verwijdert het punt,
   Redo herstelt het. Beide wissen alle selectie.

## Test B — Edges, Mixed en meerdere Parts

1. Start **Edge** en kies meerdere rechte, niet loodrecht op WP1 staande randen
   met Ctrl+klik. Kies randen met verschillende projecties.
2. Verwacht één bron per geselecteerde rand en overeenkomstige blauwe eindige lijnen.
3. Ctrl+klik opnieuw op een geselecteerde rand: bron, highlight en preview verdwijnen.
   Test ook Remove selected sources en Clear sources.
4. Schakel naar **Mixed**. Voeg een vertex en een face toe. Verwacht dat alle drie
   typen mogelijk zijn; gedeelde boundary-randen leveren geen dubbele output.
5. Schakel naar Face: alleen face-bronnen blijven. Schakel naar Mixed: die blijven.
6. Bevestig een set randen/faces. Eén Undo verwijdert de hele toevoeging, één Redo
   herstelt alles met bestaande 2D-geometrie intact.
7. Herhaal met een verplaatst/geroteerd tweede Part en een verplaatst/geroteerd WP.
   Controleer de loodrechte projectie, niet een tweede toepassing van de Part-plaatsing.
   Bevestigde referenties blijven onafhankelijk als het bron-Part later wordt verplaatst.

## Test C — Circle/Arc en bewuste weigering

1. Maak/gebruik een Cylinder met een cirkelrand parallel aan het doel-Workplane.
   Activeer het passende WP, start Edge en selecteer de cirkelrand.
2. Verwacht een exacte ronde blauwe cirkel, met behoud van centrum en straal in WP.
   OK maakt een Construction Circle; er zijn geen extra eindpuntpunten.
3. Gebruik een geschikte Part met een begrensde cirkelboog parallel aan WP.
   Projecteer de boog; controleer eindpunten en kort/lang boogdeel.
4. Gebruik een schuin Workplane ten opzichte van een cirkelrand. Verwacht duidelijke
   diagnostiek over de ellipsprojectie, geen benadering, geen geldige deelpreview
   en uitgeschakelde OK. Verwijder de ongeldige bron en controleer herstel.
5. Een rand loodrecht op WP wordt als instortende projectie geweigerd. Ook een
   ongeldige bron in een verder geldige Mixed-selectie blokkeert de hele batch.

## Test D — Face met gat en duplicaten

1. Open een eigen .odc met een eenvoudige vlakke rechthoekige face met doorgaand
   rond gat, of maak deze via Box minus een passende Cylinder. Het gat moet geheel
   binnen de face liggen. Activeer een parallel Workplane.
2. Start Face en klik op het materiaal van de face, naast het gat.
3. Verwacht zowel buiten- als binnencontour. Bij vier rechte buitenranden en één
   volledige binnenrand: **één bron, vijf output-entiteiten**, geen extra punten.
4. Selecteer zo mogelijk twee aangrenzende faces (alle boundaries moeten exact
   projecteerbaar zijn). Een gedeelde rand verschijnt niet dubbel.
5. OK, Undo en Redo werken op de hele face-projectie. Projecteer dezelfde face
   opnieuw: output is volledig dubbel, OK maakt geen nieuwe geometrie/Undo-stap.
6. Save, Save As en Open behouden alle geprojecteerde geometrie, rol, eigenaar en
   bestaande Parts. Curves gebruiken v3-output; documenten met 2D-punten v4.

## Test E — Cancel en documentlifecycle

1. Selecteer vooraf een bestaand 2D-object. Start projectie en kies geldige bronnen.
2. Test afzonderlijk Cancel, Escape vanuit de viewport, Escape vanuit een veld en
   het kruisje van het paneel. Verwacht geen toevoeging/Undo-stap/dirty-wijziging,
   geen blauwe restgeometrie en geen 3D-bronhighlight. Veilige vorige 2D-selectie
   keert terug en normale Sketch-selectie werkt weer.
3. Start opnieuw en kies een ander 2D/3D-commando. De projectie wordt geannuleerd.
4. Start opnieuw en wissel/Finish het Workplane of verberg/verwijder het doel.
   Verwacht nette annulering, geen crash of achtergebleven bronselectie.
5. Herhaal met verbergen/wijzigen van een bron-Part, File → New en succesvol Open.
   New/Open herstellen de gewone documentcontext en wissen history/selectie.
6. Save tijdens een preview bewaart uitsluitend bevestigde geometrie. De preview,
   bronselectie en bestaande history blijven actief. Cancel daarna en Open het
   opgeslagen bestand: de niet-bevestigde projectie is afwezig.
7. Sluit het applicatievenster tijdens een preview. Beantwoord een eventuele
   unsaved-prompt bewust. Bij annuleren van sluiten blijft de sessie; bij werkelijk
   sluiten blijft geen commandovenster achter.

## Test F — Commandopaneel, resize en focus

1. Start projectie in een ruim venster. Verwacht het volledige paneel rechtsboven
   binnen de CAD-viewport, met ongeveer 12 logische pixels marge, niet aan scherm
   of hoofdvenster verankerd. Alle velden/knoppen passen zonder onnodige scrollbars.
2. Sleep het paneel binnen de viewport. Verander bronnen, modus en foutstatus.
   Het paneel springt niet terug; alleen noodzakelijke clamping is toegestaan.
3. Verklein het venster en wijzig de Structure Browser-docking. Het paneel blijft
   binnen de viewport. Scroll alleen bij echt te weinig ruimte; OK en Cancel blijven
   via scrollen bereikbaar. Vergroot weer: de natuurlijke formaten keren terug.
4. Zoom, pan en orbit terwijl het paneel open staat. Dit bevestigt geen projectie.
   Test OK/Enter en Cancel/Escape ook na focus op modus, bronlijst en diagnostiek.

## Test G — Fedora KDE HiDPI

1. Herhaal A en F bij de gebruikelijke KDE-schaal en bij 200% waar beschikbaar.
2. Bij meerdere monitoren: verplaats het hoofdvenster tussen monitoren en resize.
3. Verwacht leesbare velden, correcte muispicks, markermaat en viewport-clamping,
   geen verdwenen knoppen, onverwachte focuswisseling of losgeraakt paneel.

## Test H — Bestaande functionaliteit

1. Bevestig projectie, Finish 2D en voer een bestaande Move Face, Rotate Face,
   Delete Face of Boolean uit op geschikte geometrie. Controleer preview/Cancel/OK.
2. Activeer daarna weer WP1. Test een gewone 2D-lijn, snapping naar geprojecteerde
   eindpunten/punten/centra en een geschikte Overdraw-route op begrensde stukken.
3. Undo/Redo gebruiken één gedeelde volgorde en wissen selectie. Bestaande
   Overdraw-beperkingen blijven gelden; een ongeknipte cirkel is geen nieuwe
   uitzondering en een projected boundary is niet automatisch een gesloten Profile.

## Resultaten

| Test | Status | Opmerking/schaal |
| --- | --- | --- |
| A — Vertex | Nog niet uitgevoerd | |
| B — Edges/Mixed/Parts | Nog niet uitgevoerd | |
| C — Circle/Arc/weigering | Nog niet uitgevoerd | |
| D — Gaten/duplicaten/opslag | Nog niet uitgevoerd | |
| E — Cancel/lifecycle | Nog niet uitgevoerd | |
| F — Paneel/focus | Nog niet uitgevoerd | |
| G — KDE HiDPI | Nog niet uitgevoerd | |
| H — 2D/3D-regressies | Nog niet uitgevoerd | |
