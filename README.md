# SHS Logistics GmbH – Werksbahn-, Waggon- & Standort-Simulation (Qt 6 / C++)

Eigenes C++/Qt-6-Software-Tool für die **SHS Logistics GmbH** (*„Partner [schafft Zukunft] Stahl“*) analog zur Projektstruktur von `MeiserFEM`.

---

## 1. Schnellstart (Kompilieren, Starten & Qt Creator)

- **Kompilieren (CMake + Ninja + Qt 6.9.0 MinGW 64-bit):**
  ```bat
  Build_SHS_Logistics_Cpp.bat
  ```
- **Anwendung starten (startet automatisch das Standalone-Release):**
  ```bat
  Run_SHS_Logistics_Cpp.bat
  ```
- **Alleine lauffähiger Release-Ordner (ohne Qt-Installation auf jedem Windows-PC nutzbar):**
  - `Release_Standalone\SHS_Logistics.exe` *(inkl. aller Qt6-/MinGW-DLLs, Plugins und `tile_cache\`)*
- **Projekt direkt in Qt Creator öffnen:**
  ```bat
  Open_QtCreator.bat
  ```
  *(Oder in Qt Creator Datei `GUI\CMakeLists.txt` öffnen)*

- **Ausführliche Dokumentation & Word-Handbuch:**
  - Markdown-Handbuch: `SHS_Logistics_Dokumentation_und_Handbuch.md`
  - Microsoft-Word-Handbuch (inkl. Screenshots): `SHS_Logistics_Handbuch_und_Projektbeschreibung.docx`
  - Skript zum Neu-Generieren des Word-Handbuchs: `python Scripts\Generate-WordManual.py`

---

## 2. Funktionsumfang

1. **Corporate Design wie `shs-logistics.de`:**
   - Originales SHS-Logo (`SHS_Logo.png`), Slogan *„Partner [schafft Zukunft] Stahl“*, Farbpalette in Stahl-Anthrazit (`#48555F`), Stahlblau (`#5C839C`, `#E4EEF2`), SHS-Orange (`#EF7D00`) und Navigations-Goldgelb (`#FBD50E`).
   - Menüleiste und Schnellwahl-Buttons für alle Standorte, Zuglängen-Änderungen und API-Funktionen.

2. **Großer zentraler Karten- & Simulations-Screen (`RailMapWidget`):**
   - **Völklingen & Saarstahl AG Hauptwerk (Standardansicht):** Kombination aus **Google Maps** (Live-Kartenkacheln für Straße / Satellit / Hybrid) und einer **vereinfachten, kontrastreichen Werksdarstellung** mit Straßen (`A620`, `B51 Bismarckstraße`, `Püttlinger Straße`, `Kühlweinstraße`, `Werksstraße Tor 1–4`), Flüssen (`Saar`, `Rossel`), Werkhallen (`LD-Stahlwerk`, `Stranggießanlagen`, `Saarschmiede`, `Walzwerk Nauweiler Gewann & Ladestelle 1`, `WKE Völklinger Hütte`) sowie den Werks- und Übergabegleisen.
   - **Weitere Standorte per Klick / Zoom erreichbar:**
     - **Dillinger Hütte & ROGESA (Dillingen/Saar):** Hochöfen 4 & 5 (Stahl-/Roheisenentstehung), Grobblechwalzwerk, Hafen Dillingen und Firmensitz der SHS Logistics GmbH.
     - **Saarstahl Werk Burbach:** Drahtwerk in Saarbrücken-Burbach (Knüppel-Anlieferung per Bahn aus Völklingen).
     - **Saarstahl Werk Neunkirchen:** Walzwerk und Edelbaustahl-Zurichterei in Neunkirchen.
     - **Gesamtübersicht Saarland-Korridor:** Zeigt das gesamte Streckennetz zwischen Dillingen, Völklingen, Burbach und Neunkirchen.

3. **2D-Eisenbahnsimulation & Zuglänge dynamisch ändern:**
   - Züge und Waggons fahren als 2D-Objekte auf den Werks- und Streckengleisen.
   - Jeder Zug lässt sich jederzeit **verlängern** (`+ Samms [6A] anhängen`, `+ Rns-z [4A] anhängen`, `+ 4-Achser`, `+ 6-Achser`) oder **verkürzen** (`- Letzten Waggon abhängen` bzw. über die Waggon-Anzahl-SpinBox).

4. **Optische Unterscheidung der Waggon-Positionen (`Samms` vs. `Rns-z` sowie `4-Achser` vs. `6-Achser` & `Torpedo`):**
   - **Samms (`Samms [6A]` Schwerlast-Flachwagen):** **SHS-Orange (`#EF7D00`)**, gezeichnet mit zwei 3-achsigen Drehgestellen (**6 sichtbare Querachsen**) und Brammen-/Knüppel-Ladung.
   - **Rns-z (`Rns-z [4A]` Drehgestell-Rungenwagen):** **SHS-Stahlblau (`#0284C7`)**, gezeichnet mit zwei 2-achsigen Drehgestellen (**4 sichtbare Querachsen**), gelben Seitenrungen und Grobblech-/Stabstahl-Ladung.
   - **4-Achser Standard (`4-Achser [4A]` Shimmns/Eaos):** **Smaragdgrün (`#10B981`)**, 4 Achsen + 4 konzentrische Drahtcoils.
   - **Torpedopfannenwagen (`Torpedo [6A]` mit 1420 °C Flüssigroheisen):** **Karmesinrot (`#E11D48`)**, 6 Achsen + spindelförmiger 2D-Torpedokessel mit glühendem 1420 °C Flüssigeisen-Kern.
   - Umschaltbarer Farbmodus zwischen *Alle 4 Bauarten getrennt*, *Fokus Samms vs. Rns-z* und *Strikt 4-Achser vs. 6-Achser*.

5. **Quellen (`⬆️ Güter-Aufnahme`), Senken (`⬇️ Güter-Abladung`), Güter-Objekte & Lokomotiv-Engpass:**
   - **Aktiver 4-Phasen-Zyklus:** Züge halten an den 2D-Verladebrücken ihrer **Quelle** (`⬆️ AUFNAHME XX%`, Waggons `#1..#N` werden nacheinander beladen), fahren beladen zur **Senke**, halten dort zur **Abladung** (`⬇️ ABLADUNG XX%`, Waggons `#1..#N` entladen zu `Leerwagen`) und fahren als Leerzug zurück zur Quelle.
   - **Güter-Objekte (`CargoOrder`):** Flüssigroheisen (1420 °C), Stahlbrammen, Dillinger Grobbleche, Stahlknüppel, Vorblöcke, aufgerollter Walzdraht (Coils), Stabstahl und Freiform-Schmiedestücke lassen sich als Chargen anlegen und automatisch auf passende Waggons verteilen.
   - **Lokomotiv-Pool & Zwischenlagerung (`🅿️ KEINE LOK – GELAGERT`):** Reichen die verfügbaren Lokomotiven im Pool nicht aus (oder wird die Lok abgekuppelt), bleiben die beladenen Waggons auf Abstellgleisen zwischengelagert, bis wieder eine freie Lok angekuppelt wird.

6. **Positionierung über Bahn-API (DB API / SAVVY Telematik) oder Zufalls-Testmodus:**
   - **`🎲 Zufällige Positionen (Test, F8)`:** Verteilt alle Züge und Waggons auf Knopfdruck zufällig auf den Gleisen (wahlweise im gesamten Netz oder gezielt auf dem Werksgelände Völklingen).
   - **`📡 DB-API Ortung (F9)`:** Ruft Waggon-Koordinaten inkl. **Ril-100/DS100-Betriebsstellen** (`SVK`, `SDL`, `SBU`, `SNK`), **VzG-Streckenkilometern** (`VzG 3230` / `VzG 3511`), 12-stelligen UIC-Nummern und **GPS-to-Track-Snapping** ab.


