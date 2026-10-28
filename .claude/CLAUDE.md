# Mapsmith (Repo hammer-test) – Leitfaden für Claude Code

Moderner, 3D-zentrierter Karten-Editor für **Source 1** (Half-Life 2, Mapbase-Mods, Black Mesa, andere Source-Spiele),
Bedienung angelehnt an **Source 2 Hammer**, speichert normale `.vmf`. Nichts ist auf HL2
festgelegt – Spiele/Mods werden über Steam-Bibliotheken, `gameinfo.txt` und Hammer-/Hammer++-Configs gefunden.
Nutzer: Julius (`29j2003`), macht Mapping/Production, programmiert nicht selbst. Öffentliches Repo (`29j2003/hammer-test`).
Aufbau, Rezepte, Tests, offene Punkte: `docs/ENTWICKLUNG.md` (bei neuen Bausteinen dort mitpflegen).

## Aufbau
- `src/core/` – Bibliothek `smecore` ohne Qt (Namespace `sme`): Text-/Pfad-Helfer (`text`), Versionen + Prüfsummen (`version`), Steam/Spiele (`steam`, `game_discovery`, `gameinfo`,
  `hammer_config`, `game`), Dateisystem aus Ordnern + VPKs (`filesystem`), Materialien/Texturen (`material`, `texture`,
  `assets` inkl. Sounds/Soundscapes/Partikel, Soundscapes bearbeiten `soundscape`, Höhenkarten für Relief `heightmap`, Sound-Decoder `audio`, Sounds bearbeiten + WAV schreiben `sound_edit`, Sound-Kategorien `sound_categories`, Partikel aus PCF + Vorschau-Simulation `particles`, Modelle MDL/VVD/VTX `studio_model`), FGD (`entity_catalog`), Karte
  (`keyvalues`, `map`, Cordons/Pfade/Fallen lassen/Cubemap-Raum-Boxen `map_tools`, Soundscape-Bereich Kugel/Block `soundscape_area`), Geometrie (`brush`, `math`, CSG `csg`, Infodecals/Overlays auf Flächen `decals`, Formen `shapes`, Displacements anlegen/formen/nähen `displacement`),
  Bearbeiten + Undo (`editing`, `history`), Kopieren/Einfügen von Kartenobjekten (`clipboard`), Arten zum Ausblenden (`visibility`), VisGroups (`visgroups`), func_instance laden/auflösen (`instances`), Look-Werte von mapbase-fidelity pro Karte (`fidelity_look`), Hauptmenü der Mod (`mod_menu`), Texte/Captions der Mod inkl. `.dat` (`localization`), Licht passend zum Himmel (`sky_presets`), Git-Ausgaben lesen (`git_status`), neue Mod anlegen (`mod_template`), Prüfungen mit Autofix + Leaks (`checks`), Autosave-Namen (`autosave`), Entities finden (`entity_search`), Assets der Auswahl + „Find in map“ (`asset_refs`), Material ersetzen (`material_replace`), Kompilier-Einstellungen + Befehlszeilen (`compile_options`), Kompilier-Meldungen (`compile_log`),
  Export glTF/OBJ (`scene_export`).
- `src/app/` – Qt-6-Editor (`smeapp` + Programm `Mapsmith`): `main` (Start, Optionen), `editor` (Zustand, alle Änderungen über `commit()`),
  `camera`, `viewport` (Kamera, Werkzeuge, Gizmos; Clip/Vertex/Polygon in `viewport_tools`, Displacement-Pinsel in `viewport_displace`), `renderer` (OpenGL 3.3,
  Licht-Vorschau nach VRAD-Formeln, Schatten in `renderer_shadows`), `material_cache`
  (lädt im Hintergrund), `model_cache` + `model_thumbnails` (Modelle im Hintergrund, Vorschaubilder per CPU-Rasterizer), `models_page` + `model_viewer` (Seite Models: Ordner, Suche, 3D-Ansicht mit Skins/Bodygroups, Place in map),
  `asset_browser` (Materialien, Modelle), `material_editor` + `material_preview` (inkl. Schalter für die mapbase-fidelity-Shader der Mod), `sound_browser` (Kategorien, Bearbeiten mit Undo, als WAV speichern) + `sound_player` + `soundscape_editor`
  (Sounds-Seite/-Dock, Wiedergabe über QAudioSink, optional), `copy_paste`,
  `map_look_panel`, `menu_editor` (Hauptmenü der Mod), `texts_panel` (Project › Texts: Lokalisierung, Captions), `mod_page` (Project-Seite, Tab Overview), `maps_panel` (alle Karten, Stapel-Kompilieren), `new_mod_dialog` (neue Mod aus Karten), `source_control_panel` (Git: Änderungen, Commit, Pull/Push), `ship_panel` (Karte/Mod für Spieler: bspzip, ZIP, Workshop), `particle_browser`, `tool_panel` (Werkzeug-Optionen, Formen, Entity-Liste), `properties_panel` (Smart Edit, NPC-Pose,
  Justify; func_instance-Kasten in `properties_instance`), `visgroups_panel`, `panels` (Outliner, Probleme, Log), `compile` + `compile_window` (VBSP/VVIS/VRAD/Spiel, Fortschritt),
  `shortcut_dialog`, `autosaver` (Autosave, Wiederherstellen nach Absturz), `history_panel`, `find_dialog`, `replace_materials_dialog`, `updater` (Update-Prüfung, Download, Installation), `main_window` (Seitenleiste: Map, Materials, Models, Sounds, Particles, Project), `icons` (gezeichnete Icons), `style`, `toggle_switch`.
- `src/probe/` – `sme-probe`: Bericht über Steam/Spiele/Mods (Pfade maskiert) und Einzelprüfungen (`vmf`, `material`, …).
- `tests/` – doctest; `fixture` (künstliche Steam-Installation, keine Valve-Dateien), `demo` (Demo-Inhalte + Demo-Karte),
  `test_app.cpp` (echte Fenster mit simulierter Maus, Label `ui`, braucht OpenGL → unter Linux `xvfb-run`).
- Abhängigkeiten in `cmake/Dependencies.cmake`, **nur auf Commit-SHAs gepinnt** (sourcepp, doctest; stb_vorbis und
  minimp3 als Einzeldateien mit SHA-256).

## Regeln
- Namen im Code englisch, Kommentare englisch, Kopfkommentar je Datei; Tabs; Stil wie im Bestand.
- Kern ohne Qt und testbar halten: neue Logik (Bearbeiten, Prüfen, Formate) nach `src/core/` mit Tests, UI nur dünn darüber.
- Jede Kartenänderung als `Transaction`/`edit::…` → `Editor::commit()` (Undo!). Nie `doc()` direkt ändern ohne Transaction.
- Daten nie verlieren: Unbekanntes aus der VMF bleibt in `extra` erhalten; `sme-probe vmf` muss „stable“ melden.
- Fremde Dateien (VPK, VTF, MDL, VMF) sind nicht vertrauenswürdig: Grenzen prüfen, keine Pfade aus Mounts heraus (`..`).
- Keine Telemetrie. Netzwerk nur für Updates (`updater`: GitHub Releases dieses Repos, abschaltbar, Prüfsumme Pflicht)
  und über das System-`git` der Quellcode-Verwaltung – nur wenn der Nutzer Fetch/Pull/Push drückt.
  Der Probe-Bericht maskiert Benutzernamen und Home-Ordner.
- Keine Valve-Dateien im Repo (Texturen/Modelle/FGDs) – Testdaten werden erzeugt.
- Commits als `Claude <noreply@anthropic.com>` (von Julius so gewünscht; seine eigene Adresse nie verwenden), deutsche
  Commit-Nachrichten; keine E-Mail-Adressen in Dateien (`tools/check_privacy.py`, CI-Job `checks`). GitHub Actions nur mit Commit-SHA pinnen, `permissions: contents: read`.
- Doku: `README.md` (Nutzer, Englisch, aktueller Stand, kurz), Handbuch `docs/manual/` (Englisch, GitHub Pages, ausführlich,
  inkl. Mapping-Grundlagen) und `docs/ENTWICKLUNG.md` (Entwickler, Deutsch). XBLAH's Modding Tool nur in der README als Inspiration nennen
  („inspired by“), sonst nirgends; nichts übernehmen. Valves Hammer/Source 2 als Verhaltens-Referenz in Kommentaren ist ok.
- Leistung zählt: nichts pro Frame neu aufbauen, was sich nicht geändert hat; Auswahl ändert nur Flags, nicht die Geometrie.
- Nicht im Spiel/auf Windows getestet heißt nicht getestet: im Bericht ausdrücklich so nennen.

## Befehle
- Einrichten (Linux): `apt install cmake ninja-build qt6-base-dev qt6-multimedia-dev libgl1-mesa-dev xvfb` (Multimedia optional: Sound-Vorschau) (Cloud: SessionStart-Hook)
- Bauen: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`
- Tests: `ctest --test-dir build -LE ui` · UI-Tests: `xvfb-run -a ctest --test-dir build -L ui`
- Demo: `build/tests/sme-make-fixture demo` dann `build/src/app/Mapsmith --steam demo/Steam --game sourcemods/mymod demo/Steam/steamapps/sourcemods/mymod/mapsrc/demo.vmf`
- Screenshot: zusätzlich `--screenshot out.png --camera "x y z yaw pitch" --select-class npc_citizen`
- Prüfen: `python -I tools/check_privacy.py`
- Release: `VERSION` erhöhen, committen, annotierten Tag `vX.Y.Z` (Nachricht = Release-Text, Englisch) pushen →
  `.github/workflows/release.yml` baut den Installer `Mapsmith-Setup-<v>.exe` (Inno Setup, `installer/mapsmith.iss`) und die
  Windows-ZIP (für den Updater), je mit `.sha256`, und veröffentlicht das Release; die Editoren bieten es an.

## Geplant
- Siehe „Fahrplan“ in `docs/ENTWICKLUNG.md`: Displacements im Vertex-Werkzeug, Overlay-Ecken ziehen, Mesh-Modus
  (Variante B: konvex zerlegen bzw. als Prop kompilieren), echte Physik.
