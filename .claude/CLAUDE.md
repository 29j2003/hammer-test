# Source Map Editor (hammer-test) – Leitfaden für Claude Code

Moderner, 3D-zentrierter Karten-Editor für **Source 1** (Half-Life 2, Mapbase-Mods, Black Mesa, andere Source-Spiele),
Bedienung angelehnt an **Source 2 Hammer**, speichert normale `.vmf`. Nichts ist auf HL2
festgelegt – Spiele/Mods werden über Steam-Bibliotheken, `gameinfo.txt` und Hammer-/Hammer++-Configs gefunden.
Nutzer: Julius (`29j2003`), macht Mapping/Production, programmiert nicht selbst. Öffentliches Repo (`29j2003/hammer-test`).
Aufbau, Rezepte, Tests, offene Punkte: `docs/ENTWICKLUNG.md` (bei neuen Bausteinen dort mitpflegen).

## Aufbau
- `src/core/` – Bibliothek `smecore` ohne Qt (Namespace `sme`): Steam/Spiele (`steam`, `game_discovery`, `gameinfo`,
  `hammer_config`, `game`), Dateisystem aus Ordnern + VPKs (`filesystem`), Materialien/Texturen (`material`, `texture`,
  `assets` inkl. Sounds/Soundscapes/Partikel, Soundscapes bearbeiten `soundscape`, Höhenkarten für Relief `heightmap`, Sound-Decoder `audio`, Partikel aus PCF + Vorschau-Simulation `particles`, Modelle MDL/VVD/VTX `studio_model`), FGD (`entity_catalog`), Karte
  (`keyvalues`, `map`, Cordons/Pfade/Fallen lassen/Cubemap-Raum-Boxen `map_tools`), Geometrie (`brush`, `math`, CSG `csg`, Formen `shapes`),
  Bearbeiten + Undo (`editing`, `history`), Kopieren/Einfügen von Kartenobjekten (`clipboard`), Arten zum Ausblenden (`visibility`), VisGroups (`visgroups`), func_instance laden/auflösen (`instances`), Look-Werte von mapbase-fidelity pro Karte (`fidelity_look`), Licht passend zum Himmel (`sky_presets`), Git-Ausgaben lesen (`git_status`), Prüfungen mit Autofix + Leaks (`checks`), Kompilier-Meldungen (`compile_log`),
  Export glTF/OBJ (`scene_export`).
- `src/app/` – Qt-6-Editor (`smeapp` + `SourceMapEditor`): `editor` (Zustand, alle Änderungen über `commit()`),
  `viewport` (Kamera, Werkzeuge, Gizmos; Clip/Vertex/Polygon in `viewport_tools`), `renderer` (OpenGL 3.3,
  Licht-Vorschau nach VRAD-Formeln, Schatten in `renderer_shadows`), `material_cache`
  (lädt im Hintergrund), `model_cache` + `model_thumbnails` (Modelle im Hintergrund, Vorschaubilder per CPU-Rasterizer),
  `asset_browser` (Materialien, Modelle), `material_editor` + `material_preview` (inkl. Schalter für die mapbase-fidelity-Shader der Mod), `sound_browser` + `sound_player` + `soundscape_editor`
  (Sounds-Tab, Wiedergabe über QAudioSink, optional), `copy_paste`,
  `map_look_panel`, `mod_page` (Seite „Mod“ der Seitenleiste), `source_control_panel` (Git: Änderungen, Commit, Pull/Push), `particle_browser`, `tool_panel` (Werkzeug-Optionen, Formen, Entity-Liste), `properties_panel` (Smart Edit, NPC-Pose,
  Justify; func_instance-Kasten in `properties_instance`), `visgroups_panel`, `panels` (Outliner, Probleme, Log), `compile` + `compile_window` (VBSP/VVIS/VRAD/Spiel, Fortschritt),
  `shortcut_dialog`, `updater` (Update-Prüfung, Download, Installation), `main_window` (Seitenleiste: Map, Materials, Sounds, Particles, Mod), `style`.
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
- Doku nur `README.md` (Nutzer, Englisch, aktueller Stand) und `docs/ENTWICKLUNG.md` (Entwickler, Deutsch).
- Leistung zählt: nichts pro Frame neu aufbauen, was sich nicht geändert hat; Auswahl ändert nur Flags, nicht die Geometrie.
- Nicht im Spiel/auf Windows getestet heißt nicht getestet: im Bericht ausdrücklich so nennen.

## Befehle
- Einrichten (Linux): `apt install cmake ninja-build qt6-base-dev qt6-multimedia-dev libgl1-mesa-dev xvfb` (Multimedia optional: Sound-Vorschau) (Cloud: SessionStart-Hook)
- Bauen: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`
- Tests: `ctest --test-dir build -LE ui` · UI-Tests: `xvfb-run -a ctest --test-dir build -L ui`
- Demo: `build/tests/sme-make-fixture demo` dann `build/src/app/SourceMapEditor --steam demo/Steam --game sourcemods/mymod demo/Steam/steamapps/sourcemods/mymod/mapsrc/demo.vmf`
- Screenshot: zusätzlich `--screenshot out.png --camera "x y z yaw pitch" --select-class npc_citizen`
- Prüfen: `python -I tools/check_privacy.py`
- Release: `VERSION` erhöhen, committen, annotierten Tag `vX.Y.Z` (Nachricht = Release-Text, Englisch) pushen →
  `.github/workflows/release.yml` baut die Windows-ZIP + `.sha256` und veröffentlicht das Release; die Editoren bieten es an.

## Geplant
- Siehe „Fahrplan“ in `docs/ENTWICKLUNG.md`: Displacements im Vertex-Werkzeug, Entity-Icons/Overlays, Mesh-Modus
  (Variante B: konvex zerlegen bzw. als Prop kompilieren), echte Physik, Instanzen/Visgroups, Packen.
