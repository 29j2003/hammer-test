// import_dialog.cpp - Importing sounds, pictures, models and particle files (see import_dialog.h).

#include "import_dialog.h"
#include "process_guard.h"

#include "editor.h"
#include "samples_folder.h"
#include "sound_player.h"
#include "ui_helpers.h"

#include <sme/assets.h>
#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/samples.h>
#include <sme/text.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace sme::app {

namespace fs = std::filesystem;

namespace {

std::optional<std::vector<std::byte>> readBytes(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > (512ll << 20)) return std::nullopt;
	const QByteArray data = file.readAll();
	const auto* begin = reinterpret_cast<const std::byte*>(data.constData());
	return std::vector<std::byte>(begin, begin + data.size());
}

QString readText(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > (256ll << 20)) return {};
	return QString::fromUtf8(file.readAll());
}

/// A picture next to `file` with one of the suffixes ("wall" -> "wall_normal.png").
QString findSibling(const QString& file, const QStringList& suffixes) {
	const QFileInfo info(file);
	for (const auto& suffix : suffixes) {
		for (const char* ext : {"png", "tga", "jpg", "jpeg", "bmp", "PNG", "TGA", "JPG"}) {
			const QString candidate = info.dir().filePath(info.completeBaseName() + suffix + "." + ext);
			if (QFileInfo::exists(candidate)) return candidate;
		}
	}
	return {};
}

/// A picture for a model material: the .mtl's file (relative to the model), else <material>.png
/// (or .tga/.jpg) next to the model.
QString materialPicture(const QString& modelFile, const std::string& material, const ImportMesh& mesh) {
	const QDir dir = QFileInfo(modelFile).dir();
	if (const auto it = mesh.textures.find(material); it != mesh.textures.end()) {
		QString path = qs(it->second);
		path.replace('\\', '/');
		const QString candidate = QFileInfo(path).isAbsolute() ? path : dir.filePath(path);
		if (QFileInfo::exists(candidate)) return candidate;
		if (QFileInfo::exists(dir.filePath(QFileInfo(path).fileName()))) return dir.filePath(QFileInfo(path).fileName());
	}
	for (const char* ext : {"png", "tga", "jpg", "jpeg", "bmp"}) {
		const QString candidate = dir.filePath(qs(material) + "." + ext);
		if (QFileInfo::exists(candidate)) return candidate;
	}
	return {};
}

/// A small plain picture of one colour (materials without a picture).
ImageRgba solidPicture(const Vec3& color) {
	ImageRgba image;
	image.width = image.height = 8;
	for (int i = 0; i < 64; ++i) {
		image.pixels.insert(image.pixels.end(), {std::byte(std::lround(color.x * 255)), std::byte(std::lround(color.y * 255)), std::byte(std::lround(color.z * 255)),
		                                         std::byte(255)});
	}
	return image;
}

void merge(ImportRecord& into, const ImportRecord& from) {
	into.created.insert(into.created.end(), from.created.begin(), from.created.end());
	into.replaced.insert(into.replaced.end(), from.replaced.begin(), from.replaced.end());
	into.entries.insert(into.entries.end(), from.entries.begin(), from.entries.end());
}

} // namespace

ImportDialog::ImportDialog(Editor* editor, Kind kind, QWidget* parent) : QDialog(parent), m_editor(editor), m_kind(kind) {
	setObjectName("importDialog");
	static const char* titles[] = {"Import a sound", "Import a picture as a material", "Import a model", "Import a particle file"};
	setWindowTitle(tr(titles[int(kind)]));
	setMinimumWidth(560);
	buildCommon();
}

ImportDialog::~ImportDialog() {
	const bool compiling = busy();
	stopOwnProcesses(this);
	// Closed while studiomdl ran: the model was not made, so what was written for it goes again
	// (as when studiomdl fails) instead of staying in the mod unlisted.
	if (compiling && m_plan) (void)undoImport(m_plan->record);
}

QString ImportDialog::sampleFolder(Kind kind) {
	switch (kind) {
		case Kind::Sound: return samplesFolder(SAMPLE_SOUNDS);
		case Kind::Material: return samplesFolder(SAMPLE_PICTURES);
		case Kind::Model: return samplesFolder(SAMPLE_MODELS);
		case Kind::Particles: return samplesFolder(SAMPLE_PARTICLES);
	}
	return {};
}

QString ImportDialog::fileFilter(Kind kind) {
	switch (kind) {
		case Kind::Sound: return tr("Sounds (*.wav *.mp3 *.ogg *.flac)");
		case Kind::Material: return tr("Pictures (*.png *.jpg *.jpeg *.tga *.bmp *.psd *.gif)");
		case Kind::Model: return tr("Models (*.obj *.smd)");
		case Kind::Particles: return tr("Particle files (*.pcf)");
	}
	return {};
}

QString ImportDialog::studiomdlPath(Editor* editor) {
	QSettings settings;
	settings.beginGroup("games/" + QString(editor->gameKey()).replace('/', '_'));
	const QString chosen = settings.value("studiomdl").toString();
	if (!chosen.isEmpty()) return chosen;
	const QString vbsp = editor->gameSettings().vbsp;
	if (vbsp.isEmpty()) return {};
	const QDir bin = QFileInfo(vbsp).dir();
	for (const char* exe : {"studiomdl.exe", "studiomdl"}) {
		if (QFileInfo::exists(bin.filePath(exe))) return bin.filePath(exe);
	}
	return bin.filePath("studiomdl.exe");
}

void ImportDialog::buildCommon() {
	auto* v = new QVBoxLayout(this);
	auto* fileRow = new QHBoxLayout;
	m_file = new QLabel(tr("No file chosen yet."));
	m_file->setObjectName("importFile");
	m_file->setWordWrap(true);
	m_file->setTextInteractionFlags(Qt::TextSelectableByMouse);
	auto* choose = new QPushButton(tr("Choose file ..."));
	choose->setObjectName("importChoose");
	// Files to try it with: the samples Mapsmith ships (samples/ next to the program).
	auto* sample = new QPushButton(tr("Use a sample ..."));
	sample->setObjectName("importSample");
	const QString samples = sampleFolder(m_kind);
	sample->setEnabled(!samples.isEmpty());
	sample->setToolTip(samples.isEmpty() ? tr("No samples folder next to Mapsmith") : tr("Pick one of Mapsmith's sample files (%1) to try the import").arg(QDir::toNativeSeparators(samples)));
	fileRow->addWidget(m_file, 1);
	fileRow->addWidget(choose);
	fileRow->addWidget(sample);
	v->addLayout(fileRow);
	connect(choose, &QPushButton::clicked, this, [this] {
		const QString file = QFileDialog::getOpenFileName(this, windowTitle(), QString(), fileFilter(m_kind));
		if (!file.isEmpty()) setSourceFile(file);
	});
	connect(sample, &QPushButton::clicked, this, [this, samples] {
		const QString file = QFileDialog::getOpenFileName(this, tr("Sample to import"), samples, fileFilter(m_kind));
		if (!file.isEmpty()) setSourceFile(file);
	});

	m_preview = new QLabel;
	m_preview->setObjectName("importPreview");
	m_preview->setWordWrap(true);
	m_preview->setAlignment(Qt::AlignCenter);
	m_preview->setMinimumHeight(m_kind == Kind::Material ? 140 : 40);
	m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
	v->addWidget(m_preview);

	auto* form = new QFormLayout;
	m_folder = new QLineEdit(QStringLiteral("custom"));
	m_folder->setObjectName("importFolder");
	m_name = new QLineEdit;
	m_name->setObjectName("importName");
	static const char* folderHelp[] = {"Below sound/ of the mod", "Below materials/ of the mod", "Below models/ of the mod (its materials below materials/models/)",
	                                   "particles/ of the mod"};
	m_folder->setToolTip(tr(folderHelp[int(m_kind)]));
	if (m_kind != Kind::Particles) form->addRow(tr("Folder"), m_folder);
	form->addRow(tr("Name"), m_name);
	switch (m_kind) {
		case Kind::Sound: buildSound(form); break;
		case Kind::Material: buildMaterial(form); break;
		case Kind::Model: buildModel(form); break;
		case Kind::Particles: buildParticles(form); break;
	}
	m_target = new QLabel;
	m_target->setObjectName("importTarget");
	m_target->setWordWrap(true);
	m_target->setTextInteractionFlags(Qt::TextSelectableByMouse);
	form->addRow(tr("Goes to"), m_target);
	m_replace = new QCheckBox(tr("Replace the existing files (the old ones come back with Undo)"));
	m_replace->setObjectName("importReplace");
	m_replace->setVisible(false);
	form->addRow(QString(), m_replace);
	v->addLayout(form);

	m_progress = new QProgressBar;
	m_progress->setRange(0, 0);
	m_progress->setVisible(false);
	v->addWidget(m_progress);
	m_result = new QLabel;
	m_result->setObjectName("importResult");
	m_result->setWordWrap(true);
	m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
	v->addWidget(m_result);
	m_details = new QPlainTextEdit;
	m_details->setObjectName("importDetails");
	m_details->setReadOnly(true);
	m_details->setVisible(false);
	m_details->setMinimumHeight(120);
	v->addWidget(m_details);

	auto* buttons = new QHBoxLayout;
	m_undo = new QPushButton(tr("Undo import"));
	m_undo->setObjectName("importUndo");
	m_undo->setEnabled(false);
	m_import = new QPushButton(tr("Import"));
	m_import->setObjectName("importNow");
	m_import->setProperty("accent", true);
	m_import->setDefault(true);
	auto* close = new QPushButton(tr("Close"));
	buttons->addWidget(m_undo);
	buttons->addStretch(1);
	buttons->addWidget(m_import);
	buttons->addWidget(close);
	v->addLayout(buttons);
	connect(m_import, &QPushButton::clicked, this, &ImportDialog::importNow);
	connect(m_undo, &QPushButton::clicked, this, &ImportDialog::undoLast);
	connect(close, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_folder, &QLineEdit::textChanged, this, &ImportDialog::updateTarget);
	connect(m_name, &QLineEdit::textChanged, this, &ImportDialog::updateTarget);
	connect(m_replace, &QCheckBox::toggled, this, &ImportDialog::updateTarget);
	updateTarget();
}

void ImportDialog::buildSound(QFormLayout* form) {
	m_rate = new QComboBox;
	m_rate->setObjectName("importRate");
	m_rate->addItem(tr("44100 Hz (best)"), 44100);
	m_rate->addItem(tr("22050 Hz (half the size, fine for most effects)"), 22050);
	m_rate->addItem(tr("11025 Hz (radio, old recordings)"), 11025);
	m_rate->addItem(tr("Keep the file's rate if the game plays it"), 0);
	m_channels = new QComboBox;
	m_channels->setObjectName("importChannels");
	m_channels->addItem(tr("Mono - for sounds in the world (they come from a place)"), int(SoundImportOptions::Channels::Mono));
	m_channels->addItem(tr("As the file is"), int(SoundImportOptions::Channels::Keep));
	m_channels->addItem(tr("Stereo - music and sounds that are everywhere"), int(SoundImportOptions::Channels::Stereo));
	m_normalize = new QCheckBox(tr("Make it as loud as possible without distortion (normalize)"));
	m_loop = new QCheckBox(tr("Loops in the game (for ambient_generic hums, machines, rain)"));
	m_loop->setObjectName("importLoop");
	m_script = new QCheckBox(tr("Also add a sound script entry (a name maps and soundscapes can play)"));
	m_script->setObjectName("importScript");
	m_scriptName = new QLineEdit;
	m_scriptName->setObjectName("importScriptName");
	m_volume = new QDoubleSpinBox;
	m_volume->setRange(0.05, 1.0);
	m_volume->setSingleStep(0.05);
	m_volume->setValue(1.0);
	m_pitch = new QSpinBox;
	m_pitch->setRange(30, 250);
	m_pitch->setValue(100);
	m_pitch->setSuffix(QStringLiteral(" %"));
	m_soundLevel = new QComboBox;
	for (const char* level : {"SNDLVL_NORM", "SNDLVL_IDLE", "SNDLVL_TALKING", "SNDLVL_STATIC", "SNDLVL_GUNFIRE", "SNDLVL_NONE"}) m_soundLevel->addItem(level);
	m_soundLevel->setToolTip(tr("How far it is heard: NORM 75 dB, IDLE 60 dB, TALKING 80 dB, STATIC 66 dB, GUNFIRE 140 dB, NONE everywhere"));
	m_play = new QPushButton(tr("▶ Listen"));
	m_play->setEnabled(false);
	form->addRow(tr("Quality"), m_rate);
	form->addRow(tr("Channels"), m_channels);
	form->addRow(QString(), m_normalize);
	form->addRow(QString(), m_loop);
	form->addRow(QString(), m_script);
	form->addRow(tr("Script name"), m_scriptName);
	form->addRow(tr("Volume"), m_volume);
	form->addRow(tr("Pitch"), m_pitch);
	form->addRow(tr("Heard"), m_soundLevel);
	form->addRow(QString(), m_play);
	if (!SoundPlayer::available()) m_play->hide();   // after it has its parent (else it would flash up as a window)
	const auto scriptFields = [this] {
		for (QWidget* w : std::initializer_list<QWidget*>{m_scriptName, m_volume, m_pitch, m_soundLevel}) w->setEnabled(m_script->isChecked());
		updateTarget();
	};
	connect(m_script, &QCheckBox::toggled, this, scriptFields);
	connect(m_scriptName, &QLineEdit::textChanged, this, &ImportDialog::updateTarget);
	scriptFields();
	connect(m_play, &QPushButton::clicked, this, [this] {
		if (!m_audio) return;
		if (!m_player) m_player = new SoundPlayer(this);
		if (m_player->playing()) {
			m_player->stop();
			return;
		}
		m_player->setLoop(false);
		m_player->play(std::make_shared<const DecodedAudio>(prepareImportedSound(*m_audio, soundOptions())));
	});
}

void ImportDialog::buildMaterial(QFormLayout* form) {
	m_use = new QComboBox;
	m_use->setObjectName("importUse");
	m_use->addItem(tr("Brushes (walls, floors) - LightmappedGeneric"), int(MaterialImportOptions::Use::Brush));
	m_use->addItem(tr("Models - VertexLitGeneric"), int(MaterialImportOptions::Use::Model));
	m_surface = new QComboBox;
	m_surface->setObjectName("importSurface");
	m_surface->setEditable(true);
	for (const auto& prop : commonSurfaceProps()) m_surface->addItem(qs(prop));
	m_surface->setToolTip(tr("What it is made of: footsteps, bullet holes and impact sounds"));
	m_alpha = new QComboBox;
	m_alpha->setObjectName("importAlpha");
	m_alpha->addItem(tr("From the picture"), int(MaterialImportOptions::Alpha::Auto));
	m_alpha->addItem(tr("Solid"), int(MaterialImportOptions::Alpha::None));
	m_alpha->addItem(tr("Cut-out (fences, leaves: $alphatest)"), int(MaterialImportOptions::Alpha::AlphaTest));
	m_alpha->addItem(tr("See-through (glass: $translucent)"), int(MaterialImportOptions::Alpha::Translucent));
	m_maxSize = new QComboBox;
	for (const int size : {512, 1024, 2048, 4096}) m_maxSize->addItem(QStringLiteral("%1 px").arg(size), size);
	m_maxSize->setCurrentIndex(2);
	m_maxSize->setToolTip(tr("The game needs sizes like 256, 512, 1024: the picture is scaled to the nearest one, at most this"));
	m_compress = new QCheckBox(tr("Compress (DXT: 4-8x smaller, slightly blurry)"));
	m_compress->setChecked(true);
	m_normalFile = new QLabel(tr("none (a picture named like it with _normal next to it is taken)"));
	m_normalFile->setObjectName("importNormal");
	m_normalFile->setWordWrap(true);
	form->addRow(tr("For"), m_use);
	form->addRow(tr("Surface"), m_surface);
	form->addRow(tr("Transparency"), m_alpha);
	form->addRow(tr("Size"), m_maxSize);
	form->addRow(QString(), m_compress);
	form->addRow(tr("Normal map"), m_normalFile);
	connect(m_use, &QComboBox::currentIndexChanged, this, &ImportDialog::updateTarget);
}

void ImportDialog::buildModel(QFormLayout* form) {
	m_modelKind = new QComboBox;
	m_modelKind->setObjectName("importModelKind");
	m_modelKind->addItem(tr("Static prop - part of the world, never moves (prop_static)"), int(ModelImportOptions::Kind::Static));
	m_modelKind->addItem(tr("Physics prop - falls, can be picked up (prop_physics)"), int(ModelImportOptions::Kind::Physics));
	m_modelKind->addItem(tr("Dynamic prop - moves by inputs (prop_dynamic)"), int(ModelImportOptions::Kind::Dynamic));
	m_units = new QComboBox;
	m_units->setObjectName("importUnits");
	m_units->addItem(tr("Metres (Blender, most programs)"), 39.37);
	m_units->addItem(tr("Centimetres"), 0.3937);
	m_units->addItem(tr("Inches = game units (Source tools)"), 1.0);
	m_yUp = new QCheckBox(tr("Y is up in the file (most programs; Source tools: off)"));
	m_yUp->setChecked(true);
	m_surface = new QComboBox;
	m_surface->setObjectName("importSurface");
	m_surface->setEditable(true);
	for (const auto& prop : commonSurfaceProps()) m_surface->addItem(qs(prop));
	m_collision = new QComboBox;
	m_collision->setObjectName("importCollision");
	m_collision->addItem(tr("Wrapped - one solid shape around it (best for most props)"), int(ModelImportOptions::Collision::Hull));
	m_collision->addItem(tr("Box around it"), int(ModelImportOptions::Collision::Box));
	m_collision->addItem(tr("Its separate parts (detailed, e.g. a table: top and legs)"), int(ModelImportOptions::Collision::Parts));
	m_mass = new QDoubleSpinBox;
	m_mass->setRange(0, 50000);
	m_mass->setDecimals(1);
	m_mass->setSuffix(QStringLiteral(" kg"));
	m_mass->setSpecialValueText(tr("from its size"));
	m_materials = new QListWidget;
	m_materials->setObjectName("importMaterials");
	m_materials->setMaximumHeight(110);
	form->addRow(tr("Kind"), m_modelKind);
	form->addRow(tr("Units"), m_units);
	form->addRow(QString(), m_yUp);
	form->addRow(tr("Surface"), m_surface);
	form->addRow(tr("Collision"), m_collision);
	form->addRow(tr("Mass"), m_mass);
	form->addRow(tr("Materials"), m_materials);
	for (auto* box : {m_modelKind, m_units, m_collision}) connect(box, &QComboBox::currentIndexChanged, this, &ImportDialog::updateTarget);
	connect(m_yUp, &QCheckBox::toggled, this, &ImportDialog::updateTarget);
}

void ImportDialog::buildParticles(QFormLayout* form) {
	m_precache = new QCheckBox(tr("Load with every map (needed for effects started by code; maps find their own)"));
	m_precache->setObjectName("importPrecache");
	m_precache->setChecked(true);
	m_systems = new QListWidget;
	m_systems->setObjectName("importSystems");
	m_systems->setMaximumHeight(120);
	form->addRow(QString(), m_precache);
	form->addRow(tr("Systems in it"), m_systems);
}

fs::path ImportDialog::modDir() const {
	const auto* entry = m_editor->gameEntry();
	return entry ? entry->gameDir : fs::path();
}

SoundImportOptions ImportDialog::soundOptions() const {
	SoundImportOptions o;
	o.folder = m_folder->text().toStdString();
	o.name = m_name->text().toStdString();
	o.sampleRate = m_rate->currentData().toInt();
	o.channels = SoundImportOptions::Channels(m_channels->currentData().toInt());
	o.normalize = m_normalize->isChecked();
	o.loop = m_loop->isChecked();
	o.script = m_script->isChecked();
	o.scriptName = m_scriptName->text().trimmed().toStdString();
	o.volume = m_volume->value();
	o.pitch = m_pitch->value();
	o.soundLevel = m_soundLevel->currentText().toStdString();
	return o;
}

MaterialImportOptions ImportDialog::materialOptions() const {
	MaterialImportOptions o;
	o.folder = m_folder->text().toStdString();
	o.name = m_name->text().toStdString();
	o.use = MaterialImportOptions::Use(m_use->currentData().toInt());
	o.surfaceProp = m_surface->currentText().trimmed().toStdString();
	o.alpha = MaterialImportOptions::Alpha(m_alpha->currentData().toInt());
	o.maxSize = unsigned(m_maxSize->currentData().toInt());
	o.compress = m_compress->isChecked();
	return o;
}

ModelImportOptions ImportDialog::modelOptions() const {
	ModelImportOptions o;
	o.folder = m_folder->text().toStdString();
	o.name = m_name->text().toStdString();
	o.kind = ModelImportOptions::Kind(m_modelKind->currentData().toInt());
	o.scale = m_units->currentData().toDouble();
	o.yUp = m_yUp->isChecked();
	o.surfaceProp = m_surface->currentText().trimmed().toStdString();
	o.collision = ModelImportOptions::Collision(m_collision->currentData().toInt());
	o.mass = m_mass->value();
	return o;
}

bool ImportDialog::setSourceFile(const QString& path) {
	m_source.clear();
	m_audio.reset();
	m_image.reset();
	m_normalImage.reset();
	m_mesh.reset();
	m_pcf.clear();
	m_file->setText(QDir::toNativeSeparators(path));
	m_preview->setPixmap(QPixmap());
	const auto fail = [this](const QString& why) {
		m_preview->setText(QStringLiteral("⚠ ") + why);
		updateTarget();
		return false;
	};
	const auto data = readBytes(path);
	if (!data) return fail(tr("The file cannot be read."));
	std::string error;
	const QString suffix = QFileInfo(path).suffix().toLower();
	switch (m_kind) {
		case Kind::Sound: {
			const auto audio = decodeAudio(*data, path.toStdString(), &error);
			if (!audio) return fail(tr("Not a sound Mapsmith can read (%1).").arg(qs(error)));
			m_audio = std::make_shared<DecodedAudio>(*audio);
			m_preview->setText(tr("%1, %2 Hz, %3, %4 s").arg(qs(audio->format)).arg(audio->sampleRate)
			                       .arg(audio->channels == 1 ? tr("mono") : tr("stereo")).arg(audio->seconds(), 0, 'f', 2));
			m_loop->setChecked(audio->loopStart.has_value());
			m_play->setEnabled(true);
			break;
		}
		case Kind::Material: {
			auto image = decodeImageFile(*data, &error);
			if (!image) return fail(tr("Not a picture Mapsmith can read (PNG, JPG, TGA, BMP)."));
			m_image = std::move(image);
			m_preview->setPixmap(QPixmap::fromImage(toQImage(*m_image)).scaled(220, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation));
			const QString normal = findSibling(path, {"_normal", "_n", "_nrm", "_bump", "_normalmap"});
			m_normalFile->setText(tr("none (a picture named like it with _normal next to it is taken)"));
			if (!normal.isEmpty()) {
				if (const auto bytes = readBytes(normal)) {
					if (auto image2 = decodeImageFile(*bytes)) {
						m_normalImage = std::move(image2);
						m_normalFile->setText(QFileInfo(normal).fileName());
					}
				}
			}
			const auto alpha = imageAlpha(*m_image);
			m_preview->setToolTip(tr("%1 × %2 px%3").arg(m_image->width).arg(m_image->height)
			                          .arg(alpha == ImageAlpha::Opaque ? QString() : (alpha == ImageAlpha::Binary ? tr(", with cut-out parts") : tr(", see-through"))));
			break;
		}
		case Kind::Model: {
			const QString text = readText(path);
			if (suffix == QLatin1String("obj")) {
				const QString library = qs(objMaterialLibrary(text.toStdString()));
				const QString mtl = library.isEmpty() ? QString() : readText(QFileInfo(path).dir().filePath(library));
				m_mesh = readObj(text.toStdString(), mtl.toStdString(), &error);
				m_units->setCurrentIndex(0);
				m_yUp->setChecked(true);
			} else {
				m_mesh = readSmd(text.toStdString(), &error);
				m_units->setCurrentIndex(2);   // SMDs are in game units, Z-up
				m_yUp->setChecked(false);
			}
			if (!m_mesh) return fail(tr("Not a model Mapsmith can read (%1).").arg(qs(error)));
			m_materials->clear();
			for (const auto& material : m_mesh->materials) {
				const QString picture = materialPicture(path, material, *m_mesh);
				auto* item = new QListWidgetItem(picture.isEmpty() ? tr("%1 - no picture found, plain colour").arg(qs(material))
				                                                    : tr("%1 - %2").arg(qs(material), QFileInfo(picture).fileName()),
				                                 m_materials);
				item->setForeground(picture.isEmpty() ? QColor(230, 180, 90) : QColor(150, 210, 150));
			}
			break;
		}
		case Kind::Particles: {
			const auto systems = particleSystemNames(*data);
			if (systems.empty()) return fail(tr("Not a particle file, or it holds no particle systems."));
			m_pcf = *data;
			m_systems->clear();
			for (const auto& name : systems) m_systems->addItem(qs(name));
			m_preview->setText(tr("%n particle system(s)", nullptr, int(systems.size())));
			break;
		}
	}
	m_source = path;
	m_name->setText(qs(importName(QFileInfo(path).completeBaseName().toStdString())));
	if (m_kind == Kind::Sound) {
		QString group = qs(importName(splitTrimmed(importFolder(m_folder->text().toStdString()), '/').empty()
		                                  ? std::string("custom")
		                                  : splitTrimmed(importFolder(m_folder->text().toStdString()), '/').back()));
		if (!group.isEmpty()) group[0] = group[0].toUpper();
		m_scriptName->setText(group + QLatin1Char('.') + m_name->text());
	}
	m_result->clear();
	updateTarget();
	return true;
}

std::vector<fs::path> ImportDialog::targets() const {
	const fs::path mod = modDir();
	if (mod.empty()) return {};
	switch (m_kind) {
		case Kind::Sound: return {soundImportTarget(mod, soundOptions())};
		case Kind::Material: return materialImportTargets(mod, materialOptions(), m_normalImage.has_value());
		case Kind::Model: {
			auto files = modelOutputFiles(mod, modelOptions());
			files.resize(1);   // the .mdl stands for all of them
			return files;
		}
		case Kind::Particles: return {particleImportTarget(mod, m_name->text().toStdString())};
	}
	return {};
}

void ImportDialog::updateTarget() {
	if (!m_target || !m_import) return;   // still being built
	const fs::path mod = modDir();
	const bool named = !importName(m_name->text().toStdString()).empty();
	QStringList shown;
	bool exists = false;
	for (const auto& file : targets()) {
		std::error_code ec;
		const auto relative = fs::relative(file, mod, ec);
		shown << QDir::fromNativeSeparators(fromPath(ec ? file : relative));
		exists = exists || fs::exists(file, ec);
	}
	if (m_kind == Kind::Sound && m_script && m_script->isChecked() && !mod.empty()) {
		QFile scripts(fromPath(importSoundScriptFile(mod)));
		if (scripts.open(QIODevice::ReadOnly) && scripts.readAll().contains(("\"" + m_scriptName->text().trimmed() + "\"").toUtf8())) exists = true;
		shown << tr("sound script %1").arg(m_scriptName->text().trimmed());
	}
	if (mod.empty()) {
		m_target->setText(tr("Open a game or mod first (the box at the top left)."));
	} else if (!named) {
		m_target->setText(tr("Give it a name (letters, digits, _ and -)."));
	} else {
		m_target->setText(shown.join(QStringLiteral("\n")));
	}
	if (m_kind == Kind::Model && m_mesh) {
		// The size it will have in the map, so wrong units show before importing.
		const Bounds box = placedMesh(*m_mesh, modelOptions()).bounds();
		const Vec3 size = box.empty() ? Vec3{} : box.max - box.min;
		m_preview->setText(tr("%n triangle(s), %1 material(s). In the map: %2 × %3 × %4 units (a door is about 56 × 108, a player 72 high).", nullptr,
		                      int(m_mesh->triangles.size()))
		                       .arg(m_mesh->materials.size())
		                       .arg(size.x, 0, 'f', 0)
		                       .arg(size.y, 0, 'f', 0)
		                       .arg(size.z, 0, 'f', 0));
	}
	m_replace->setVisible(exists && named);
	if (exists && named) m_replace->setText(tr("Replace what is there already (the old files come back with Undo)"));
	const bool loaded = !m_source.isEmpty();
	m_import->setEnabled(loaded && named && !mod.empty() && (!exists || m_replace->isChecked()) && !busy());
	m_import->setToolTip(exists && !m_replace->isChecked() ? tr("Something with this name is there already - tick Replace or choose another name") : QString());
}

void ImportDialog::showResult(const QString& text, bool ok) {
	m_result->setText((ok ? QStringLiteral("✔ ") : QStringLiteral("⚠ ")) + text);
	m_result->setStyleSheet(ok ? QStringLiteral("color: #8fdc9a;") : QStringLiteral("color: #ff8a7a;"));
}

void ImportDialog::finishImport(const ImportRecord& record, const QString& text) {
	m_last = record;
	rememberImport(modDir(), record);
	showResult(text, true);
	m_undo->setEnabled(true);
	m_replace->setChecked(false);
	updateTarget();
	emit modChanged();
	emit finished(true);
}

bool ImportDialog::importNow() {
	if (!m_import->isEnabled()) return false;
	const fs::path mod = modDir();
	const bool overwrite = m_replace->isChecked();
	std::string error;
	switch (m_kind) {
		case Kind::Sound: {
			if (!m_audio) return false;
			const auto* game = m_editor->game();
			const std::string manifest = game ? game->files.readText("scripts/game_sounds_manifest.txt").value_or(std::string()) : std::string();
			const auto options = soundOptions();
			const auto record = importSound(mod, *m_audio, options, manifest, overwrite, &error);
			if (!record) break;
			const QString wave = qs(importFolder(options.folder)) + (importFolder(options.folder).empty() ? "" : "/") + qs(importName(options.name)) + ".wav";
			finishImport(*record, options.script ? tr("Imported sound/%1 - use the name %2 in ambient_generic and soundscapes").arg(wave, qs(options.scriptName))
			                                     : tr("Imported sound/%1 - use %1 in ambient_generic").arg(wave));
			return true;
		}
		case Kind::Material: {
			if (!m_image) return false;
			const auto options = materialOptions();
			const auto record = importMaterial(mod, *m_image, m_normalImage ? &*m_normalImage : nullptr, options, overwrite, &error);
			if (!record) break;
			finishImport(*record, tr("Imported: %1 - find it in the Materials page and the Asset Browser").arg(qs(record->asset)));
			return true;
		}
		case Kind::Model: return importModel();
		case Kind::Particles: {
			if (m_pcf.empty()) return false;
			const auto* game = m_editor->game();
			const std::string manifest = game ? game->files.readText("particles/particles_manifest.txt").value_or(std::string()) : std::string();
			const auto record = importParticles(mod, m_pcf, m_name->text().toStdString(), manifest, m_precache->isChecked(), overwrite, &error);
			if (!record) break;
			finishImport(*record, tr("Imported: %1 - its systems are in the Particles page and info_particle_system").arg(qs(record->asset)));
			return true;
		}
	}
	showResult(tr("Not imported: %1").arg(qs(error)), false);
	emit finished(false);
	return false;
}

bool ImportDialog::importModel() {
	if (!m_mesh) return false;
	const fs::path mod = modDir();
	const auto options = modelOptions();
	const QString studiomdl = studiomdlPath(m_editor);
	if (studiomdl.isEmpty() || !QFileInfo::exists(studiomdl)) {
		showResult(tr("studiomdl (the game's model compiler) was not found: %1. It comes with Source SDK Base 2013 (its bin folder) - choose it with "
		              "\"studiomdl ...\".")
		               .arg(QDir::toNativeSeparators(studiomdl)),
		           false);
		auto* pick = findChild<QPushButton*>("importPickStudiomdl");
		if (!pick) {
			pick = new QPushButton(tr("studiomdl ..."), this);
			pick->setObjectName("importPickStudiomdl");
			layout()->addWidget(pick);
			connect(pick, &QPushButton::clicked, this, [this] {
				const QString file = QFileDialog::getOpenFileName(this, tr("studiomdl.exe of the game"), QString(), tr("studiomdl (studiomdl*)"));
				if (file.isEmpty()) return;
				QSettings settings;
				settings.beginGroup("games/" + QString(m_editor->gameKey()).replace('/', '_'));
				settings.setValue("studiomdl", file);
				m_result->setText(tr("studiomdl set - press Import again."));
			});
		}
		emit finished(false);
		return false;
	}
	const bool overwrite = m_replace->isChecked();
	std::string error;
	// The materials first (below materials/models/<folder>/), each from its picture or colour;
	// ones that are there already are used as they are unless Replace is ticked.
	ImportRecord materials{"model", modelImportName(options), {}, {}, {}};
	const auto names = modelMaterialNames(*m_mesh);
	for (std::size_t i = 0; i < m_mesh->materials.size(); ++i) {
		const std::string& material = m_mesh->materials[i];
		MaterialImportOptions mo;
		mo.folder = options.folder;
		mo.name = importName(material).empty() ? std::string("default") : importName(material);
		mo.use = MaterialImportOptions::Use::Model;
		mo.surfaceProp = options.surfaceProp;
		std::error_code ec;
		if (!overwrite && fs::exists(materialImportTargets(mod, mo, false).front(), ec)) continue;
		std::optional<ImageRgba> picture;
		const QString file = materialPicture(m_source, material, *m_mesh);
		if (!file.isEmpty()) {
			if (const auto bytes = readBytes(file)) picture = decodeImageFile(*bytes);
		}
		if (!picture) {
			const auto color = m_mesh->colors.find(material);
			picture = solidPicture(color != m_mesh->colors.end() ? color->second : Vec3{0.6, 0.6, 0.6});
		}
		const auto record = importMaterial(mod, *picture, nullptr, mo, overwrite, &error);
		if (!record) {
			undoImport(materials);
			showResult(tr("Not imported: material %1: %2").arg(qs(material), qs(error)), false);
			emit finished(false);
			return false;
		}
		merge(materials, *record);
	}
	auto plan = prepareModelImport(mod, *m_mesh, options, overwrite, &error);
	if (!plan) {
		undoImport(materials);
		showResult(tr("Not imported: %1").arg(qs(error)), false);
		emit finished(false);
		return false;
	}
	merge(plan->record, materials);
	m_plan = std::move(plan);
	// studiomdl in the background; its output goes into the details box.
	auto* process = new QProcess(this);
	m_process = process;
	process->setProcessChannelMode(QProcess::MergedChannels);
	process->setWorkingDirectory(fromPath(m_plan->qc.parent_path()));
	m_details->clear();
	m_details->setVisible(true);
	connect(process, &QProcess::readyRead, this, [this, process] { m_details->appendPlainText(QString::fromLocal8Bit(process->readAll()).trimmed()); });
	connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
		m_details->appendPlainText(QString::fromLocal8Bit(process->readAll()).trimmed());
		process->deleteLater();
		modelCompiled(status == QProcess::NormalExit ? code : -1);
	});
	connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError e) {
		if (e != QProcess::FailedToStart) return;
		process->deleteLater();
		m_details->appendPlainText(process->errorString());
		modelCompiled(-1);
	});
	QStringList arguments;
	for (const auto& a : m_plan->arguments) arguments << qs(a);
	m_progress->setVisible(true);
	showResult(tr("Compiling %1 with studiomdl ...").arg(qs(m_plan->record.asset)), true);
	m_result->setStyleSheet(QString());
	process->start(studiomdl, arguments);
	updateTarget();
	return true;
}

void ImportDialog::modelCompiled(int exitCode) {
	m_progress->setVisible(false);
	m_process = nullptr;
	if (!m_plan) return;
	std::string error;
	const auto options = modelOptions();
	const bool ok = collectModelOutputs(*m_plan, modDir(), options, &error);
	if (!ok) {
		const auto problems = studiomdlProblems(m_details->toPlainText().toStdString());
		QString why = problems.empty() ? qs(error) : qs(problems.front());
		undoImport(m_plan->record);
		m_plan.reset();
		showResult(tr("studiomdl could not make the model (exit code %1): %2 - its full output is below.").arg(exitCode).arg(why), false);
		updateTarget();
		emit finished(false);
		return;
	}
	const ImportRecord record = m_plan->record;
	m_plan.reset();
	finishImport(record, tr("Imported: %1 - find it in the Models page (Place as %2)")
	                         .arg(qs(record.asset),
	                              options.kind == ModelImportOptions::Kind::Static ? QStringLiteral("prop_static")
	                                                                               : (options.kind == ModelImportOptions::Kind::Physics ? QStringLiteral("prop_physics")
	                                                                                                                                    : QStringLiteral("prop_dynamic"))));
}

bool ImportDialog::undoLast() {
	if (!m_last) return false;
	std::string error;
	const bool ok = undoImport(*m_last, &error);
	forgetImport(modDir(), *m_last);
	showResult(ok ? tr("Taken back: %1").arg(qs(m_last->asset)) : tr("Partly taken back: %1").arg(qs(error)), ok);
	m_last.reset();
	m_undo->setEnabled(false);
	updateTarget();
	emit modChanged();
	return ok;
}

QString ImportDialog::resultText() const {
	return m_result->text();
}

bool ImportDialog::busy() const {
	return m_process && m_process->state() != QProcess::NotRunning;
}

} // namespace sme::app
