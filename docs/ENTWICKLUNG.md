# Entwicklung – Mapsmith

Ziel: ein Karten-Editor für Source 1, der sich wie Source 2 Hammer bedient (3D-Ansicht zuerst, Gizmos, Asset-Browser,
Licht-Vorschau), aber normale `.vmf` für HL2, Mapbase, Black Mesa und andere Source-Spiele schreibt. Spiele und Mods
werden automatisch gefunden.

## Architektur im Überblick

```
src/core/   smecore    – Kern ohne Qt (Namespace sme): Formate, Karte, Geometrie, Bearbeiten, Undo, Prüfungen
src/app/    smeapp     – Qt-6-Oberfläche (Namespace sme::app) + Programm Mapsmith (main.cpp)
src/probe/  sme-probe  – Diagnose auf der Kommandozeile (nur Kern)
tests/      doctest    – sme_tests (Kern), sme_app_tests (Label "ui", echtes Fenster), Fixture + Demo
```

**Schichten:** Der Kern kennt kein Qt und ist ohne Fenster testbar. `smeapp` baut darauf auf; `sme-probe` und die
Kern-Tests nutzen nur den Kern, die UI-Tests zusätzlich `smeapp`. Neue Logik gehört in den Kern, die Oberfläche bleibt
dünn. Jede Datei beginnt mit einem Kopfkommentar, öffentliche Klassen und Funktionen haben `///`-Kommentare im Header.

**Weg einer Änderung:**
1. Ein Werkzeug oder Panel ruft eine Kern-Funktion `edit::…` auf (oder baut selbst eine `Transaction`: Objekte mit
   `touch…` anmelden, ändern, `finish()`). Ergebnis ist ein `Change` mit dem Zustand vorher/nachher der berührten Objekte.
2. `Editor::commit(change)` (beim Ziehen und bei Schiebereglern `commitMerged`) legt ihn in die `History` und sendet
   `documentChanged`.
3. Ansichten (`Viewport` → `SceneRenderer::rebuild`) und Panels (Outliner, Eigenschaften, Probleme, History …) hören auf
   die Signale des `Editor` (`documentChanged`, `selectionChanged`, `gameChanged`, `settingsChanged` …) und zeichnen
   sich neu. Eine neue Auswahl setzt nur Flags, die Geometrie bleibt.

Undo/Redo spielen die gespeicherten Zustände zurück und senden dieselben Signale. `doc()` nie ohne `Transaction` ändern.

**Fenster (`MainWindow`):** Die Seitenleiste links schaltet ganze Seiten um: **Map** (Ansichten + Docks), **Materials**
(`MaterialEditor`), **Models** (`ModelsPage`), **Sounds** (`SoundBrowser`), **Particles** (`ParticleBrowser`) und **Project** mit den Tabs Overview
(`ModPage`), Maps (`MapsPanel`), Menu (`MenuEditor`), Source control (`SourceControlPanel`) und Ship (`ShipPanel`).
Docks der Map-Seite: links Tool, Outliner, VisGroups, Map Look (als Tabs); unten Asset Browser, Sounds, Particles,
Compile, Problems und History (als Tabs) sowie Log; rechts Object Properties. Sounds und Particles wandern zwischen Dock
und Seite (`MainWindow::showPage`). Alles wird in `MainWindow::createDocks` und `createActions` verdrahtet.

**Ein Feature einbauen:**
1. Kern: Funktion im passenden Modul (`src/core/include/sme/<modul>.h` + `src/core/src/<modul>.cpp`), Kartenänderungen
   als `Change`; ein neues Modul in `src/core/CMakeLists.txt` eintragen. Test in `tests/test_*.cpp` (neue Testdatei in
   `tests/CMakeLists.txt`).
2. Oberfläche: Widget in `src/app/` (in `src/app/CMakeLists.txt` eintragen); es liest den `Editor` und ändert die Karte
   nur über `Editor::commit`.
3. Hauptfenster: in `MainWindow::createDocks` (Dock, Seite oder Project-Tab) bzw. `createActions` (Menü, Knopf, Kürzel;
   Aktionen tragen ihren englischen Namen als `objectName`) einhängen; ein Schritt in `tests/test_app.cpp`.
4. Doku: Modultabellen unten, ggf. Rezepte; für Nutzer README und Handbuch (`docs/manual/`); Modulliste in
   `.claude/CLAUDE.md`.

**Bauen und testen (Linux):**
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build -LE ui                  # Kern-Tests (sme_tests)
xvfb-run -a ctest --test-dir build -L ui       # UI-Tests (sme_app_tests)
python3 -I tools/check_privacy.py              # Privatsphäre-Prüfung (CI-Job checks)
python3 -I -m unittest discover -s tools/tests # Werkzeug-Tests (Inhalt des Feature-Bundles, CI-Job checks)
```
Windows und CI: siehe „Bauen, CI, Abgabe“.

## Module

### Kern (`src/core`, Namespace `sme`)

| Modul | Aufgabe |
|---|---|
| `text` | Pfad-Normalisierung (Kleinbuchstaben, `/`), case-insensitive Suche auf der Platte; `shortSummary` (Kurzform langer Beschreibungen: erster Satz, sonst an einer Wortgrenze gekürzt mit „…“) |
| `version` | Versionsnummern vergleichen, Prüfsummen-Listen lesen (Update-Prüfung) |
| `math` | nur Header: `Vec3`, `Mat3`, `Plane`, `Bounds`, `Ray` |
| `steam` | Steam finden (Registry, Standardordner, `SME_STEAM_ROOT`), `libraryfolders.vdf`, `appmanifest_*.acf` |
| `gameinfo` | `gameinfo.txt` lesen, `SearchPaths` auflösen (`|gameinfo_path|`, `|all_source_engine_paths|`, `|appid_N|`, `custom/*`, `[$WIN32]`) |
| `filesystem` | `GameFileSystem`: Ordner + VPKs in Suchreihenfolge, erster Treffer gewinnt; `pak01_dir.vpk` vor dem Ordner; thread-sicher (Ordner-Index einmal gebaut, `noteWrittenFile` trägt vom Editor geschriebene Dateien nach) |
| `game_discovery` | alle Spiele (App-Ordner mit `gameinfo.txt`) und Mods (`sourcemods`), eindeutige Namen (`label`), FGD-Vorschläge (Hammer++ > Hammer > Mod > Haupt-FGD des Spiels); Mapbase-Mods bekommen die Mapbase-FGD aus `sourcemods/mapbase_shared` dazu, wenn keine geladen ist (`isMapbaseFgd`: Text oder `@include`-Kette erwähnt Mapbase); Spiele auf Mapbase-Basis (bekannte App, z. B. Entropy : Zero 2) nehmen die FGD, die die Haupt-FGD einbindet (`ez2.fgd`); Ordner ohne eigene `client.dll` neben einem spielbaren Ordner derselben App = `baseContent` (unten in der Liste, Warnung beim Kompilieren) |
| `hammer_config` | `GameConfig.txt` / `hammerplusplus_gameconfig.txt` importieren (FGDs, Tools, Ordner) |
| `game` | `LoadedGame`: das geöffnete Spiel – gameinfo, Dateisystem, Entity-Klassen |
| `material`, `texture`, `assets` | VMT inkl. `patch`, VTF → RGBA (vtfpp), Material-Kategorien, MDL-Kopf (Sequenzen, `$includemodel`), Soundscripts (mit Dateien, Kanal, Lautstärke, Pitch, Soundlevel; schreiben/ersetzen, Manifest ergänzen), Soundscapes (Übersicht; Bearbeiten in `soundscape`), „Meintest du …“; `editMaterialText` ändert nur die genannten Parameter im VMT-Text (Proxies, Reihenfolge, `[$WIN32]` bleiben; Patch → `insert`/`replace`) |
| `audio` | `soundFileLoops` (nur Chunk-Köpfe: `cue `/`smpl` = Schleife, MP3 nie, OGG/FLAC: unbekannt); Sounds decodieren: WAV (PCM 8/16/24/32, Float, MS-ADPCM, IMA-ADPCM, MP3 in WAV), MP3 (minimp3), OGG (stb_vorbis; beide als Einzeldatei mit Hash gepinnt, gebaut in `codecs*.c`), FLAC (eigener Decoder nach xiph.org/flac/format.html: STREAMINFO, Constant/Verbatim/Fixed/LPC, Rice, Stereo-Dekorrelation; für den Import, getestet gegen ffmpeg); Loop-Punkt aus `cue`/`smpl`; Wellenform |
| `soundscape` | Soundscape-Skripte lesen und schreiben (Regeln, DSP-Namen, Manifest), Details unter „Oberfläche – Einzelheiten“ |
| `sound_categories` | Kategorien der Sounds-Seite (Waffen, NPCs, Umgebung, Physik, Spieler, Fahrzeuge, Türen, Musik, Oberfläche, Stimmen, Sonstiges): Dateien nach dem ersten Ordner, sonst nach Wörtern im Pfad; Soundscript-Einträge nach dem Namen (`Weapon_…`, `Wood.Impact…`, `….Step…`), sonst nach ihrer ersten Datei; `soundFolder` |
| `asset_import` | Eigene Dateien in die Mod: `importSound` (WAV aus WAV/MP3/OGG/FLAC: Rate, Kanäle, Normalisieren, Loop-Punkt; optional Soundscript-Eintrag in `scripts/game_sounds_mapsmith.txt` + Zeile im Manifest), `importMaterial` (Bild → VTF 7.4 über vtfpp: Zweierpotenz, Mipmaps, DXT1/DXT5 bzw. BGRA8888 für Normal Maps; VMT LightmappedGeneric/VertexLitGeneric, Alpha → `$alphatest`/`$translucent`), `importParticles` (`.pcf` + `particles_manifest.txt`). Fehlt der Mod ein Manifest, wird erst das des Spiels kopiert (sonst fehlten alle Sounds/Partikel des Spiels). Jeder Import liefert einen `ImportRecord` (neue Dateien, ersetzte Dateien mit `.mapsmith-backup`, eingefügte Zeilen); `undoImport` nimmt genau das zurück, `cfg/mapsmith_imports.txt` hält die Liste über Neustarts (Pfade nur innerhalb der Mod) |
| `model_import` | OBJ (+ MTL: `map_Kd`, `Kd`; Polygone als Fächer, negative Indizes, fehlende Normalen pro Fläche) und Referenz-SMD lesen (Grenzen geprüft; mit mehr als einem Knochen „rigged“: `ImportMesh::bones` + Gewichte je Vertex, `writeSmd` schreibt sie zurück, die Kollision bekommt dasselbe Skelett), `placedMesh` (Y-up → Z-up – nicht bei rigged SMD, die ist schon Source-Ausrichtung –, Einheiten, auf den Boden in die Mitte), Kollision als Hülle (`solidFromPoints` + `buildBrushGeometry`), Box oder Teile (`$concave`), Masse aus dem Hüllvolumen, SMD/QC schreiben; `prepareModelImport` legt die Quellen in `modelsrc/<ordner>/<name>/`, `collectModelOutputs` sammelt nach studiomdl `.mdl/.vvd/.vtx/.phy` |
| `sound_edit` | Sounds bearbeiten (rein, ohne Dateien): `trimmed`, `cut`, `silence`, `fadeIn`/`fadeOut`, `applyGain`, `normalize`, `reverse` auf einem Frame-Bereich (`FrameRange`, leer = alles; der Loop-Punkt wandert mit); `resampled`/`speedChanged` mit gefensterter Sinc-Interpolation (filtert beim Heruntertakten), `toMono`; `encodeWav` schreibt 16-bit-PCM mit `cue `-Punkt (den liest Source) und `smpl`-Loop; `encodeFlac` schreibt FLAC mit VERBATIM-Subframes (16 bit, Blöcke à 4096, CRC-8/CRC-16; für die Beispiel-Dateien) |
| `studio_model` | Dreiecke eines Modells aus `.mdl` (Bodyparts, Meshes, Skins, `$cdmaterials`), `.vvd` (Vertices, Fixups) und `.dx90.vtx` (LOD 0, Strip-Groups 25/33 Byte); Referenz-Pose, alles grenzgeprüft; Box nur aus den gezeichneten Dreiecken. Varianten (`ModelVariant`, Schlüssel `pfad#s=1;b=0;g1=2`): Skin, `body` und Bodygroup-Wahl wie `SetBodygroup` – `edit::entityModelKey` macht daraus den Schlüssel für Model-Cache und Renderer (Skin/Body der Props, `hardware` von `prop_door_rotating` als Gruppe 1) |
| `studio_anim` | Skelett-Animation (`AnimatedModel`): Knochen, Sequenzen und Animationen aus `.mdl` v44–49 samt `$includemodel` (Knochen über den Namen zugeordnet), Anim-Blöcke (`.ani`), Sektionen, RLE-Werte (`mstudioanimvalue_t`), Quaternion48/64, Delta-Animationen (in der Vorschau auf die Pose der Idle-Sequenz `idleSequence()` gelegt, nicht auf die Ruhepose), `layer` für Deltas/Autoplay/Gesten-Grundposen (`…default` neben `…Delta`), Blends mit der mittleren Probe (`blends`), Sequenzen gleichen Namens aus mehreren Include-Modellen nur einmal (erste gewinnt wie im Spiel, `alsoIn` nennt die anderen), `STUDIO_ALLZEROS` ohne Daten; `localPose`/`boneToModel`/`skinning` (Knochen × `poseToBone`), `skinVertices` mit den VVD-Gewichten (`ModelGeometry::weights`); Vorschau-Sequenzen (`model` −1) für importierte Animationen; alles grenzgeprüft (Fuzz-Test) |
| `animation_use` | Welche Keys Sequenzen halten (`DefaultAnim`, die fünf Felder von `scripted_sequence`), welches Modell ein Entity bewegt (eigenes, das NPC des Skripts, FGD-`studio`), Fundstellen in der Karte (`sequenceUses`); `edit::useSequence` (Prop, Skript-Feld oder Pose-Helfer `<name>_pose` beim NPC, optional Modellwechsel); Animations-Seite: `npcClassModels` (Modelle der NPC-Klassen aus der FGD) und `isCharacterModel` (NPCs vs. Models: reine Animations-Dateien `_shared`/`_gestures` …, Props-/Waffen-Ordner, Figuren-Wörter); `isAnimatedModel`/`hasAnimations` (nur Modelle mit Skelett > 1 Knochen und mehr als einer stillen Pose `idle`/`ref`/`ragdoll` …, oder mit `$includemodel`; `ModelInfo::boneCount` aus dem Header) und `classifySequence` (Waffe als Bits aus Aktivität + Name, „rifle“ = SMG1 + AR2, ohne Waffenwort Unarmed; Hocken; Art Idle/Move/Action/Gesture/Scene – Szenen am Kartennamen `d1_`/`ep1_`/`e3_` oder ohne Aktivität) |
| `npc_scripts` | Was die Engine mit `scripted_sequence` macht (Kopfkommentar: ohne Namen startet es selbst; benannt + Start on Spawn parkt den NPC im Pre Action Idle bis `BeginSequence`; Loop Action vs. Loop in Post Idle; m_fMoveTo; Citizen-Köpfe nur mit citizentype 4 fest). `scriptState` (wie es startet, Starter-Entities, Loop, NPC), `sequenceTriggerBox`, `citizenModels`; `edit::poseNpc` (Pose-Helfer mit Start on Spawn 16 + 32 + 64; repariert alte Helfer ohne 16), `edit::startSequenceAtMapStart` (logic_auto OnMapSpawn, vorhandenes zuerst), `edit::addSequenceTrigger` (trigger_once, Clients, OnStartTouch → BeginSequence, Start on Spawn), `edit::setSequenceLoop`, `edit::keepCitizenLook` – je ein Undo-Schritt |
| `animation_import` | „Add animation“: `.smd` lesen (`parseSmd`), Knochen zuordnen (`matchSkeleton`), Vorschau-Sequenz (`addPreviewSequence`, Wurzel um +90° Z wie studiomdl); Quellen für das Hüll-Modell `models/mapsmith/<pfad>.mdl`: `referenceSmd` (Dreiecke + Skelett aus dem kompilierten Modell, −90° Z zurück), `animationModelQc` (`$includemodel` des Originals, Attachments, `$surfaceprop`, `$keyvalues`, neue `$sequence`s), `animations.txt` (`readAnimationList`/`writeAnimationList`); `completedAnimationSmd`: Teil-Animationen (nur einige Knochen) und relative Dateien (Kopfkommentar `// mapsmith: relative`, Werte auf die Ruhepose) werden beim Import zur vollständigen Animation des Modells (fehlende Knochen in Ruhepose, Wurzel −90° Z zurück) – Vorschau und studiomdl bekommen dieselbe Datei |
| `particles` | Partikelsysteme aus PCF (DMX über sourcepp), Vorschau-Simulation `ParticleSimulation`; `writeParticleFile` schreibt PCF selbst (sourcepp kann DMX nur lesen): binäres DMX Version 2 „format pcf 1“ wie die HL2-Dateien (String-Tabelle mit Typen/Attributnamen als u16, Elementnamen und String-Werte inline, Attributtypen der Orange Box: int 2, float 3, bool 4, string 5, color 8, vector3 10, element array 15), Wurzel `DmElement` mit `particleSystemDefinitions`, Teile als `DmeParticleOperator`, Element-IDs aus den Namen (gleiche Bytes bei gleichem Inhalt) |
| `samples` | Beispiel-Dateien für die Importer (`makeSamples`/`writeSamples`): synthetisierte Sounds (Loop-WAV, FLAC), gezeichnete Bilder (Fliesen + Normal Map, Gitter mit Alpha), Kiste als OBJ/MTL/PNG, Hängeschild als rigged SMD mit zwei Knochen + Animationen, relative Animationen für das ValveBiped-Skelett, PCF mit zwei Systemen, `README.txt`. MP3/OGG kann Mapsmith nicht kodieren: `tests/samples_media/click.mp3`/`wind_loop.ogg` sind einmal mit `tools/make_sample_media.sh` (ffmpeg) aus Mapsmiths eigenen WAVs (`sme-make-samples --media-sources`) gemacht und liegen im Repo |
| `entity_catalog` | FGDs laden (toolpp), Basisklassen auflösen, Helfer (`studio`, `size`, `color`, `iconsprite`); Editor-Sprite einer Klasse/Entity: `edit::classSprite`, `edit::entitySprite` (`sprite()` → eigenes `model`) |
| `asset_refs` | `modelUsage` (Modelle der Karte mit Anzahl: „Used in this map“ und `crate01 · 3×` in Asset Browser und Models-Seite, `ModelListModel::UsageRole`); welche Assets (Material, Modell, Sound, Partikelsystem, Soundscape) die Auswahl nennt (`selectionAssets`) und was ein Asset benutzt (`objectsUsing`, „Find in map“); Grundlage der „Show in …“-Menüs (`MainWindow::fillAssetMenu`, `AssetMenuFiller` in `app/asset_menu.h`) |
| `entity_search` | Entities finden: Klasse, Name, `key=value` (Platzhalter `*`/`?`, `wildcardMatch`), Ausgangsziel, Modell, Sound; Abfrage-Syntax mit `class:`/`name:`/`target:`/`model:`/`sound:`, alle Begriffe müssen passen |
| `keyvalues`, `map` | VMF lesen/schreiben; Unbekanntes bleibt in `extra` (verlustfrei); beschädigte VMF: `"` im Wert wird zu `'`, Brushes ohne gültige Ebene fallen weg, `VmfLoadReport` (Log beim Öffnen); `"` wird nie geschrieben (`appendQuoted`, `edit::withoutQuotes`) |
| `brush` | Flächen aus Ebenen, UV wie die Engine, Picking, Block, Verschieben/Drehen (Texture Lock), Skalieren, Displacements |
| `displacement` | Displacements: `dispinfo` lesen/schreiben wie Hammer (normals, distances, offsets, offset_normals, alphas, triangle_tags, allowed_verts), anlegen (`makeDisplacement`, Power 2–4, Elevation in den Distanzen), Formen (`sculpt`: Raise/Smooth/Flatten/Noise/Alpha mit Kosinus-Abfall in der Flächenebene), Nähen (`sewDisplacements`), Undo-Schritte in `edit::` |
| `cubemaps` | env_cubemap-Sonden für die Reflexions-Vorschau: alle Cubemaps mit Raum-Box (`collectCubemaps`, Box aus dem parallax_obb), welche Sonde eine Fläche spiegelt wie VBSP (`probeForFace`: erst `sides`, sonst die nächste), Box-Projektion (`boxProjectedDirection`), Lage der sechs Würfelseiten (`cubeFaceForward`/`cubeFaceUp`/`cubeFaceCoordinates`, der Shader nutzt dieselbe) |
| `decals` | Infodecals und info_overlay auf den Brushes: Flächen der Karte (`collectSurfaceFaces`), Decal-Achsen wie die Engine (Boden: S entlang +X, Wand: S waagerecht, T nach unten; aufrecht, nicht gespiegelt), Decal auf der nächsten Fläche und Flächen derselben Ebene (`decalPatches`, Größe = Textur × `$decalscale`, `decalSize` liest VMT + VTF-Kopf), Overlay aus Basis + uv0..uv3 mit VBSPs Eckreihenfolge der Texturkoordinaten, entlang der Normalen auf jede Fläche in `sides` projiziert und an ihr beschnitten (`overlayPatches`, `clipToPolygon`), Keys für ein neues Overlay auf einer Fläche (`overlayKeysOnFace`) |
| `csg` | konvexe Hülle (Punkte → Brush), Teilen an einer Ebene, Abziehen (Carve), Hollow, Merge (mit „exakt?“), Kante abrunden/abschrägen, Keil/Fase an einer Kante (`cutEdge`), Vertex verschieben (bleibt konvex) |
| `shapes` | Formen des Block-Werkzeugs: Block, Keil, Zylinder, Spitze, Kugel, Bogen (Ring/Spirale), Treppe |
| `editing`, `history` | Operationen als Undo-Schritte (`Transaction` merkt sich nur berührte Objekte) |
| `clipboard` | Kartenobjekte kopieren/einfügen als VMF-Text (`PasteOptions`: Anzahl, Versatz, Drehung, eindeutige Namen) |
| `visibility` | Arten zum Ausblenden (`ObjectKind`, `objectsOfKinds`) |
| `visgroups` | VisGroups: Liste aus dem Block `visgroups` vor der Welt (verschachtelt, `name`/`visgroupid`/`color`), Mitglieder über `visgroupid` im `editor`-Block von Welt-Brushes und Entities (mehrere möglich, bleiben in `extra`); Änderungen als Transaction (Gruppe anlegen – auch aus der Auswahl und unter einer Gruppe –, umbenennen, Farbe, löschen: Untergruppen rücken hoch, Mitglieder verlieren nur diese Id; Auswahl hinzufügen/entfernen). Die Gruppenliste ist ein Kopfblock: `Change` merkt sich dafür `headerBefore/After` (`Transaction::touchHeader`). Sichtbarkeit ist keine Kartenänderung: der Editor hält die versteckten Gruppen, `hiddenByVisGroups` liefert die Objekte (auch über Eltern-Gruppen), `writeVisGroupShown` schreibt beim Speichern `visgroupshown` in eine Kopie, `hiddenVisGroupsInFile` liest es beim Öffnen |
| `instances` | `func_instance`: Einstellungen (`file`, `targetname`, `fixup_style`, `replaceNN` = „$var Wert“), Datei suchen (Ordner der Karte, Kartenordner des Spiels, ohne Endung → `.vmf`, `\` → `/`), `InstanceCache` (pro Pfad + Änderungszeit), `prepareInstance` (Variablen ersetzen – längste zuerst, Vorgaben aus `func_instance_parms` –, Namen mit Prefix/Postfix wie VBSP, `@`/`!` bleiben, Keys mit FGD-Typ `target_source/target_destination/target_name_or_class`, ohne FGD eine feste Liste; Ausgänge; drehen + verschieben mit Texture Lock), `buildInstancePreview`, `collapseInstance` (neue Ids, VisGroup-Ids der Instanz entfernt, ein Undo-Schritt) |
| `map_tools` | Cordons (lesen/schreiben, Karte nur mit Inhalt der Cordons + Versiegelung), path_track-Ketten, Fallen lassen; `makeWater` (Oberseite Wasser, Rest nodraw, `water_lod_control` + `env_cubemap` mit `sides`, ein Undo-Schritt); Cubemap-Raum-Boxen (`roomAround` per Strahlen in 6 Richtungen, `setCubemapRoomBox` legt/ändert die `parallax_obb` und setzt `parallaxobb`); Start-Raum `edit::starterRoom` (sechs Brushes ohne Leak, Licht, info_player_start, env_cubemap; ein Undo-Schritt); Tutorial-Karte `edit::tutorialMap` (Raum, Tunnel, Außenbereich mit `tools/toolsskybox` und Himmel über `applySky`, optional 3D-Skybox mit `sky_camera` Maßstab 16 in eigenem dichten Raum unter der Karte und `env_global_light` nur für Mapbase; je Bereich `env_soundscape` (`pickSoundscape` nach Wörtern im Namen) und `env_cubemap`; `item_suit` mit Short Logon und Waffen auf dem Startpunkt – sofort aufgenommen, sonst holen sich unbewaffnete NPCs sie –, drei `item_ammo_crate`; Bürger mit festem Modell ohne Waffe, Ziel `npc_combine_s` ohne Waffe/Granaten, per zwei `ai_relationship` (Neutral = 4) friedlich; `info_node`s, Plattform mit Stufen + Rampe, Props; nur Inhalte jeder SDK-2013-SP-Installation; Test mit Flutfüllung auf 16er-Raster prüft die Dichtheit) |
| `recent_projects` | Liste „zuletzt bearbeitet“ für die Startseite: `rememberProject` (neueste oben, je Karte einmal, Pfad ohne Groß-/Kleinschreibung und Slash-Richtung), Text `zeit\tspiel\tpfad` je Zeile |
| `soundscape_area` | Wo ein Soundscape-Entity zu hören ist: Kugel (`radius`, −1 = überall; `soundscapeRadius`) oder Block (`trigger_soundscape`, dessen Key `soundscape` den Namen trägt; `soundscapeTrigger`). `setSoundscapeRadius`, `makeSoundscapeBlock` (macht `env_soundscape` zu `env_soundscape_triggerable`, vergibt einen Namen, legt den Trigger an oder ändert seine Größe), `removeSoundscapeBlock` (zurück zur Kugel) – je ein Undo-Schritt |
| `layout_checks` | Prüfungen zur Lage (von `checkMap` angehängt, Fixes über `applyFix`): Spielerstart im Brush (Hülle 32 × 32 × 72 über dem Origin gegen Welt + `func_detail`, Hint/Skip/Trigger/Wasser blockieren nicht; Fix hebt auf die Oberkante), Punkt-Entities außerhalb der Welt-Box (Leak), Lightmap-Scale < 4 / > 128 (Werkzeug-Flächen nicht; Fix 16 / 64), laufende NPCs ohne `info_node` (Fix: 3 × 3 Knoten im Abstand 96 um jedes NPC, nur auf freiem Boden, `nodeSpotsAround`). Box-gegen-Brush: Trennung über die Brush-Ebenen und die Box-Achsen |
| `map_helpers` | Seile: `edit::makeRope` (move_rope + keyframe_ropes, `NextKey`, FGD-Standards, Slack), `ropePointsOf` (Origins bzw. Oberkante von Brushes), `ropeCurves` + `ropeSag` (Parabel: Länge ≈ Spannweite + 8·Durchhang²/(3·Spannweite)); Leitern: `edit::makeLadder` (dünne Achse = Tiefe, Vorderseite = Seite mit Platz für den Spieler; `func_useableladder` mit `point0`/`point1` 18 Einheiten davor, `info_ladder_dismount` oben hinter der Kante und unten einen Schritt zurück) |
| `checks` | Prüfungen mit Autofix, Leak aus `.lin`; Prop-Klasse passend zum Modell (`betterPropClass`), `ambient_generic` „Is NOT Looped“ gegen die WAV, mehrere `sky_camera`; `CheckCache` (im Problems-Panel) liest jedes Modell und jeden Sound nur einmal je Spiel; Spiegelungen: glänzende Materialien (`$envmap env_cubemap`, je Material einmal gelesen) ohne `env_cubemap` → `NoCubemap` (Autofix setzt eins über den Spielerstart), kompilierte Karte (`CheckContext::compiledMap`, nur wenn nicht älter als die `.vmf`; `readBspCubemaps` aus `compile_log`, erneut nur bei geänderter Datei) mit Samples aber ohne gebaute Cubemaps → `CubemapsNotBuilt` mit `Problem::action` „buildCubemaps“ (keine Kartenänderung: `ProblemsPanel::actionRequested` → `MainWindow::buildCubemapsInCompile` öffnet den Compile-Dialog mit Cubemaps an) |
| `doors` | Türen: `DoorSettings` ↔ Keys (`readDoor`, `edit::setDoor`), Rahmen aus der Brush-Box (`doorFrame`: dünne Seite = vorn, rechts = rechte Hand, wenn man auf die Vorderseite schaut), Scharnier-Origin (`hingePoint`), `movedir` ↔ Richtung, Schiebeweg wie `CBaseDoor::Spawn` (Größe − 2 − Lip), Vorschau-Bewegung (`doorMotion`: Scharnier, Drehungen gegen den Uhrzeigersinn, bei „away from player“ beide; Prop-Türen: vorwärts = −distance wie `CPropDoorRotating`), `edit::makeDoor` (Brush-Türen behalten die Brushes, Doppeltür per `splitSolid` in der Mitte, gleiche `targetname`; Prop-Tür ersetzt den Block, Gierwinkel aus der Modell-Box, ohne Box HL2-Konvention Blatt entlang +Y), Tür-Sounds/-Modelle, `uniqueTargetName`, Areaportal finden/verknüpfen |
| `io_links` | Verbinden in einem Schritt: `rankedOutputs`/`rankedInputs` (kleine Vorzugstabellen je Klasse/Familie `trigger_` + FGD-Reihenfolge), `suggestConnections`, `edit::connect` (benennt das Ziel, falls nötig, im selben Undo-Schritt); `edit::setTarget` (Ziel-Feld wie *Target NPC* auf ein Entity ohne Namen: bekommt im selben Schritt einen) |
| `map_budget` | Zählt die Karte gegen die Grenzen der Engine (`MAX_MAP_*` aus `bspfile.h`, Edicts 2048; `budgetLimitsForApp`: 16384 Entities ab L4D2-Zweig); Ebenen als Paare wie VBSP, Edicts ohne weg-kompilierte Entities; Dock *Map Budget* (`BudgetPanel` in `panels`, zählt 250 ms nach einer Änderung und nur sichtbar) |
| `compile_options` | Was ein Kompilieren ausführt (`CompileSettings`: Schritte, Qualität/Presets, LDR/HDR, Cubemaps, Spielfenster, Zusatz-Parameter je Werkzeug, `serialize`/`parse`), bekannte Parameter je Werkzeug (`knownParameters`), daraus die Befehlszeilen (`compileCommands`); Startoptionen des Spiels, die die Einstellungen regeln (Konsole, Fenster, Auflösung, Intro), werden aus den Spiel-Startoptionen herausgenommen; `cheats` (Standard an) setzt beim Spielstart `+sv_cheats 1` vor `+map` (impulse 101, noclip), nicht beim Cubemap-Bau |
| `compile_log` | Zeilen von VBSP/VVIS/VRAD einordnen (Fehler/Warnung + Erklärung), Fortschritt aus `0...1...2`; Cubemaps: `readBspCubemaps` (Lump 42 Samples, Lump 40 Pakfile-Zentralverzeichnis: `materials/maps/<map>/c<x>_<y>_<z>[.hdr].vtf`, nicht `cubemapdefault`) und `judgeCubemapBuild` – das Spiel endet nach `-buildcubemaps` immer mit Exit-Code 1, deshalb zählt die Karte in `maps/` (vorher/nachher, Zeitstempel), nicht der Exit-Code |
| `heightmap` | Höhenkarten fürs Relief: aus der Normal-Map (Frankot-Chellappa) oder der Helligkeit, Zonen, VTF schreiben, `fidelityReliefSource` (Regeln von `deploy.ps1` plus `reliefFitByName`) |
| `relief_pattern` | Mustererkennung fürs Relief: `detectReliefLayout` (Läuferverband, Raster, Reihen ohne Stoßfugen; auch hochkant), `heightFromLayout` (Steine oben flach mit kurzer, weich auslaufender Fase (ein Drittel des Wegs zur Steinmitte, höchstens 1,2 % der Textur) – die runden Kanten über den halben Stein (29.10.) machten jeden Ziegel zum Kissen, das überall Licht fing und tiefer aussah, als es war; Fugen nur wenig tiefer: Ziegel 0,3, Fliesen 0,55 – tiefe Fugen mit steilen Wänden warfen im Spiel einen schwarzen Streifen an jeder Fliese und verschmierten Ziegel bei flachem Blick; Fenster/Flächen ohne Muster flach; Regeln `pattern3`), `heightFromTexture` (Muster, sonst Helligkeit), `setUnitZone` (ganzer Stein als Zone); `reliefFitByName`/`reliefFitByLook`/`reliefFitInGame`: wo Relief keinen Sinn hat (Fenster, Glas, Schilder, Gitter, Türen, Blech, Putz, Stoff, Lampen; große glatte Scheibe, Scheiben zwischen Sprossen (Blöcke 8 × 8 in bis zu 256 px: zur Hälfte dunkel und gleichmäßig, als großes Rechteck mit Wand drumherum), strukturlose Fläche, durchsichtig); `reliefFitByMask`: `$envmapmask` hell über einem großen Rechteck und dunkel drumherum = spiegelnde Scheibe |
| `fidelity_look` | Look-Werte von mapbase-fidelity pro Karte (Liste, `cfg/fidelity.cfg` lesen, in der Karte lesen/schreiben) |
| `fidelity_effects` | Game enhancements: Effekte von mapbase-fidelity (Liquid, Wind, Water; Relief „aus“-Kopien, wo Mapsmith keins will oder es ausgeschaltet ist, sonst macht es `deploy.bat` – außer auf Wänden der eigenen Karten: Muster-Relief `makePatternRelief`; nichts mit Mod-Shadern ohne `bin/game_shader_dx6.dll`, `EnhancementOptions`) nach den Regeln und mit dem Text von `deploy.ps1` (`deployWouldApply`, `makeEffectMaterial`, Marker `DEPLOY_MARKER`), Einstellungen der Mod in `cfg/mapsmith_enhancements.txt` (`EnhancementSettings`: Effekt aus, Modelle/Materialien ausgenommen), Kandidaten aus den Original-Materialien des Spiels (`GameFileSystem::readOriginal` überspringt den Mod-Ordner), `planEnhancements` (an = Text wie deploy, aus = Kopie des Originals mit `MAPSMITH_MARKER`, damit `deploy.bat` sie als eigene Datei stehen lässt; Dateien der Mod ohne Marker nie anfassen → `keptOwn`); Ausschluss wirkt pro Material, also für alle Modelle mit diesem Material |
| `sky_presets` | Licht passend zum Himmel (`applySky`, auch als Teil einer größeren Transaktion, Tabelle der Himmel) |
| `material_replace` | Material ersetzen (ganze Karte, Auswahl oder gewählte Flächen; exakt oder Platzhalter; Ausrichtung behalten oder an der Welt ausrichten; Overlays/Decals), `countMaterialMatches` für die Vorschau |
| `autosave` | Namen und Buchführung der Autosaves (`<karte>.<JJJJMMTT-HHMMSS>.vmf` in `autosave/` neben der Karte), Liste neueste zuerst, welche gelöscht werden (`autosavesToRemove`), Name der `.vmx`-Sicherung |
| `git_status` | Git-Ausgaben lesen (`status`, `log` ohne E-Mail), Remote → Web-Adresse |
| `mod_code` | Quellcode der Mod (Mapbase-Checkout mit mapbase-fidelity): `modCodeScripts` (erkannt an `fidelity/update.ps1` + `sp/src` oder `update.bat`), `findModCode` (Suchwurzeln selbst, dann Unterordner – der Name der Mod zuerst, max. 300 je Wurzel), `modCodeSearchRoots` (GitHub Desktop, Visual Studio, Laufwerks-Ordner), `modCodeCommand` (PowerShell mit `-NoProfile -NonInteractive -File … -GamePath <mod>`, Windows mit `-ExecutionPolicy Bypass`, sonst `pwsh`; nie `pause`, nie `Read-Host`), `classifyScriptLine` (Statuswörter von `update.ps1`/`deploy.ps1`/`report.ps1` → Schritt + Art), `ScriptRun` (Schritt nur vorwärts, Fehlerzeilen ohne Dopplung, Zips des Berichts, `advice()` über `updateFailureAdvice`) |
| `mod_install` | Funktionen der Mod aus Mapsmith installieren (ohne Compiler): Feature-Bundle (`readFeatureBundle`, `manifest.txt` = `mapsmith-features 1`, `version`, `<sha256>  <pfad>`; Pfade aus dem Ordner heraus machen das Manifest ungültig; `verifyFeatureBundle`), `modBranch` (Episodic, wenn `gameinfo.txt` außerhalb von Kommentaren `ep2`/`episodic` nennt), `featureInstallPlan` (`bin/<zweig>/client|server|game_shader_dx9.dll` → `bin/`, `bin/game_shader_dx6.dll`, `fidelity/game/**` → Mod), `installFeatures` (nur geänderte Dateien, über Temp-Datei + `rename`; fremde Dateien einmal als `<datei>.mapsmith-backup`; `exec fidelity\t// mapbase-fidelity` wie deploy.ps1; VRAD ins SDK-`bin` mit `fidelity_backup/` + `fidelity_backup.txt` wie update.ps1; `cfg/mapsmith_installed.txt` mit Version, Zweig, Dateien + SHA-256, Backups), `removeFeatures` (Backups zurück, eigene unveränderte Dateien weg, eigene Exec-Zeile weg, VRAD zurück), eigenes SHA-256 (`sha256Hex`, `fileSha256`), `bundledScripts` (deploy.ps1/report.ps1 aus dem Bundle); `missingFeatureFiles` (Dateien des Plans, die in der Mod fehlen → Features-Karte „Incomplete“, Auto-Update repariert), bin-Ordner einer Mapbase-Mod: `mapbaseModDlls` (client, server, game_shader_dx9), `checkModBin` (fehlende DLLs, dazu `game_shader_dx6.dll` bei installierten Funktionen; `game_shader_dx9.dll` älter als die des Bundles nach PE-Linkzeit `peLinkTime`), `copyMapbaseBin` (fehlende DLLs aus `sourcemods/mapbase_<zweig>/bin`, nie überschreiben) |
| `mod_features` | Funktionen von mapbase-fidelity als eine Liste (`modFeatures`: Id, Gruppe Graphics/Effects/Sound/Materials, Titel, eine Zeile, die Ein/Aus-ConVars (`FCVAR_ARCHIVE`) und/oder der Material-Effekt der Game enhancements); `writeFeaturesCfg`/`parseFeaturesCfg` (`cfg/mapsmith_features.cfg`: jeder Schalter 1/0), `saveFeatureChoice`/`hookFeatureChoice` (Zeile `exec mapsmith_features // mapsmith` in `autoexec.cfg`, nur mit Add-on), `withFeatureEffects`/`featuresOffBySettings` (Material-Funktionen ↔ `EnhancementSettings`), `generatedModFiles` (was deploy.ps1 und Mapsmith in der Mod gemacht haben: Marker in der ersten Zeile, Manifeste, Shader/DLL/cfg des Add-ons), `removeEditorFiles` |
| `mod_base_files` | Was eine HL2-Mod vom Basisspiel braucht, die Engine aber nur aus der Mod liest: `cfg/skill.cfg` (+ `skill_episodic.cfg` bei Episodic) und `skill_manifest.cfg` (der Spielcode führt bei jedem Kartenstart `exec skill_manifest.cfg` aus; ohne sie sind alle `sk_max_*` 0 – impulse 101, Munitionskisten und Pickups geben keine Munition, `sk_plr_dmg_*` und NPC-Gesundheit ebenso 0) und die HUD-Texte in `resource/<mod>_<sprache>.txt` (Token wie `#HL2_Pulse_Rifle` werden nur dort gesucht; fehlende Sprachen fallen auf Englisch zurück). `checkModBaseFiles` (fehlt `HL2_Pulse_Rifle` in der englischen Datei → Texte fehlen), `addModBaseFiles` mit zwei Lesern: Valves Mod-Vorlage aus dem Bundle (`template/<spiel>/…`, Episodic hat kein skill.cfg → das aus hl2) vor den Spieldateien (`cfg/skill.cfg`, `resource/hl2_*.txt`, `episodic_*`, `ep2_*`); `skill_manifest.cfg` wird erzeugt (exec je vorhandener Datei); Token nur ergänzt (eigene Texte und `GameTitle` bleiben, Schlüssel = Name + Bedingung wie `[$X360]`), andere Sprachen nur, wo eine Quelle sie hat; nie überschreiben |
| `mod_template` | Neue Mod: `gameInfoText` (SDK 2013 Singleplayer/Multiplayer, Mapbase mit `mapbaseMounts` aus `sourcemods/mapbase_shared`), `modFolders`, `modResourceFile` (UTF-16 LE mit BOM, `GameTitle`), `modAutoexec` (setzt `sv_cheats` nicht mehr: Cheats kommen über „Run the game“), `isValidModFolder`/`suggestModFolder`, `createMod` (verweigert vorhandene Ordner, kopiert Karten nach `mapsrc`); Anführungszeichen im Titel werden zu `'` (die Engine kennt keine Escapes) |
| `mod_menu` | Hauptmenü der Mod: gameinfo-Anzeige-Keys (zeilenweise, Rest bleibt), Kapitel-Configs, Lokalisierung UTF-16, ChapterBackgrounds, GameMenu.res, Laden/Speichern mit `.bak` |
| `localization` | Texte der Mod: Lokalisierungsdateien verlustfrei (Kommentare, `[$X360]`-Bedingungen, Reihenfolge), Dateisätze `<name>_<sprache>.txt`, Caption-Tags, Captions → `.dat` (VCCD) |
| `shipping` | Karte/Mod für Spieler: `mapDependencies` (Flächen-, Overlay-, Decal- und Sprite-Materialien samt Texturen und Materialien, die ihre Parameter nennen – jeder Parameter, dessen Wert als `.vtf`/`.vmt` existiert –, Patch-Includes, 2D-Himmel mit `_hdr`, Modelle mit `.vvd`/`.vtx`/`.phy`/`.ani`, Include-Modellen und Materialien über `$cdmaterials`, Sounds aus Entity-Keys über Soundscripts, Soundscapes mit ihren Sounds, PCF der benutzten Effekte, Detail-Sprites); Herkunft je Datei: `Custom` (Mod-Ordner), `Shared` (anderer Ordner außerhalb `steamapps/common`), `BaseGame` (unter `steamapps/common`), `Missing`; `fileOnDisk` (case-insensitive), Liste für `bspzip`, ZIP-Schreiber (gespeichert, CRC32 lokal, keine Namen mit `..`), `planModPackage` (ohne Entwicklungsdateien), Workshop-VDF für steamcmd |
| `scene_export` | Export als glTF/OBJ (Meter, Y oben, Texturen als PNG, Modelle) |

### Oberfläche (`src/app`, Namespace `sme::app`)

| Datei(en) | Aufgabe |
|---|---|
| `main` | Start, Kommandozeilen-Optionen (Automatik, Screenshots), alte Einstellungen übernehmen; Startseite vor `show()` (kein Aufblitzen der Karte), das Spiel (`selectGame`: Mounts, FGDs, Browser) erst nach dem ersten Bild (`FirstFrameWatch`) – sofort nur, wenn eine Karte, Wiederherstellung oder Optionen wie `--screenshot`/`--page` es brauchen; hat man vorher über die Startseite schon ein Projekt geöffnet, bleibt dessen Spiel |
| `editor`, `editor_helpers` | `Editor`: Spiel, Karte, Undo, Auswahl, Werkzeug, Einstellungen; alle Änderungen über `commit()`; Make rope / Make ladder aus der Auswahl in `editor_helpers` |
| `main_window` | Hauptfenster: Seitenleiste, Ansichten, Docks, Menüs, Werkzeugleisten, Statusleiste |
| `camera` | Kamera einer Ansicht (Matrizen, Strahlen) |
| `viewport`, `viewport_tools`, `viewport_displace`, `viewport_doors`, `viewport_ropes` | eine Ansicht; hängende Seile (`drawRopes`, nur nach Änderungen neu berechnet, mit den Helfern ausblendbar) in `viewport_ropes`;: Kamera, Picking, Gizmos, Werkzeuge; Clip/Vertex/Polygon in `viewport_tools`, Displacement-Werkzeug (Pinsel, Striche) in `viewport_displace`, Tür-Vorschau (Scharnier, Bogen, Geist, „Test open“) in `viewport_doors`; Verbinden-Modus und anklickbare Verbindungslinien (`connectionAt`) in `viewport` |
| `renderer`, `renderer_shadows`, `renderer_cubemaps` | OpenGL 3.3: Brushes, Displacements, Entities, Modelle, Himmel, Licht-Vorschau; Picking (`pick`): Modelle über ihre Dreiecke (nur Box-Treffer zählt an der Rückseite der Box), Punkt-Entity-Boxen gewinnen bis 16 Einheiten hinter einem Modell (nicht hinter Brushes), der durchsichtige NPC-Platzhalter eines `scripted_sequence` verliert Gleichstände und das Skript bekommt sein Sprite an den Füßen; Größen-Griffe (`Viewport::resizeHandles`) nur bei Brushes; Schattenkarten; Cubemap-Reflexionen; Ansicht *Lightmap density* (`u_mode` 2: Lightmap-Scale je Fläche als R8UI-Textur auf Einheit 16, beim Neuaufbau gefüllt, Schachbrett je Luxel, Legende `densityLegend` im Viewport) |
| `material_cache` | Materialien/Texturen im Hintergrund laden (auch Relief-Höhen) |
| `model_cache`, `model_thumbnails` | Modelle im Hintergrund laden; Vorschaubilder per CPU-Rasterizer |
| `asset_browser` | Dock Asset Browser: Materialien und Modelle, Indizierung im Hintergrund; Kacheln über `AssetTileDelegate` (Bild nie eingefärbt, Auswahl als oranger Rahmen, Name einzeilig in der Mitte gekürzt), Mindesthöhe = eine Kachelreihe mit Namen (`minimumSizeHint`), „Recent ▾“ als Popup |
| `models_page`, `model_viewer` | Seite Models: Ordnerbaum + Suche + Vorschaubilder, 3D-Ansicht (OpenGL, Skins, Bodygroups, Drahtgitter), Infos, „Place in map“ |
| `animations_page` | Seite Animations: Modelle (Karte zuerst, Suche; nur echte Animationsmodelle – `checkModels`/`checkNext` lesen die Header in Paketen zu 48 im Hintergrund, nur solange die Seite sichtbar ist, Karte zuerst, Ergebnis in `m_animatedModels` je Spiel), Filter Waffe/Haltung/Art mit Anzahl (`updateFilters`, `classifySequence`; `selectSequence` setzt versteckende Filter zurück), Sequenzliste (Aktivität, Länge, Herkunft), Wiedergabe im `ModelViewer` (`setPose`, Timer nur sichtbar), „Used in this map“, „Use on selected entity“, „Add animation …“ (Vorschau sofort, „Build into the mod“ schreibt `modelsrc/mapsmith/<pfad>/` und startet `studiomdl -game <mod> -nop4 -verbose model.qc`; Programm neben `vbsp`, im Test per `setStudiomdlProgram` ein Shell-Skript). ▶ neben Sequenz-Feldern in Object Properties (`animationPreviewRequested`) öffnet die Seite |
| `material_editor`, `material_preview` | Seite Materials: VMT bearbeiten, Live-Vorschau (OpenGL), Schalter für die mapbase-fidelity-Shader |
| `import_dialog` | „Import …“ auf den Seiten Sounds, Materials (Material-Editor), Models, Particles und unter *File › Import into the mod*: ein Dialog je Art (Vorschau, Ordner/Name, Optionen, „Goes to“, Replace nur angehakt, Ergebniszeile, Undo); Modelle kompiliert studiomdl im Hintergrund (Pfad: Einstellung `games/<spiel>/studiomdl`, sonst neben VBSP). `MainWindow::openImport`/`showImports` (Liste mit „Remove import“), danach liest `reloadGameSoon` die Mod neu |
| `sound_browser`, `sound_player`, `soundscape_editor` | Seite/Dock Sounds: Kategorien, Baum, Wiedergabe (QAudioSink, optional), Sound bearbeiten und als WAV speichern, Soundscript- und Soundscape-Editor |
| `particle_browser` | Seite/Dock Particles: Liste, Vorschau (`ParticlePreview`), Teile als Karten |
| `tool_panel` | Dock Tool: Optionen des Werkzeugs, Formen, Entity-Liste (`EntityPicker`) |
| `dialog_fit` | Fenster passen auf jeden Bildschirm (`DialogFitter` app-weit in `MainWindow` installiert, wirkt beim ersten Show): eigene Dialoge kommen in eine Scroll-Fläche (`dialogFitScroll`), Button-Zeilen am Ende (QDialogButtonBox bzw. Zeilen nur aus Knöpfen) bleiben darunter sichtbar; Größe höchstens `availableWindowArea()` (freie Bildschirmfläche minus Rahmen/Titelleiste), zentriert; Hauptfenster ebenso begrenzt; Qt-Standarddialoge und Popups bleiben unberührt; Tests: `setAvailableWindowAreaForTests` |
| `wheel_guard` | Mausrad app-weit (in `MainWindow` installiert): Tab-Leisten schalten nie per Rad, Felder in Scroll-Bereichen nur mit Fokus (WheelFocus → StrongFocus beim Polish) oder – mit `setWheelWhenResting` (Himmel) – wenn die Maus 350 ms ruht und der Bereich 600 ms nicht gescrollt wurde |
| `dock_layout` | Docks (`DockLayout`, von `MainWindow::createDocks` angelegt): Rechtsklick auf Tab (`MainWindow::contextMenuEvent` → `dockAtTab`, Titel → Dock) oder Kopfzeile (`dockHeader` mit ⋯-Knopf, nur wenn kein Tab das Dock benennt: schwebend oder allein) → Menü `dockMenu` (Float as window, Reattach, Back to original position, Set this as original position, Reset all panels). Schwebend = echtes Fenster (`Qt::Window` mit Min/Max/Close statt Qt-Tool-Fenster; nach dem Herausziehen erst, wenn keine Maustaste mehr gedrückt ist), Schließen des Fensters = Reattach. „Original“ je Dock: Bereich + Tab-Gruppe (Objektnamen) oder schwebend + Geometrie; Werk-Zustand aus `captureFactory()` nach dem Aufbau, eigener in `window/dockHome/<Name>`. Layout: `saveState(1)` in `window/state` (`closeEvent`; auf anderen Seiten der Stand vor dem Wegräumen, `m_mapDockState`), `restore()` am Ende des Konstruktors (dann keine Werk-Größen per `singleShot`). Schwebende Docks bleiben auf anderen Seiten sichtbar (außer den Browsern, die in ihre Seite wandern) |
| `startup_trace` | Meilensteine des Starts (`startupMark`): mit `SME_STARTUP_TRACE=1` nach stderr (ms seit Start), mit `=quit` beendet sich das Programm kurz nach dem ersten Bild (zum Messen). Erstes Bild = erstes Paint des Hauptfensters + leere Ereignisschlange (`FirstFrameWatch` in `main.cpp`) |
| `lazy_refresh` | `RefreshWhenShown`: Seiten, die beim Start niemand sieht (Project › Menu, Maps), lesen die Mod erst beim Anzeigen (`request()` sofort, wenn sichtbar, sonst beim nächsten Show); Texts liest per `showEvent` (`m_reloadOnShow`) |
| `ui_helpers` | Nur Header: `qs` (Text des Kerns → QString), `toQImage` (ImageRgba → QImage, prüft die Pixelzahl), `noteLabel`/`capsLabel` (Beschriftungen in den Rollen des Style Sheets) |
| `text_copy` | Kopieren: `makeTreeCopyable` (Strg+C, Kontextmenü Copy/Copy all) für Bäume wie die Kompilier-Meldungen, `addCopyAll` für Log und Kompilier-Ausgabe |
| `start_page` | Startseite (in `m_pages`, nicht in `Page`): Continue mit Bild (beim Speichern aus dem 3D-View, `AppDataLocation/pictures`), Recent, Start new (`MainWindow::startNewProject` → `NewModDialog` + `createModAndSwitch` + `makeStarterMap`), „Show at startup“ (`start/show`); `--page start` für Screenshots |
| `properties_panel`, `properties_instance`, `properties_door`, `properties_script` | Dock Object Properties: Smart Edit, Outputs/Inputs, NPC-Pose (`edit::poseNpc`), Justify; Hilfe-Fläche unten nur mit dem ersten Satz (`sme::shortSummary`, `setHelp`), Link More…/Less (`#more`/`#less`) schaltet, gemerkt in `properties/helpExpanded`; func_instance-Kasten; Door-Kasten (Öffnen, Richtung, Tempo, Schließen, Sounds mit ▶, Test open, Areaportal); Kasten „scriptBox“ für `scripted_sequence` (Startzustand in Worten aus `scriptState`, „Start on map start“, „Start with a trigger …“ wählt den neuen Trigger aus, Loop, Move to position, Not interrupted, Repeatable) und „citizenLook“ für `npc_citizen` (Kopf wählen, „Keep this head“) in `properties_script` |
| `connect_popup` | Popup nach Quelle + Ziel: vorgeschlagene Output→Input-Paare, Listen, Delay; Enter verbindet (`Editor::beginConnect`/`finishConnect` → Signal `connectRequested`) |
| `panels` | Docks Outliner, Problems, Log |
| `visgroups_panel` | Dock VisGroups |
| `map_look_panel` | Dock Map Look (mapbase-fidelity-Look der Karte) |
| `history_panel` | Dock History (alle Undo-Schritte) |
| `compile`, `compile_window` | Kompilieren (`CompileRunner`), Kompilier-Dialog, Spiel-Einstellungen; Dock Compile |
| `mod_page` | Project › Overview: Spiel/Mod, Ordner, Karten, Suchpfade, FGDs, Game enhancements; Zeile „Add missing files“, wenn einer HL2-Mod Skill-Dateien oder HUD-Texte fehlen (`checkOpenModBaseFiles`/`addOpenModBaseFiles`) |
| `enhancements` | `GameEnhancements` (gehört dem `Editor`): bei Spielwechsel und jeder Schalter-Änderung `planEnhancements` im Hintergrund, schreibt/entfernt die Dateien (`QSaveFile`, `noteWrittenFile`), meldet `materialsChanged` (Viewport/Browser laden neu); Kandidaten einmal pro Spiel. `EnhancementSwitches` (Schalter eines Modells: Object Properties, Models-Seite), `EnhancementsSection` (Project › Overview) |
| `maps_panel` | Project › Maps: alle Karten, Stapel-Kompilieren, Dateiaktionen |
| `menu_editor` | Project › Menu: Hauptmenü der Mod mit Vorschau |
| `texts_panel` | Project › Texts: Lokalisierung und Captions der Mod (Tabelle Token × Sprache, Caption-Vorschau, Speichern, Captions kompilieren) |
| `source_control_panel` | Project › Source control: Git (Änderungen, Commit, Fetch/Pull/Push, Verlauf); meldet `pulled(root, changed)` und „Update mod“ (`updateModRequested`) an Mod code |
| `mod_code_panel` | Project › Build from source (letzter Tab, nur wer die Mod selbst baut): `update.ps1`/`deploy.ps1`/`report.ps1` per `QProcess` ohne Konsole (Schritte, Fortschritt, Log, Zusammenfassung mit Rat, Cancel beendet den ganzen Prozessbaum), Checkout finden/wählen je Mod, Optionen je Mod (nach Pull aktualisieren, beim Start `git fetch` bzw. Pull & Update), Zeile auf dem Overview (`banner()`); nach Erfolg `modUpdated` → `MainWindow` öffnet das Spiel neu (Caches, Game enhancements); `reportHowTo()` = Tooltip aller „Make report“-Knöpfe (Map-Menü, Compile, Problems → `MainWindow::makeReport`) |
| `features_panel` | Project › Features: Kasten *Install* (`featureBundleDir()` = `$SME_FEATURES_DIR` oder `features/` neben dem Programm; `installNow`/`removeNow` über `mod_install`, danach deploy.ps1 des Bundles über `ModCodePanel` → `installed()` → Spiel neu lesen; Auto-Update einmal je Sitzung und Mod, QSettings `features/autoUpdate`, Standard an; Angebot „Install features“ auf dem Overview für Mapbase-Mods ohne Add-on), `FeatureChecklist` (auch im New-mod-Dialog, kompakt), `FeaturesPanel` (Haken sofort gespeichert: Schalter → `cfg/mapsmith_features.cfg`, Material-Funktionen → Game enhancements), Angebot einmal auf dem Overview (`offer()`), Fresh install inline bestätigt (Liste aus `generatedModFiles`) → `ModCodeAction::RemoveGameFiles` → Funktionen schreiben → Update mod bzw. Deploy |
| `ship_panel` | Project › Ship: Abhängigkeiten, bspzip, ZIPs, Workshop |
| `new_mod_dialog` | „New mod …“: neue Mod aus Karten; bei New project zusätzlich „First map“ (Tutorial-Karte mit optionaler 3D-Skybox oder leerer Raum, `showFirstMapChoice`); `addOpenModBaseFiles` (Leser: `features/template` und das Dateisystem des offenen Spiels, dann lose Dateien einer HL2-Installation, App 220) |
| `find_dialog`, `replace_materials_dialog` | Entities finden + Entity-Bericht; Materialien ersetzen |
| `shortcut_dialog` | Tastenkürzel-Editor |
| `autosaver` | Autosave, Wiederherstellen nach Absturz |
| `updater` | Update-Prüfung, Download, Installation |
| `copy_paste` | Strg+C/V-Helfer für Namen und Key/Values |
| `icons`, `style`, `toggle_switch` | gezeichnete Icons, dunkles Theme, Ein/Aus-Schalter |

### Probe und Tests

| Datei(en) | Aufgabe |
|---|---|
| `src/probe/main`, `report` | `sme-probe`: Bericht über Steam/Spiele/Mods (Pfade maskiert) und Einzelprüfungen (`vmf`, `material`, `model` …) |
| `tests/fixture`, `demo`, `make_fixture` | künstliche Steam-Installation (keine Valve-Dateien), Demo-Inhalte + Demo-Karte; `sme-make-fixture` schreibt sie auf die Platte |
| `tests/make_samples` | `sme-make-samples DIR --media tests/samples_media`: der Ordner `samples/` (Ziel `samples` baut ihn bei jedem Build nach `<build>/samples`; das Windows-Paket kopiert ihn neben `Mapsmith.exe`, die Linux-CI lädt ihn als Artefakt hoch). Die App findet ihn über `samples_folder` (`$SME_SAMPLES_DIR`, `samples/` neben dem Programm oder bis zu drei Ordner darüber); „Use a sample …“ in `import_dialog` und auf der Animations-Seite öffnet ihn |
| `tests/test_*.cpp`, `main` | Kern-Tests (`sme_tests`); `test_app.cpp` = UI-Tests (`sme_app_tests`) |
| `tests/export_web` | `sme-export-web`: Demo-Karte als JSON für die Browser-Vorschau (Entwicklungswerkzeug) |

### Oberfläche – Einzelheiten

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
- Sounds-Seite bzw. -Dock (`SoundBrowser`): Kategorien-Liste (Zahlen je Kategorie, „All sounds“, „Soundscapes“) | Baum
  (je Kategorie ein Zweig, darin Ordner; Soundscript-Einträge stehen im Ordner ihrer ersten Datei vor den Dateien, damit
  `Weapon_Pistol.Single` bei `weapons/pistol/*.wav` liegt) | rechts Player + Bearbeiten und darunter (Seite, hoch) bzw.
  daneben (Dock, niedrig; `resizeEvent` dreht den Splitter) der Soundscript-Eintrag oder der Soundscape-Editor. Suchen
  geht über alle Kategorien. Beim ersten Füllen ist die erste Kategorie mit Sounds gewählt und ihr erster Sound gezeigt
  (ohne Abspielen). `reveal(name, kind)` wählt Kategorie und Eintrag (z. B. „Edit soundscape“ der Eigenschaften).
  Klick decodiert im Hintergrund (`decodeAudio`) und spielt über `SoundPlayer` (QAudioSink im Format des Geräts, eigene
  Umrechnung von Rate/Kanälen; kein FFmpeg-Plugin nötig, Gerät erst beim ersten Abspielen geöffnet). Speichert Einträge
  in `scripts/game_sounds_<mod>.txt` + Manifest.
- Sound bearbeiten: `SoundWaveform` wählt per Ziehen einen Bereich (Shift erweitert; Klick ohne Ziehen spielt ab dort).
  Jede Funktion (`sound_edit`) ist ein Schritt in einem eigenen Undo-Stapel des Browsers (`applyEdit`, `undoEdit`,
  `redoEdit`, „Revert“); er gilt für den gezeigten Sound und wird beim Laden eines anderen geleert. „▶ Selection“
  spielt eine Kopie des Bereichs (Abspielkopf mit Versatz). `saveEdited` schreibt `encodeWav` nach
  `sound/<pfad>.wav` im Mod-Ordner (nie ins Spiel; eine vorhandene Datei der Mod nur nach Rückfrage; Pfade mit `..`,
  `:` oder absolut werden abgelehnt); Vorschlag ist der Pfad des gezeigten Sounds (die Mod spielt ihn dann statt des
  Spiel-Sounds). Mit „+ sound script entry“ landet die Datei im Eintrag rechts und `saveEntry` speichert ihn.
- Soundscape-Editor (`SoundscapeEditor`, `soundscape_editor.cpp`): rechte Spalte des Sound-Browsers (Stapel mit dem
  Soundscript-Eintrag). Kern-Modul `soundscape`: `parseSoundscapes` liest alle Regeln (`playlooping`, `playrandom` mit
  `rndwave`, `playsoundscape`, unbekannte Blöcke als `Other`), Unbekanntes bleibt in `extra`; `upsertSoundscape`/
  `removeSoundscape` schreiben die Datei, `addToSoundscapeManifest` setzt die Mod-Datei an den Anfang des Manifests
  (gleichnamige Soundscapes der Mod sollen gewinnen – im Spiel nicht geprüft), `dspPresets` = Namen der DSP-Nummern
  0–28. Karten binden per Referenz an `m_scape.rules[i]`; Hinzufügen/Entfernen baut alle Karten neu (Entfernen/
  Verschieben verzögert per `QueuedConnection`). Speichern/Löschen macht der `SoundBrowser` (`readModFile`/
  `writeModFile`, danach neu einlesen und wieder auswählen).
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
  `env_cubemap` mit Box. Soundscape-Kugel: `Viewport::selectedSoundscapeSphere` (genau ein Soundscape-Entity, Radius > 0,
  kein Trigger), drei Kreise + Quadrate (`Handle::RadiusMin…`); beim Ziehen nur Vorschau (`m_dragRadius`, Text im
  Overlay), beim Loslassen ein Commit. Eigenschaften: Kasten „soundscapeArea“ (`PropertiesPanel::buildSoundscapeArea`,
  Signal `editSoundscapeRequested` für die Sounds-Seite); der Schieberegler ändert die Karte erst beim Loslassen, weil
  das Panel nach jeder Änderung neu aufgebaut wird.
- Eigenschaften: Schalter „Smart Edit“ (aus = Tabelle aller Keys inkl. nicht gesetzter FGD-Keys), Zeile „neuer Key“,
  Feld „Name“ bei jeder Entity. Smart Edit: Klassen-Combo oben, `QTabBar` „propertyTabs“ (Keyvalues/Outputs/
  Inputs mit Zahlen, gewählter Tab in `properties/tab`), Gruppen nach `EntityProperty::origin` (die FGD-Klasse, die den
  Key definiert – beim Laden gesetzt) über `addGroup` (aufklappbar, Zustand in `properties/collapsed`, beim Suchen
  offen), Zeilen „Label | Editor“ über `addRow`; die Beschreibung steht als Property „help“ am Editor und erscheint im
  Hilfebereich unten (`showHelp` über `QApplication::focusChanged`), ohne Fokus die Klassenbeschreibung. Ja/Nein-Listen
  (0 No / 1 Yes) und Flags als `ToggleSwitch` (QCheckBox, eigene Zeichnung). Himmel: `skyboxNames` (Kern, `assets`)
  + Bild der Vorderseite im Hintergrund (`loadSkies`, einmal pro Spiel), Combo „skyPicker“; Karte > Map properties
  wählt nichts aus und zeigt die Welt. Himmel wählen → `Editor::setSky` → Kern `applySky` (`sky_presets`): Tabelle aus
  Valves „List of skies“ (erste Beispielkarte je Himmel, 132 Einträge, auch `_hdr`-Varianten), ein Undo-Schritt; setzt
  `light_environment` (fehlt er, neu über `info_player_start`), `env_sun` (gleiche angles/pitch) sowie `shadow_control`
  und `env_global_light` (angles = −pitch, yaw) – nur vorhandene; ohne bekannte Richtung nur Farben. Kartenobjekte kopieren/einfügen: Kern-Modul `clipboard` (`copyObjects` = VMF-Text,
  `pasteObjects` mit `PasteOptions`: Anzahl, Versatz und Drehung je Kopie, Mittelpunkt, eindeutige Namen samt Ausgängen,
  Präfix; ein Undo-Schritt), `Editor::copySelection/cutSelection/paste`, `MainWindow::pasteHere` (unter der Maus über
  `Viewport::pasteCenter` → `restingPlace`, dieselbe Rechnung wie beim Platzieren) und `pasteSpecialDialog`.
  Sichtbarkeit: Kern-Modul `visibility` (`ObjectKind`, `objectsOfKinds` – ganze Brushes/Entities, bei normalen
  Brushes einzelne Tool-/Himmelsflächen); `Editor::hidden()` = von Hand versteckt (H) + gefilterte Arten, zwischengespeichert
  und bei jeder Änderung neu (`m_hiddenDirty`), Maske in QSettings `view/hiddenKinds`; Menü `Viewport::fillShowMenu`.
  Versteckt wird nur über Flags (`FLAG_HIDDEN`), die Geometrie bleibt.
  Statusleiste: `MainWindow::updateStatusInfo` (Auswahl, Maße, Raster/Snap/Winkel), `Viewport::showHoverInfo`
  (3D: Punkt unter der Maus + Abstand, im Flächenmodus das Material), `statsChanged` = Zeit pro Bild (die Views
  zeichnen nur bei Änderungen, Bilder pro Sekunde sagen wenig), `statsDetail` als Tooltip.
  Himmel: `SceneRenderer::bindSky` bindet die sechs Seiten `skybox/<skyname>{rt,lf,bk,ft,up,dn}` auf Einheiten 5–10,
  Himmelsflächen (`%compilesky` oder Name `tools/toolsskybox*`) zeigen sie per Blickrichtung (Abbildung wie Quake II,
  das GoldSrc/Source übernommen haben – im Spiel nicht verglichen). Wolken: GLSL-Port von `fidelity_sky_ps30.fxc`
  (mymod), stehend. Farbkorrektur: Port von `fidelity_grade_ps2x.fxc` ohne Schärfe/Clarity, nur im Lighting-Preview.
  Werte: Kern-Modul `fidelity_look` (Liste der Werte, die Karten setzen dürfen = `FCVAR_SERVER_CAN_EXECUTE` in mymod,
  `parseCfgValues` für `cfg/fidelity.cfg`, `mapLookValues`/`setMapLookValues` über `logic_auto` „fidelity_look“ →
  `point_clientcommand` „fidelity_look_command“, OnMapSpawn mit 0,1 s Verzögerung), `Editor::lookValue` (Karte → cfg →
  Standard), `Editor::fidelityLook` (automatisch an, wenn die Mod mapbase-fidelity hat), Panel `MapLookPanel`.
  Relief ohne deploy: `fidelityReliefSource` (Kern, `heightmap`) = `Get-ParallaxSource` aus `deploy.ps1` (Ordner,
  Shader, Ausschlüsse, Holz, Tiefe 0,01 / Normal-Map und Ziegel/Fliesen 0,006); `MaterialCache::setRelief` macht die Höhenkarten im Hintergrund
  (`reliefHeights`, geteilt pro Quelltextur); der Welt-Shader läuft per Parallax Occlusion Mapping hinein (24 Schritte,
  Tangentenraum aus `dFdx`/`dFdy`, Normale aus der Höhe, Abdunkeln 0,25, Ausblenden 600–1000 Einheiten) – nur im
  Lighting-Preview und mit dem Look-Schalter. Ziegel ohne Normal-Map nehmen im Viewport `heightFromTexture` (Muster aus
  `relief_pattern`, sonst Helligkeit) – im Spiel macht sie weiter `height_map.cs` (`courses7` mit vermessenen Fassaden),
  solange das Material nicht im Material-Editor gespeichert wird. Wo Mapsmith kein Relief will (`reliefFitInGame`: Name,
  Flags, Aussehen der Textur in 256 px, Spiegelmaske, pro Texturdatei gemerkt), zeigt der Viewport keins.
  **Gleiche Rechnung wie das Spiel:** Material-Vorschau (`material_preview`) und 3D-View (`renderer`) laufen wie
  `FidelityParallaxShift`/`FidelityParallaxHeightNormal` aus `fidelity_parallax_common.h`: 8–32 Schritte je nach Winkel,
  Verschiebung bei flachem Blick höchstens 2,5 × Tiefe und höchstens 4 Welteinheiten (`r_fidelity_parallax_maxshift`;
  Vorschau: Texturgröße × 0,25 Einheiten), Bezugsebene Wände 0,5 / Böden 0,2, Ausblenden unter etwa 17°
  (`smoothstep(0.08, 0.3, N·V)`), Normale aus der Höhe 1,5 Texel auseinander mit Steigung höchstens 1. Vorher: 5 × Tiefe
  ohne Grenze und ungebremste Normalen – ab 0,02 wurden Ziegel zu langen Platten verschmiert, schon 0,006 sah tiefer aus
  als im Spiel (Julius, 29.10.2026). `parallaxReach` (Kern, `heightmap`) rechnet dieselbe Reichweite für die Tests.
  Wer was entscheidet: (1) `deploy.ps1` (`Get-ParallaxSource`) macht Relief für alle passenden Wände der Mod; Mapsmith
  bildet das in `deployWouldApply(Relief)` nach (`fidelity_effects`). (2) Game enhancements (Effekt `Relief` in
  `automaticEffects`) schreibt nur „aus“-Kopien mit `MAPSMITH_MARKER` („off (relief) … - no depth for a window“) für
  Materialien, die Mapsmith nicht will oder die ausgeschaltet sind; `deploy.ps1` lässt Dateien ohne seinen Marker stehen
  (Kopf von `deploy.ps1`: „The mod's own materials … are never overwritten“). Wieder an → Kopie wird gelöscht, das nächste
  *Update mod* macht das Relief. (3) Im Material-Editor gespeicherte Materialien sind eigene Materialien der Mod: ihre
  Höhenkarte `<name>_relief.vtf` (Muster/Normal-Map/Helligkeit + Zonen) nutzt das Spiel so. (4) Wände der eigenen Karten
  (offene Karte + alle `.vmf` im Map-Ordner, `vmfMaterialNames` liest nur die `"material"`-Zeilen) bekommen von Game
  enhancements das **Muster-Relief** (`makePatternRelief`: `heightFromTexture` mit Layout ab Sicherheit 0,5, sonst bleibt
  deploys): VMT in der Form von `New-ParallaxMaterial` (Shader getauscht, `$parallaxmap`/`$parallaxdepth` 0,006 = Ziegeltiefe, `$parallaxao` 0,25,
  ohne eigene Spiegelung `$envmap env_cubemap` + `$envmapwetonly 1`, Rest des bereinigten Texts), erste Zeile
  `PATTERN_RELIEF_MARKER … hash <fnv64>` (Regeln + Original-VMT + Textur-Bytes: neu nur bei Änderung), Höhenkarte
  `<material>_mapsmith_relief.vtf` (deploy kennt nur seine `_fidh`-Dateien aus dem Manifest). Nur schlichte
  `LightmappedGeneric` ohne Textur-Transformationen (die Höhe liegt in den Koordinaten der Basistextur, der Shader liest sie
  in denen der Bump-Map) – Blends behalten deploys zweilagiges Relief. Aus/ausgenommen → Material und Höhenkarte gehen.
  **Shader-DLL** (`modHasShaderDll`, `bin/game_shader_dx6.dll`): ohne sie kennt die Engine `Fidelity_*`/`Water_Fidelity`
  nicht und nimmt `Wireframe_DX8` (`materialsystem/cmaterial.cpp`, `MissingShaderName`; der Shader setzt
  `MATERIAL_VAR_WIREFRAME`) – ein Boden wird durchsichtig (Julius' rosa-lila Boden war der Sonnenuntergangs-Himmel
  darunter), Flaschen-Meshes werden abgelehnt („does not support vertex format“). Deshalb `EnhancementOptions::shaderDll`:
  ohne DLL schreibt `planEnhancements` nichts mit diesen Shadern (`heldBack`), entfernt alles Erzeugte mit ihnen (auch
  Muster-Relief alter Karten) wie `deploy.ps1`; `ownMaterialsNeedingShaderDll` listet eigene Materialien, die dann kaputt
  sind; Overview zeigt „Install features“ (`GameEnhancements::installFeaturesRequested` → `MainWindow` → Features-Tab,
  `installNow`). Der Material-Editor speichert solche Shader ohne DLL nicht (`fidelityShaders()` = nur noch DLL; die
  `.vcs` allein reichen nicht). `encodeHeightVtf` schreibt Byte für Byte wie `WriteI8` in `height_map.cs` (7.2, 80 Byte
  Kopf, keine Flags, Reflectivity 0,5, I8, Box-Mips, kleinste zuerst).
  Hammer-Funktionen: `edit::mirror` (Spiegeln, Windung der Ebenenpunkte umdrehen, Texturachsen gespiegelt, Winkel
  über M·R·diag(1,−1,1)), `edit::similarObjects`, `edit::objectWithId`; `Editor::snapSelectionToGrid`,
  `transformSelection` (Teile mit `commitMerged` zu einem Undo-Schritt „Transform“). Werkzeug-Panel: `ToolPanel::showKeys`
  (Zeilen mit `action:<Name>` zeigen das aktuelle Kürzel der Aktion).
  Entity-Werkzeug: `Viewport::updateGhost` zeigt die scharfe Klasse unter der Maus (Box + Modell über
  `SceneRenderer::drawGhostModel`). Ein aus dem Browser gezogenes Modell: `updateDropGhost` (Drag-Events, Modell
  halbdurchsichtig, Name + Größe im Overlay, Drehung bei Random yaw einmal je Ziehen); `dropEvent` setzt das
  `prop_static` genau auf `Ghost::origin`. `restingPlace` legt die Mitte der Box unter den Cursor (Modelle, deren
  Ursprung weit von der Geometrie liegt, z. B. Teile der Citadel). Das Bild am Mauszeiger beim Ziehen:
  `assetDragCard` (Karte mit Vorschau, Name, Pfad; `AssetIconView::startDrag`). Auswahl-Werkzeug: `resizeHandles()` – Größen-Quadrate wie beim Skalieren, sobald
  Brushes ausgewählt sind; getroffen wird das nächste, Quadrate auf abgewandten Seiten verlieren Gleichstand.
  Namen kopieren/einfügen: `copy_paste` (`addCopyShortcut`, `onStandardKey` – Item-Views
  verschlucken Strg+C/V sonst; `parseKeyValueLines` für Wiki-Text).
- Oberfläche: Icons zeichnet `icons.cpp` selbst (QPainter, keine Bilddateien; `icon(name, kürzel)`), Stil in `style.cpp`
  (neutrale Grautöne, ein Orange). Werkzeugleisten: „Pages“ (Seiten), „Tools“ (nur Icons, Taste in der Ecke, eigene
  Spalte über `addToolBarBreak`), „Main“ (Spiel, Undo/Redo, Objects/Faces als Segment, Raster, Snap, Drehschritt,
  Texture lock, 1/4 Ansichten, Ansicht-Schalter `view:*`, Check, „Run map“ orange, Panel-Schalter links/unten/rechts).
  Docks links (Tool, Outliner, VisGroups, Map Look) und unten (Asset Browser, Sounds, Particles, Compile, Problems,
  History, Log) haben keine Titelleiste (Tabs benennen sie), rechts nur „Object Properties“ mit schmalem Kopf. Brush-Aktionen stehen im Werkzeug-Panel (Kasten „selectionTools“), nicht mehr rechts.
  Ansicht-Optionen im Editor: `showGrid3D`, `showEntityNames` (2D, ab 64 Einheiten ≥ 18 px, max. 400), `showConnections`
  (`Viewport::drawConnections`, Ziele auch mit `*` am Ende).
- Seitenleiste: `MainWindow` hat links die Werkzeugleiste „Pages“ (vor den Werkzeugen) und in der Mitte
  einen `QStackedWidget` (`workspacePages`) mit den Seiten aus `MainWindow::Page`: Map (die Views), Materials
  (`MaterialEditor`), Models (`ModelsPage`), Sounds, Particles, Project (`QTabWidget` „projectTabs“: Overview = `ModPage`, Features, Maps, Menu,
  Texts, Source control, Ship, Build from source). Sound- und Partikel-Browser sind beim Mappen
  Docks; `showPage()` hängt sie in ihre Seite um und beim Zurückgehen wieder ins Dock. Solange nicht „Map“ vorn ist,
  blendet `setMapWorkspaceShown(false)` Docks und Werkzeugleisten (außer der Seitenleiste) aus und schaltet die Tasten
  der Karte ab (außer Datei/Hilfe). `raiseDock("Map"/"Materials"/"Material Editor"/"Project")` zeigt die Seite ("Mod" geht
  weiterhin), jeder andere Dock-Name die Karte mit dem Dock. Screenshots: `--page project|sounds|…`, `--pick-edge` (Kante gewählt).
- Seite Models (`ModelsPage`): nutzt die Modell-Liste des Asset Browsers (`modelNames()`, nach `materialsIndexed`) und
  dessen `ModelThumbnails`; Ordnerbaum mit Anzahl je Ordner (inkl. Unterordnern), Filter `ModelFolderFilter`
  (Ordner-Präfix + Suchwörter). Gewähltes Modell wird in einem eigenen `QThreadPool` (ein Thread, Generationszähler)
  geladen: `readModelInfo` (Skins, Bodygroups mit Optionen, `$staticprop`, Masse, `$keyvalues`), `modelSequences`,
  `.phy` vorhanden?, `loadModelGeometry(fs, path, skin, …, body)`. `body` = `ModelInfo::bodyValue(Auswahl je Gruppe)`
  wie die Engine (Summe Auswahl × Produkt der Optionen davor); `buildModelGeometry` nimmt pro Body Part das Modell
  `(body / base) % nummodels`. `ModelViewer` (QOpenGLWidget) zeichnet nur bei Änderung: Textur je Material aus dem
  `MaterialCache` der Ansichten, Halb-Lambert-Licht, Bodengitter, Drahtgitter mit Polygon-Offset. „Place in map“ →
  `Editor::setCurrentEntity(Klasse, {model, skin, body})` (prop_static ohne body), Seite Map, Werkzeug Entity; der
  Geist und `placeEntity` übernehmen diese Keyvalues, `setCurrentEntityClass` leert sie. Ziehen auf den Map-Knopf der
  Seitenleiste öffnet die Map (`PageDropSwitch`), damit das Ziehen in einer Ansicht enden kann.
- Karten-Übersicht: `MapsPanel` (`maps_panel.cpp`, eigenständiges Widget mit `(Editor*, CompileRunner*)`, die
  Project-Seite des Hauptfensters zeigt es). Liste aller `.vmf` im Kartenordner (`GameSettings::mapDir`, rekursiv,
  ohne `*_cordon.vmf`); `bspState` vergleicht die BSP in `<mod>/maps` (sonst neben der VMF) mit der VMF. Letztes
  Ergebnis in QSettings `maps/<spiel>/<pfad>` = `ok|ISO-Zeit` bzw. `failed|…`. Stapel: `compileMaps` nimmt die
  gespeicherten Kompilier-Einstellungen (`loadCompileOptions`) ohne Spiel und Cubemaps, startet die nächste Karte per
  `QTimer::singleShot(0)` nach `CompileRunner::finished` (der Runner meldet Fehler wie fehlendes VBSP sofort, noch in
  `start`); am Ende optional nur der Spiel-Schritt mit der letzten guten Karte. Signale `openMapRequested`,
  `compileStarted`, `batchFinished`. Dateiaktionen ohne Rückfrage (`createMap` …) für Tests, die Knöpfe fragen.
- Neue Mod: `NewModDialog`/`runNewModDialog` (`new_mod_dialog.cpp`), `createModAndSwitch` legt über `createMod` an,
  ruft `Editor::discover` mit dem bekannten Steam-Ordner neu auf und öffnet `sourcemods/<ordner>`
  (`gameChanged` → Hauptfenster aktualisiert die Spieleliste); danach `addOpenModBaseFiles` (Skill-Dateien, HUD-Texte).
  New project: `MainWindow::makeStarterMap(name, tutorial, skybox3d)` wählt Materialien, die das Spiel hat
  (`files.exists`, sonst DEV-Texturen), Soundscapes aus `soundscapes(game->files)` und `env_global_light` bei Mapbase.
- Hauptmenü der Mod: Kern `mod_menu` (ohne Qt) liest/schreibt `gameinfo.txt` (nur die Anzeige-Keys aus
  `gameInfoDisplayKeys()`, zeilenweise im GameInfo-Block ersetzt/eingefügt/entfernt – Kommentare, Abstände, CRLF und
  alles andere bleiben), `cfg/chapterN.cfg` (`chapterMap`/`setChapterMap`), die Lokalisierung
  `resource/<mod>_<sprache>.txt` (UTF-16 LE mit BOM: `decodeTextFile`/`encodeUtf16Le`, eigener Leser mit `\"`-Escapes,
  Token `<MODORDNER>_Chapter<N>_Title`), `scripts/ChapterBackgrounds.txt` und `resource/GameMenu.res` (`GameMenuItem`,
  unbekannte Keys bleiben in `extra`). `loadModMenu` nimmt fehlende Dateien vom Grundspiel (über das Dateisystem),
  `saveModMenu` nummeriert Kapitel neu, entfernt überzählige `chapterN.cfg`, legt vor dem ersten Überschreiben je Datei
  einmal `<datei>.bak` an und lässt unveränderte Dateien unberührt. UI: `MenuEditor` (`menu_editor.cpp`, eigenständiges
  Widget für die Project-Seite) mit `MenuPreview` (QPainter-Attrappe im Stil von Source 2013, Hintergrund aus
  `materials/console/<bg>_widescreen.vtf`).
- Texte der Mod: Kern `localization` (ohne Qt). `parseLocalizationFile`/`writeLocalizationFile` halten eine Datei
  so, wie sie war: `//`-Kommentare hängen am Token darunter (`LocalizedToken::comments`), Bedingungen wie `[$X360]`
  bleiben am Token, andere Einträge des `lang`-Blocks bleiben als Text (`extra`), die Reihenfolge bleibt; Escapes
  `\"`, `\\`, `\n` werden aufgelöst und beim Schreiben wieder gesetzt. `loadLocalizationFile` liest jede Kodierung
  (`decodeTextFile`), `saveLocalizationFile` schreibt UTF-16 LE mit BOM (einmal `.bak`). `findLocalizationSets` gruppiert
  `resource/<name>_<sprache>.txt` nach Name (Sprachen aus `sourceLanguages()`), Captions (`closecaption`, `subtitles`)
  zuletzt. `compileCaptions` ersetzt Valves captioncompiler: Kopf `VCCD`, Version 1, Blockanzahl, Blockgröße 8192,
  Verzeichnisgröße, Datenoffset (auf 512 aufgerundet); Verzeichnis nach CRC32 des kleingeschriebenen Tokens sortiert
  (das Spiel sucht binär) mit Block, Byte-Offset und Länge in Bytes inkl. Null; Daten als volle 8-KiB-Blöcke mit
  UTF-16-Strings. Zu lange Texte und doppelte Tokens stehen in `skipped`. `saveModMenu` schreibt die Kapiteltitel
  auf die Datei, wie sie gerade auf der Platte liegt (nur diese Tokens ändern sich), damit Menu- und Texts-Tab sich
  nichts überschreiben; nach dem Speichern liest der jeweils andere Tab nach (`MenuEditor::refreshTitles`,
  `TextsPanel::reloadIfUnchanged`). UI: `TextsPanel` (`texts_panel.cpp`): Tabelle Token × Sprache (fehlende
  Übersetzungen markiert, „Used by“ aus `menuUseOfToken`), Feld darunter mit Knöpfen für Caption-Tags und
  Vorschau (`parseCaption` → Rich Text), „+ Caption for a sound …“ aus den Soundscript-Namen des Asset Browsers.
  Geschrieben wird nur in den Mod-Ordner (`localizationPath`).
- Quellcode-Verwaltung: `SourceControlPanel` (`source_control_panel.cpp`, eigenständiges Widget für die
  Project-Seite) ruft das System-`git` per `QProcess` auf, nacheinander aus einer Warteschlange (`run` → `startNext`),
  nie blockierend; `GIT_TERMINAL_PROMPT=0`, damit nichts auf ein Passwort in einer unsichtbaren Konsole wartet.
  Auslesen im Kern `git_status`: `parseGitStatus` (`status --porcelain=v2 --branch -z`, auch Umbenennungen und
  Konflikte), `parseGitLog` (Format `gitLogFormat()`: Kurz-Id, Autor-**Name**, relative Zeit, Betreff – nie die
  E-Mail), `remoteWebUrl` (https/ssh/scp-Form → Webseite, Zugangsdaten fallen weg). Ordner je Spiel in `QSettings`
  `games/<key>/repository`, Standard = Mod-Ordner. Commit = `add -A -- <angehakte Pfade>` + `commit -m … -- <Pfade>`,
  also nur die angehakten Dateien. Netzwerk nur bei Fetch/Pull/Push durch den Nutzer. Test: `test_git.cpp` (Parser),
  `test_app.cpp` („source control“: echtes Repo im Temp-Ordner, Identität über Umgebungsvariablen).
- Mod code (`ModCodePanel`, `mod_code_panel.cpp`; Kern `mod_code`): Julius soll nie eine `.bat` starten. Die `.bat`
  rufen nur `powershell … -File <x>.ps1 %*` und dann `pause` – Mapsmith ruft die `.ps1` direkt (`modCodeCommand`),
  immer mit `-GamePath` (sonst fragt das Skript per `Read-Host`) und `-NonInteractive`, Standard-Eingabe leer, unter
  Windows `CREATE_NO_WINDOW`; Ausgabe zeilenweise (Windows: OEM-Codepage) → `ScriptRun::feed`, Fehlerkanal →
  `feedError`. Cancel: `taskkill /T /F` (MSBuild und Compiler hängen am Prozess) plus `kill()`. Schritte kommen aus
  den Statuswörtern von `update.ps1` (`found`, `making`/`ok … .sln`, `building games.sln`, `copied bin\…`,
  `maptools.sln`/`vrad`/`saved` → VRAD, `mapbase-fidelity game files ->` → Spieldateien, `All set.` → fertig); ändert
  `update.ps1` seine Ausgaben, `classifyScriptLine` und `test_mod_code.cpp` mitziehen. Einstellungen je Mod in
  `QSettings` `games/<key>/modCode/…` (`folder`, `noVrad`, `autoAfterPull`, `checkOnStart`, `pullOnStart`,
  `updatedHead` = Commit des letzten erfolgreichen Updates, `updatedAt`). Suche nach dem Checkout im Hintergrund
  (`QtConcurrent`), ein gewählter Ordner gewinnt (`m_searchGeneration`). Netzwerk nur über das System-`git` und nur,
  wenn eine Automatik eingeschaltet ist oder der Nutzer Check now / Pull & update drückt; ohne Automatik nur lokal
  `rev-parse HEAD` gegen `updatedHead` („changed since its last update“). Nach Erfolg (`Update`/`Deploy`)
  `modUpdated` → `MainWindow::selectGame(gleiches Spiel)`: Dateisystem, Material-/Modell-Caches und Game enhancements
  neu, die offene Karte bleibt. Der Bau selbst (VS 2022, MSBuild) ist nur unter Windows prüfbar; der UI-Test
  („mod code“) setzt mit `setCommandFactory` ein Shell-Kommando ein, das die Zeilen der „Skripte“ ausgibt.
- Features (`FeaturesPanel`, `features_panel.cpp`; Kern `mod_features`): Julius wählt die Funktionen von
  mapbase-fidelity per Haken statt per Konsole. Die Ein/Aus-Schalter des Add-ons sind `FCVAR_ARCHIVE` und stehen mit
  Absicht nicht in `fidelity.cfg` (Spieler-Config); Mapsmith schreibt sie alle (1/0) nach `cfg/mapsmith_features.cfg`
  und hängt `exec mapsmith_features\t// mapsmith` an `autoexec.cfg` (läuft nach `config.cfg`, also gewinnt die Wahl
  der Mod bei jedem Start). deploy.ps1 schreibt/entfernt nur Zeilen mit `// mapbase-fidelity` und rührt Datei und Zeile
  nicht an. Ohne Add-on (neue Mod) wird nur die Datei geschrieben; `hookFeatureChoice` ergänzt die Zeile, sobald das
  Spiel mit Add-on geöffnet wird (sonst „unknown command“ je Schalter). Nur Ein/Aus-Schalter, nie Look-/Mix-Werte
  (Regel der Mod: die sind `FCVAR_SERVER_CAN_EXECUTE`, Karten setzen sie). Neue ConVar im Add-on → `modFeatures()` und
  `test_mod_features.cpp` ergänzen. Die Material-Funktionen sind die Game-enhancements-Schalter
  (`setEffectEnabled`); ohne laufende Game enhancements direkt in `mapsmith_enhancements.txt`. Das Angebot auf dem
  Overview erscheint, solange die Mod mit Add-on noch keine `mapsmith_features.cfg` hat. Fresh install: Liste aus
  `generatedModFiles` (inline bestätigen, kein Dialog), dann Mapsmiths eigene Dateien (`MAPSMITH_MARKER`) löschen,
  `deploy.ps1 -Remove` (`ModCodeAction::RemoveGameFiles`, löst kein `modUpdated` aus), Funktionen neu schreiben,
  Update mod bzw. Install game files (`finished` von `ModCodePanel`, nächster Schritt per `QTimer::singleShot(0)`).
  UI-Test „features“ mit `setCommandFactory` wie bei Mod code.
- Funktionen aus Mapsmith (`mod/`, `tools/export_mod_addon.py`, `.github/workflows/mod.yml`, Kern `mod_install`,
  `FeaturesPanel`): Julius baut nichts selbst – kein Pull, kein Visual Studio, kein `update.bat`. Der Add-on-Code
  (mapbase-fidelity) liegt deshalb (öffentlich) hier in `mod/`, aus dem privaten Mod-Repo exportiert:
  `mod/MAPBASE` (Mapbase-Repo, Basis-Commit = `develop` d9fd335, exportierter Commit), `mod/sp/…` (alle Dateien, die
  das Add-on unter `sp/` hinzufügt), `mod/mapbase.patch` (alle Änderungen an Mapbase-Dateien: Haken mit
  `// mapbase-fidelity`, VPC/VGC, VS2022-Schalter, VRAD) und `mod/fidelity/…` (Spieldateien, Skripte, ohne Tests).
  `mod/` nie von Hand ändern. **Nach Änderungen am Add-on** (im Mod-Repo, gemergt):
  `python3 tools/export_mod_addon.py --repo <mymod>` (liest nur den Commit `--ref`, Standard `master`), committen;
  `--check` prüft, dass `mod/` (im Index, Zeilenenden normalisiert) genau der Export ist. Geprüft am 28.10.2026:
  Basis + Patch + `mod/sp` ergibt exakt den `sp/`-Baum von mymod `52465da`, und der Linux-Prüfbau `client_episodic
  server_episodic shaders_fidelity` davon läuft durch. Nimmt die Mod ein neueres Mapbase, `DEFAULT_BASE` im Skript
  anpassen. CI (`mod.yml`, Windows-Runner `windows-2022` wegen Toolset v143; von `ci.yml` und `release.yml` gerufen):
  Mapbase am Basis-Commit (sparse `sp`, `core.autocrlf false`, damit der Patch passt), Patch + Dateien, VPC
  (`creategameprojects.bat`, `createmaptoolsprojects.bat`), `tools/repair_vpc_solution.py` (fehlende
  Release|Win32-Bauzeilen wie `Repair-Solution` in update.ps1), MSBuild `games.sln`, `shaders.sln` (Mapbases eigene
  Shader `game_shader_dx9.dll`, VPC `createshadersprojects.bat`, Gruppe `shaders`) und aus `maptools.sln` nur
  `Vrad_dll;Vrad_launcher`, dann `tools/collect_mod_build.py` (sammelt je Zweig client, server und game_shader_dx9, dazu Valves Mod-Vorlage aus `sp/game/mod_hl2`/`mod_episodic` als `template/<spiel>/cfg/skill*.cfg` und `template/<spiel>/resource/mod_<spiel>_*.txt` – wird nicht in die Mod installiert, nur von `mod_base_files` gelesen;
  Test `tools/tests/test_collect_mod_build.py`) → Feature-Bundle (Artefakt `mapsmith-features`,
  Cache-Schlüssel = Hash von `mod/**` + Skripten, also nur neu gebaut, wenn sich das Add-on ändert). Das Windows-Paket
  legt es als `features/` neben `Mapsmith.exe` (ZIP und Installer; der Updater bringt es mit). Version des Bundles =
  `mod/fidelity/VERSION` + `+` + exportierter Commit (7 Zeichen); weicht sie von `cfg/mapsmith_installed.txt` ab,
  aktualisiert der Features-Tab die Mod beim Öffnen. Ohne Checkout führt „Update mod“ (Build from source, Source
  control, Hinweise anderer Panels) `installNow` aus, und deploy.ps1/report.ps1 laufen aus dem Bundle (Make report
  ohne Quellcode). Neue Mapbase-Mod (`createModAndSwitch`): Funktionen gleich installiert, deploy.ps1 beim ersten
  Öffnen (QSettings `features/deployPending/<ordner>`). Tests: `test_mod_install.cpp` (Kern, mit
  `test::makeFeatureBundle`), UI-Test „features from Mapsmith“. Nicht prüfbar hier: der Windows-Bau in der CI bis er
  einmal läuft, und das Spiel mit den gebauten DLLs.
  **Mapbases Shader-DLL** (`bin/game_shader_dx9.dll`) gehört zu jeder Mapbase-Mod: fehlt sie, kennt die Engine
  `SDK_screenspace_general`, `SDK_VertexLitGeneric`, `SDK_UnlitGeneric`, `SDK_Engine_Post`, `DepthOfField`, `SDK_Cable`
  nicht und zeichnet sie als Wireframe – die Vollbild-Passes werden zu schwarzen Linien quer durchs Bild (Bericht
  von Julius, Mod „test2“, 29.10.2026). Deshalb baut `mod.yml` sie mit, das Bundle installiert sie, das
  Problems-Panel warnt (`ModBinIncomplete`, Aktion `installFeatures` → Features-Tab, `installNow`, neu prüfen; Kontext
  `mapbaseModDir` + `bundle`), und eine neue Mapbase-Mod endet mit vollständigem `bin` (Bundle, sonst
  `copyMapbaseBin`; der Hinweis am Ende nennt, was noch fehlt). `fidelity/rain_map` ist keine Datei, sondern eine
  prozedurale Textur des Clients (`fidelity_wetness.cpp`, `CreateProceduralTexture`) – die Meldung „not found“ beim
  Laden ist harmlos. UI-Test „problems: a Mapbase mod without Mapbase's shaders …“.
- Material-Editor (`MaterialEditor` + `MaterialPreview`): Felder für die häufigen Parameter, Tabelle für den Rest,
  Vorschau (Wand/Würfel/Kugel, Sonne/Lampe/Nacht/unbeleuchtet; ssbump über die drei Basisvektoren). Speichern schreibt
  nach `<mod>/materials/<name>.vmt` (VPK-Material → Kopie in der Mod) und meldet `materialSaved` → `MaterialCache::reload`.
  Gruppen je Art (`Kind`: Wand, Überblendung, Modell, Wasser, unbeleuchtet), Reihenfolge: Material, Wasser,
  Wasserglanz, Flüssigkeit, Normal-Map, Relief, Spiegelung, Transparenz, Detail, Glanzlichter, Rest.
  Aufbau: links Suche + zuletzt bearbeitete (`QSettings` `materialEditor/recent`), Mitte Gruppen
  (`FeatureGroup`: Kopf mit Pfeil und ggf. Schalter; Schalter aus = zugeklappt), rechts die Vorschau mit Werkzeugleiste.
  Feature-Schalter (`setFeature`: `bump`, `env`, `detail`, `phong`) legen ihre Parameter beim Ausschalten in
  `m_parked` und holen sie beim Einschalten zurück; Relief/Wasserglanz/Flüssigkeit wechseln den Shader (`setShader`).
  Texturfelder zeigen ein 64-px-Bild (aus denselben Hintergrund-Ladungen wie die Vorschau).
- mapbase-fidelity im Material-Editor (Shader der Mod, `mod/sp/src/materialsystem/fidelity/`): Schalter immer nutzbar
  (die Vorschau zeigt es); findet `fidelityShaders()` die Shader-DLL der Mod nicht (`bin/game_shader_dx6.dll`), steht in
  der Gruppe ein Hinweis, unter „Kind“ die Warnung mit **Install features**, und *Save* schreibt solche Shader nicht (das
  Spiel zeigte sonst ein durchsichtiges Drahtgitter). Neue Relief-Tiefe wie deploy (`fidelityReliefSource`: 0,01, Normal-Map und Ziegel/Fliesen 0,006; „Darker in the depths“ 0,25). Relief = `Fidelity_Parallax` bzw. `Fidelity_WorldVertexTransition`
  (`$parallaxmap` weiß = oben, `$parallaxdepth`, `$parallaxao`, `$parallaxfade`); Wasser = `Water_Fidelity`
  (`$fidelityglint`, `$fidelityfoam`); Flüssigkeit = `Fidelity_Liquid` (`$liquidcolor`, `$fill` −1 = zufällig, …).
  Beim Umschalten fallen die Parameter der anderen Variante weg (`RELIEF_KEYS`, `WATER_KEYS`, `LIQUID_KEYS`).
  Höhenkarte: Kern-Modul `heightmap` (ohne Qt) – aus der Normal-Map per Frankot-Chellappa wie `fidelity/height_map.cs`
  der Mod, sonst aus der Helligkeit; `invertHeights`, `applyHeightZones` (`HeightZone::Flat` = 1, `Inverted` = 1 − h),
  `encodeHeightVtf` (I8 mit Mips, Form wie `WriteI8` von deploy). `MaterialEditor::makeHeightMap(HeightFrom)`: Auto = Muster (`relief_pattern`, ab
  Sicherheit 0,5), sonst Normal-Map, sonst Helligkeit; das gefundene Layout (`reliefLayout()`) kommt auch beim Laden
  einer vorhandenen Höhenkarte mit. Zonen malt `HeightZonePainter` (links malen, rechts löschen, Kacheln laufen über den
  Rand; mit Layout: Fugen eingeblendet, „Whole bricks“ setzt per `setUnitZone` ganze Steine).
  Live in den Ansichten: jede Änderung (`refreshPreview` → `scheduleLive`, 120 ms) schickt `liveChanged` (ganzer Text
  per `newMaterialText` + aktuelle Höhenkarte); `MaterialCache::setLive` liest das Material daraus statt aus der Datei
  (`loadInfo` mit `liveVmt`), `clearLive` beim Speichern oder Wechseln (`liveReverted`). `m_tickets` verwirft ältere
  Ladevorgänge desselben Materials, die nach einem neueren fertig werden. Speichern schreibt `<name>_relief.vtf` und setzt `$parallaxmap`. Materialien von `deploy.bat` (erste Zeile
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
- VisGroups: `VisGroupsPanel` (`visgroups_panel.cpp`, Baum „visGroupTree“, Haken = sichtbar, Name per F2/„Rename“,
  Doppelklick wählt Mitglieder; Knöpfe „visNew“ mit Namensfeld „visGroupName“, „visAdd“, „visRemove“, „visSelect“,
  „visColor“, „visDelete“). Das Panel ist eigenständig; es kommt als Tab neben den Outliner (MainWindow bindet es ein).
  `Editor::setVisGroupVisible` hält die versteckten Gruppen (`m_hiddenVisGroups`), `Editor::hidden()` nimmt ihre
  Mitglieder dazu; `openMap` liest sie aus der Datei, `saveMap` schreibt `visgroupshown` in eine Kopie (kein Undo).
- Instanzen: `Editor::instancePreview` (Cache je Datei + Änderungszeit + Keys der func_instance, max. 256 Einträge)
  liefert dem Renderer über `SceneRenderer::setInstanceProvider` Brushes und Punkt-Boxen; der Renderer legt die Brushes
  in `m_instanceSolids` (deque, feste Adressen) ab, `PickItem::detached` zeigt darauf – Klicken wählt die func_instance,
  im Flächenmodus werden sie übersprungen, ihre Seiten kommen nicht in `m_sideFaces` (Ids der Instanz-Datei). Kanten
  türkis. Eigenschaften: Kasten „instanceBox“ (`properties_instance.cpp`: Datei „instanceFile“ + Durchsuchen,
  Variablen-Tabelle „instanceReplacements“, Parameter der Instanz, „instanceReload“ = `Editor::reloadInstances`,
  „instanceCollapse“ = `Editor::collapseInstance`). Verschachtelte Instanzen werden nicht gezeichnet (nur die eigene
  Ebene), beim Auflösen bleiben sie als func_instance stehen.
- Kanten (wie Source 2): Ecken des Vertex-Werkzeugs liegen in der Ansicht (`m_vertexSelection`) und gespiegelt im
  Editor (`Editor::vertexCorners`, Signal `vertexCornersChanged`, alle Ansichten folgen). `edit::edgesAtCorners` findet
  die Kanten zwischen gewählten Ecken (als Flächenpaar), `edit::shapeEdges` macht Keil/Fase (`cutEdge` im Kern `csg`)
  oder Rundung (`roundEdge`) nacheinander in einem Undo-Schritt (Seiten behalten ihre Ids). UI: Kasten „edgeTools“ im
  Werkzeug-Panel + Abschnitt im Rechtsklick-Menü; Werte in `brushes/edgeSize` und `brushes/roundSegments`.
- Werkzeuge Clip/Vertex/Polygon in `viewport_tools.cpp`; ihre Vorschau läuft über `setPreviewSolids` mit einem leeren
  Hilfsdokument (nur für Ids), die Auswahl wird währenddessen ausgeblendet. Rechtsklick ohne Bewegung →
  `contextMenuRequested` → Menü im `MainWindow` (Aktionen aus `m_contextActions`).
- Schatten (`renderer_shadows.cpp`): Sonnen-Schattenkarte (orthografisch über die ganze Karte) und ein Atlas mit 16
  Kacheln (4 Spot-Lichter je 1, 2 Punkt-Lichter je 6 Würfelseiten). Neu gezeichnet nur bei Geometrie-/Licht-Änderung oder
  wenn sich die nächsten Lichter ändern. Shader: `u_lightShadow[i]` (erste Kachel, Punktlicht ja/nein), PCF 2x2.
  Schatten-Sampler liegen fest auf Einheit 3 und 4 (zwei Sampler-Typen auf einer Einheit wären ein GL-Fehler).
- Cubemap-Reflexionen (`renderer_cubemaps.cpp`): jede env_cubemap wird vom Ursprung aus in sechs 128er-Bilder gezeichnet
  (90°-Kamera je Seite, Lage aus `sme/cubemaps.h`), alle in einem Atlas (8 Cubemaps je Zeile, je 6 Seiten, höchstens 256
  Cubemaps). Erst 300 ms nach der letzten Änderung, dann 6 Seiten pro Bild (`buildProbeFaces`, die Ansicht fordert per
  `m_cubemapTimer` weitere Bilder an); Map > Build cubemaps sofort. Gezeichnet wird mit `drawSurfaces(params, true)`:
  ohne Entity-Kästen, Sprites, Auswahlfarbe, Schatten, Farbkorrektur und Reflexionen; Tool-Texturen unsichtbar außer dem
  Himmel. Shader: je Fläche die Sonde aus einer R16UI-Textur (Einheit 15, wie die Flags), Sondendaten (Ursprung, Box) in
  einer RGBA32F-Textur (Einheit 14), Atlas auf 13, `$envmapmask` auf 12. Die Spiegelung kommt wie in der Engine oben
  drauf (`hdr +=`), mit `$envmaptint`, Maske und Fresnel (`$fresnelreflection`, Water 0). Ohne env_cubemap: der Himmel.
  Ausgewählte env_cubemaps zeigen eine Spiegelkugel (`drawProbeSpheres`, `u_probeSphere`).
- Kompilieren: `CompileRunner::plan` setzt die Programme der Spiel-Einstellungen in die Befehle aus `compileCommands`
  ein und meldet strukturiert (`stepStarted`, `stepProgress`, `problem` …) an das `CompileWindow`. Cubemaps: das Spiel
  mit `+map <karte> -buildcubemaps` (`+mat_hdr_level 0/2`, bei „Both“ zwei Läufe), wartet bis es sich beendet; braucht
  das Kopieren. `CompileDialog`: Ansicht „Simple“ (Karten) und „Advanced“ (Schritte, Parameter-Liste mit Suche,
  eigene Parameter) auf denselben `CompileOptions`; gespeichert pro Spiel in QSettings `compile/<spiel>` (Text aus
  `serialize`), die zuletzt gewählte Ansicht in `compile/advanced`.
  Mit aktiven Cordons wird `<karte>_cordon.vmf` geschrieben und kompiliert.
- Partikel: `particleSystems()` liest `particles_manifest.txt` und die PCFs mit dem DMX-Leser von sourcepp (`kvpp::DMX`).
  Kern-Modul `particles`: `readParticleDefinitions` macht daraus Systeme mit ihren Werten und Teilen (emitters,
  initializers, operators, renderers, forces, constraints, children; Werte als Text + Zahlen). `ParticleSimulation`
  rechnet die gängigen Funktionen nach Namen nach (emit_continuously/instantaneously/noise, Lifetime/Radius/Color/Alpha/
  Rotation Random, Position Within Sphere/Box, Velocity Random/Noise, Movement Basic, Lifespan Decay, Alpha Fade
  (and Decay, In/Out Random), Radius Scale, Color Fade, random force), Kinder mit Verzögerung, fester Zufalls-Seed,
  höchstens 3000 Partikel und 16 Systeme; ohne Emitter mehr startet sie neu (`loop`). Unbekannte Teile werden gezeigt,
  ändern aber nichts. UI `particle_browser`: Liste | `ParticlePreview` (QPainter, Sprites aus dem Material, `$additive`
  → Plus-Mischung, Farben in 32 Stufen zwischengespeichert, Trails als Striche, Kamera passt sich an) | Karten der Teile.
  Simuliert nur, solange die Vorschau sichtbar ist und läuft. Demo: `makeDemoFirePcf`/`makeDemoSparksPcf` (`PcfWriter`
  schreibt binäres DMX Version 2). Screenshot: `--page particles --particle demo_fire_small`.
- Tastenkürzel: Aktionen tragen ihren englischen Namen als `objectName`; `ShortcutDialog` speichert unter `shortcuts/<Name>`.
- Einrasten: `Editor::snapGrid()` (0 wenn Shift+W aus) für alle Positionen, `Editor::snapAngle()` für Drehungen;
  Drehpunkt wird auf das halbe Raster gesetzt, damit Brushes auf dem Raster bleiben.

## Sicherheit und Suche (Autosave, Verlauf, Finden, Ersetzen)

- `autosaver.{h,cpp}`: `Autosaver(Editor*)` – Timer (`autosave/minutes`, Standard 5, 0 = aus), schreibt nur, wenn sich
  die History-Revision seit dem letzten Autosave geändert hat; Text auf dem UI-Thread (`serialize`), Schreiben (QSaveFile)
  und Aufräumen (`autosave/keep`, Standard 10) in einem eigenen `QThreadPool` mit einem Thread. Sitzungs-Markierung
  `<AppData>/session-open.txt` mit dem Kartenpfad; `aboutToQuit` entfernt sie. `recoveryAfterCrash()` (vor dem
  Anlegen des Autosavers aufrufen!) und `recoveryForMap()` liefern Autosaves, die neuer sind als die Kartendatei
  (1 s Spielraum), `offerRecovery()` fragt, `openMapWithRecovery()` öffnet mit Nachfrage. `Editor::openRecovered`
  setzt den Pfad der Originalkarte und `m_recovered` (zählt als ungespeichert). `Editor::saveMap` legt vorher
  `<karte>.vmx` an (`editor/vmxBackup`, Standard an).
- `history_panel.{h,cpp}`: `HistoryPanel` (Dock History unten, Tab neben dem Asset Browser) – Zeile 0 = geöffnete Karte, Zeile i = nach Änderung i; Klick →
  `Editor::undoTo` (verzögert, weil das die Liste neu aufbaut). `History::size/index/nameAt`.
- `find_dialog.{h,cpp}`: `FindDialog` (nicht modal, Tabs Find / Entity report), `showFindDialog(editor, parent,
  frame, tab)` hält ein Exemplar pro Fenster. `replace_materials_dialog.{h,cpp}`: `ReplaceMaterialsDialog`,
  `showReplaceMaterialsDialog(editor, parent, bekannteMaterialien)`.

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
  Modell-Vorschaubilder, Sounds-Seite, Ziehen von Sounds/Soundscapes/Modellen/Partikeln in die Karte, Drehen mit
  Rotations-Raster, Flächenmodus, Autofix-Knopf, Malen, ganze Wand, Block im 2D-View, Kompilier-Fenster (mit
  Shell-Skripten als Tools, nur Linux), Formen mit Live-Ändern, Hollow/Merge, Rechtsklick-Menü, Clip/Vertex/Polygon
  im 2D-View, Speichern/Laden, Text-Overlay, Blöcke nacheinander, Entity-Liste (Icons, Suche „global light“, Brush-Entity
  per Box, Random yaw), Ecken-Einrasten, Ansichtsmenü, Smart Edit aus + Kopieren/Einfügen, Cubemap-Raum-Box, Sounds
  (Decoder, Soundscript speichern), Material-Editor (Vorschau, Speichern, Kopie, Relief mit Höhenkarte/Umkehren/Zonen,
  Wasser, Flüssigkeit, deploy.bat-Marker, Zonen-Maler). Die Demo-Mod hat dafür eine Platzhalter-Datei
  `shaders/fxc/fidelity_parallax_ps30.vcs`, eine Normal-Map für die Ziegel, Wasser und eine Flasche.
- `test_fuzz.cpp` – alle Leser fremder Dateien (MDL/VVD/VTX/ANI, VTF, Bilder, Höhenkarten, WAV/MP3/OGG/FLAC, PCF, BSP,
  SMD/OBJ/MTL, VMT/KeyValues, VMF) mit abgeschnittenen, verfälschten und zufälligen Bytes (feste Startwerte): kein
  Absturz, kein Hängen, keine Riesen-Speicheranforderung. Mit AddressSanitizer bauen (`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"`,
  dazu `-DXZ_SANDBOX=no`) und `sme_tests -tc='fuzz*'` laufen lassen, wenn ein Leser dazukommt oder sich ändert; neue
  Leser dort eintragen. Fremde Bibliotheken (vtfpp, vpkpp, kvpp) werfen bei kaputten Dateien Ausnahmen – jeder Aufruf
  steht in `try`/`catch`, Datenbereiche vor dem Lesen prüfen (vtfpp prüft `getImageDataRaw` nicht).
- `test_performance.cpp` (Kern) und „performance: a big map …“ in `test_app.cpp` – grobe Zeitgrenzen (Game enhancements
  bei 5500 Materialien, Karte mit 20 000 Brushes lesen/schreiben, 6000 Brushes + 1500 Entities öffnen/bearbeiten/
  auswählen/zeichnen); die Grenzen sind großzügig und fangen nur Verschlechterungen um ein Vielfaches.
- Startzeit: `SME_STARTUP_TRACE=quit build/src/app/Mapsmith --steam demo/Steam --game sourcemods/mymod` schreibt die
  Meilensteine (Qt bereit, Spiele gefunden, Fenster gebaut, erstes Bild, Spiel gelesen) und beendet sich. Demo unter
  xvfb (Software-OpenGL), Startseite an, Oktober 2026: erstes nutzbares Bild vorher etwa 860–995 ms (Spiel vor dem
  Fenster gelesen), danach etwa 615–655 ms; das Spiel ist 15–50 ms später gelesen. Den größten Teil kostet der Bau der
  Widgets (etwa 450–550 ms, davon die vier GL-Ansichten 100–280 ms). Der UI-Test „panels: …“ prüft eine großzügige
  Grenze (8 s bis zum gezeigten Fenster mit Spiel) und dass Project › Menu beim Start nichts liest.
  Danach (Aufräum-Durchgang, Median aus 9 Läufen): Fenster bauen 463 → 269 ms, erstes nutzbares Bild 763 → 601 ms;
  Befehle (callgrind, Konstruktor `MainWindow`) 2,48 → 1,15 Mrd. Gekostet hatte vor allem das app-weite Style Sheet:
  jedes Widget, das mit Inhalt in ein anderes wandert (Dock, Tab, Seite, Splitter, Scroll-Fläche), wird samt Inhalt
  neu poliert und neu vermessen (Textumbruch!). Siehe Rezept „Neues Panel“. Messen: `valgrind --tool=callgrind
  --toggle-collect='sme::app::MainWindow::MainWindow*'` mit `SME_STARTUP_TRACE=quit`.
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
- **Panel startet ein Programm (`QProcess`)**: den Prozess als Kind des Panels anlegen und im Destruktor zuerst
  `stopOwnProcesses(this)` (`process_guard.h`) aufrufen – sonst meldet `~QProcess` beim Schließen `finished` an ein
  halb abgebautes Panel (Absturz). Hintergrund-Jobs, die das `GameFileSystem` lesen, bei `Editor::gameAboutToChange`
  abwarten (`waitForOwnTasks(this)` wartet auf alle `QFutureWatcher`-Kinder; ein Sound, der beim Spielwechsel noch
  dekodiert wurde, las sonst im alten Dateisystem weiter). Beim Beenden wartet `main` auf den Thread-Pool, bevor
  Fenster und Editor abgebaut werden.
- **Neues Panel / neue Seite**: Widgets dort bauen, wo sie bleiben – Elternteil im Konstruktor angeben
  (`new QWidget(splitter)`, Inhalt einer Scroll-Fläche mit `scroll->viewport()` als Eltern, Docks: `host(title)` in
  `MainWindow::createDocks`, Project-Tabs: `projectTabs`). Ein fertiges Widget umzuhängen poliert und vermisst es mit dem
  app-weiten Style Sheet komplett neu (Start-Zeit, Neuaufbau von Object Properties). Sichtbarkeit nur bei Änderung
  setzen (ein `setVisible` auf ein sichtbares Panel stößt dessen Layout sofort an); Panels, die selten zu sehen sind und
  bei jeder Änderung neu lesen, mit `RefreshWhenShown` (`lazy_refresh`).
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
  Ohne Tag-Push (z. B. aus einer Cloud-Sitzung, die nur ihren Branch pushen darf): auf github.com *Releases → Draft a
  new release*, Tag `vX.Y.Z` mit *Create new tag on publish* auf dem Branch anlegen, Text einfügen, veröffentlichen –
  der Workflow läuft dann ebenfalls und hängt Setup, ZIP und Prüfsummen an dieses Release an.

- Linux: siehe `.claude/CLAUDE.md`. Windows: Visual Studio 2022 oder neuer + Qt 6.8 (`-DCMAKE_PREFIX_PATH=<Qt>/msvc2022_64`).
- CI (`.github/workflows/ci.yml`): `checks` (Privatsphäre), `linux` (Build, Tests, UI-Tests unter Xvfb, Screenshots als
  Artefakt), `mod` (die Funktionen der Mod aus `mod/` auf Mapbase gebaut, `mod.yml`, gecacht), `windows` (MSVC-Build,
  Tests, ZIP mit `Mapsmith.exe`, `sme-probe.exe`, Qt-DLLs, `features/` (Feature-Bundle), Prüfsumme, dazu der Installer
  `Mapsmith-Setup-<version>.exe` aus `installer/mapsmith.iss` (Inno Setup, pro Benutzer nach `%LOCALAPPDATA%\Programs\Mapsmith`,
  ohne Adminrechte; der Updater ersetzt später die Dateien dort aus der ZIP). Programm-Icon: `src/app/resources/mapsmith.ico`
  (`Mapsmith --write-icon DIR` + `tools/make_ico.py`; gezeichnet in `icons.cpp` `drawAppLogo`). Der frühere Name war
  „Source Map Editor“; `main.cpp` übernimmt dessen Einstellungen einmal (`migrateOldSettings`).
- Abhängigkeiten auf Commit-SHAs (`cmake/Dependencies.cmake`); Actions auf SHAs, Dependabot hält sie aktuell.

## Handbuch (GitHub Pages)

Das Nutzer-Handbuch liegt in `docs/` und wird von GitHub Pages mit Jekyll gebaut (kein eigener Workflow):
`docs/_config.yml` (Theme `just-the-docs` als `remote_theme`, `ENTWICKLUNG.md` ausgeschlossen), Startseite
`docs/index.md`, Seiten in `docs/manual/*.md` (Front Matter `title` + `nav_order`), Bilder in `docs/manual/images/`.
Adresse: <https://29j2003.github.io/hammer-test/> (die README verlinkt sie oben).

**Einmalig einschalten** (braucht Admin-Rechte am Repo):

1. Auf GitHub im Repo **Settings → Pages** öffnen.
2. Unter *Build and deployment* bei **Source** „**Deploy from a branch**“ wählen.
3. Bei **Branch** `main` und den Ordner **`/docs`** wählen, **Save**.
4. Nach ein bis zwei Minuten steht die Seite unter der Adresse oben; jeder Push auf den Branch baut sie neu
   (Fortschritt unter *Actions* → „pages build and deployment“).

Regeln fürs Handbuch: Englisch, nur der aktuelle Stand, Knopf- und Menünamen genau wie im Code (`tr("…")`), keine
anderen Editoren nennen (nur die README als „inspired by“), keine E-Mail-Adressen. Bilder höchstens 1600 px breit und
mit 256 Farben gespeichert (zusammen unter ~8 MB); sie entstehen mit der Demo und `--screenshot` (siehe Befehle in
`.claude/CLAUDE.md`). Neue Funktion → passende Handbuch-Seite mitpflegen; neue Seite → Link in `docs/index.md`.

## Offene Punkte / Annahmen (noch nicht am echten Spiel geprüft)

- Alles ist bisher nur mit erzeugten Testdaten geprüft – **nicht** mit echten HL2/Mapbase/Black-Mesa-Installationen und
  nicht auf Windows. Nächster Schritt: `sme-probe report` und `sme-probe vmf` auf Julius' PC.
- MDL-Offsets (`studiohdr_t` v44–49) stammen aus `studio.h`; mit echten Modellen gegenprüfen (`sme-probe` zeigt Sequenzen).
- Black Mesa (Xengine) ist ungetestet; VMF-Unterschiede unbekannt.
- PCF-Dateien: nur binäre DMX (wie sie die Spiele ausliefern); Text-DMX wird übersprungen.
- Sound-Wiedergabe über QAudioSink ist unter Windows noch nicht von Hand geprüft (unter Linux ohne Audiogerät nur die
  Decoder getestet; diese gegen FFmpeg verglichen).
- Modelle: VTX-Layout (25/33 Byte) wird ausprobiert; mit echten HL2-/Mapbase-Modellen prüfen (`sme-probe model GAME PATH`).
- Decals/Overlays (`decals`): Infodecals landen auf der nächsten Fläche und Flächen derselben Ebene; um Ecken legt die
  Engine sie zusätzlich, der Editor nicht. Overlays auf Displacements nutzen die flache Grundfläche. Die Eckreihenfolge
  der Texturkoordinaten folgt VBSP (uv0: StartU/StartV, uv1: StartU/EndV, uv2: EndU/EndV, uv3: EndU/StartV); nicht im
  Spiel gegen echte Karten verglichen.
- Material-Vorschau ist eine Annäherung an die Shader der Engine (kein HDR, Cubemap = einfacher Himmel).
- Menü-Editor: nicht im Spiel getestet. Offen: ob die Kapiteltitel-Tokens bei jeder Mod-Basis gefunden werden (der
  Ordnername wird groß geschrieben; die Engine vergleicht ohne Groß-/Kleinschreibung), das Logo-Bild bei `gamelogo 1`
  (`resource/GameLogo.res`) wird nicht bearbeitet, die Vorschau nutzt nicht das Schema der Mod (Schriften, Farben).
- Ship (`ship_panel`, Kern `shipping`): `bspzip -addorupdatelist <bsp> <liste> <bsp> -game <mod>` und der Start von steamcmd
  (`+login <name> +workshop_build_item <vdf> +quit`, eigenes Konsolenfenster unter Windows) sind nur mit fehlendem Werkzeug
  getestet, nicht mit echtem bspzip/steamcmd. Die Abhängigkeiten kommen aus der offenen VMF, nicht aus der BSP; Materialien
  in PCF-Dateien und Modelle, die erst Skripte (z. B. Gibs, `propdata`) laden, werden nicht gefunden. Eigene PCF brauchen
  im Spiel zusätzlich ein Partikel-Manifest. Die Seite „Project“ im `MainWindow` bindet das Panel ein (`ShipPanel(Editor*)`).

- Game enhancements (`fidelity_effects`, `enhancements`): Regeln und Text aus `deploy.ps1` von mymod nachgebaut und gegen
  dessen Ausgabe-Text getestet, aber nicht im Spiel geprüft. Nicht nachgebaut: die eigenen Modell-Kopien für dunkles Glas
  (`Get-ContainerModels`), Relief automatisch (bleibt beim Material-Editor). Viele Materialien beim ersten Öffnen (HL2:
  alle Nicht-Modell-Materialien werden einmal fürs Wasser gelesen) – Dauer mit echtem HL2 messen.

## Fahrplan

### Recherche Oktober 2026: was einem Source-1-Mapper noch fehlt (nach Nutzen für Mapper ohne Programmierkenntnisse ÷ Aufwand)

Quellen: Valve Developer Community (Compile Errors, Leak, Node graph, info_node, Func detail, VIS optimization, Move rope/Keyframe rope, Hammer++, Third Party Tools, Crowbar, VIDE, Bspzip), interlopers.net-Fehlerprüfer (in Foren empfohlen), Mapbase-Releases (ModDB v7.0/v7.3), MapCore-Thread zu Hammer++. developer.valvesoftware.com war aus der Cloud-Umgebung nicht abrufbar; die Punkte stützen sich auf Suchergebnisse dieser Seiten.

Erledigt (Branch `research`): häufige Kompilierfehler erklärt; Spielerstart im Brush (Fix), Entities im Nichts, extreme Lightmap-Scales (Fix), NPCs ohne `info_node` (Fix: Knoten setzen); Make rope (mit hängender Vorschau, wie Hammer++); Make ladder.

1. **Play from here**: Karte mit dem Spieler an der Kameraposition starten (temporärer `info_player_start` mit Master-Flag in einer Kopie der VMF, schnelles Kompilieren). Hoher Nutzen, mittlerer Aufwand.
2. **Nodegraph sichtbar machen**: Verbindungen der `info_node`s (Sichtlinie + Bodenabstand wie `ai_show_connect`) im Viewport, unverbundene Knoten rot; Knoten-Malwerkzeug (Klick setzt Knoten im Raster). Hoch / mittel.
3. **Visleaf-/Portal-Ansicht** aus der `.prt` (wie Hammer „Load portal file“), mit Hinweisen zu `func_detail`/Hint. Mittel / mittel.
4. **Areaportal-Prüfung vor dem Kompilieren**: Areaportal muss die Öffnung ganz füllen und Welt-Brushes berühren (nicht `func_detail`). Mittel / mittel.
5. **Logik-Debugger**: I/O-Kette einer Entity simulieren („wenn der Trigger feuert, passiert …“), Zeitleiste mit Delays. Hoch / hoch.
6. **Modell dekompilieren** (wie Crowbar): QC/SMD aus `.mdl` für Anpassungen (Skins, Hitbox, Kollision), gleich wieder bauen. Mittel / hoch.
7. **Inhalte packen und prüfen** (wie VIDE/Pakrat): fehlende Custom-Dateien einer Karte finden und in die BSP packen; Ship-Panel erweitert. Mittel / mittel.
8. **3D-Skybox-Vorschau**: `sky_camera`-Bereich im Maßstab um die Karte zeichnen, „Send to skybox“ (Kopie mit Faktor 1/16). Mittel / mittel.
9. **Displacements im Vertex-Werkzeug** und Nähen über verschiedene Power (alter Punkt 2). Mittel / mittel.
10. **Overlay-Ecken im Viewport ziehen**, Decals über Ecken (alter Punkt 3). Mittel / mittel.
11. **Leitern/Seile weiter**: Seil-Werkzeug mit Shift-Ziehen für neue Keyframes (wie Hammer), Leiter-Modelle mit `info_ladder_dismount`-Vorschau. Niedrig / niedrig.
12. **Prop-Prüfung mit `prop_data`** (nur-statisch/nur-dynamisch, Masse) und Budget-Grenzen weiterer Spiele (Black Mesa, Strata) mit Quelle (alter Punkt 1). Mittel / niedrig.
13. **Instanzen bearbeiten** direkt in der Karte (öffnen, ändern, zurückschreiben). Mittel / mittel.
14. **Mesh-Modus (Variante B)**: freie Meshes → konvex zerlegen bzw. als Static Prop + Nodraw-Hülle (alter Punkt 4). Hoch / hoch.
15. **Echte Physik-Simulation, VRAD-Schnellvorschau** (alter Punkt 5). Mittel / hoch.

### Älterer Fahrplan

1. Quick wins aus der Hammer-Schmerzpunkt-Recherche erledigt (Prop-Klasse, Map Budget, Make water, Sound-Schleifen, robuste VMF, Lightmap density, benutzte Modelle, sky_camera). Offen: Budget-Grenzen weiterer Spiele (Black Mesa, Strata) mit Quelle ergänzen; Prop-Prüfung mit `prop_data` (nur-statisch/nur-dynamisch).
1. Rückmeldung von echten Installationen (Probe-Bericht, `sme-probe model`), Fehler beheben.
2. ~~Displacement-Malen~~ (Werkzeug Displacement, Shift+D); offen: einzelne Displacement-Punkte mit dem Vertex-Werkzeug ziehen, Nähen über verschiedene Power (T-Stellen).
3. ~~Overlays/Decals im Viewport zeichnen~~ (Kern `decals`, Renderer zeichnet sie gemischt über den Brushes); offen: Ecken eines Overlays im Viewport ziehen, Decals um Ecken.
4. Mesh-Modus (Variante B): freie Meshes → beim Kompilieren konvex zerlegen bzw. als Static Prop + Nodraw-Hülle.
5. Echte Physik-Simulation, VRAD-Schnellvorschau.
