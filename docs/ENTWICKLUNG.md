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
| `game_discovery` | alle Spiele (App-Ordner mit `gameinfo.txt`) und Mods (`sourcemods`), eindeutige Namen (`label`), FGD-Vorschläge (Hammer++ > Hammer > Mod > Haupt-FGD des Spiels); Mapbase-Mods bekommen die Mapbase-FGD aus `sourcemods/mapbase_shared` dazu, wenn keine geladen ist (`isMapbaseFgd`: Text oder `@include` erwähnt Mapbase) |
| `hammer_config` | `GameConfig.txt` / `hammerplusplus_gameconfig.txt` importieren (FGDs, Tools, Ordner) |
| `material`, `texture`, `assets` | VMT inkl. `patch`, VTF → RGBA (vtfpp), Material-Kategorien, MDL-Kopf (Sequenzen, `$includemodel`), Soundscripts (mit Dateien), Soundscapes, „Meintest du …“ |
| `studio_model` | Dreiecke eines Modells aus `.mdl` (Bodyparts, Meshes, Skins, `$cdmaterials`), `.vvd` (Vertices, Fixups) und `.dx90.vtx` (LOD 0, Strip-Groups 25/33 Byte); Referenz-Pose, alles grenzgeprüft |
| `entity_catalog` | FGDs laden (toolpp), Basisklassen auflösen, Helfer (`studio`, `size`, `color`, `iconsprite`) |
| `keyvalues`, `map` | VMF lesen/schreiben; Unbekanntes bleibt in `extra` (verlustfrei) |
| `brush` | Flächen aus Ebenen, UV wie die Engine, Picking, Block, Verschieben/Drehen (Texture Lock), Skalieren, Displacements |
| `editing`, `history` | Operationen als Undo-Schritte (`Transaction` merkt sich nur berührte Objekte) |
| `checks` | Prüfungen mit Autofix, Leak aus `.lin` |
| `csg` | konvexe Hülle (Punkte → Brush), Teilen an einer Ebene, Abziehen (Carve), Hollow, Merge (mit „exakt?“), Kante abrunden/abschrägen, Vertex verschieben (bleibt konvex) |
| `shapes` | Formen des Block-Werkzeugs: Block, Keil, Zylinder, Spitze, Kugel, Bogen (Ring/Spirale), Treppe |
| `compile_log` | Zeilen von VBSP/VVIS/VRAD einordnen (Fehler/Warnung + Erklärung), Fortschritt aus `0...1...2` |
| `map_tools` | Cordons (lesen/schreiben, Karte nur mit Inhalt der Cordons + Versiegelung), path_track-Ketten, Fallen lassen |
| `scene_export` | Export als glTF/OBJ (Meter, Y oben, Texturen als PNG, Modelle) |

### Oberfläche (`src/app`)

- `Editor` hält Spiel, Karte, Undo, Auswahl, Werkzeug. **Jede Änderung** läuft über `Editor::commit(Change)`.
  Formen: `Editor::createShape` merkt sich die zuletzt erstellte Form; `setShapeOptions` baut sie neu und fasst das mit
  `commitMerged` in denselben Undo-Schritt, solange Block-Werkzeug aktiv, nichts geändert und die Form ausgewählt ist.
  Cordons stehen im Footer der VMF (`cordons`), ohne Undo (wie in Hammer), aber mit „geändert“-Markierung.
- `Viewport` = eine Ansicht (3D oder Top/Front/Side). Eigene `SceneRenderer`-Instanz pro Ansicht (kein GL-Sharing nötig).
- `SceneRenderer` baut Geometrie nur bei Dokumentänderung neu; Auswahl/Verstecken sind Flags pro Fläche in einer Textur
  (`FLAG_SELECTED`, `FLAG_HIDDEN`) → ein Klick baut nichts neu. Beim Ziehen werden ausgewählte Flächen ausgeblendet und
  mit Matrix als Vorschau gezeichnet; erst beim Loslassen entsteht die Änderung.
- `MaterialCache` lädt VMT/VTF in Threads; der Viewport bekommt `materialReady` und lädt die Textur hoch.
- `AssetBrowser` indiziert im Hintergrund alle Materialien (mit Kategorie) und Modelle. Modelle: `ModelCache` (gehört dem
  `Editor`, lädt in Threads, meldet `modelReady`, trägt die Modellgröße in den `EntityCatalog` ein → Auswahlbox passt),
  `ModelThumbnails` zeichnet Vorschaubilder per CPU (kein GL-Kontext nötig) nur für sichtbare Einträge.
- Sounds-Tab (`SoundBrowser`): Soundscripts (gruppiert, Dateien beim Aufklappen), Dateien, Soundscapes; Ziehen in die Karte
  setzt `ambient_generic`/`env_soundscape`. Wiedergabe mit Qt Multimedia (Backend erst beim ersten Abspielen erzeugt).
- Tool-Dock (`ToolPanel`): Optionen des aktiven Werkzeugs; beim Entity-Werkzeug die Entity-Liste (Suche, Gruppen, zuletzt benutzt).
- Renderer: Modelle als eigene VBOs pro Modell, pro Instanz eine Flag-Zelle (`u_faceOverride`), ohne Culling.
  Live-Vorschau-Brushes (`setPreviewSolids`, z. B. Skalieren mit echten Texturkoordinaten) ohne Flags (`u_faceOverride = -2`).
- Kamera: Umsehen misst immer die echte Cursor-Position (`QCursor::pos`) statt Ereignis-Positionen und setzt den Cursor
  zurück, bevor er den Rand erreicht – keine Sprünge durch alte Ereignisse. Umsehen läuft neben Werkzeug-Aktionen (Block).
- `PropertiesPanel` baut sich bei jeder Auswahl-/Dokumentänderung neu auf (verzögert, damit Editoren ihr Signal beenden).
- Texturieren: Malen (Strg + rechte Maus) schreibt jede Fläche einzeln über `Editor::commitMerged` – gleiche Namen
  werden in `History::push(…, merge)` zu **einem** Undo-Schritt pro Strich zusammengefasst. „Ganze Wand“ =
  `edit::coplanarConnectedFaces` (gleiche Ebene, Flächen-Bounds berühren sich, über alle Brushes).
- Werkzeuge Clip/Vertex/Polygon in `viewport_tools.cpp`; ihre Vorschau läuft über `setPreviewSolids` mit einem leeren
  Hilfsdokument (nur für Ids), die Auswahl wird währenddessen ausgeblendet. Rechtsklick ohne Bewegung →
  `contextMenuRequested` → Menü im `MainWindow` (Aktionen aus `m_contextActions`).
- Schatten (`renderer_shadows.cpp`): Sonnen-Schattenkarte (orthografisch über die ganze Karte) und ein Atlas mit 16
  Kacheln (4 Spot-Lichter je 1, 2 Punkt-Lichter je 6 Würfelseiten). Neu gezeichnet nur bei Geometrie-/Licht-Änderung oder
  wenn sich die nächsten Lichter ändern. Shader: `u_lightShadow[i]` (erste Kachel, Punktlicht ja/nein), PCF 2x2.
  Schatten-Sampler liegen fest auf Einheit 3 und 4 (zwei Sampler-Typen auf einer Einheit wären ein GL-Fehler).
- Kompilieren: `CompileRunner` meldet strukturiert (`stepStarted`, `stepProgress`, `problem` …) an das `CompileWindow`.
  Mit aktiven Cordons wird `<karte>_cordon.vmf` geschrieben und kompiliert.
- Partikel: `particleSystems()` liest `particles_manifest.txt` und die PCFs mit dem DMX-Leser von sourcepp (`kvpp::DMX`).
- Tastenkürzel: Aktionen tragen ihren englischen Namen als `objectName`; `ShortcutDialog` speichert unter `shortcuts/<Name>`.
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

- `sme_tests` – Kern: Steam/Spiele/Suchpfade/VPK/VMT/VTF/FGD/Hammer-Config, Mapbase-FGD, VMF-Roundtrip, Geometrie,
  Undo, Editing, CSG/Formen/Werkzeuge (`test_csg.cpp`), Material-Kategorien, MDL/VVD/VTX, Soundscripts/Soundscapes,
  Partikel, Kompilier-Meldungen, Export, Prüfungen + Autofix, Leak, Probe-Bericht (keine persönlichen Pfade).
- `sme_app_tests` (Label `ui`) – startet das echte Hauptfenster und klickt: Spielemenü, Block-Werkzeug (auch Umsehen
  während des Platzierens), Auswahl, Gizmo (auch Shift-Kopie), Skalieren, Undo, Entity-Werkzeug + Entity-Liste,
  Modell-Vorschaubilder, Sounds-Tab, Ziehen von Sounds/Soundscapes/Modellen/Partikeln in die Karte, Drehen mit
  Rotations-Raster, Flächenmodus, Autofix-Knopf, Malen, ganze Wand, Block im 2D-View, Kompilier-Fenster (mit
  Shell-Skripten als Tools, nur Linux), Formen mit Live-Ändern, Hollow/Merge, Rechtsklick-Menü, Clip/Vertex/Polygon
  im 2D-View, Speichern/Laden.
- Testdaten erzeugt `tests/fixture.cpp` (HL2 in zwei Bibliotheken, SDK Base 2013, Mapbase-artige Mod, VPKs, Hammer++)
  und `tests/demo.cpp` (prozedurale Texturen, Modell mit Animationen, Sounds, Demo-Karte mit absichtlichen Fehlern).

## Rezepte

- **Neue Prüfung**: `Problem::Kind` ergänzen, in `checkMap` erkennen (mit `objects` zum Auswählen und `howToFix`), bei
  eindeutiger Lösung `fixLabel` + Fall in `applyFix`; Test in `tests/test_assets.cpp`.
- **Neuer Schlüsseltyp im Smart Edit**: `PropertiesPanel::makeEditor` (Typ aus der FGD, z. B. `studio`, `sound`,
  `target_destination`); Sequenz-Schlüssel in `isSequenceKey`.
- **Neues Werkzeug**: `Editor::Tool`, Knopf + Kürzel in `MainWindow::createActions`, Hinweis in `ToolPanel::refresh`,
  Maus in `Viewport` (Klick-Werkzeuge in `viewport_tools.cpp`: `toolMousePress`, `toolKey`, `drawToolLines`), die
  Änderung selbst als Funktion in `edit::` (Kern, mit Test) und ein Schritt in `tests/test_app.cpp`.
- **Neue Form**: `ShapeOptions::Kind` + Fall in `makeShape` (Punkte → `solidFromPoints`), Eintrag im Tool-Dock
  (`makeShapePage`), Test in `tests/test_csg.cpp`.
- **Neue Brush-Operation**: Geometrie in `csg` (bleibt konvex, `usable()` prüft), Undo-Schritt in `edit::`, Aufruf in
  `Editor`, Aktion im Menü „Brushes“ und in `m_contextActions` (Rechtsklick).
- **Neues Spiel**: meist nichts zu tun. Bekannte App-IDs (Name, Engine-Zweig, Haupt-FGD) in `game_discovery.cpp`.

## Bauen, CI, Abgabe

- Linux: siehe `.claude/CLAUDE.md`. Windows: Visual Studio 2022 oder neuer + Qt 6.8 (`-DCMAKE_PREFIX_PATH=<Qt>/msvc2022_64`).
- CI (`.github/workflows/ci.yml`): `checks` (Privatsphäre), `linux` (Build, Tests, UI-Tests unter Xvfb, Screenshots als
  Artefakt), `windows` (MSVC-Build, Tests, ZIP mit `SourceMapEditor.exe`, `sme-probe.exe`, Qt-DLLs, Prüfsumme).
- Abhängigkeiten auf Commit-SHAs (`cmake/Dependencies.cmake`); Actions auf SHAs, Dependabot hält sie aktuell.

## Offene Punkte / Annahmen (noch nicht am echten Spiel geprüft)

- Alles ist bisher nur mit erzeugten Testdaten geprüft – **nicht** mit echten HL2/Mapbase/Black-Mesa-Installationen und
  nicht auf Windows. Nächster Schritt: `sme-probe report` und `sme-probe vmf` auf Julius' PC.
- MDL-Offsets (`studiohdr_t` v44–49) stammen aus `studio.h`; mit echten Modellen gegenprüfen (`sme-probe` zeigt Sequenzen).
- Black Mesa (Xengine) ist ungetestet; VMF-Unterschiede unbekannt.
- PCF-Dateien: nur binäre DMX (wie sie die Spiele ausliefern); Text-DMX wird übersprungen.
- Sound-Wiedergabe hängt am Qt-Multimedia-Backend (Windows: FFmpeg; Linux: GStreamer); ADPCM-/MP3-in-WAV ungetestet.
- Modelle: VTX-Layout (25/33 Byte) wird ausprobiert; mit echten HL2-/Mapbase-Modellen prüfen (`sme-probe model GAME PATH`).
  Sprites/Overlays/Decals werden noch nicht gezeichnet.

## Fahrplan

1. Rückmeldung von echten Installationen (Probe-Bericht, `sme-probe model`), Fehler beheben.
2. Displacements mit dem Vertex-Werkzeug bearbeiten (Reddit-Wunsch), Displacement-Malen.
3. Entity-Icons (Sprites), Overlays/Decals, Partikel-Vorschau.
4. Mesh-Modus (Variante B): freie Meshes → beim Kompilieren konvex zerlegen bzw. als Static Prop + Nodraw-Hülle.
5. Echte Physik-Simulation (Hammer++ Physics), Instanzen, Visgroups, Kubemaps, VRAD-Schnellvorschau, Packen (wie CompilePal).
