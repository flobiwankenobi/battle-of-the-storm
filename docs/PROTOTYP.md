# Battle of the Storm – Prototyp: Stand, Start und Entscheidungen

Dieses Dokument beschreibt den gebauten Prototyp: wie er gestartet wird, was er enthält, welche offenen Punkte aus TDD und GDD beim Bau entschieden wurden und was nicht geprüft ist. Grundlage sind das TDD in Version 0.1 und das GDD in Version 0.4.

## Versionierung

| Version | Datum | Autor | Änderung |
| --- | --- | --- | --- |
| 0.1 | 2026-11-12 | Claude, im Auftrag von Florian | Erste Fassung zum ersten Stand des Prototyps |

## 1. Starten

Voraussetzungen: Unreal Engine 5.8 aus dem Epic Games Launcher und Visual Studio 2022 mit der Arbeitslast „Spieleentwicklung mit C++“. Beides ist auf dem Entwicklungsrechner vorhanden.

### Im Editor

1. `BattleOfTheStorm.uproject` doppelklicken. Beim ersten Start fragt der Editor, ob das Modul gebaut werden soll: mit „Ja“ bestätigen.
2. „Play“ drücken. Die Projekteinstellung startet einen dedizierten Server im Editor und zwei Clients („Play As Client“).
3. Nach 10 Sekunden Wartezeit beginnt die Heldenwahl. Freie Plätze füllt der Server mit Test-Bots auf 8 Spieler auf.

Der Konsolenbefehl `BotSStart` (Taste `^` oder `~` öffnet die Konsole) startet das Match sofort.

### Ohne Editor-Oberfläche

Server mit Fenster für das Log:

```bat
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Dev\Battle of the Storm\BattleOfTheStorm.uproject" /Engine/Maps/Entry -server -log
```

Client, der sich mit dem Server auf demselben Rechner verbindet:

```bat
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\Dev\Battle of the Storm\BattleOfTheStorm.uproject" 127.0.0.1 -game -windowed -ResX=1600 -ResY=900
```

Ein Match nur mit Bots, zum Beobachten im Log: an den Server-Aufruf `-BotsOnly` anhängen.

### Bauen und Tests von der Kommandozeile

```bat
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BattleOfTheStormEditor Win64 Development -Project="C:\Dev\Battle of the Storm\BattleOfTheStorm.uproject"
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Dev\Battle of the Storm\BattleOfTheStorm.uproject" -ExecCmds="Automation RunTests BotS; Quit" -TestExit="Automation Test Queue Empty" -unattended -nullrhi
```

## 2. Steuerung

| Aktion | Taste |
| --- | --- |
| Bewegen | W, A, S, D |
| Zielen | Mauszeiger, der Held blickt dorthin |
| Basisangriff | Linke Maustaste, gehalten wiederholend |
| Fähigkeiten 1 bis 3 | 1, 2, 3 |
| Ultimative Fähigkeit | 4, ab Trupp-Level 8 |
| Sturmlauf | Z, 1 s Kanalisieren |
| Aktivgegenstände | Q, E |
| Aufheben, Truhe öffnen | F |
| Wiederbeleben | F halten |
| Karte | Tab |
| Talent wählen | Strg+1, Strg+2 oder Klick auf die Karte |

## 3. Was der Prototyp enthält

Der Stand deckt die Meilensteine 0 bis 6 des TDD im Code ab. Meilenstein 7 (eigenständiger Server, Spieltest mit 8 Menschen, Messungen) ist nicht gebaut.

| Bereich | Stand |
| --- | --- |
| Projekt | Unreal Engine 5.8.3, C++-Modul `BattleOfTheStorm`, Gameplay Ability System, Enhanced Input |
| Match-Ablauf | Warten, Heldenwahl, Wahl des Startpunkts, Match, Siegbildschirm mit Platzierung |
| Trupps | 4 Trupps à 2, gemeinsames Level, gemeinsame Erfahrung |
| Helden | Kordo, Elowen, Sefa, Kiro mit Basisangriff, 3 Fähigkeiten, Merkmal, einer Ultimativen Fähigkeit |
| Talente | 2 Stufen (Level 1 und 4) mit je 2 Optionen pro Held, 16 Talente |
| Kampf | Projektile, Flächen mit Vorwarnzeit, Sprünge, Nahkampf, Schild vor Lebenspunkten, Betäubung, Festhalten, Verlangsamung |
| Niederschlag | Niederschlag, Ausbluten über 30 s, Vollenden durch Schaden, Wiederbelebung in 5 s |
| Sturm | 3 Phasen bis Durchmesser 0, Schaden in Prozent, Wirkung im Sturm, Sturmwand, Anzeige des Auges |
| Beute | 16 Truhen, 4 Lager mit je 3 Kreaturen, 14 Relikte in 2 Seltenheiten, Heiltrank, Schildphiole |
| Karte | Graubox einer Ruinenstadt, 440 × 440 m, aus den Daten erzeugt |
| Sicht | Sichtlinie und geteilte Truppsicht auf dem Client |
| Oberfläche | Fähigkeitenleiste, Truppanzeige, Sturmanzeige, Minikarte, Karte, Talentauswahl, Heldenwahl, Startpunkt-Wahl, Siegbildschirm, Lebensbalken |
| Test-Bots | Füllen freie Plätze, folgen dem Auge, sammeln Beute, kämpfen, beleben wieder |
| Tests | 7 Automatisierungstests für die Rechenlogik |

Alle Zahlenwerte stehen in `Content/BotS/Data/GameData.json`. Die Datei wird beim Start gelesen. Eine Änderung braucht keinen neuen Build, nur einen Neustart.

## 4. Helden-Kits

Die Fähigkeiten 1 bis 3 und die Talente fehlten im GDD. Die folgenden Kits sind für den Prototyp entworfen. Namen, Figuren und Fähigkeiten sind eigenständig. Fähigkeitswerte skalieren mit dem Trupp-Level um 4 % pro Level, Basisangriffe über den Angriffsschaden des Helden.

### Kordo (Tank)

| Slot | Name | Wirkung | Abklingzeit | Mana |
| --- | --- | --- | --- | --- |
| Merkmal | Geröll | 25 % des erlittenen Schadens werden zu einem Schild, höchstens 300 | – | – |
| 1 | Steinwurf | Projektil, 140 Schaden, verlangsamt 30 % für 2 s | 6 s | 40 |
| 2 | Bebenschlag | Fläche um Kordo, 0,5 s Vorwarnung, 120 Schaden, 0,75 s Betäubung | 12 s | 60 |
| 3 | Felssturm | Sprung 7 m, am Ziel 100 Schaden und Rückstoß | 14 s | 50 |
| 4 | Erdrutsch | Breite Welle in einer Linie, 300 Schaden, schleudert zurück | 80 s | 100 |

Talente Level 1: Harter Fels (+15 % Lebenspunkte) oder Lawine (Steinwurf durchschlägt, +40 Schaden). Level 4: Nachbeben (Bebenschlag betäubt länger, größerer Radius) oder Rammbock (Felssturm weiter und öfter).

### Elowen (Heiler)

| Slot | Name | Wirkung | Abklingzeit | Mana |
| --- | --- | --- | --- | --- |
| Merkmal | Landregen | Heilung auf Ziele unter 30 % Lebenspunkten ist 25 % stärker | – | – |
| 1 | Regenguss | Zielfläche, heilt Verbündete um 260 | 5 s | 50 |
| 2 | Hagelkorn | Projektil, 110 Schaden, verlangsamt 35 % für 1,5 s | 7 s | 40 |
| 3 | Rückenwind | Verbündete im Umkreis: Schild 120 und +30 % Tempo für 3 s | 14 s | 60 |
| 4 | Wolkenbruch | Zielfläche, heilt 5 s lang alle 0,5 s um 90 | 90 s | 100 |

Talente Level 1: Dauerregen (Regenguss öfter und billiger) oder Eisregen (Regenguss verlangsamt Gegner). Level 4: Sturmböe (Rückenwind stärker und länger) oder Graupel (Hagelkorn hält 1 s fest).

### Sefa (Fernkampf-Assassine)

| Slot | Name | Wirkung | Abklingzeit | Mana |
| --- | --- | --- | --- | --- |
| Merkmal | Sturmgeladen | Im Sturm +20 % Angriffsgeschwindigkeit | – | – |
| 1 | Blitzlanze | Schnelles Projektil, durchschlägt, 230 Schaden | 6 s | 45 |
| 2 | Funkenfeld | Zielfläche, 0,8 s Vorwarnung, 200 Schaden, hält 1 s fest | 11 s | 55 |
| 3 | Entladung | Sprung 5 m, danach +20 % Tempo für 2 s | 13 s | 40 |
| 4 | Gewitterzelle | Zielfläche, 4 s lang alle 0,5 s 85 Schaden | 90 s | 100 |

Talente Level 1: Hochspannung (Blitzlanze +50 Schaden) oder Schnellfeuer (+15 % Angriffsgeschwindigkeit). Level 4: Überladung (Funkenfeld größer, hält länger fest) oder Blitzableiter (Entladung weiter und öfter).

### Kiro (Nahkampf-Assassine)

| Slot | Name | Wirkung | Abklingzeit | Mana |
| --- | --- | --- | --- | --- |
| Merkmal | Kombo | Nach einer Fähigkeit verursacht der nächste Basisangriff doppelten Schaden | – | – |
| 1 | Klingenwirbel | Fläche um Kiro, 190 Schaden | 5 s | 35 |
| 2 | Schattensprung | Sprung 7 m, am Ziel 150 Schaden | 9 s | 40 |
| 3 | Wurfklinge | Projektil, 130 Schaden, verlangsamt 40 % für 2 s | 8 s | 35 |
| 4 | Tausend Schnitte | 2 s unverwundbar, 8 Treffer zu je 75 Schaden im Umkreis | 100 s | 100 |

Talente Level 1: Scharfe Klingen (+12 % Basisangriffsschaden) oder Wirbelwind (Klingenwirbel größer und öfter). Level 4: Hinterhalt (Schattensprung betäubt 0,5 s) oder Leichtfuß (+10 % Lauftempo).

Abgleich mit dem Tempo-Ziel aus GDD-Abschnitt 9: Kiro gegen Sefa auf gleichem Level verursacht mit Basisangriffen 189 Schaden pro Sekunde und mit drei Fähigkeiten 470 Schaden. Sefas 1.350 Lebenspunkte sind damit rechnerisch nach rund 4 s aufgebraucht. Das Ziel liegt bei 3 bis 5 s. Gemessen im Spiel ist der Wert nicht.

## 5. Entscheidungen zu offenen Punkten

Florian hat festgelegt, dass offene Punkte beim Bau selbst entschieden und als Annahme dokumentiert werden. Jede Zeile ist eine solche Annahme und kann ohne Umbau geändert werden, sofern „Daten“ in der Spalte „Ändern über“ steht.

| Offener Punkt | Entscheidung | Begründung | Ändern über |
| --- | --- | --- | --- |
| Engine-Version (TDD 18) | Unreal Engine 5.8.3 | Einzige installierte Version auf dem Entwicklungsrechner | Neuinstallation |
| Umfang des Prototyps (TDD 2) | 8 Spieler, 4 Helden, 1 Biom, 3 Sturmphasen wie vorgeschlagen | Kleinster Umfang, der alle drei Prototyp-Fragen beantwortet | Daten |
| Truppaufteilung (TDD 18) | 4 Trupps à 2 | Mehr als zwei Trupps sind nötig, damit sich das Match wie ein Battle-Royale anfühlt. Bei 2 Spielern pro Trupp gibt es eine echte Heldenwahl | Daten (`SquadSize`) |
| Auswahl der Helden (TDD 18) | Kordo, Elowen, Sefa, Kiro | Vorschlag des TDD: je ein Held aus vier Rollen | Daten |
| Biom (TDD 18) | Ruinenstadt | Vorschlag des TDD: deckt Sichtlinien, Engstellen und Nahkampf ab | Daten (Kartenwerte) |
| Blickrichtung (TDD 18) | Dauerhaft zum Mauszeiger | Passt zu Schnellzauber und Zielen mit der Maus | Daten (`bFaceCursor`) |
| Schilde und Sturmschaden (TDD 18) | Sturmschaden geht am Schild vorbei | Der Sturm soll für jeden Helden gleich wirken (GDD 4). Kordos Merkmal und die Schildphiole würden ihn sonst entwerten | Daten (`bShieldBlocksStorm`) |
| Gegenstände beim Ausschalten (TDD 18) | Ausgeschaltete Helden lassen alles fallen | Belohnt den Sieger eines Kampfes, wie im Genre üblich | Daten (`bDropItemsOnElimination`) |
| Kits und Talente (GDD 16) | Siehe Abschnitt 4 | Fehlten im GDD und blockierten den Kampfkern | Daten |
| Mana (TDD 6) | 500 Mana, 4 pro Sekunde | Fehlte im GDD. Rund 8 bis 10 Fähigkeiten aus vollem Vorrat | Daten |
| Lebensregeneration (TDD 6) | 4 Lebenspunkte pro Sekunde | Fehlte im GDD. Bewusst niedrig, damit Heiler und Tränke zählen | Daten |
| Erfahrung je Level (TDD 8) | 120 bis 180 je Level bis Level 8 (zusammen 1.020), danach 220. Kreatur 80, Held 150 plus 15 je Level des Besiegten | Ein Trupp erreicht Level 8 rechnerisch mit 2 Lagern (480) und 3 besiegten Helden (rund 560) | Daten |
| Sichtweite (TDD 11) | 22 m, im Sturm 11 m | Entspricht etwa dem Bildausschnitt der Kamera | Daten |
| Kamera (TDD 7) | Neigung 60°, Abstand 24 m, Sichtfeld 55°, Vorlauf 15 % bis höchstens 4 m | Startwerte, im Spiel zu prüfen | Daten |
| Zahlenwerte der Gegenstände (TDD 10) | Gewöhnlich +8 % oder +5 % Tempo, Selten das Doppelte. Heiltrank 500 über 5 s, Schildphiole 350 für 5 s | Fehlten im GDD | Daten |
| Niederschlag | Vorrat von 30 % der Lebenspunkte, der in 30 s abläuft. Gegnerischer Schaden vollendet schneller | Verbindet Ausbluten und Vollenden in einer Regel | Daten |
| Sturmlauf | Bewegung während des Kanalisierens ist erlaubt. Fähigkeiten und Schaden beenden den Sturmlauf, Sturmschaden nicht | Sonst wäre die Flucht aus dem Sturm unmöglich. Das GDD regelt beides nicht | Code |
| Gleichzeitiges Ausscheiden (TDD 8) | Gewinner ist der zuletzt ausgeschiedene Trupp | Vereinfachung der im TDD vorgeschlagenen Regel | Code |
| Sprache des Prototyps (TDD 12) | Deutsch | Sprache von GDD und TDD | Code |

Offen bleiben, weil sie keine Bauentscheidung sind: Teamgröße und verfügbare Zeit, Referenzrechner für das Bildraten-Ziel, Ablage des TDD in Dropbox.

## 6. Abweichungen vom TDD

Der Prototyp wurde vollständig über die Kommandozeile gebaut, ohne den Editor zu bedienen. Daraus folgen die meisten Abweichungen: Blueprints, Widgets und Karten sind Binärdateien, die nur der Editor erzeugt.

| TDD | Umsetzung | Folge |
| --- | --- | --- |
| Inhalte als Blueprints und Daten-Assets (Abschnitt 3, 13) | Eine JSON-Datei `GameData.json` und C++-Klassen | Kein Git LFS nötig, das Repository enthält keine Binärdateien. Werte sind weiter ohne Neukompilieren änderbar. Die Namen `DA_`, `DT_`, `BP_` gibt es nicht |
| Oberfläche mit UMG-Widgets (Abschnitt 12) | Mit Canvas im `ABotSHUD` gezeichnet | Zweckmäßig, nicht hübsch. Für das fertige Spiel neu zu bauen |
| Karte als Level (Abschnitt 11) | Zur Laufzeit aus einem Startwert erzeugt, auf Server und Clients identisch. Als Level dient die leere Engine-Karte `/Engine/Maps/Entry` | Die Karte lässt sich nicht im Editor bearbeiten, nur über die Kartenwerte in den Daten |
| Navigationsnetz und Behavior Tree für Kreaturen (Abschnitt 3) | Kreaturen laufen direkt auf ihr Ziel zu. Bots nutzen eine eigene Wegsuche auf dem Kartenraster | Kein Navigationsnetz im Projekt |
| Fähigkeiten lokal vorhergesagt gestartet (Abschnitt 5) | Fähigkeiten laufen nur auf dem Server | Die Wirkung erscheint um die Latenz verzögert. Bewegung und Sturmlauf sind vorhergesagt |
| Sprünge | Sofortiger Versatz auf dem Server, der Client wird über die Bewegungskorrektur nachgezogen | Sichtbarer Sprung statt einer Bewegung |
| Relikte und Level als dauerhafte Effekte (Abschnitt 6, 10) | Der Server berechnet die Grundwerte neu und setzt sie. Zeitlich begrenzte Wirkungen laufen als Effekte | Gleiche Wirkung, weniger Effekte im Netz |
| Aktivgegenstand als gewährte Fähigkeit (Abschnitt 10) | Umgesetzt wie beschrieben, die Fähigkeit liest ihre Werte aus dem Gegenstand | Keine |
| Heldeneigene Ressource Geröll als Attribut-Satz (Abschnitt 6) | Geröll erzeugt direkt Schild | Keine eigene Anzeige für Geröll |
| Eigene Kollisionskanäle `Projectile` und `VisionBlocker` (Abschnitt 11) | Der vorhandene Kanal `Visibility` für Sicht und Projektile gegen Wände. Treffer auf Helden rechnet der Server geometrisch | Keine |
| Tastenbelegung in Daten (Abschnitt 7) | Belegung steht an einer Stelle im Code, als Enhanced-Input-Aktionen | Änderung braucht einen Build |
| Gameplay Cues für Effekte (Abschnitt 6) | Projektile und Flächen sind replizierte Akteure mit Platzhalter-Körpern in Beziehungsfarbe | Keine Trefferanzeige |
| Zielvorschau, Ping-Rad (Abschnitt 2) | Nicht enthalten, wie im TDD vorgesehen | – |
| Texttabelle (Abschnitt 12) | Texte stehen in den Daten und im HUD-Code | Übersetzung braucht Umbau |

## 7. Teststand

Stand 2026-11-12, alles auf dem Entwicklungsrechner von Florian.

| Prüfung | Ergebnis |
| --- | --- |
| Build `BattleOfTheStormEditor` Win64 Development | Fehlerfrei, für den Stand vor dem Selbsttest-Modus (siehe unten) |
| Automatisierungstests `BotS` | 7 von 7 bestanden: Sturmkreis, Auge im Kreis, Erfahrung mit Aufholbonus, Schild vor Lebenspunkten, gewichtete Beute, Spieldaten, Kartenlayout |
| Match nur mit Bots auf dediziertem Server (`-server -BotsOnly`, ohne Grafik) | Ein Match lief ohne Fehler und ohne Absturz von der Heldenwahl bis zum Sieger durch, Dauer rund 3 min 20 s. Im Log belegt: Truppzuteilung, Heldenwahl, Start, Talente, Niederschlag, Vollenden, Ausscheiden mit Platzierung, Erfahrung und Level, Truhen, Aufheben, Sieger, Neustart des Servers |

Nicht geprüft:

- **Kein Client wurde gestartet.** Kamera, Steuerung, HUD, Sichtsystem, Sturmwand und die Darstellung im Client sind ungetestet. Ob Umlaute in der Engine-Schrift erscheinen und ob die Belichtung stimmt, ist offen.
- Der Selbsttest-Modus des Clients (`-BotSDemo`, wählt Held und Startpunkt, bewegt sich, speichert Bildschirmfotos) und die Einstellung `bAutoManageActiveCameraTarget` sind geschrieben, aber **nicht gebaut**. Der nächste Build kann daran scheitern.
- Die angehobenen Erfahrungswerte (`XpPerLevel`) sind nach dem Bot-Match geändert und nicht erneut gelaufen.
- Sturm bis zum Ende: Das Bot-Match endete in Phase 1 durch Kämpfe. Sturmschaden, Phasen 2 und 3 und das Ende durch den Sturm sind im Lauf nicht vorgekommen.
- Wiederbelebung, Ultimative Fähigkeiten (Level 8) und Lager kamen im Log des Laufs nicht vor.
- Netzwerk unter Latenz, Bewegungsvorhersage des Sturmlaufs, zwei Clients im Editor, Leistungswerte, eigenständiger Server.
- Die Tests im Editor mit „Play“ hat niemand ausgeführt.

## 8. Nächste Schritte

1. Spieltest durch einen Menschen im Editor: Fühlt sich die Steuerung gut an? Das ist die erste Frage des Prototyps und lässt sich nicht automatisch beantworten.
2. Netzwerk-Emulation im Editor mit 100 ms einschalten und Bewegung, Sturmlauf und Fähigkeiten prüfen.
3. Werte in `GameData.json` nach dem Spieltest anpassen.
4. Entscheidungen aus Abschnitt 5 in GDD und TDD übernehmen, soweit Florian sie bestätigt.
5. Für Meilenstein 7: Engine aus dem Quellcode bauen, damit ein eigenständiger Server entsteht.
