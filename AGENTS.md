# AGENTS.md – SHS_LOGISTICS

Diese Datei definiert den Projektkontext, die Architekturvorgaben und die Arbeitsweise für KI-Agenten im Workspace `SHS_LOGISTICS`.

---

## 1. Projektübersicht
- **Projektname:** SHS_LOGISTICS – Werksbahn-, Waggon- & Logistik-Leitsystem für die **SHS Logistics GmbH** (Stahl-Holding-Saar)
- **Technologie:** C++17 mit **Qt 6.9.0** (`Qt6::Widgets`, `Qt6::Network`, `Qt6::PrintSupport`, MinGW 13.1.0 64-bit, CMake + Ninja), analog zur Projektstruktur von `MeiserFEM`, direkt in **Qt Creator** (`GUI/CMakeLists.txt`) zu öffnen.
- **Corporate Design:** Angelehnt an den Webauftritt der SHS Logistics GmbH (`https://www.shs-logistics.de/shslogistics/startseite/`):
  - **Farbschema:** Stahl-Anthrazit (`#48555F`, `#434E57`), Stahlblau (`#5C839C`, `#A2B1BE`, `#E4EEF2`), SHS-Orange (`#EF7D00`), Navigations-Goldgelb (`#FBD50E`).
  - **Branding:** Originales SHS-Logo (`SHS_Logo.png` / `SHSIcon.png` via Qt Resource `SHSRes.qrc`) und Leitmotiv *„Partner [schafft Zukunft] Stahl“*.

---

## 2. Kernfunktionen & Fachdomäne
1. **Interaktive 2D-Karten- & Werksansicht (`RailMapWidget`):**
   - **Hauptfokus Völklingen (Saarstahl AG):** Google-Maps-Kartenhintergrund (Live-Tiles für Straßenkarte, Satellit, Hybrid mit lokalem Tile-Cache) kombiniert mit einer gut sichtbaren, **vereinfachten schematischen Darstellung** des Saarstahl-Werksgeländes in Völklingen (Straßen A620, B51/Bismarckstraße, Werksstraßen, Saar, Rossel, Werkhallen wie LD-Stahlwerk, Strangguss, Walzwerk Nauweiler Gewann, Saarschmiede, Versand/Ladestellen sowie alle Werks- und Übergabegleise).
   - **Weitere Standorte mit Direkt-Zoom:**
     - **Dillinger Hütte (Dillingen/Saar):** ROGESA Hochöfen, Stahlwerk, Grobblechwalzwerk, Hafen Dillingen, Firmensitz SHS Logistics GmbH (Roheisen- & Brammen-Transport nach Völklingen).
     - **Werk Burbach (Saarbrücken-Burbach):** Saarstahl Drahtwerk (Knüppel-Anlieferung aus Völklingen).
     - **Werk Neunkirchen (Neunkirchen/Saar):** Saarstahl Walzwerk (Qualitäts- und Edelbaustähle).
     - **Gesamtübersicht Saarland-Korridor:** Komplette Bahnverbindung Dillingen ↔ Völklingen ↔ Burbach ↔ Neunkirchen.
2. **2D-Eisenbahnsimulation & Zugkonfiguration:**
   - Züge und einzelne Waggons bewegen sich als maßstäbliche 2D-Objekte entlang der Bahngleise (Play/Pause, Geschwindigkeitsregler, Richtungswechsel, Weichen-/Routenwahl).
   - **Zuglänge dynamisch ändern:** Züge können im laufenden Betrieb jederzeit verlängert (`+ Waggon anhängen`) oder verkürzt (`- Waggon abhängen` bzw. über Waggon-Anzahl-Steuerung) werden.
3. **Waggon-Klassifizierung & Optische Unterscheidung:**
   - Einzelne Positionen der Waggons im Zugverband werden nummeriert (`#1`, `#2`, ...) und farblich sowie geometrisch (Achsen/Drehgestelle/Rungen) klar unterschieden:
     - **Samms (6-Achser Schwerlast-Flachwagen):** SHS-Orange (`#EF7D00`), zwei 3-achsige Drehgestelle (6 sichtbare Achsen), für schwere Knüppel/Brammen.
     - **Rns-z (4-Achser Drehgestell-Rungenwagen):** SHS-Stahlblau (`#0284C7`), zwei 2-achsige Drehgestelle (4 sichtbare Achsen) + seitliche Rungen-Markierungen, für Walzstahl/Profile/Draht.
     - **Weitere 4-Achser (z. B. Shimmns/Eaos):** Smaragdgrün (`#10B981`), 4 sichtbare Achsen.
     - **Weitere 6-Achser (z. B. Faals/Torpedopfannenwagen):** Karmesinrot/Violett (`#DC2626`), 6 sichtbare Achsen.
     - Umschaltbarer Farbmodus: *Färbung nach Waggon-Bauart (Samms / Rns-z)* oder *Färbung strikt nach Achsanzahl (4-Achser vs. 6-Achser)*.
4. **Bahn-API-Schnittstelle (DB API) & Zufalls-Testpositionierung:**
   - **API-Ortung:** Abruf von Waggon- und Zugpositionen über eine REST/JSON-Schnittstelle (DB Cargo / DB InfraGO Telematik-Format mit `DB-Client-Id` & `DB-Api-Key` bzw. integriertem DB-API-Telematik-Simulator) inklusive GPS-to-Track-Snapping (`lat`/`lon` -> nächste Gleisposition).
   - **Test-Modus (Zufallspositionierung):** Ein-Klick-Funktion, um Züge und Einzelwaggons (`Samms`, `Rns-z`, `4-Achser`, `6-Achser`) zu Testzwecken zufällig auf den Werks- und Streckengleisen zu verteilen.
5. **Quellen, Senken, Güter-Objekte & Lokomotiv-Engpass (Zwischenlagerung):**
   - **10 reale Quellen & Senken:** `Q-DL-ROGESA` (ROGESA Hochöfen 4 & 5: Flüssigroheisen 1420 °C), `Q-DL-STRANG` (Strangguss Dillingen: Brammen), `QS-DL-BLECH` (Dillinger Grobblechwalzwerk: Grobbleche), `S-DL-HAFEN` (Hafen Dillingen), `QS-VK-LD` (LD-Stahlwerk & Strangguss Völklingen: Knüppel, Vorblöcke, Schmiedeblöcke), `QS-VK-SCHMIEDE` (Saarschmiede: Turbinenwellen/Schmiedestücke), `QS-VK-WALZ` (Walzwerk Nauweiler Gewann & Ladestelle 1: Stabstahl, Profile, Drahtcoils), `P-VK-ABSTELL` (Übergabebahnhof & Abstellgleise Völklingen), `QS-BB-DRAHT` (Drahtwerk Burbach: Aufgerollter Walzdraht / Coils), `QS-NK-WALZ` (Walzwerk Neunkirchen: Stabstahl, Edelbaustahl & Draht).
   - **Güter als Objekte (`CargoOrder`):** Im Tab *„📦 Güter, Quellen & Senken“* können Güterchargen (Stahlknüppel, Dillinger Grobbleche, Stahlbrammen, aufgerollter Walzdraht/Coils, Vorblöcke, Stabstahl, Flüssigroheisen, Schmiedeteile) als Objekte angelegt und automatisch auf passende Waggons (`Samms [6A]`, `Rns-z [4A]`, `Shimmns [4A Coil]`, `Torpedo [6A]`) verteilt werden. Zudem wird das geladene Stahlprodukt detailgetreu auf jedem 2D-Waggon gezeichnet (konzentrische Drahtrollen bei Coils, Blechpakete mit Zurrgurten bei Grobblechen, Brammenblöcke, Knüppelbündel, glühende Torpedopfanne).
   - **Lokomotiv-Pool & Waggon-Zwischenlagerung:** Ist im Lokomotiv-Pool keine freie Lokomotive verfügbar (oder wird die Lok abgekuppelt), werden die mit Gütern beladenen Waggons als **abgestellte Waggongruppe (`🅿️ KEINE LOK – GELAGERT`)** auf den Abstell-/Werksgleisen zwischengelagert, bis eine freie Lokomotive angekuppelt wird.

---

## 3. Repository-Struktur
- `AGENTS.md` – Projektkontext und Architekturregeln
- `README.md` – Bedienungs- und Build-Anleitung
- `SHS_Logo.png`, `SHSIcon.png` – Offizielle Marken-Assets der SHS Logistics GmbH
- `Build_SHS_Logistics_Cpp.bat` – Kompiliert das C++/Qt-6-Projekt via CMake + Ninja + MinGW 64-bit und führt `windeployqt` aus
- `Run_SHS_Logistics_Cpp.bat` – Startet die kompilierte Anwendung (`GUI/build/Desktop_Qt_6_9_0_MinGW_64_bit-Debug/SHS_Logistics.exe`)
- `Open_QtCreator.bat` – Öffnet das Projekt direkt in Qt Creator (`C:\Qt\qtcreator-18.0.2\bin\qtcreator.exe`)
- `GUI/` – C++/Qt-Quellcode:
  - `CMakeLists.txt` – Qt 6 CMake-Projektdatei
  - `main.cpp` – Einstiegspunkt mit Fusion-Style und SHS-Corporate-Theme
  - `mainwindowshslogistics.h` / `.cpp` / `.ui` – Hauptfenster mit Menüleiste, SHS-Header, Standort-Schnellwahl, Zug-/Waggon-Editor und DB-API-Steuerung
  - `railmapwidget.h` / `.cpp` – Interaktives 2D-Karten- und Eisenbahnsimulations-Widget (Google Maps Tile-Loader + vereinfachte Werks-/Gleisgeometrie + 2D-Zug-Renderer)
  - `SHSRes.qrc` – Eingebettete Icons und Logos
