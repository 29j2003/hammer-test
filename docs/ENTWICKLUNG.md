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
| `filesystem` | `GameFileSystem`: Ordner + VPKs in Suchreihenfolge, erster Treffer gewinnt; `pak01_dir.vpk` vor dem Ordner; thread-sicher (Ordner-Index einmal gebaut, `noteWrittenFile` trägt vom Editor geschriebene Dateien nach) |
| `game_discovery` | alle Spiele (App-Ordner mit `gameinfo.txt`) und Mods (`sourcemods`), eindeutige Namen (`label`), FGD-Vorschläge (Hammer++ > Hammer > Mod > Haupt-FGD des Spiels); Mapbase-Mods bekommen die Mapbase-FGD aus `sourcemods/mapbase_shared` dazu, wenn keine geladen ist (`isMapbaseFgd`: Text oder `@include`-Kette erwähnt Mapbase); Spiele auf Mapbase-Basis (bekannte App, z. B. Entropy : Zero 2) nehmen die FGD, die die Haupt-FGD einbindet (`ez2.fgd`); Ordner ohne eigene `client.dll` neben einem spielbaren Ordner derselben App = `baseContent` (unten in der Liste, Warnung beim Kompilieren) |
| `hammer_config` | `GameConfig.txt` / `hammerplusplus_gameconfig.txt` importieren (FGDs, Tools, Ordner) |
| `material`, `texture`, `assets` | VMT inkl. `patch`, VTF → RGBA (vtfpp), Material-Kategorien, MDL-Kopf (Sequenzen, `$includemodel`), Soundscripts (mit Dateien, Kanal, Lautstärke, Pitch, Soundlevel; schreiben/ersetzen, Manifest ergänzen), Soundscapes, „Meintest du …“; `editMaterialText` ändert nur die genannten Parameter im VMT-Text (Proxies, Reihenfolge, `[$WIN32]` bleiben; Patch → `insert`/`replace`) |
| `audio` | Sounds decodieren: WAV (PCM 8/16/24/32, Float, MS-ADPCM, IMA-ADPCM, MP3 in WAV), MP3 (minimp3), OGG (stb_vorbis; beide als Einzeldatei mit Hash gepinnt, gebaut in `codecs*.c`); Loop-Punkt aus `cue`/`smpl`; Wellenform |
| `studio_model` | Dreiecke eines Modells aus `.mdl` (Bodyparts, Meshes, Skins, `$cdmaterials`), `.vvd` (Vertices, Fixups) und `.dx90.vtx` (LOD 0, Strip-Groups 25/33 Byte); Referenz-Pose, alles grenzgeprüft |
| `entity_catalog` | FGDs laden (toolpp), Basisklassen auflösen, Helfer (`studio`, `size`, `color`, `iconsprite`); Editor-Sprite einer Klasse/Entity: `edit::classSprite`, `edit::entitySprite` (`sprite()` → eigenes `model`) |
| `keyvalues`, `map` | VMF lesen/schreiben; Unbekanntes bleibt in `extra` (verlustfrei) |
| `brush` | Flächen aus Ebenen, UV wie die Engine, Picking, Block, Verschieben/Drehen (Texture Lock), Skalieren, Displacements |
| `editing`, `history` | Operationen als Undo-Schritte (`Transaction` merkt sich nur berührte Objekte) |
| `checks` | Prüfungen mit Autofix, Leak aus `.lin` |
| `csg` | konvexe Hülle (Punkte → Brush), Teilen an einer Ebene, Abziehen (Carve), Hollow, Merge (mit „exakt?“), Kante abrunden/abschrägen, Vertex verschieben (bleibt konvex) |
| `shapes` | Formen des Block-Werkzeugs: Block, Keil, Zylinder, Spitze, Kugel, Bogen (Ring/Spirale), Treppe |
| `compile_log` | Zeilen von VBSP/VVIS/VRAD einordnen (Fehler/Warnung + Erklärung), Fortschritt aus `0...1...2` |
| `map_tools` | Cordons (lesen/schreiben, Karte nur mit Inhalt der Cordons + Versiegelung), path_track-Ketten, Fallen lassen; Cubemap-Raum-Boxen (`roomAround` per Strahlen in 6 Richtungen, `setCubemapRoomBox` legt/ändert die `parallax_obb` und setzt `parallaxobb`) |
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
- Sounds-Tab (`SoundBrowser`): drei Spalten – Liste (Soundscripts gruppiert, Dateien, Soundscapes) | Player mit
  Wellenform (`SoundWaveform`) | Soundscript-Eintrag (speichert in `scripts/game_sounds_<mod>.txt` + Manifest). Klick
  decodiert im Hintergrund (`decodeAudio`) und spielt über `SoundPlayer` (QAudioSink im Format des Geräts, eigene
  Umrechnung von Rate/Kanälen; kein FFmpeg-Plugin nötig, Gerät erst beim ersten Abspielen geöffnet).
- Tool-Dock (`ToolPanel`, links): Optionen des aktiven Werkzeugs; beim Entity-Werkzeug `EntityPicker` (Point/Brush,
  Icons aus `iconsprite` über den `MaterialCache` bzw. Editor-Modell über `ModelThumbnails`, „armed“-Klasse,
  Random yaw). Brush-Klasse scharf → Entity-Werkzeug zieht eine Box wie das Block-Werkzeug (`Editor::createBrushEntity`,
  Werkzeug-Textur aus `edit::brushEntityMaterial`).
- Text im Viewport: `OverlayRecorder` sammelt Texte/Rahmen, gemalt wird nur bei Änderung per CPU in ein Bild, das der
  Renderer als Quad zeichnet (`drawOverlay`). Vorher setzt `endFrame()` den GL-Zustand zurück (u. a.
  `GL_UNPACK_ALIGNMENT = 4`) – mit 1 war Qt-Text je nach Treiber verschmiert oder weg.
- Ansichtsmenü oben links in jedem `Viewport` (`fillViewMenu`): Ansichtstyp, Darstellung, Schatten.
- Sprites im 3D-View: Entities mit Sprite liegen am Ende der Entity-Boxen; im 3D werden statt dieser Boxen instanzierte
  Billboards gezeichnet (`drawSprites`, ein Batch pro Material). `parallax_obb`-Flächen haben `FLAG_VOLUME` (nur sichtbar,
  wenn ausgewählt).
- Verschieben: Ecken-Quadrate (`Handle::Corner`) rasten auf Ecken anderer Brushes/Entity-Boxen ein (14 px am Bildschirm),
  sonst Raster. Cubemap-Raum-Box: Quadrate auf den Seiten (`Handle::RoomMin…`) bei genau einer ausgewählten
  `env_cubemap` mit Box.
- Eigenschaften: Schalter „Smart Edit“ (aus = Tabelle aller Keys inkl. nicht gesetzter FGD-Keys), Zeile „neuer Key“,
  Feld „Name“ bei jeder Entity. Kartenobjekte kopieren/einfügen: Kern-Modul `clipboard` (`copyObjects` = VMF-Text,
  `pasteObjects` mit `PasteOptions`: Anzahl, Versatz und Drehung je Kopie, Mittelpunkt, eindeutige Namen samt Ausgängen,
  Präfix; ein Undo-Schritt), `Editor::copySelection/cutSelection/paste`, `MainWindow::pasteHere` (unter der Maus über
  `Viewport::pasteCenter` → `restingPlace`, dieselbe Rechnung wie beim Platzieren) und `pasteSpecialDialog`.
  Sichtbarkeit: Kern-Modul `visibility` (`ObjectKind`, `objectsOfKinds` – ganze Brushes/Entities, bei normalen
  Brushes einzelne Tool-/Himmelsflächen); `Editor::hidden()` = von Hand versteckt (H) + gefilterte Arten, zwischengespeichert
  und bei jeder Änderung neu (`m_hiddenDirty`), Maske in QSettings `view/hiddenKinds`; Menü `Viewport::fillShowMenu`.
  Versteckt wird nur über Flags (`FLAG_HIDDEN`), die Geometrie bleibt.
  Entity-Werkzeug: `Viewport::updateGhost` zeigt die scharfe Klasse unter der Maus (Box + Modell über
  `SceneRenderer::drawGhostModel`). Auswahl-Werkzeug: `resizeHandles()` – Größen-Quadrate wie beim Skalieren, sobald
  Brushes ausgewählt sind; getroffen wird das nächste, Quadrate auf abgewandten Seiten verlieren Gleichstand.
  Namen kopieren/einfügen: `copy_paste` (`addCopyShortcut`, `onStandardKey` – Item-Views
  verschlucken Strg+C/V sonst; `parseKeyValueLines` für Wiki-Text).
- Material-Editor (`MaterialEditor` + `MaterialPreview`): Felder für die häufigen Parameter, Tabelle für den Rest,
  Vorschau (Wand/Würfel/Kugel, Sonne/Lampe/Nacht/unbeleuchtet; ssbump über die drei Basisvektoren). Speichern schreibt
  nach `<mod>/materials/<name>.vmt` (VPK-Material → Kopie in der Mod) und meldet `materialSaved` → `MaterialCache::reload`.
  Abschnitte je Art (`Kind`: Wand, Überblendung, Modell, Wasser, unbeleuchtet), Reihenfolge: Material, Wasser,
  Flüssigkeit, Normal-Map, Relief, Spiegelung, Transparenz, Detail, Rest.
- mapbase-fidelity im Material-Editor (Shader der Mod `29j2003/mymod`, `sp/src/materialsystem/fidelity/`): Schalter nur,
  wenn `fidelityShaders()` die Shader findet (`shaders/fxc/fidelity_parallax_ps30.vcs` oder `bin/game_shader_dx6.dll`
  der Mod), sonst Hinweis „deploy.bat“. Relief = `Fidelity_Parallax` bzw. `Fidelity_WorldVertexTransition`
  (`$parallaxmap` weiß = oben, `$parallaxdepth`, `$parallaxao`, `$parallaxfade`); Wasser = `Water_Fidelity`
  (`$fidelityglint`, `$fidelityfoam`); Flüssigkeit = `Fidelity_Liquid` (`$liquidcolor`, `$fill` −1 = zufällig, …).
  Beim Umschalten fallen die Parameter der anderen Variante weg (`RELIEF_KEYS`, `WATER_KEYS`, `LIQUID_KEYS`).
  Höhenkarte: Kern-Modul `heightmap` (ohne Qt) – aus der Normal-Map per Frankot-Chellappa wie `fidelity/height_map.cs`
  der Mod, sonst aus der Helligkeit; `invertHeights`, `applyHeightZones` (`HeightZone::Flat` = 1, `Inverted` = 1 − h),
  `encodeHeightVtf` (I8 mit Mips). Zonen malt `HeightZonePainter` (links malen, rechts löschen, Kacheln laufen über den
  Rand). Speichern schreibt `<name>_relief.vtf` und setzt `$parallaxmap`. Materialien von `deploy.bat` (erste Zeile
  `// mapbase-fidelity: generated by deploy.ps1 (…)`) werden markiert; Speichern lässt die Marker-Zeile weg, damit
  `deploy.bat` sie nicht mehr überschreibt.
  `MaterialPreview` hat drei Modi: Oberfläche (mit Relief als Parallax Occlusion Mapping, 32 Schritte), Wasser (Wellen
  aus `$normalmap`, Fresnel, Sonnenglanz; die Uhr läuft nur, solange es sichtbar ist) und Flüssigkeit (Füllstand,
  Schaumlinie, Glas). Eine Annäherung – im Spiel nachprüfen.
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
- **Parallax-Cubemaps (Mapbase-VBSP `map.cpp`/`cubemap.cpp`)**: `env_cubemap` Key `parallaxobb` = Name einer
  `parallax_obb` (eine Box); VBSP baut daraus die inverse OBB-Matrix, entfernt die Box und schreibt `$envMapParallax`,
  `$envMapParallaxOBB1-3` und `$envMapOrigin` in die gepatchten Materialien. Im Material ist nichts von Hand zu setzen.

## Tests

- `sme_tests` – Kern: Steam/Spiele/Suchpfade/VPK/VMT/VTF/FGD/Hammer-Config, Mapbase-FGD, VMF-Roundtrip, Geometrie,
  Undo, Editing, CSG/Formen/Werkzeuge (`test_csg.cpp`), Material-Kategorien, MDL/VVD/VTX, Soundscripts/Soundscapes,
  Partikel, Kompilier-Meldungen, Export, Prüfungen + Autofix, Leak, Probe-Bericht (keine persönlichen Pfade).
- `sme_app_tests` (Label `ui`) – startet das echte Hauptfenster und klickt: Spielemenü, Block-Werkzeug (auch Umsehen
  während des Platzierens), Auswahl, Gizmo (auch Shift-Kopie), Skalieren, Undo, Entity-Werkzeug + Entity-Liste,
  Modell-Vorschaubilder, Sounds-Tab, Ziehen von Sounds/Soundscapes/Modellen/Partikeln in die Karte, Drehen mit
  Rotations-Raster, Flächenmodus, Autofix-Knopf, Malen, ganze Wand, Block im 2D-View, Kompilier-Fenster (mit
  Shell-Skripten als Tools, nur Linux), Formen mit Live-Ändern, Hollow/Merge, Rechtsklick-Menü, Clip/Vertex/Polygon
  im 2D-View, Speichern/Laden, Text-Overlay, Blöcke nacheinander, Entity-Liste (Icons, Suche „global light“, Brush-Entity
  per Box, Random yaw), Ecken-Einrasten, Ansichtsmenü, Smart Edit aus + Kopieren/Einfügen, Cubemap-Raum-Box, Sounds
  (Decoder, Soundscript speichern), Material-Editor (Vorschau, Speichern, Kopie, Relief mit Höhenkarte/Umkehren/Zonen,
  Wasser, Flüssigkeit, deploy.bat-Marker, Zonen-Maler). Die Demo-Mod hat dafür eine Platzhalter-Datei
  `shaders/fxc/fidelity_parallax_ps30.vcs`, eine Normal-Map für die Ziegel, Wasser und eine Flasche.
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

- **Updates**: `Updater` fragt beim Start `api.github.com/repos/29j2003/hammer-test/releases/latest` (abschaltbar,
  `--no-update-check`, `SME_NO_UPDATE_CHECK`; Tests und Screenshots fragen nie). Neuer als `VERSION` (`compareVersions`)
  → Frage „Update now / Later / Skip“, Versionsknopf unten rechts wird zum Update-Knopf. Download der
  `*-windows.zip`, SHA-256 aus der `.sha256` (`checksumFor`) muss passen; dann schreibt der Editor ein PowerShell-Skript,
  das auf das Ende des Editors wartet, die ZIP über den Programmordner entpackt und die neue Version startet. Nur unter
  Windows und wenn der Ordner beschreibbar ist, sonst öffnet sich die Release-Seite.
- **Release**: `VERSION` erhöhen und committen, `git tag -a vX.Y.Z -m "<Release-Text>"`, Tag pushen. Der Workflow
  `release.yml` prüft Tag = `VERSION`, baut, testet, packt und erstellt das Release (nur dieser Job darf schreiben).

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
- Sound-Wiedergabe über QAudioSink ist unter Windows noch nicht von Hand geprüft (unter Linux ohne Audiogerät nur die
  Decoder getestet; diese gegen FFmpeg verglichen).
- Modelle: VTX-Layout (25/33 Byte) wird ausprobiert; mit echten HL2-/Mapbase-Modellen prüfen (`sme-probe model GAME PATH`).
  Overlays/Decals werden noch nicht gezeichnet.
- Material-Vorschau ist eine Annäherung an die Shader der Engine (kein HDR, Cubemap = einfacher Himmel).

## Fahrplan

1. Rückmeldung von echten Installationen (Probe-Bericht, `sme-probe model`), Fehler beheben.
2. Displacements mit dem Vertex-Werkzeug bearbeiten (Reddit-Wunsch), Displacement-Malen.
3. Overlays/Decals, Partikel-Vorschau.
4. Mesh-Modus (Variante B): freie Meshes → beim Kompilieren konvex zerlegen bzw. als Static Prop + Nodraw-Hülle.
5. Echte Physik-Simulation (Hammer++ Physics), Instanzen, Visgroups, VRAD-Schnellvorschau, Packen (wie CompilePal).
