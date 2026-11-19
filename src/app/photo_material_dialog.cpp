// photo_material_dialog.cpp - "New material from photo ..." (see photo_material_dialog.h).

#include "photo_material_dialog.h"

#include "editor.h"
#include "material_preview.h"
#include "ui_helpers.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>

#include <sme/asset_import.h>
#include <sme/text.h>

namespace sme::app {

namespace {

constexpr int PREVIEW_SIZE = 256;
constexpr int TILE_VIEW = 300;

QSlider* slider(int min, int max, int value, const QString& tip) {
	auto* s = new QSlider(Qt::Horizontal);
	s->setRange(min, max);
	s->setValue(value);
	s->setToolTip(tip);
	return s;
}

/// The picture three times in each direction (tiles meet: seams show at once).
QPixmap tiled(const QImage& image) {
	QPixmap out(TILE_VIEW, TILE_VIEW);
	out.fill(Qt::black);
	if (image.isNull()) return out;
	const QImage tile = image.scaled(TILE_VIEW / 3, TILE_VIEW / 3, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	QPainter painter(&out);
	for (int y = 0; y < 3; ++y) {
		for (int x = 0; x < 3; ++x) painter.drawImage(x * tile.width(), y * tile.height(), tile);
	}
	return out;
}

} // namespace

PhotoMaterialDialog::PhotoMaterialDialog(Editor* editor, QWidget* parent) : QDialog(parent), m_editor(editor) {
	setObjectName("photoMaterialDialog");
	setWindowTitle(tr("New material from photo"));
	setAttribute(Qt::WA_DeleteOnClose);
	m_timer.setSingleShot(true);
	m_timer.setInterval(120);
	connect(&m_timer, &QTimer::timeout, this, &PhotoMaterialDialog::updatePreview);

	auto* layout = new QVBoxLayout(this);
	layout->addWidget(noteLabel(tr("1. Pick a photo  2. Adjust until the 3 x 3 tiles look seamless  3. Create - the material goes into your mod.")));
	auto* top = new QHBoxLayout;
	auto* pick = new QPushButton(tr("Photo ..."));
	pick->setObjectName("photoPick");
	connect(pick, &QPushButton::clicked, this, &PhotoMaterialDialog::choosePhoto);
	top->addWidget(pick);
	m_message = new QLabel;
	m_message->setWordWrap(true);
	top->addWidget(m_message, 1);
	layout->addLayout(top);

	auto* row = new QHBoxLayout;
	auto* form = new QFormLayout;
	m_name = new QLineEdit;
	m_name->setObjectName("photoName");
	m_name->setPlaceholderText(tr("brick_wall"));
	m_folder = new QLineEdit(QStringLiteral("custom"));
	m_folder->setObjectName("photoFolder");
	m_use = new QComboBox;
	m_use->setObjectName("photoUse");
	m_use->addItem(tr("World (brushes)"));
	m_use->addItem(tr("Model"));
	m_surface = new QComboBox;
	m_surface->setObjectName("photoSurface");
	m_surface->addItem(tr("Auto"), QString());
	for (const auto& prop : commonSurfaceProps()) m_surface->addItem(qs(prop), qs(prop));
	m_size = new QComboBox;
	m_size->setObjectName("photoSize");
	m_size->addItem(tr("Auto"), 0);
	for (const int s : {256, 512, 1024, 2048}) m_size->addItem(QString::number(s), s);
	form->addRow(tr("Name"), m_name);
	form->addRow(tr("Folder"), m_folder);
	form->addRow(tr("Use on"), m_use);
	form->addRow(tr("Surface"), m_surface);
	form->addRow(tr("Size"), m_size);
	m_cropSize = slider(10, 100, 100, tr("How much of the photo: 100 = the whole shorter side"));
	m_cropX = slider(0, 100, 50, tr("Where the square sits, left to right"));
	m_cropY = slider(0, 100, 50, tr("Where the square sits, top to bottom"));
	m_seam = slider(0, 50, 25, tr("Blends the edges so tiles meet without a seam"));
	m_flatten = slider(0, 100, 60, tr("Takes out uneven light (one side brighter than the other)"));
	m_bump = slider(0, 400, 100, tr("Depth of the bump map made from the photo; 0 = none"));
	m_gloss = slider(0, 100, 0, tr("Shine: bright parts reflect; 0 = matte"));
	m_cropSize->setObjectName("photoCrop");
	m_seam->setObjectName("photoSeam");
	m_flatten->setObjectName("photoFlatten");
	m_bump->setObjectName("photoBump");
	m_gloss->setObjectName("photoGloss");
	form->addRow(tr("Crop"), m_cropSize);
	form->addRow(tr("Crop left/right"), m_cropX);
	form->addRow(tr("Crop up/down"), m_cropY);
	form->addRow(tr("Seam blend"), m_seam);
	form->addRow(tr("Flatten light"), m_flatten);
	form->addRow(tr("Bump strength"), m_bump);
	form->addRow(tr("Gloss"), m_gloss);
	row->addLayout(form);

	auto* pictures = new QVBoxLayout;
	auto* titles = new QHBoxLayout;
	titles->addWidget(new QLabel(tr("Photo (tiled)")));
	titles->addWidget(new QLabel(tr("Texture (tiled)")));
	pictures->addLayout(titles);
	auto* tiles = new QHBoxLayout;
	m_before = new QLabel;
	m_after = new QLabel;
	m_after->setObjectName("photoAfter");
	for (auto* label : {m_before, m_after}) {
		label->setFixedSize(TILE_VIEW / 3 * 3, TILE_VIEW / 3 * 3);
		label->setScaledContents(true);
		tiles->addWidget(label);
	}
	pictures->addLayout(tiles);
	m_3d = new MaterialPreview;
	m_3d->setShape(MaterialPreview::Shape::Cube);
	m_3d->setMinimumSize(240, 200);
	pictures->addWidget(m_3d, 1);
	row->addLayout(pictures, 1);
	layout->addLayout(row, 1);

	auto* buttons = new QDialogButtonBox;
	m_create = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
	m_create->setObjectName("photoCreate");
	m_create->setEnabled(false);
	buttons->addButton(QDialogButtonBox::Close);
	connect(m_create, &QPushButton::clicked, this, [this] { create(false); });
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	for (auto* s : {m_cropSize, m_cropX, m_cropY, m_seam, m_flatten, m_bump, m_gloss}) connect(s, &QSlider::valueChanged, this, &PhotoMaterialDialog::schedule);
	for (auto* c : {m_use, m_surface}) connect(c, &QComboBox::currentIndexChanged, this, &PhotoMaterialDialog::schedule);
	connect(m_name, &QLineEdit::textChanged, this, &PhotoMaterialDialog::schedule);
	resize(980, 620);
}

void PhotoMaterialDialog::choosePhoto() {
	const QString path = QFileDialog::getOpenFileName(this, tr("Photo for the material"), QString(), tr("Pictures (*.png *.jpg *.jpeg *.tga *.bmp)"));
	if (!path.isEmpty()) loadPhoto(path);
}

bool PhotoMaterialDialog::loadPhoto(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 256LL * 1024 * 1024) {
		showMessage(tr("Cannot read %1").arg(path), true);
		return false;
	}
	const QByteArray bytes = file.readAll();
	std::string error;
	auto image = decodeImageFile(std::as_bytes(std::span(bytes.constData(), std::size_t(bytes.size()))), &error);
	if (!image) {
		showMessage(tr("Not a picture Mapsmith can read (%1)").arg(qs(error)), true);
		return false;
	}
	m_photo = std::move(*image);
	if (m_name->text().trimmed().isEmpty()) m_name->setText(QFileInfo(path).completeBaseName().toLower());
	showMessage(tr("%1 x %2 pixels").arg(m_photo.width).arg(m_photo.height), false);
	updatePreview();
	return true;
}

PhotoMaterialOptions PhotoMaterialDialog::options() const {
	PhotoMaterialOptions o;
	o.cropSize = m_cropSize->value() / 100.0;
	o.cropX = m_cropX->value() / 100.0;
	o.cropY = m_cropY->value() / 100.0;
	o.seamBlend = m_seam->value() / 100.0;
	o.flattenLight = m_flatten->value() / 100.0;
	o.bumpStrength = m_bump->value() / 100.0;
	o.gloss = m_gloss->value() / 100.0;
	o.model = m_use->currentIndex() == 1;
	o.size = unsigned(m_size->currentData().toInt());
	o.folder = m_folder->text().trimmed().toStdString();
	o.name = m_name->text().trimmed().toStdString();
	o.surfaceProp = m_surface->currentData().toString().toStdString();
	return o;
}

void PhotoMaterialDialog::schedule() {
	m_timer.start();
}

void PhotoMaterialDialog::updatePreview() {
	if (m_photo.width == 0) return;
	auto o = options();
	o.size = PREVIEW_SIZE;
	std::string error;
	m_preview = makePhotoMaterial(m_photo, o, &error);
	m_create->setEnabled(m_preview.has_value() && !m_name->text().trimmed().isEmpty() && m_editor->gameEntry());
	if (!m_preview) {
		showMessage(qs(error), true);
		return;
	}
	m_before->setPixmap(tiled(toQImage(cropPhotoSquare(m_photo, o.cropX, o.cropY, o.cropSize))));
	m_after->setPixmap(tiled(toQImage(m_preview->color)));
	m_3d->setTexture(MaterialPreview::Base, toQImage(m_preview->color));
	m_3d->setTexture(MaterialPreview::Bump, toQImage(m_preview->normal));
	m_3d->setTexture(MaterialPreview::Mask, toQImage(m_preview->mask));
	MaterialPreview::Settings settings;
	if (o.gloss > 0) {
		if (o.model) {
			settings.phong = true;
			settings.phongBoost = float(0.5 + 3 * o.gloss);
			settings.phongExponent = float(8 + 32 * o.gloss);
		} else {
			settings.envmap = true;
			settings.maskMode = m_preview->normal.width ? 3 : 1;
			const float tint = float(0.1 + 0.6 * o.gloss);
			settings.envTint = {tint, tint, tint};
		}
	}
	m_3d->setSettings(settings);
	if (o.surfaceProp.empty()) m_surface->setItemText(0, tr("Auto (%1)").arg(qs(m_preview->surfaceProp)));
}

bool PhotoMaterialDialog::create(bool overwrite) {
	const auto* entry = m_editor->gameEntry();
	if (!entry || m_photo.width == 0) return false;
	const auto o = options();
	std::string error;
	const auto made = makePhotoMaterial(m_photo, o, &error);
	if (!made) {
		showMessage(qs(error), true);
		return false;
	}
	auto record = writePhotoMaterial(entry->gameDir, o, *made, overwrite, &error);
	if (!record && !overwrite && error.find("already exists") != std::string::npos && isVisible()) {
		if (QMessageBox::question(this, windowTitle(), tr("%1\nReplace it? The old files are kept as backups.").arg(qs(error))) == QMessageBox::Yes) {
			record = writePhotoMaterial(entry->gameDir, o, *made, true, &error);
		} else {
			return false;
		}
	}
	if (!record) {
		showMessage(qs(error), true);
		return false;
	}
	rememberImport(entry->gameDir, *record);
	const QString name = qs(photoMaterialName(o));
	showMessage(tr("Created %1 (%2 x %2) - File > Import > Imported files ... removes it again").arg(name).arg(made->color.width), false);
	emit created(name);
	return true;
}

void PhotoMaterialDialog::showMessage(const QString& text, bool error) {
	m_message->setText(text);
	m_message->setProperty("role", error ? "error" : "description");
	m_message->style()->unpolish(m_message);
	m_message->style()->polish(m_message);
}

} // namespace sme::app
