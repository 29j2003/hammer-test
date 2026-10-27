# Source Map Editor (hammer-test) – Leitfaden für Claude Code

Moderner, 3D-zentrierter Karten-Editor für **Source 1** (Half-Life 2, Mapbase-Mods, Black Mesa, andere Source-Spiele),
Bedienung angelehnt an **Source 2 Hammer**, speichert normale `.vmf`. Wie XBLAH's Modding Tool: nichts ist auf HL2
festgelegt – Spiele/Mods werden über Steam-Bibliotheken, `gameinfo.txt` und Hammer-/Hammer++-Configs gefunden.
Nutzer: Julius (`29j2003`), macht Mapping/Production, programmiert nicht selbst. Öffentliches Repo (`29j2003/hammer-test`).
Aufbau, Rezepte, Tests, offene Punkte: `docs/ENTWICKLUNG.md` (bei neuen Bausteinen dort mitpflegen).

## Aufbau
- `src/core/` – Bibliothek `smecore` ohne Qt (Namespace `sme`): Steam/Spiele (`steam`, `game_discovery`, `gameinfo`,
  `hammer_config`, `game`), Dateisystem aus Ordnern + VPKs (`filesystem`), Materialien/Texturen (`material`, `texture`,
  `assets`), FGD (`entity_catalog`), Karte (`keyvalues`, `map`), Geometrie (`brush`, `math`), Bearbeiten + Undo
  (`editing`, `history`), Prüfungen mit Autofix + Leaks (`checks`).
- `src/app/` – Qt-6-Editor (`smeapp` + `SourceMapEditor`): `editor` (Zustand, alle Änderungen über `commit()`),
  `viewport` (Kamera, Werkzeuge, Gizmos), `renderer` (OpenGL 3.3, Licht-Vorschau nach VRAD-Formeln), `material_cache`
  (lädt im Hintergrund), `asset_browser`, `properties_panel` (Smart Edit, NPC-Pose), `panels` (Outliner, Probleme, Log),
  `compile` (VBSP/VVIS/VRAD/Spiel), `main_window`, `style`.
- `src/probe/` – `sme-probe`: Bericht über Steam/Spiele/Mods (Pfade maskiert) und Einzelprüfungen (`vmf`, `material`, …).
- `tests/` – doctest; `fixture` (künstliche Steam-Installation, keine Valve-Dateien), `demo` (Demo-Inhalte + Demo-Karte),
  `test_app.cpp` (echte Fenster mit simulierter Maus, Label `ui`, braucht OpenGL → unter Linux `xvfb-run`).
- Abhängigkeiten in `cmake/Dependencies.cmake`, **nur auf Commit-SHAs gepinnt** (sourcepp, doctest).

## Regeln
- Namen im Code englisch, Kommentare englisch, Kopfkommentar je Datei; Tabs; Stil wie im Bestand.
- Kern ohne Qt und testbar halten: neue Logik (Bearbeiten, Prüfen, Formate) nach `src/core/` mit Tests, UI nur dünn darüber.
- Jede Kartenänderung als `Transaction`/`edit::…` → `Editor::commit()` (Undo!). Nie `doc()` direkt ändern ohne Transaction.
- Daten nie verlieren: Unbekanntes aus der VMF bleibt in `extra` erhalten; `sme-probe vmf` muss „stable“ melden.
- Fremde Dateien (VPK, VTF, MDL, VMF) sind nicht vertrauenswürdig: Grenzen prüfen, keine Pfade aus Mounts heraus (`..`).
- Kein Netzwerkzugriff, keine Telemetrie. Der Probe-Bericht maskiert Benutzernamen und Home-Ordner.
- Keine Valve-Dateien im Repo (Texturen/Modelle/FGDs) – Testdaten werden erzeugt.
- Commits nur als `29j2003 <44442596+29j2003@users.noreply.github.com>`, deutsche Commit-Nachrichten; keine E-Mail-Adressen
  in Dateien (`tools/check_privacy.py`, CI-Job `checks`). GitHub Actions nur mit Commit-SHA pinnen, `permissions: contents: read`.
- Doku nur `README.md` (Nutzer, Englisch, aktueller Stand) und `docs/ENTWICKLUNG.md` (Entwickler, Deutsch).
- Leistung zählt: nichts pro Frame neu aufbauen, was sich nicht geändert hat; Auswahl ändert nur Flags, nicht die Geometrie.
- Nicht im Spiel/auf Windows getestet heißt nicht getestet: im Bericht ausdrücklich so nennen.

## Befehle
- Einrichten (Linux): `apt install cmake ninja-build qt6-base-dev libgl1-mesa-dev xvfb` (Cloud: SessionStart-Hook)
- Bauen: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`
- Tests: `ctest --test-dir build -LE ui` · UI-Tests: `xvfb-run -a ctest --test-dir build -L ui`
- Demo: `build/tests/sme-make-fixture demo` dann `build/src/app/SourceMapEditor --steam demo/Steam --game sourcemods/mymod demo/Steam/steamapps/sourcemods/mymod/mapsrc/demo.vmf`
- Screenshot: zusätzlich `--screenshot out.png --camera "x y z yaw pitch" --select-class npc_citizen`
- Prüfen: `python -I tools/check_privacy.py`

## Geplant
- Siehe „Fahrplan“ in `docs/ENTWICKLUNG.md`: Modelle im Viewport, Vertex-/Kanten-Werkzeuge (konvex bleibend), Mesh-Modus
  (Variante B: konvex zerlegen bzw. als Prop kompilieren), Sonnenschatten, Instanzen/Visgroups, Overlays, Clip-Werkzeug.
