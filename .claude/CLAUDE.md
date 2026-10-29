# Mapsmith (Repo hammer-test) – Leitfaden für Claude Code

Moderner, 3D-zentrierter Karten-Editor für **Source 1** (Half-Life 2, Mapbase-Mods, Black Mesa, andere Source-Spiele),
Bedienung angelehnt an **Source 2 Hammer**, speichert normale `.vmf`. Nichts ist auf HL2
festgelegt – Spiele/Mods werden über Steam-Bibliotheken, `gameinfo.txt` und Hammer-/Hammer++-Configs gefunden.
Nutzer: Julius (`29j2003`), macht Mapping/Production, programmiert nicht selbst. Öffentliches Repo (`29j2003/hammer-test`).
Aufbau, Rezepte, Tests, offene Punkte: `docs/ENTWICKLUNG.md` (bei neuen Bausteinen dort mitpflegen).

## Aufbau
- `src/core/` – Bibliothek `smecore` ohne Qt (Namespace `sme`): Text-/Pfad-Helfer (`text`), Versionen + Prüfsummen (`version`), Steam/Spiele (`steam`, `game_discovery`, `gameinfo`,
  `hammer_config`, `game`), Dateisystem aus Ordnern + VPKs (`filesystem`), Materialien/Texturen (`material`, `texture`,
  `assets` inkl. Sounds/Soundscapes/Partikel, Soundscapes bearbeiten `soundscape`, Höhenkarten für Relief `heightmap`, Mustererkennung (Ziegel, Fliesen, Bretter) + wo Relief Sinn hat `relief_pattern`, Sound-Decoder `audio`, Sounds bearbeiten + WAV schreiben `sound_edit`, eigene Sounds/Bilder/Partikel importieren `asset_import`, Beispiel-Dateien für die Importer `samples`, Modelle aus OBJ/SMD für studiomdl `model_import`, Sound-Kategorien `sound_categories`, Partikel aus PCF + Vorschau-Simulation `particles`, Modelle MDL/VVD/VTX `studio_model`, Skelett-Animation `studio_anim`), FGD (`entity_catalog`), Karte
  (`keyvalues`, `map`, Cordons/Pfade/Fallen lassen/Cubemap-Raum-Boxen/Make water/Start-Raum + Tutorial-Karte `map_tools`, Grenzen der Engine `map_budget`, Start-Raum/zuletzt bearbeitet `recent_projects`, Soundscape-Bereich Kugel/Block `soundscape_area`), Geometrie (`brush`, `math`, CSG `csg`, Cubemap-Sonden für die Reflexions-Vorschau `cubemaps`, Infodecals/Overlays auf Flächen `decals`, Formen `shapes`, Displacements anlegen/formen/nähen `displacement`),
  Bearbeiten + Undo (`editing`, `history`), Kopieren/Einfügen von Kartenobjekten (`clipboard`), Arten zum Ausblenden (`visibility`), VisGroups (`visgroups`), func_instance laden/auflösen (`instances`), Look-Werte von mapbase-fidelity pro Karte (`fidelity_look`), Game enhancements: Effekte von mapbase-fidelity automatisch pro Material, Einstellungen der Mod (`fidelity_effects`), Hauptmenü der Mod (`mod_menu`), Texte/Captions der Mod inkl. `.dat` (`localization`), Licht passend zum Himmel (`sky_presets`), Git-Ausgaben lesen (`git_status`), Quellcode der Mod finden + update.ps1/deploy.ps1/report.ps1 starten und lesen (`mod_code`), Funktionen der Mod aus Mapsmiths Bundle installieren/aktualisieren/entfernen inkl. Backups + VRAD (`mod_install`), Funktionen von mapbase-fidelity als Checkliste + was eine Neuinstallation entfernt (`mod_features`), neue Mod anlegen (`mod_template`), Skill-Dateien + HUD-Texte des Basisspiels für HL2-Mods (`mod_base_files`), Prüfungen mit Autofix + Leaks (`checks`), Lage-Prüfungen Spielerstart/Nichts/Lightmap-Scale/NPC-Knoten (`layout_checks`), Seile + Leitern in einem Schritt (`map_helpers`), Türen anlegen/einstellen + Vorschau-Bewegung (`doors`), Entities verbinden mit Vorschlägen (`io_links`), Animationen in der Karte + setzen, echte Animationsmodelle, Sequenz-Filter (`animation_use`), scripted_sequence starten/loopen + Trigger/Kartenstart + feste Citizen-Köpfe (`npc_scripts`), .smd-Animationen importieren + Hüll-Modell (`animation_import`), Autosave-Namen (`autosave`), Entities finden (`entity_search`), Assets der Auswahl + „Find in map“ (`asset_refs`), Material ersetzen (`material_replace`), Kompilier-Einstellungen + Befehlszeilen (`compile_options`), Kompilier-Meldungen (`compile_log`),
  Export glTF/OBJ (`scene_export`).
- `src/app/` – Qt-6-Editor (`smeapp` + Programm `Mapsmith`): `main` (Start, Optionen), `editor` (Zustand, alle Änderungen über `commit()`; Seil/Leiter aus der Auswahl in `editor_helpers`),
  `camera`, `viewport` (Kamera, Werkzeuge, Gizmos; Clip/Vertex/Polygon in `viewport_tools`, Displacement-Pinsel in `viewport_displace`, Tür-Vorschau in `viewport_doors`, hängende Seile in `viewport_ropes`), `renderer` (OpenGL 3.3,
  Licht-Vorschau nach VRAD-Formeln, Schatten in `renderer_shadows`, Cubemap-Reflexionen in `renderer_cubemaps`), `material_cache`
  (lädt im Hintergrund), `model_cache` + `model_thumbnails` (Modelle im Hintergrund, Vorschaubilder per CPU-Rasterizer), `models_page` + `model_viewer` (Seite Models: Ordner, Suche, 3D-Ansicht mit Skins/Bodygroups, Place in map), `animations_page` (Seite Animations: Sequenzen abspielen, Fundstellen, Use on selected entity, Add animation → studiomdl),
  `asset_browser` (Materialien, Modelle), `material_editor` + `material_preview` (inkl. Schalter für die mapbase-fidelity-Shader der Mod), `sound_browser` (Kategorien, Bearbeiten mit Undo, als WAV speichern) + `sound_player` + `soundscape_editor`
  (Sounds-Seite/-Dock, Wiedergabe über QAudioSink, optional), `copy_paste`,
  `map_look_panel`, `menu_editor` (Hauptmenü der Mod), `texts_panel` (Project › Texts: Lokalisierung, Captions), `mod_page` (Project-Seite, Tab Overview) + `enhancements` (Game enhancements: im Hintergrund schreiben, Schalter je Mod/Modell), `maps_panel` (alle Karten, Stapel-Kompilieren), `new_mod_dialog` (neue Mod aus Karten), `source_control_panel` (Git: Änderungen, Commit, Pull/Push), `mod_code_panel` (Project › Build from source, nur wer die Mod selbst baut: Update mod/Spieldateien/Bericht ohne .bat, Automatik nach Pull/beim Start; ohne Checkout die Skripte des Bundles), `features_panel` (Project › Features: Funktionen aus dem Bundle installieren/aktualisieren/entfernen, Checkliste der Mod-Funktionen, einmal angeboten, Fresh install), `ship_panel` (Karte/Mod für Spieler: bspzip, ZIP, Workshop), `particle_browser`, `tool_panel` (Werkzeug-Optionen, Formen, Entity-Liste), `properties_panel` (Smart Edit, NPC-Pose,
  Justify; func_instance-Kasten in `properties_instance`, Door-Kasten in `properties_door`, scripted_sequence- und Citizen-Kasten in `properties_script`), `connect_popup` (Verbinden: Output→Input-Vorschläge), `visgroups_panel`, `panels` (Outliner, Probleme, Map Budget, Log), `compile` + `compile_window` (VBSP/VVIS/VRAD/Spiel, Fortschritt),
  `wheel_guard` (Mausrad scrollt Panels statt Tabs/Felder zu ändern), `dialog_fit` (jeder Dialog/jedes Fenster passt auf den Bildschirm, Inhalt scrollt, Knöpfe bleiben sichtbar), `import_dialog` („Import …“ auf den Seiten: Sounds, Bilder, Modelle, Partikel in die Mod; „Use a sample …“), `samples_folder` (wo die Beispiel-Dateien liegen), `start_page` (Startseite: Continue, Recent, Start new), `text_copy` (Texte kopieren), `shortcut_dialog`, `autosaver` (Autosave, Wiederherstellen nach Absturz), `history_panel`, `find_dialog`, `replace_materials_dialog`, `updater` (Update-Prüfung, Download, Installation), `main_window` (Seitenleiste: Map, Materials, Models, Animations, Sounds, Particles, Project), `icons` (gezeichnete Icons), `style`, `toggle_switch`.
- `mod/` – das Add-on mapbase-fidelity (Spiel-Code, Shader, Spieldateien) für Mapbase, **erzeugt** von
  `tools/export_mod_addon.py` aus dem Mod-Repo (nie von Hand ändern; `--check`); CI `mod.yml` baut es auf Mapbase
  (`tools/repair_vpc_solution.py`, `tools/collect_mod_build.py`) zum Feature-Bundle `features/` neben `Mapsmith.exe`,
  das Project › Features (`features_panel`, Kern `mod_install`) mit einem Klick in die Mod installiert – Julius baut nichts.
- `src/probe/` – `sme-probe`: Bericht über Steam/Spiele/Mods (Pfade maskiert) und Einzelprüfungen (`vmf`, `material`, …).
- `tests/` – doctest; `fixture` (künstliche Steam-Installation, keine Valve-Dateien), `demo` (Demo-Inhalte + Demo-Karte), `make_samples` (`sme-make-samples`: Beispiel-Dateien → `<build>/samples`, MP3/OGG aus `samples_media`),
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
  und über das System-`git` der Quellcode-Verwaltung – nur wenn der Nutzer Fetch/Pull/Push drückt oder in Project › Mod code
  eine Automatik (Fetch/Pull beim Start) selbst eingeschaltet hat (Standard: aus).
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
