# Entwicklung – Source Map Editor

Ziel: ein Karten-Editor für Source 1, der sich wie Source 2 Hammer bedient (3D-Ansicht zuerst, Gizmos, Asset-Browser,
Licht-Vorschau), aber normale `.vmf` für HL2, Mapbase, Black Mesa und andere Source-Spiele schreibt. Spiele und Mods
werden wie bei XBLAH's Modding Tool automatisch gefunden.

## Aufbau

```
src/core/   smecore   – ohne Qt, vollständig getestet
src/app/    smeapp    – Qt-6-Oberfläche (+ SourceMapEditor = main.cpp)
src/probe/  sme-probe – Diagnose auf der Kommandozeile
tests/      doctest   – Kern-Tests, UI-Tests (Label "ui"), Fixture + Demo
```

### Kern (`src/core`, Namespace `sme`)

| Modul | Aufgabe |
|---|---|
| `text` | Pfad-Normalisierung (Kleinbuchstaben, `/`), case-insensitive Suche auf der Platte |
| `steam` | Steam finden (Registry, Standardordner, `SME_STEAM_ROOT`), `libraryfolders.vdf`, `appmanifest_*.acf` |
| `gameinfo` | `gameinfo.txt` lesen, `SearchPaths` auflösen (`|gameinfo_path|`, `|all_source_engine_paths|`, `|appid_N|`, `custom/*`, `[$WIN32]`) |
| `filesystem` | `GameFileSystem`: Ordner + VPKs in Suchreihenfolge, erster Treffer gewinnt; `pak01_dir.vpk` vor dem Ordner; thread-sicher |
| `game_discovery` | alle Spiele (App-Ordner mit `gameinfo.txt`) und Mods (`sourcemods`), FGD-Vorschläge (Hammer++ > Hammer > Mod > Mapbase-FGD > Haupt-FGD des Spiels) |
| `hammer_config` | `GameConfig.txt` / `hammerplusplus_gameconfig.txt` importieren (FGDs, Tools, Ordner) |
| `material`, `texture`, `assets` | VMT inkl. `patch`, VTF → RGBA (vtfpp), Material-Kategorien, MDL-Kopf (Sequenzen, `$includemodel`), Soundscripts, „Meintest du …“ |
| `entity_catalog` | FGDs laden (toolpp), Basisklassen auflösen, Helfer (`studio`, `size`, `color`, `iconsprite`) |
| `keyvalues`, `map` | VMF lesen/schreiben; Unbekanntes bleibt in `extra` (verlustfrei) |
| `brush` | Flächen aus Ebenen, UV wie die Engine, Picking, Block, Verschieben/Drehen (Texture Lock), Skalieren, Displacements |
| `editing`, `history` | Operationen als Undo-Schritte (`Transaction` merkt sich nur berührte Objekte) |
| `checks` | Prüfungen mit Autofix, Leak aus `.lin` |

### Oberfläche (`src/app`)

- `Editor` hält Spiel, Karte, Undo, Auswahl, Werkzeug. **Jede Änderung** läuft über `Editor::commit(Change)`.
- `Viewport` = eine Ansicht (3D oder Top/Front/Side). Eigene `SceneRenderer`-Instanz pro Ansicht (kein GL-Sharing nötig).
- `SceneRenderer` baut Geometrie nur bei Dokumentänderung neu; Auswahl/Verstecken sind Flags pro Fläche in einer Textur
  (`FLAG_SELECTED`, `FLAG_HIDDEN`) → ein Klick baut nichts neu. Beim Ziehen werden ausgewählte Flächen ausgeblendet und
  mit Matrix als Vorschau gezeichnet; erst beim Loslassen entsteht die Änderung.
- `MaterialCache` lädt VMT/VTF in Threads; der Viewport bekommt `materialReady` und lädt die Textur hoch.
- `AssetBrowser` indiziert im Hintergrund alle Materialien (mit Kategorie), Modelle, Sounds.
- `PropertiesPanel` baut sich bei jeder Auswahl-/Dokumentänderung neu auf (verzögert, damit Editoren ihr Signal beenden).
- Texturieren: Malen (Strg + rechte Maus) schreibt jede Fläche einzeln über `Editor::commitMerged` – gleiche Namen
  werden in `History::push(…, merge)` zu **einem** Undo-Schritt pro Strich zusammengefasst. „Ganze Wand“ =
  `edit::coplanarConnectedFaces` (gleiche Ebene, Flächen-Bounds berühren sich, über alle Brushes).
- Einrasten: `Editor::snapGrid()` (0 wenn Shift+W aus) für alle Positionen, `Editor::snapAngle()` für Drehungen;
  Drehpunkt wird auf das halbe Raster gesetzt, damit Brushes auf dem Raster bleiben.

## Wichtige Formeln (aus dem Quellcode von VBSP/VRAD, Source SDK 2013 / Mapbase)

- **plane**: `normal = normalize((p0 - p1) × (p2 - p1))`, zeigt nach außen; `dist = dot(p0, normal)`.
- **UV**: `u = dot(P, uaxis) / scaleU + shiftU` (Texel), geteilt durch Texturbreite. Welt-Ausrichtung: Boden/Decke
  `[1 0 0]`/`[0 -1 0]`, X-Wände `[0 1 0]`/`[0 0 -1]`, Y-Wände `[1 0 0]`/`[0 0 -1]`.
- **Displacement**: Ecken im Uhrzeigersinn von außen, beginnend an der Ecke nächst `startposition`; Zeile i läuft p0→p1,
  Spalte j von der Kante p0→p3 Richtung p1→p2; `vert = flach + offset + normal * distance`. `elevation` ignoriert VBSP.
- **Licht** (`lightmap.cpp`): Farbe linear `(c/255)^2.2 * brightness/255`; Abfall `(c + 100l + 10000q) / (c + l·d + q·d²)`
  (alle 0 → c = 1); `_fifty_percent_distance` → inverse Quadratik durch 1, ½, 1/256. Spot: innen voll, Rand
  `((cos - cosOuter)/(cosInner - cosOuter))^exponent`, mal cos. Richtung: Yaw aus `angle` oder `angles[1]`,
  Pitch aus `pitch` (≠ 0) sonst `angles[0]`, `dir.z = sin(pitch)` → -90 zeigt nach unten.
- **IDs**: Solids und Entities teilen einen Zähler, Seiten haben einen eigenen.
- **Ausgaben**: `"Output" "ziel,Input,Parameter,Verzögerung,Anzahl"`; Trenner `,` (Mapbase/2013), ESC (0x1B) wird gelesen.

## Tests

- `sme_tests` – Kern: Steam/Spiele/Suchpfade/VPK/VMT/VTF/FGD/Hammer-Config, VMF-Roundtrip, Geometrie, Undo, Editing,
  Material-Kategorien, MDL, Soundscripts, Prüfungen + Autofix, Leak, Probe-Bericht (keine persönlichen Pfade).
- `sme_app_tests` (Label `ui`) – startet das echte Hauptfenster und klickt: Block-Werkzeug, Auswahl, Gizmo (auch
  Shift-Kopie), Undo, Entity-Werkzeug, Drehen mit Rotations-Raster, Flächenmodus, Autofix-Knopf, Malen (ein Undo-Schritt),
  ganze Wand per Doppelklick, Block im 2D-View, Speichern/Laden.
- Testdaten erzeugt `tests/fixture.cpp` (HL2 in zwei Bibliotheken, SDK Base 2013, Mapbase-artige Mod, VPKs, Hammer++)
  und `tests/demo.cpp` (prozedurale Texturen, Modell mit Animationen, Sounds, Demo-Karte mit absichtlichen Fehlern).

## Rezepte

- **Neue Prüfung**: `Problem::Kind` ergänzen, in `checkMap` erkennen (mit `objects` zum Auswählen und `howToFix`), bei
  eindeutiger Lösung `fixLabel` + Fall in `applyFix`; Test in `tests/test_assets.cpp`.
- **Neuer Schlüsseltyp im Smart Edit**: `PropertiesPanel::makeEditor` (Typ aus der FGD, z. B. `studio`, `sound`,
  `target_destination`); Sequenz-Schlüssel in `isSequenceKey`.
- **Neues Werkzeug**: `Editor::Tool`, Knopf + Kürzel in `MainWindow::createActions`, Maus in `Viewport`, die Änderung
  selbst als Funktion in `edit::` (Kern, mit Test) und ein Schritt in `tests/test_app.cpp`.
- **Neues Spiel**: meist nichts zu tun. Bekannte App-IDs (Name, Engine-Zweig, Haupt-FGD) in `game_discovery.cpp`.

## Bauen, CI, Abgabe

- Linux: siehe `.claude/CLAUDE.md`. Windows: Visual Studio 2022 + Qt 6.8 (`-DCMAKE_PREFIX_PATH=<Qt>/msvc2022_64`).
- CI (`.github/workflows/ci.yml`): `checks` (Privatsphäre), `linux` (Build, Tests, UI-Tests unter Xvfb, Screenshots als
  Artefakt), `windows` (MSVC-Build, Tests, ZIP mit `SourceMapEditor.exe`, `sme-probe.exe`, Qt-DLLs, Prüfsumme).
- Abhängigkeiten auf Commit-SHAs (`cmake/Dependencies.cmake`); Actions auf SHAs, Dependabot hält sie aktuell.

## Offene Punkte / Annahmen (noch nicht am echten Spiel geprüft)

- Alles ist bisher nur mit erzeugten Testdaten geprüft – **nicht** mit echten HL2/Mapbase/Black-Mesa-Installationen und
  nicht auf Windows. Nächster Schritt: `sme-probe report` und `sme-probe vmf` auf Julius' PC.
- MDL-Offsets (`studiohdr_t` v44–49) stammen aus `studio.h`; mit echten Modellen gegenprüfen (`sme-probe` zeigt Sequenzen).
- Black Mesa (Xengine) ist ungetestet; VMF-Unterschiede unbekannt.
- Modelle werden noch als Kästen gezeichnet, Sprites/Overlays/Decals noch nicht.

## Fahrplan

1. Rückmeldung von echten Installationen (Probe-Bericht), Fehler beheben.
2. Modelle im Viewport (MDL/VVD/VTX), Entity-Icons, Sonnenschatten in der Licht-Vorschau.
3. Vertex-/Kanten-/Flächen-Werkzeuge (Extrudieren, Abschrägen), immer konvex – sonst Warnung.
4. Mesh-Modus (Variante B): freie Meshes → beim Kompilieren konvex zerlegen bzw. als Static Prop + Nodraw-Hülle.
5. Clip-Werkzeug, Instanzen, Visgroups, Overlays, Kubemaps, VRAD-Schnellvorschau.
