// glow_dialog.cpp - Making parts of a model glow (see glow_dialog.h).

#include "glow_dialog.h"

#include "dialog_fit.h"
#include "editor.h"
#include "model_viewer.h"

#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/material.h>
#include <sme/text.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <span>

namespace sme::app {

namespace {

QImage toQImage(const ImageRgba& image) {
	QImage out(image.width, image.height, QImage::Format_RGBA8888);
	if (image.pixels.size() >= std::size_t(image.width) * image.height * 4) {
		for (int y = 0; y < image.height; ++y) std::memcpy(out.scanLine(y), image.pixels.data() + std::size_t(y) * image.width * 4, std::size_t(image.width) * 4);
	}
	return out;
}

void setButtonColor(QPushButton* button, const QColor& color) {
	button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; }")
	                          .arg(color.name(), color.lightness() > 140 ? QStringLiteral("#202020") : QStringLiteral("#f0f0f0")));
}

} // namespace

GlowDialog::GlowDialog(Editor* editor, ModelViewer* viewer, QString model, QStringList materials,
                       std::vector<std::pair<std::string, std::string>> animations, QWidget* parent)
	: QDialog(parent), m_editor(editor), m_viewer(viewer), m_model(std::move(model)), m_animations(std::move(animations)) {
	setObjectName("glowDialog");
	setWindowTitle(tr("Glow - %1").arg(m_model));
	setAttribute(Qt::WA_DeleteOnClose);
	auto* v = new QVBoxLayout(this);
	auto* intro = new QLabel(tr("Parts of the material light up like lamps, screens or sights - in the texture's own colour, tinted. "
	                            "With mapbase-fidelity a steady glow also lights the surroundings."));
	intro->setWordWrap(true);
	intro->setProperty("role", "description");
	v->addWidget(intro);

	auto* form = new QFormLayout;
	m_material = new QComboBox;
	m_material->setObjectName("glowMaterial");
	m_material->addItems(materials);
	form->addRow(tr("Material"), m_material);

	// Where it glows.
	auto* where = new QWidget;
	auto* wv = new QVBoxLayout(where);
	wv->setContentsMargins(0, 0, 0, 0);
	m_auto = new QRadioButton(tr("Bright, colourful parts (found automatically)"));
	m_auto->setObjectName("glowAuto");
	m_auto->setChecked(true);
	auto* thresholdRow = new QHBoxLayout;
	thresholdRow->addSpacing(20);
	thresholdRow->addWidget(new QLabel(tr("Fewer")));
	m_threshold = new QSlider(Qt::Horizontal);
	m_threshold->setObjectName("glowThreshold");
	m_threshold->setRange(0, 100);
	m_threshold->setValue(40);   // threshold 0.6
	m_threshold->setToolTip(tr("How bright a part must be to glow"));
	thresholdRow->addWidget(m_threshold, 1);
	thresholdRow->addWidget(new QLabel(tr("More")));
	m_picture = new QRadioButton(tr("Where a picture is white"));
	m_picture->setObjectName("glowPicture");
	m_choosePicture = new QPushButton(tr("Choose picture ..."));
	m_choosePicture->setToolTip(tr("A black and white picture laid over the texture: white glows. Paint it over the texture in any paint program."));
	auto* pictureRow = new QHBoxLayout;
	pictureRow->addWidget(m_picture);
	pictureRow->addWidget(m_choosePicture);
	pictureRow->addStretch(1);
	m_whole = new QRadioButton(tr("The whole material"));
	m_whole->setObjectName("glowWhole");
	auto* whereGroup = new QButtonGroup(this);
	whereGroup->addButton(m_auto);
	whereGroup->addButton(m_picture);
	whereGroup->addButton(m_whole);
	wv->addWidget(m_auto);
	wv->addLayout(thresholdRow);
	wv->addLayout(pictureRow);
	wv->addWidget(m_whole);
	form->addRow(tr("Where"), where);

	// Look.
	m_color = new QPushButton(tr("White"));
	m_color->setObjectName("glowColor");
	setButtonColor(m_color, m_colorValue);
	form->addRow(tr("Colour"), m_color);
	m_strength = new QSlider(Qt::Horizontal);
	m_strength->setObjectName("glowStrength");
	m_strength->setRange(10, 400);
	m_strength->setValue(100);
	m_strength->setToolTip(tr("100 % = the texture's colour at full brightness; more glows brighter than the texture"));
	form->addRow(tr("Brightness"), m_strength);
	auto* pulseRow = new QHBoxLayout;
	m_pulse = new QCheckBox(tr("Pulse, every"));
	m_pulse->setObjectName("glowPulse");
	m_pulseSeconds = new QDoubleSpinBox;
	m_pulseSeconds->setRange(0.1, 10);
	m_pulseSeconds->setValue(1.0);
	m_pulseSeconds->setSuffix(tr(" s"));
	pulseRow->addWidget(m_pulse);
	pulseRow->addWidget(m_pulseSeconds);
	pulseRow->addStretch(1);
	form->addRow(QString(), pulseRow);

	// When.
	m_always = new QRadioButton(tr("Always"));
	m_always->setObjectName("glowAlways");
	m_always->setChecked(true);
	m_during = new QRadioButton(tr("Only during these animations:"));
	m_during->setObjectName("glowDuring");
	auto* whenGroup = new QButtonGroup(this);
	whenGroup->addButton(m_always);
	whenGroup->addButton(m_during);
	auto* when = new QWidget;
	auto* whenLayout = new QVBoxLayout(when);
	whenLayout->setContentsMargins(0, 0, 0, 0);
	whenLayout->addWidget(m_always);
	whenLayout->addWidget(m_during);
	m_animationList = new QListWidget;
	m_animationList->setObjectName("glowAnimations");
	m_animationList->setMaximumHeight(120);
	for (const auto& [sequence, activity] : m_animations) {
		auto* item = new QListWidgetItem(activity.empty() ? QString::fromStdString(sequence)
		                                                  : QStringLiteral("%1   (%2)").arg(QString::fromStdString(sequence), QString::fromStdString(activity)),
		                                 m_animationList);
		item->setData(Qt::UserRole, QString::fromStdString(activity.empty() ? sequence : activity));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
	}
	whenLayout->addWidget(m_animationList);
	auto* rangeRow = new QHBoxLayout;
	rangeRow->addWidget(new QLabel(tr("From")));
	m_from = new QDoubleSpinBox;
	m_from->setRange(0, 100);
	m_from->setSuffix(QStringLiteral(" %"));
	m_to = new QDoubleSpinBox;
	m_to->setRange(0, 100);
	m_to->setValue(100);
	m_to->setSuffix(QStringLiteral(" %"));
	rangeRow->addWidget(m_from);
	rangeRow->addWidget(new QLabel(tr("to")));
	rangeRow->addWidget(m_to);
	rangeRow->addWidget(new QLabel(tr("of the animation")));
	rangeRow->addStretch(1);
	whenLayout->addLayout(rangeRow);
	auto* whenNote = new QLabel(tr("Needs Mapbase (its VScript material proxy). The light of mapbase-fidelity stays with steady glows."));
	whenNote->setWordWrap(true);
	whenNote->setProperty("role", "description");
	whenLayout->addWidget(whenNote);
	form->addRow(tr("When"), when);
	v->addLayout(form);

	m_maskView = new QLabel;
	m_maskView->setObjectName("glowMaskView");
	m_maskView->setFixedHeight(140);
	m_maskView->setAlignment(Qt::AlignCenter);
	v->addWidget(m_maskView);
	m_info = new QLabel;
	m_info->setObjectName("glowInfo");
	m_info->setWordWrap(true);
	v->addWidget(m_info);

	auto* buttons = new QDialogButtonBox;
	m_apply = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
	m_apply->setObjectName("glowApply");
	m_apply->setProperty("accent", true);
	m_restore = buttons->addButton(tr("Restore original"), QDialogButtonBox::ResetRole);
	m_restore->setObjectName("glowRestore");
	auto* close = buttons->addButton(QDialogButtonBox::Close);
	v->addWidget(buttons);
	// Apply and Restore keep the dialog open (Accept would close it).
	disconnect(buttons, nullptr, this, nullptr);
	connect(m_apply, &QPushButton::clicked, this, &GlowDialog::apply);
	connect(m_restore, &QPushButton::clicked, this, &GlowDialog::restore);
	connect(close, &QPushButton::clicked, this, &QDialog::close);

	const auto changed = [this] {
		if (!m_loading) updatePreview();
	};
	connect(m_material, &QComboBox::currentIndexChanged, this, &GlowDialog::loadMaterial);
	for (auto* r : {m_auto, m_picture, m_whole, m_always, m_during}) connect(r, &QRadioButton::toggled, this, changed);
	connect(m_threshold, &QSlider::valueChanged, this, changed);
	connect(m_strength, &QSlider::valueChanged, this, changed);
	connect(m_pulse, &QCheckBox::toggled, this, changed);
	connect(m_animationList, &QListWidget::itemChanged, this, changed);
	connect(m_color, &QPushButton::clicked, this, [this, changed] {
		const QColor c = QColorDialog::getColor(m_colorValue, this, tr("Glow colour"));
		if (!c.isValid()) return;
		m_colorValue = c;
		m_color->setText(c.name());
		setButtonColor(m_color, c);
		changed();
	});
	connect(m_choosePicture, &QPushButton::clicked, this, [this, changed] {
		const QString file = QFileDialog::getOpenFileName(this, tr("Mask picture"), QString(), tr("Pictures (*.png *.jpg *.jpeg *.tga *.bmp)"));
		if (file.isEmpty()) return;
		QFile in(file);
		if (!in.open(QIODevice::ReadOnly) || in.size() > 64 * 1024 * 1024) return;
		const QByteArray data = in.readAll();
		std::string error;
		m_pictureImage = decodeImageFile(std::as_bytes(std::span(data.constData(), std::size_t(data.size()))), &error);
		if (!m_pictureImage) m_info->setText(tr("Cannot read the picture: %1").arg(QString::fromStdString(error)));
		m_picture->setChecked(true);
		changed();
	});
	loadMaterial();
	fitDialogToScreen(this);
}

GlowDialog::~GlowDialog() {
	if (m_viewer) m_viewer->clearGlow();
}

void GlowDialog::selectMaterial(const QString& material) {
	const int index = m_material->findText(material, Qt::MatchFixedString);
	if (index >= 0) m_material->setCurrentIndex(index);
}

GlowSettings GlowDialog::settings() const {
	GlowSettings s;
	s.mask = m_auto->isChecked() ? GlowSettings::Mask::Auto : m_picture->isChecked() ? GlowSettings::Mask::Picture : GlowSettings::Mask::Whole;
	s.threshold = 1.0 - m_threshold->value() / 100.0;
	s.red = m_colorValue.redF();
	s.green = m_colorValue.greenF();
	s.blue = m_colorValue.blueF();
	s.strength = m_strength->value() / 100.0;
	s.pulse = m_pulse->isChecked();
	s.pulseSeconds = m_pulseSeconds->value();
	s.when = m_during->isChecked() ? GlowSettings::When::DuringAnimation : GlowSettings::When::Always;
	for (int i = 0; i < m_animationList->count(); ++i) {
		if (m_animationList->item(i)->checkState() == Qt::Checked) s.animations.push_back(m_animationList->item(i)->data(Qt::UserRole).toString().toStdString());
	}
	s.fromCycle = m_from->value() / 100.0;
	s.toCycle = std::max(m_from->value(), m_to->value()) / 100.0;
	return s;
}

void GlowDialog::setSettings(const GlowSettings& s) {
	m_loading = true;
	(s.mask == GlowSettings::Mask::Auto ? m_auto : s.mask == GlowSettings::Mask::Picture ? m_picture : m_whole)->setChecked(true);
	m_threshold->setValue(int(std::lround((1.0 - s.threshold) * 100)));
	m_colorValue = QColor::fromRgbF(float(s.red), float(s.green), float(s.blue));
	m_color->setText(m_colorValue.name());
	setButtonColor(m_color, m_colorValue);
	m_strength->setValue(int(std::lround(s.strength * 100)));
	m_pulse->setChecked(s.pulse);
	m_pulseSeconds->setValue(s.pulseSeconds);
	(s.when == GlowSettings::When::Always ? m_always : m_during)->setChecked(true);
	for (int i = 0; i < m_animationList->count(); ++i) {
		auto* item = m_animationList->item(i);
		const auto name = item->data(Qt::UserRole).toString().toStdString();
		const bool on = std::any_of(s.animations.begin(), s.animations.end(), [&](const std::string& a) { return toLower(a) == toLower(name); });
		item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
	}
	m_from->setValue(s.fromCycle * 100);
	m_to->setValue(s.toCycle * 100);
	m_loading = false;
	updatePreview();
}

void GlowDialog::loadMaterial() {
	m_base.reset();
	const auto* game = m_editor->game();
	const std::string name = m_material->currentText().toStdString();
	if (game && !name.empty()) {
		if (const auto material = sme::loadMaterial(game->files, name)) {
			const std::string texture = normalizePath(material->param("$basetexture"));
			if (const auto data = texture.empty() ? std::nullopt : game->files.read("materials/" + texture + ".vtf")) m_base = decodeTexture(*data, 1024);
			// A glow Mapsmith wrote before: its settings come back into the fields.
			if (const auto earlier = readGlowSettings(*material)) {
				setSettings(*earlier);
				updateButtons();
				return;
			}
		}
	}
	updatePreview();
	updateButtons();
}

void GlowDialog::updatePreview() {
	const GlowSettings s = settings();
	ImageRgba mask;
	if (m_base) {
		switch (s.mask) {
		case GlowSettings::Mask::Auto: mask = autoGlowMask(*m_base, s.threshold); break;
		case GlowSettings::Mask::Picture: mask = m_pictureImage ? pictureGlowMask(*m_pictureImage, m_base->width, m_base->height) : pictureGlowMask({}, m_base->width, m_base->height); break;
		case GlowSettings::Mask::Whole: mask = pictureGlowMask(ImageRgba{1, 1, {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}}}, m_base->width, m_base->height); break;
		}
	}
	m_coverage = glowCoverage(mask);
	m_maskImage = mask.width ? toQImage(mask) : QImage();
	// The picture: the texture with the glowing parts lit in the glow colour.
	if (m_base) {
		QImage shown = toQImage(*m_base).convertToFormat(QImage::Format_RGB32);
		const QColor tint = m_colorValue;
		for (int y = 0; y < shown.height(); ++y) {
			auto* row = reinterpret_cast<QRgb*>(shown.scanLine(y));
			const uchar* m = m_maskImage.constScanLine(y);
			for (int x = 0; x < shown.width(); ++x) {
				const double a = m[x * 4] / 255.0;
				const QRgb p = row[x];
				const auto mix = [&](int base, double t) { return std::clamp(int(base * 0.45 * (1 - a) + std::min(255.0, base * t * s.strength) * a), 0, 255); };
				row[x] = qRgb(mix(qRed(p), tint.redF()), mix(qGreen(p), tint.greenF()), mix(qBlue(p), tint.blueF()));
			}
		}
		m_maskView->setPixmap(QPixmap::fromImage(shown.scaled(m_maskView->height() * shown.width() / std::max(1, shown.height()), m_maskView->height(),
		                                                       Qt::KeepAspectRatio, Qt::SmoothTransformation)));
		m_info->setText(tr("%1 % of the texture glows.").arg(m_coverage * 100, 0, 'f', 1) +
		                (m_coverage > 0.6 ? QStringLiteral(" ") + tr("That is a lot - move the slider towards \"Fewer\".") : QString()));
	} else {
		m_maskView->clear();
		m_info->setText(tr("The material's texture cannot be read - only model materials with a base texture can glow."));
	}
	if (m_viewer) {
		const QVector3D tint(float(s.red * s.strength), float(s.green * s.strength), float(s.blue * s.strength));
		m_viewer->clearGlow();
		if (!m_maskImage.isNull()) m_viewer->setGlow(m_material->currentText(), m_maskImage, tint);
	}
	updateButtons();
}

std::optional<ImportRecord> GlowDialog::currentRecord() const {
	const auto* entry = m_editor->gameEntry();
	if (!entry) return std::nullopt;
	const std::string name = normalizeMaterialName(m_material->currentText().toStdString());
	std::optional<ImportRecord> found;
	for (const auto& record : loadImports(entry->gameDir)) {
		if (record.kind == "glow" && iequals(record.asset, name)) found = record;   // the newest
	}
	return found;
}

void GlowDialog::updateButtons() {
	const bool haveMod = m_editor->gameEntry() != nullptr;
	m_apply->setEnabled(haveMod && m_base.has_value());
	m_restore->setEnabled(haveMod && currentRecord().has_value());
}

bool GlowDialog::apply() {
	const auto* entry = m_editor->gameEntry();
	const auto* game = m_editor->game();
	if (!entry || !game) return false;
	const auto s = settings();
	// An earlier glow goes first, so the backups keep the mod's own original.
	if (const auto earlier = currentRecord()) {
		(void)undoImport(*earlier);
		(void)forgetImport(entry->gameDir, *earlier);
		for (const auto& file : earlier->created) m_editor->game()->files.noteRemovedFile(file);
		for (const auto& file : earlier->replaced) m_editor->game()->files.noteWrittenFile(file);
	}
	std::string error;
	const auto files = planGlow(game->files, m_material->currentText().toStdString(), s, m_pictureImage ? &*m_pictureImage : nullptr, &error);
	const auto record = files ? applyGlow(entry->gameDir, *files, &error) : std::nullopt;
	if (!record) {
		m_info->setText(tr("Could not apply: %1").arg(QString::fromStdString(error)));
		return false;
	}
	(void)rememberImport(entry->gameDir, *record);
	for (const auto& file : record->created) m_editor->game()->files.noteWrittenFile(file);
	for (const auto& file : record->replaced) m_editor->game()->files.noteWrittenFile(file);
	m_info->setText(tr("Applied: the material in the mod glows now (%1 % of it). Start the map again in the game to see it.").arg(files->coverage * 100, 0, 'f', 1));
	updateButtons();
	emit modChanged();
	return true;
}

bool GlowDialog::restore() {
	const auto* entry = m_editor->gameEntry();
	const auto record = currentRecord();
	if (!entry || !record) return false;
	std::string error;
	if (!undoImport(*record, &error)) {
		m_info->setText(tr("Could not restore everything: %1").arg(QString::fromStdString(error)));
		return false;
	}
	(void)forgetImport(entry->gameDir, *record);
	for (const auto& file : record->created) m_editor->game()->files.noteRemovedFile(file);
	for (const auto& file : record->replaced) m_editor->game()->files.noteWrittenFile(file);
	m_info->setText(tr("The original material is back."));
	if (m_viewer) m_viewer->clearGlow();
	updateButtons();
	emit modChanged();
	return true;
}

} // namespace sme::app
