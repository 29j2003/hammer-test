// material_editor.cpp - Material editor dock (see material_editor.h).

#include "material_editor.h"

#include "editor.h"
#include "material_preview.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QStringListModel>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <cstring>
#include <set>

#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/keyvalues.h>
#include <sme/text.h>
#include <sme/texture.h>

namespace sme::app {

namespace {

// Keys the fields edit; the table lists every other parameter.
const std::set<QString> COVERED = {
	"$basetexture", "$basetexture2", "$bumpmap", "$ssbump", "$envmap", "$envmaptint", "$envmapmask", "$basealphaenvmapmask",
	"$normalmapalphaenvmapmask", "$translucent", "$alphatest", "$additive", "$nocull", "$selfillum", "$detail", "$detailscale",
	"$detailblendmode", "$phong", "$phongexponent", "$phongboost", "$surfaceprop", "$parallaxmap", "$parallaxdepth", "$parallaxao",
	"$parallaxfade", "$normalmap", "$reflecttint", "$reflectamount", "$refracttint", "$refractamount", "$fogcolor", "$fogstart",
	"$fogend", "$fidelityglint", "$fidelityfoam", "$liquidcolor", "$liquidalpha", "$fill", "$fillmin", "$fillmax", "$wobble",
	"$viscosity", "$bubbles", "$glasscolor", "$glassalpha", "$tintedglass", "$splash", "$spill"};

// Parameters only the fidelity variants of a shader know (removed when switching back).
const QStringList RELIEF_KEYS = {"$parallaxmap", "$parallaxmap2", "$parallaxdepth", "$parallaxao", "$parallaxfade", "$parallaxblend",
                                 "$parallaxmodulate", "$envmapwetonly"};
const QStringList WATER_KEYS = {"$fidelityglint", "$fidelityfoam"};
const QStringList LIQUID_KEYS = {"$liquidcolor", "$liquidalpha", "$surfacecolor", "$foamwidth", "$glasscolor", "$glassalpha", "$fill",
                                 "$fillmin", "$fillmax", "$wobble", "$labelthreshold", "$bubbles", "$tintedglass", "$viscosity",
                                 "$liquidkey", "$splash", "$spill"};

const QStringList SURFACES = {"default", "concrete", "brick", "rock", "metal", "metal_box", "metalpanel", "metalgrate", "metalvent",
                              "chainlink", "wood", "wood_box", "wood_crate", "wood_plank", "wood_panel", "dirt", "grass", "gravel",
                              "sand", "mud", "snow", "ice", "tile", "plaster", "glass", "computer", "carpet", "cardboard",
                              "plastic", "rubber", "paper", "porcelain", "water", "slime", "quicksand", "ladder", "foliage"};

constexpr int SECTION_SPACING = 10;

QVector3D parseColor(const QString& text, QVector3D fallback = {1, 1, 1}) {
	QString t = text;
	t.remove('[').remove(']').remove('{').remove('}');
	const auto parts = t.split(' ', Qt::SkipEmptyParts);
	if (parts.size() < 3) return fallback;
	QVector3D c(parts[0].toFloat(), parts[1].toFloat(), parts[2].toFloat());
	if (text.contains('{')) c /= 255.f;   // {r g b} is 0..255
	return c;
}

QImage toImage(const ImageRgba& image) {
	QImage out(int(image.width), int(image.height), QImage::Format_RGBA8888);
	for (unsigned y = 0; y < image.height; ++y) {
		std::memcpy(out.scanLine(int(y)), image.pixels.data() + std::size_t(y) * image.width * 4, std::size_t(image.width) * 4);
	}
	return out;
}

/// Grey help text under a switch.
QLabel* hint(const QString& text) {
	auto* label = new QLabel(text);
	label->setWordWrap(true);
	label->setStyleSheet("color: #8a90a0;");
	return label;
}

bool startsWithI(const QString& text, const char* prefix) {
	return text.startsWith(QLatin1String(prefix), Qt::CaseInsensitive);
}

} // namespace

// --- Zone painter ------------------------------------------------------------------------

HeightZonePainter::HeightZonePainter(const QImage& texture, unsigned zonesWidth, unsigned zonesHeight, std::vector<std::uint8_t> zones, QWidget* parent)
	: QWidget(parent), m_texture(texture), m_width(zonesWidth), m_height(zonesHeight), m_zones(std::move(zones)) {
	if (m_zones.size() != std::size_t(m_width) * m_height) {
		m_zones.assign(std::size_t(m_width) * m_height, 0);
	}
	setMinimumSize(320, 320);
	setCursor(Qt::CrossCursor);
}

QSize HeightZonePainter::sizeHint() const {
	return {520, 520};
}

void HeightZonePainter::clear() {
	std::fill(m_zones.begin(), m_zones.end(), 0);
	update();
}

QRectF HeightZonePainter::imageRect() const {
	const double side = std::min(width(), height()) - 8.0;
	const double aspect = m_width > 0 ? double(m_height) / m_width : 1.0;
	const double w = aspect <= 1 ? side : side / aspect, h = aspect <= 1 ? side * aspect : side;
	return {(width() - w) / 2, (height() - h) / 2, w, h};
}

void HeightZonePainter::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.fillRect(rect(), QColor(24, 26, 31));
	const QRectF area = imageRect();
	p.drawImage(area, m_texture);
	// Zones as colour over the texture: blue = no relief, orange = inverted.
	QImage overlay(int(m_width), int(m_height), QImage::Format_ARGB32_Premultiplied);
	overlay.fill(Qt::transparent);
	for (unsigned y = 0; y < m_height; ++y) {
		auto* line = reinterpret_cast<QRgb*>(overlay.scanLine(int(y)));
		for (unsigned x = 0; x < m_width; ++x) {
			const auto zone = static_cast<HeightZone>(m_zones[std::size_t(y) * m_width + x]);
			if (zone == HeightZone::Flat) line[x] = qPremultiply(qRgba(70, 150, 255, 140));
			else if (zone == HeightZone::Inverted) line[x] = qPremultiply(qRgba(255, 150, 40, 140));
		}
	}
	p.drawImage(area, overlay);
	p.setPen(QColor(255, 255, 255, 60));
	p.drawRect(area);
}

void HeightZonePainter::paintAt(const QPointF& pos, HeightZone zone) {
	const QRectF area = imageRect();
	if (!area.contains(pos) || m_width == 0) return;
	const double u = (pos.x() - area.left()) / area.width(), v = (pos.y() - area.top()) / area.height();
	const int cx = int(u * m_width), cy = int(v * m_height);
	const int r = std::max(1, m_brush / 2);
	for (int y = cy - r; y <= cy + r; ++y) {
		for (int x = cx - r; x <= cx + r; ++x) {
			if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r) continue;
			// The texture tiles: painting over its edge goes on at the other side.
			const unsigned wx = unsigned((x % int(m_width) + int(m_width)) % int(m_width));
			const unsigned wy = unsigned((y % int(m_height) + int(m_height)) % int(m_height));
			m_zones[std::size_t(wy) * m_width + wx] = std::uint8_t(zone);
		}
	}
	update();
}

void HeightZonePainter::mousePressEvent(QMouseEvent* event) {
	paintAt(event->position(), event->button() == Qt::RightButton ? HeightZone::Normal : m_zone);
}

void HeightZonePainter::mouseMoveEvent(QMouseEvent* event) {
	if (event->buttons() & Qt::LeftButton) paintAt(event->position(), m_zone);
	else if (event->buttons() & Qt::RightButton) paintAt(event->position(), HeightZone::Normal);
}

// --- Editor --------------------------------------------------------------------------------

MaterialEditor::MaterialEditor(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	auto* outer = new QHBoxLayout(this);
	outer->setContentsMargins(6, 6, 6, 6);
	auto* split = new QSplitter(Qt::Horizontal);
	split->setChildrenCollapsible(false);
	outer->addWidget(split);

	// Left: the fields.
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* fields = new QWidget;
	auto* v = new QVBoxLayout(fields);
	v->setContentsMargins(4, 4, 10, 4);
	v->setSpacing(SECTION_SPACING);
	m_title = new QLabel(tr("<span style='font-size:13pt'><b>No material</b></span>"));
	m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
	v->addWidget(m_title);
	m_where = hint(tr("Right-click a material in the Asset Browser and choose Edit material, or press Edit next to a selected face."));
	m_where->setTextInteractionFlags(Qt::TextSelectableByMouse);
	v->addWidget(m_where);
	m_notice = new QLabel;
	m_notice->setWordWrap(true);
	m_notice->setProperty("role", "warning");
	m_notice->setVisible(false);
	v->addWidget(m_notice);
	scroll->setWidget(fields);
	split->addWidget(scroll);
	buildFields();
	v->addStretch(1);

	// Right: preview, light and shape, save.
	auto* right = new QWidget;
	auto* r = new QVBoxLayout(right);
	r->setContentsMargins(8, 4, 4, 4);
	r->setSpacing(8);
	m_preview = new MaterialPreview;
	r->addWidget(m_preview, 1);
	auto* choices = new QHBoxLayout;
	choices->setSpacing(8);
	m_shape = new QComboBox;
	m_shape->addItems({tr("Wall"), tr("Cube"), tr("Sphere")});
	m_lighting = new QComboBox;
	m_lighting->addItems({tr("Sun (outdoors)"), tr("Lamp (indoors, warm)"), tr("Night (dim, cold)"), tr("Unlit (texture only)")});
	m_lighting->setToolTip(tr("Right drag in the preview moves the light"));
	choices->addWidget(new QLabel(tr("Shape")));
	choices->addWidget(m_shape);
	choices->addSpacing(8);
	choices->addWidget(new QLabel(tr("Light")));
	choices->addWidget(m_lighting, 1);
	r->addLayout(choices);
	r->addWidget(hint(tr("Left drag turns the shape, right drag moves the light, the wheel zooms. "
	                     "An approximation of the game's shaders - check the look in the game.")));
	auto* buttons = new QHBoxLayout;
	buttons->setSpacing(8);
	m_save = new QPushButton(tr("Save"));
	m_save->setDefault(true);
	m_saveAs = new QPushButton(tr("Save as new ..."));
	m_save->setEnabled(false);
	m_saveAs->setEnabled(false);
	buttons->addWidget(m_save, 1);
	buttons->addWidget(m_saveAs, 1);
	r->addLayout(buttons);
	m_status = hint(QString());
	r->addWidget(m_status);
	// In a scroll area as well, so a low dock still leaves the viewport its room.
	auto* rightScroll = new QScrollArea;
	rightScroll->setWidgetResizable(true);
	rightScroll->setFrameShape(QFrame::NoFrame);
	rightScroll->setWidget(right);
	split->addWidget(rightScroll);
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 2);

	connect(m_shape, &QComboBox::currentIndexChanged, this, [this](int i) { m_preview->setShape(static_cast<MaterialPreview::Shape>(i)); });
	connect(m_lighting, &QComboBox::currentIndexChanged, this, [this](int i) { m_preview->setLighting(static_cast<MaterialPreview::Lighting>(i)); });
	connect(m_save, &QPushButton::clicked, this, [this] { save(); });
	connect(m_saveAs, &QPushButton::clicked, this, [this] {
		bool ok = false;
		const QString name = QInputDialog::getText(this, tr("Save as new material"), tr("Name below materials/ (e.g. mymod/walls/brick_dark):"),
		                                           QLineEdit::Normal, m_name + "_copy", &ok);
		if (ok && !name.trimmed().isEmpty()) {
			save(name.trimmed());
		}
	});
	connect(&m_textureWatcher, &QFutureWatcherBase::finished, this, [this] {
		if (m_textureWatcher.future().resultCount() == 0) return;
		const auto images = m_textureWatcher.result();
		const QString keys[4] = {"$basetexture", "$bumpmap", "$envmapmask", "$detail"};
		for (int slot = 0; slot < 4; ++slot) {
			const auto it = images.find(keys[slot]);
			m_preview->setTexture(static_cast<MaterialPreview::Slot>(slot), it == images.end() ? QImage() : it->second);
		}
		// Water: its waves are in $normalmap.
		if (const auto it = images.find("$normalmap"); it != images.end()) {
			m_preview->setTexture(MaterialPreview::Bump, it->second);
		}
		const auto base = images.find("$basetexture");
		m_baseImage = base == images.end() ? QImage() : base->second;
		emit previewUpdated();
	});
	connect(editor, &Editor::gameChanged, this, [this] {
		m_name.clear();
		m_values.clear();
		m_edits.clear();
		m_title->setText(tr("<span style='font-size:13pt'><b>No material</b></span>"));
		m_save->setEnabled(false);
		m_saveAs->setEnabled(false);
		updateSections();
		// Texture names for the completers, read once per game in the background.
		auto* game = m_editor->game();
		if (!game) return;
		const GameFileSystem* fs = &game->files;
		auto* watcher = new QFutureWatcher<QStringList>(this);
		connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
			watcher->deleteLater();
			auto* model = new QStringListModel(watcher->result(), this);
			for (auto& [key, field] : m_textureFields) {
				auto* completer = new QCompleter(model, field);
				completer->setCaseSensitivity(Qt::CaseInsensitive);
				completer->setFilterMode(Qt::MatchContains);
				field->setCompleter(completer);
			}
		});
		watcher->setFuture(QtConcurrent::run([fs] {
			QStringList names;
			for (const auto& file : fs->list("materials", ".vtf")) {
				names << QString::fromStdString(file.substr(10, file.size() - 14));   // without materials/ and .vtf
			}
			return names;
		}));
	});
	updateSections();
}

QFormLayout* MaterialEditor::section(QGroupBox*& box, const QString& title) {
	box = new QGroupBox(title);
	auto* form = new QFormLayout(box);
	form->setContentsMargins(12, 12, 12, 10);
	form->setHorizontalSpacing(12);
	form->setVerticalSpacing(8);
	form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	qobject_cast<QVBoxLayout*>(m_title->parentWidget()->layout())->addWidget(box);
	return form;
}

QLineEdit* MaterialEditor::textureField(const QString& key, const QString& placeholder) {
	auto* field = new QLineEdit;
	field->setObjectName(key);
	field->setPlaceholderText(placeholder);
	field->setClearButtonEnabled(true);
	connect(field, &QLineEdit::editingFinished, this, [this, field, key] {
		if (field->text().trimmed() != value(key)) setValue(key, field->text().trimmed());
	});
	m_textureFields[key] = field;
	return field;
}

QWidget* MaterialEditor::colorField(const QString& key, const QString& placeholder, QLineEdit** out) {
	auto* row = new QWidget;
	auto* h = new QHBoxLayout(row);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(6);
	auto* field = new QLineEdit;
	field->setObjectName(key);
	field->setPlaceholderText(placeholder);
	connect(field, &QLineEdit::editingFinished, this, [this, field, key] {
		if (field->text().trimmed() != value(key)) setValue(key, field->text().trimmed());
	});
	auto* pick = new QToolButton;
	pick->setText(tr("Pick ..."));
	connect(pick, &QToolButton::clicked, this, [this, key, placeholder] {
		const QVector3D c = parseColor(value(key).isEmpty() ? placeholder : value(key));
		const QColor color = QColorDialog::getColor(QColor::fromRgbF(std::clamp(c.x(), 0.f, 1.f), std::clamp(c.y(), 0.f, 1.f), std::clamp(c.z(), 0.f, 1.f)), this);
		if (color.isValid()) {
			setValue(key, QStringLiteral("[%1 %2 %3]").arg(color.redF(), 0, 'f', 2).arg(color.greenF(), 0, 'f', 2).arg(color.blueF(), 0, 'f', 2));
			loadValuesIntoFields();
		}
	});
	h->addWidget(field, 1);
	h->addWidget(pick);
	m_colorFields[key] = field;
	if (out) *out = field;
	return row;
}

QWidget* MaterialEditor::sliderField(const QString& key, double minimum, double maximum, double fallback, int decimals, QSlider** out) {
	auto* row = new QWidget;
	auto* h = new QHBoxLayout(row);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(8);
	auto* slider = new QSlider(Qt::Horizontal);
	slider->setObjectName(key);
	slider->setRange(0, 1000);
	auto* label = new QLabel;
	label->setMinimumWidth(44);
	label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	const auto toValue = [=](int position) { return minimum + (maximum - minimum) * position / 1000.0; };
	connect(slider, &QSlider::valueChanged, this, [=, this](int position) {
		const double v = toValue(position);
		label->setText(QString::number(v, 'f', decimals));
		if (!m_loading) setValue(key, QString::number(v, 'f', decimals));
	});
	slider->setProperty("valueMin", minimum);
	slider->setProperty("valueMax", maximum);
	slider->setProperty("fallback", fallback);
	slider->setProperty("decimals", decimals);
	label->setText(QString::number(fallback, 'f', decimals));
	h->addWidget(slider, 1);
	h->addWidget(label);
	m_sliders[key] = slider;
	if (out) *out = slider;
	return row;
}

void MaterialEditor::buildFields() {
	// Shown instead of a switch's effect when the mod lacks the shaders.
	const auto needsFidelity = [this] {
		auto* label = hint(tr("Needs mapbase-fidelity's shaders in the mod: run its deploy.bat once."));
		m_fidelityMissing.push_back(label);
		return label;
	};
	const auto check = [this](const QString& key, const QString& text, const QString& tip = {}) {
		auto* box = new QCheckBox(text);
		box->setObjectName(key);
		box->setToolTip(tip);
		connect(box, &QCheckBox::toggled, this, [this, key](bool on) {
			if (!m_loading) setValue(key, on ? "1" : "");
		});
		return box;
	};

	// What kind of material it is: decides which sections make sense.
	auto* kindForm = section(m_texturesBox, tr("Material"));
	m_kindBox = new QComboBox;
	m_kindBox->setObjectName("kind");
	m_kindBox->addItem(tr("Wall or floor"), "LightmappedGeneric");
	m_kindBox->addItem(tr("Blended ground (two textures, displacements)"), "WorldVertexTransition");
	m_kindBox->addItem(tr("Model"), "VertexLitGeneric");
	m_kindBox->addItem(tr("Without light (screens, signs)"), "UnlitGeneric");
	m_kindBox->addItem(tr("Water"), "Water");
	connect(m_kindBox, &QComboBox::activated, this, [this](int index) {
		const QString base = m_kindBox->itemData(index).toString();
		if (base.compare(baseShader(), Qt::CaseInsensitive) != 0) setShader(base);
	});
	m_shaderName = hint(QString());
	auto* kindRow = new QWidget;
	auto* kindLayout = new QVBoxLayout(kindRow);
	kindLayout->setContentsMargins(0, 0, 0, 0);
	kindLayout->setSpacing(2);
	kindLayout->addWidget(m_kindBox);
	kindLayout->addWidget(m_shaderName);
	kindForm->addRow(tr("Kind"), kindRow);
	auto* texture = textureField("$basetexture", tr("folder/texture (without .vtf)"));
	kindForm->addRow(tr("Texture"), texture);
	m_rows["$basetexture"] = texture;
	auto* second = textureField("$basetexture2", tr("second texture, painted in with alpha"));
	kindForm->addRow(tr("Second texture"), second);
	m_rows["$basetexture2"] = second;
	m_surface = new QComboBox;
	m_surface->setObjectName("surfaceprop");
	m_surface->setEditable(true);
	m_surface->addItems(SURFACES);
	m_surface->setToolTip(tr("Footsteps, bullet impacts and physics (scripts/surfaceproperties.txt)"));
	connect(m_surface, &QComboBox::currentTextChanged, this, [this](const QString& text) {
		if (!m_loading) setValue("$surfaceprop", text.trimmed());
	});
	kindForm->addRow(tr("Surface"), m_surface);

	// Water (also Water_Fidelity).
	auto* water = section(m_waterBox, tr("Water"));
	m_fidelityWater = new QCheckBox(tr("Sun glint and foam at the shore"));
	m_fidelityWater->setObjectName("fidelityWater");
	connect(m_fidelityWater, &QCheckBox::toggled, this, [this](bool on) {
		if (m_loading) return;
		setShader(on ? "Water_Fidelity" : "Water");
	});
	water->addRow(QString(), m_fidelityWater);
	water->addRow(QString(), needsFidelity());
	auto* glint = sliderField("$fidelityglint", 0, 3, 1, 2);
	water->addRow(tr("Glint"), glint);
	m_rows["$fidelityglint"] = glint;
	auto* foam = sliderField("$fidelityfoam", 0, 3, 1, 2);
	water->addRow(tr("Foam"), foam);
	m_rows["$fidelityfoam"] = foam;
	water->addRow(tr("Waves (normal map)"), textureField("$normalmap", tr("nature/water_canals03_normal")));
	water->addRow(tr("Reflection colour"), colorField("$reflecttint", "[1 1 1]"));
	water->addRow(tr("Reflection strength"), sliderField("$reflectamount", 0, 2, 0.8, 2));
	water->addRow(tr("Refraction colour"), colorField("$refracttint", "[1 1 1]"));
	water->addRow(tr("Refraction strength"), sliderField("$refractamount", 0, 2, 0.5, 2));
	water->addRow(tr("Colour in the depth"), colorField("$fogcolor", "{30 40 40}"));
	water->addRow(tr("Clear down to"), sliderField("$fogstart", 0, 1000, 0, 0));
	water->addRow(tr("Opaque from"), sliderField("$fogend", 0, 2000, 300, 0));

	// Liquid in glass props: Fidelity_Liquid.
	auto* liquid = section(m_liquidBox, tr("Liquid inside (mapbase-fidelity)"));
	m_liquid = new QCheckBox(tr("This is a glass container with liquid inside"));
	m_liquid->setObjectName("liquid");
	connect(m_liquid, &QCheckBox::toggled, this, [this](bool on) {
		if (m_loading) return;
		setShader(on ? "Fidelity_Liquid" : "VertexLitGeneric");
	});
	liquid->addRow(QString(), m_liquid);
	liquid->addRow(QString(), needsFidelity());
	liquid->addRow(QString(), hint(tr("Bottles, jars and jugs: the liquid sways when the prop moves, can splash when it breaks "
	                                  "and pour out when it tips over (Fidelity_Liquid).")));
	auto* liquidColor = colorField("$liquidcolor", tr("[0.5 0.3 0.1] - empty: the texture's colour"));
	liquid->addRow(tr("Liquid colour"), liquidColor);
	m_rows["$liquidcolor"] = liquidColor;
	auto* liquidAlpha = sliderField("$liquidalpha", 0, 1, 0.85, 2);
	liquid->addRow(tr("Liquid opacity"), liquidAlpha);
	m_rows["$liquidalpha"] = liquidAlpha;
	m_fillMode = new QComboBox;
	m_fillMode->setObjectName("fillMode");
	m_fillMode->addItems({tr("Different in every prop"), tr("Always the same")});
	connect(m_fillMode, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (m_loading) return;
		setValue("$fill", index == 0 ? "" : (value("$fill").isEmpty() || value("$fill").toDouble() < 0 ? "0.6" : value("$fill")));
		loadValuesIntoFields();
	});
	liquid->addRow(tr("Fill level"), m_fillMode);
	m_rows["fillMode"] = m_fillMode;
	auto* fill = sliderField("$fill", 0, 1, 0.6, 2);
	liquid->addRow(tr("Filled to"), fill);
	m_rows["$fill"] = fill;
	auto* fillMin = sliderField("$fillmin", 0, 1, 0.25, 2);
	liquid->addRow(tr("At least"), fillMin);
	m_rows["$fillmin"] = fillMin;
	auto* fillMax = sliderField("$fillmax", 0, 1, 0.8, 2);
	liquid->addRow(tr("At most"), fillMax);
	m_rows["$fillmax"] = fillMax;
	auto* wobble = sliderField("$wobble", 0, 2, 1, 2);
	liquid->addRow(tr("Sways"), wobble);
	m_rows["$wobble"] = wobble;
	auto* viscosity = sliderField("$viscosity", 0, 1, 0, 2);
	liquid->addRow(tr("Thick (0 water .. 1 syrup)"), viscosity);
	m_rows["$viscosity"] = viscosity;
	auto* glassColor = colorField("$glasscolor", "[0.75 0.85 0.8]");
	liquid->addRow(tr("Glass colour"), glassColor);
	m_rows["$glasscolor"] = glassColor;
	auto* glassAlpha = sliderField("$glassalpha", 0, 1, 0.3, 2);
	liquid->addRow(tr("Glass opacity"), glassAlpha);
	m_rows["$glassalpha"] = glassAlpha;
	m_splash = check("$splash", tr("Splashes when the prop breaks"));
	liquid->addRow(QString(), m_splash);
	m_rows["$splash"] = m_splash;
	m_spill = check("$spill", tr("Open bottle: pours out when tipped over"));
	liquid->addRow(QString(), m_spill);
	m_rows["$spill"] = m_spill;

	auto* bump = section(m_bumpBox, tr("Normal map"));
	bump->addRow(tr("Normal map"), textureField("$bumpmap", tr("folder/texture_normal")));
	m_ssbump = check("$ssbump", tr("Self-shadowed ($ssbump, made with height2ssbump)"));
	bump->addRow(QString(), m_ssbump);

	// Relief: mapbase-fidelity's Fidelity_Parallax.
	auto* relief = section(m_reliefBox, tr("Relief (real depth, mapbase-fidelity)"));
	m_relief = new QCheckBox(tr("Give this surface real depth"));
	m_relief->setObjectName("relief");
	connect(m_relief, &QCheckBox::toggled, this, [this](bool on) {
		if (!m_loading) setRelief(on);
	});
	relief->addRow(QString(), m_relief);
	relief->addRow(QString(), needsFidelity());
	relief->addRow(QString(), hint(tr("Stones, joints and grooves really go into the wall and hide each other at a slant "
	                                  "(Fidelity_Parallax). Needs a height map: white is the top.")));
	relief->addRow(tr("Depth"), sliderField("$parallaxdepth", 0, 0.1, 0.02, 3));
	relief->addRow(tr("Darker in the depths"), sliderField("$parallaxao", 0, 1, 0.35, 2));
	relief->addRow(tr("Fades out from"), sliderField("$parallaxfade", 200, 4000, 1000, 0));
	auto* heightRow = new QWidget;
	auto* hl = new QVBoxLayout(heightRow);
	hl->setContentsMargins(0, 0, 0, 0);
	hl->setSpacing(6);
	m_heightLabel = hint(tr("none yet"));
	hl->addWidget(m_heightLabel);
	auto* makeRow = new QHBoxLayout;
	makeRow->setSpacing(6);
	m_fromNormals = new QPushButton(tr("From the normal map"));
	m_fromNormals->setObjectName("heightFromNormals");
	m_fromTexture = new QPushButton(tr("From the texture"));
	m_fromTexture->setObjectName("heightFromTexture");
	m_fromTexture->setToolTip(tr("Bright parts high, dark joints low - for walls without a normal map"));
	makeRow->addWidget(m_fromNormals);
	makeRow->addWidget(m_fromTexture);
	hl->addLayout(makeRow);
	auto* adjustRow = new QHBoxLayout;
	adjustRow->setSpacing(6);
	m_invert = new QCheckBox(tr("Upside down"));
	m_invert->setObjectName("heightInvert");
	m_invert->setToolTip(tr("What sticks out goes in and the other way round"));
	m_zonesButton = new QPushButton(tr("Paint zones ..."));
	m_zonesButton->setObjectName("paintZones");
	m_zonesButton->setToolTip(tr("Paint where the surface stays flat or is turned upside down"));
	adjustRow->addWidget(m_invert);
	adjustRow->addStretch(1);
	adjustRow->addWidget(m_zonesButton);
	hl->addLayout(adjustRow);
	relief->addRow(tr("Height map"), heightRow);
	connect(m_fromNormals, &QPushButton::clicked, this, [this] { makeHeightMap(true); });
	connect(m_fromTexture, &QPushButton::clicked, this, [this] { makeHeightMap(false); });
	connect(m_invert, &QCheckBox::toggled, this, [this](bool on) {
		if (!m_loading) setHeightInverted(on);
	});
	connect(m_zonesButton, &QPushButton::clicked, this, &MaterialEditor::paintZones);

	auto* env = section(m_envBox, tr("Reflections"));
	m_reflection = new QComboBox;
	m_reflection->setObjectName("reflection");
	m_reflection->addItems({tr("None"), tr("The nearest env_cubemap"), tr("A cubemap texture of its own")});
	connect(m_reflection, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (m_loading) return;
		if (index == 0) setValue("$envmap", "");
		else if (index == 1) setValue("$envmap", "env_cubemap");
		else if (value("$envmap").isEmpty() || value("$envmap") == "env_cubemap") setValue("$envmap", "editor/cubemap");
	});
	env->addRow(tr("Reflects"), m_reflection);
	env->addRow(tr("Strength / colour"), colorField("$envmaptint", "[1 1 1]"));
	m_mask = new QComboBox;
	m_mask->addItems({tr("Everywhere"), tr("Mask texture ($envmapmask)"), tr("The texture's alpha"), tr("The normal map's alpha")});
	m_mask->setToolTip(tr("Where the surface reflects: white reflects, black does not"));
	connect(m_mask, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (m_loading) return;
		m_loading = true;
		setValue("$basealphaenvmapmask", index == 2 ? "1" : "");
		setValue("$normalmapalphaenvmapmask", index == 3 ? "1" : "");
		if (index != 1) setValue("$envmapmask", "");
		m_loading = false;
		loadValuesIntoFields();
	});
	env->addRow(tr("Where"), m_mask);
	auto* maskField = textureField("$envmapmask", tr("folder/texture_mask"));
	env->addRow(tr("Mask texture"), maskField);
	env->addRow(QString(), hint(tr("Mapbase: reflections line up with the room when the env_cubemap that captures this surface has "
	                               "a room box (select the env_cubemap: Add room box); VBSP adds $envmapparallax itself.")));

	auto* alpha = section(m_alphaBox, tr("Transparency and glow"));
	m_transparency = new QComboBox;
	m_transparency->addItems({tr("Solid"), tr("See-through (glass, smoke)"), tr("Cut out (fences, leaves)"), tr("Glowing, adds light (beams)")});
	connect(m_transparency, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (m_loading) return;
		m_loading = true;
		setValue("$translucent", index == 1 ? "1" : "");
		setValue("$alphatest", index == 2 ? "1" : "");
		setValue("$additive", index == 3 ? "1" : "");
		m_loading = false;
		refreshPreview(false);
	});
	alpha->addRow(tr("Transparency"), m_transparency);
	m_nocull = check("$nocull", tr("Show both sides ($nocull)"));
	alpha->addRow(QString(), m_nocull);
	m_selfillum = check("$selfillum", tr("Glows ($selfillum, where the alpha is white)"));
	alpha->addRow(QString(), m_selfillum);

	auto* detail = section(m_detailBox, tr("Detail and shine"));
	detail->addRow(tr("Detail texture"), textureField("$detail", tr("detail/noise_detail_01")));
	m_detailScale = new QDoubleSpinBox;
	m_detailScale->setRange(0.01, 64);
	m_detailScale->setValue(4);
	m_detailScale->setKeyboardTracking(false);
	connect(m_detailScale, &QDoubleSpinBox::valueChanged, this, [this](double scale) {
		if (!m_loading) setValue("$detailscale", QString::number(scale));
	});
	detail->addRow(tr("Detail size"), m_detailScale);
	m_detailBlend = new QComboBox;
	m_detailBlend->addItems({tr("Multiply"), tr("Add"), tr("Over the texture")});
	connect(m_detailBlend, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (!m_loading) setValue("$detailblendmode", index == 0 ? "" : QString::number(index));
	});
	detail->addRow(tr("Detail blend"), m_detailBlend);
	m_phong = check("$phong", tr("Shiny highlights ($phong)"));
	detail->addRow(QString(), m_phong);
	auto* exponent = sliderField("$phongexponent", 1, 150, 5, 0);
	detail->addRow(tr("Highlight size"), exponent);
	m_rows["$phongexponent"] = exponent;
	auto* boost = sliderField("$phongboost", 0, 10, 1, 1);
	detail->addRow(tr("Highlight strength"), boost);
	m_rows["$phongboost"] = boost;

	// Everything else, as text.
	QGroupBox* otherBox = nullptr;
	auto* otherForm = section(otherBox, tr("Other parameters"));
	m_other = new QTableWidget;
	m_other->setObjectName("otherParams");
	m_other->setColumnCount(2);
	m_other->setHorizontalHeaderLabels({tr("Parameter"), tr("Value")});
	m_other->horizontalHeader()->setStretchLastSection(true);
	m_other->verticalHeader()->setVisible(false);
	m_other->setMinimumHeight(130);
	connect(m_other, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
		if (m_loading || item->column() != 1) return;
		setValue(m_other->item(item->row(), 0)->text(), item->text());
	});
	otherForm->addRow(m_other);
	auto* addRow = new QHBoxLayout;
	addRow->setSpacing(6);
	auto* key = new QLineEdit;
	key->setPlaceholderText(tr("$parameter"));
	auto* val = new QLineEdit;
	val->setPlaceholderText(tr("value"));
	auto* add = new QPushButton(tr("+ Add"));
	connect(add, &QPushButton::clicked, this, [this, key, val] {
		QString k = key->text().trimmed().toLower();
		if (k.isEmpty() || k.contains(' ')) return;
		if (!k.startsWith('$') && !k.startsWith('%')) k.prepend('$');
		setValue(k, val->text());
		key->clear();
		val->clear();
		loadValuesIntoFields();
	});
	addRow->addWidget(key, 2);
	addRow->addWidget(val, 3);
	addRow->addWidget(add);
	otherForm->addRow(addRow);
}

MaterialEditor::Kind MaterialEditor::kind() const {
	const QString s = m_shader;
	if (startsWithI(s, "water")) return Kind::Water;
	if (s.contains("WorldVertexTransition", Qt::CaseInsensitive)) return Kind::Blend;
	if (s.compare("Fidelity_Parallax", Qt::CaseInsensitive) == 0 || s.contains("LightmappedGeneric", Qt::CaseInsensitive)) return Kind::Wall;
	if (s.contains("VertexLitGeneric", Qt::CaseInsensitive) || s.compare("Fidelity_Liquid", Qt::CaseInsensitive) == 0) return Kind::Model;
	if (s.contains("Unlit", Qt::CaseInsensitive)) return Kind::Unlit;
	return Kind::Other;
}

QString MaterialEditor::baseShader() const {
	if (m_shader.compare("Fidelity_Parallax", Qt::CaseInsensitive) == 0) return "LightmappedGeneric";
	if (m_shader.compare("Fidelity_WorldVertexTransition", Qt::CaseInsensitive) == 0) return "WorldVertexTransition";
	if (m_shader.compare("Water_Fidelity", Qt::CaseInsensitive) == 0) return "Water";
	if (m_shader.compare("Fidelity_Liquid", Qt::CaseInsensitive) == 0) return "VertexLitGeneric";
	return m_shader;
}

bool MaterialEditor::fidelityShaders() const {
	const auto* game = m_editor->game();
	if (!game) return false;
	// deploy.bat copies the shaders' .vcs files and game_shader_dx6.dll into the mod.
	const auto* entry = m_editor->gameEntry();
	return game->files.exists("shaders/fxc/fidelity_parallax_ps30.vcs") ||
	       (entry && QFileInfo::exists(fromPath(entry->gameDir) + "/bin/game_shader_dx6.dll"));
}

QString MaterialEditor::value(const QString& key) const {
	const auto it = m_values.find(key.toLower());
	return it == m_values.end() ? QString() : it->second;
}

void MaterialEditor::setValue(const QString& rawKey, const QString& text) {
	const QString key = rawKey.toLower();
	if (text.isEmpty()) {
		if (!m_values.contains(key)) return;
		m_values.erase(key);
		m_edits[key] = std::nullopt;
	} else {
		if (value(key) == text) return;
		m_values[key] = text;
		m_edits[key] = text;
	}
	m_save->setEnabled(!m_name.isEmpty());
	m_status->setText(tr("Changed - Save writes it into the mod"));
	const bool textureKey = key == "$basetexture" || key == "$bumpmap" || key == "$envmapmask" || key == "$detail" || key == "$normalmap";
	refreshPreview(textureKey);
	if (!COVERED.contains(key) && !m_loading) fillOtherTable();
}

void MaterialEditor::setShader(const QString& shader) {
	if (shader == m_shader) return;
	m_shader = shader;
	m_shaderChanged = true;
	// Parameters only the previous variant knew go with it.
	m_loading = true;
	if (!shader.contains("Fidelity_Parallax", Qt::CaseInsensitive) && !shader.startsWith("Fidelity_WorldVertexTransition", Qt::CaseInsensitive)) {
		for (const auto& key : RELIEF_KEYS) setValue(key, "");
	}
	if (shader.compare("Water_Fidelity", Qt::CaseInsensitive) != 0) {
		for (const auto& key : WATER_KEYS) setValue(key, "");
	}
	if (shader.compare("Fidelity_Liquid", Qt::CaseInsensitive) != 0) {
		for (const auto& key : LIQUID_KEYS) setValue(key, "");
	}
	m_loading = false;
	m_save->setEnabled(!m_name.isEmpty());
	m_status->setText(tr("Changed - Save writes it into the mod"));
	loadValuesIntoFields();
	refreshPreview(true);
}

void MaterialEditor::setRelief(bool on) {
	const Kind k = kind();
	if (k != Kind::Wall && k != Kind::Blend) return;
	if (!on) {
		setShader(k == Kind::Blend ? "WorldVertexTransition" : "LightmappedGeneric");
		return;
	}
	setShader(k == Kind::Blend ? "Fidelity_WorldVertexTransition" : "Fidelity_Parallax");
	if (value("$parallaxdepth").isEmpty()) setValue("$parallaxdepth", "0.02");
	if (!m_heightSource) {
		// A height map is needed: from the normal map when there is one, else from the texture.
		makeHeightMap(!value("$bumpmap").isEmpty());
	}
	loadValuesIntoFields();
}

void MaterialEditor::makeHeightMap(bool fromNormalMap) {
	auto* game = m_editor->game();
	const QString source = fromNormalMap ? value("$bumpmap") : value("$basetexture");
	if (!game || source.isEmpty()) {
		m_status->setText(fromNormalMap ? tr("This material has no normal map - make the height map from the texture") : tr("This material has no texture"));
		return;
	}
	const bool ssbump = value("$ssbump") == "1";
	const GameFileSystem* fs = &game->files;
	const std::string name = source.toStdString();
	const int generation = ++m_heightGeneration;
	m_heightLabel->setText(tr("Making the height map ..."));
	auto* watcher = new QFutureWatcher<std::pair<std::optional<HeightMap>, QString>>(this);
	connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation, fromNormalMap, source] {
		watcher->deleteLater();
		if (generation != m_heightGeneration) return;
		const auto [map, error] = watcher->result();
		if (!map) {
			m_heightLabel->setText(tr("Could not make one: %1").arg(error));
			return;
		}
		m_heightSource = map;
		m_heightOrigin = fromNormalMap ? tr("made from the normal map %1").arg(source) : tr("made from the texture's brightness");
		m_heightDirty = true;
		m_save->setEnabled(!m_name.isEmpty());
		m_status->setText(tr("Changed - Save writes it into the mod"));
		refreshHeights();
		emit heightsReady();
	});
	watcher->setFuture(QtConcurrent::run([fs, name, fromNormalMap, ssbump]() -> std::pair<std::optional<HeightMap>, QString> {
		std::string error;
		const auto image = loadTexture(*fs, name, 512, &error);
		if (!image) return {std::nullopt, QString::fromStdString(error)};
		auto map = fromNormalMap ? heightFromNormalMap(*image, ssbump, &error) : heightFromColor(*image, &error);
		return {std::move(map), QString::fromStdString(error)};
	}));
}

void MaterialEditor::setHeightInverted(bool inverted) {
	if (inverted == m_heightInverted) return;
	m_heightInverted = inverted;
	m_heightDirty = true;
	m_save->setEnabled(!m_name.isEmpty());
	m_status->setText(tr("Changed - Save writes it into the mod"));
	m_loading = true;
	m_invert->setChecked(inverted);
	m_loading = false;
	refreshHeights();
}

void MaterialEditor::setHeightZones(std::vector<std::uint8_t> zones, unsigned width, unsigned height) {
	m_zones = std::move(zones);
	m_zonesWidth = width;
	m_zonesHeight = height;
	m_heightDirty = true;
	m_save->setEnabled(!m_name.isEmpty());
	m_status->setText(tr("Changed - Save writes it into the mod"));
	refreshHeights();
}

std::optional<HeightMap> MaterialEditor::currentHeights() const {
	if (!m_heightSource) return std::nullopt;
	HeightMap map = *m_heightSource;
	if (m_heightInverted) invertHeights(map);
	applyHeightZones(map, m_zones, m_zonesWidth, m_zonesHeight);
	return map;
}

void MaterialEditor::refreshHeights() {
	const auto heights = currentHeights();
	m_preview->setTexture(MaterialPreview::Height, heights ? toImage(heightImage(*heights)) : QImage());
	QString text = heights ? m_heightOrigin : tr("none yet - make one from the normal map or the texture");
	if (heights && m_heightInverted) text += tr(", upside down");
	if (heights && std::any_of(m_zones.begin(), m_zones.end(), [](std::uint8_t z) { return z != 0; })) text += tr(", with painted zones");
	if (m_heightDirty && heights) text += tr(" (saved with the material)");
	m_heightLabel->setText(text);
	refreshPreview(false);
}

void MaterialEditor::loadHeightSource() {
	m_heightSource.reset();
	m_heightInverted = false;
	m_zones.clear();
	m_zonesWidth = m_zonesHeight = 0;
	m_heightDirty = false;
	auto* game = m_editor->game();
	const QString name = value("$parallaxmap");
	if (!game || name.isEmpty()) {
		refreshHeights();
		return;
	}
	const GameFileSystem* fs = &game->files;
	const std::string path = name.toStdString();
	const int generation = ++m_heightGeneration;
	auto* watcher = new QFutureWatcher<std::optional<HeightMap>>(this);
	connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation, name] {
		watcher->deleteLater();
		if (generation != m_heightGeneration) return;
		m_heightSource = watcher->result();
		m_heightOrigin = name.endsWith("_fidh") ? tr("%1 (made by deploy.bat)").arg(name) : name;
		refreshHeights();
		emit heightsReady();
	});
	watcher->setFuture(QtConcurrent::run([fs, path]() -> std::optional<HeightMap> {
		const auto image = loadTexture(*fs, path, 512);
		if (!image) return std::nullopt;
		return heightFromImage(*image);
	}));
}

void MaterialEditor::paintZones() {
	const auto heights = currentHeights();
	if (!m_heightSource) {
		m_status->setText(tr("Make a height map first"));
		return;
	}
	const unsigned w = m_heightSource->width, h = m_heightSource->height;
	std::vector<std::uint8_t> zones = m_zones.size() == std::size_t(w) * h ? m_zones : std::vector<std::uint8_t>(std::size_t(w) * h, 0);
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Relief zones - %1").arg(m_name));
	auto* v = new QVBoxLayout(&dialog);
	v->setContentsMargins(12, 12, 12, 12);
	v->setSpacing(10);
	v->addWidget(hint(tr("Left mouse paints, right mouse erases. Blue: no relief there (stays flat). Orange: upside down. "
	                     "The texture repeats, so painting over an edge goes on at the other side.")));
	const QImage background = m_baseImage.isNull() && heights ? toImage(heightImage(*heights)) : m_baseImage;
	auto* painter = new HeightZonePainter(background, w, h, zones);
	v->addWidget(painter, 1);
	auto* tools = new QHBoxLayout;
	tools->setSpacing(10);
	auto* flat = new QRadioButton(tr("No relief here"));
	auto* inverted = new QRadioButton(tr("Upside down here"));
	flat->setChecked(true);
	connect(flat, &QRadioButton::toggled, painter, [painter](bool on) {
		if (on) painter->setZone(HeightZone::Flat);
	});
	connect(inverted, &QRadioButton::toggled, painter, [painter](bool on) {
		if (on) painter->setZone(HeightZone::Inverted);
	});
	auto* brush = new QSlider(Qt::Horizontal);
	brush->setRange(2, 96);
	brush->setValue(12);
	brush->setMaximumWidth(160);
	connect(brush, &QSlider::valueChanged, painter, &HeightZonePainter::setBrush);
	auto* clear = new QPushButton(tr("Clear"));
	connect(clear, &QPushButton::clicked, painter, &HeightZonePainter::clear);
	tools->addWidget(flat);
	tools->addWidget(inverted);
	tools->addStretch(1);
	tools->addWidget(new QLabel(tr("Brush")));
	tools->addWidget(brush);
	tools->addWidget(clear);
	v->addLayout(tools);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	v->addWidget(buttons);
	dialog.resize(600, 680);
	if (dialog.exec() == QDialog::Accepted) {
		setHeightZones(painter->zones(), w, h);
	}
}

void MaterialEditor::openMaterial(const QString& rawName) {
	auto* game = m_editor->game();
	if (!game) return;
	const QString name = QString::fromStdString(normalizeMaterialName(rawName.toStdString()));
	std::string error;
	const auto material = loadMaterial(game->files, name.toStdString(), &error);
	m_name = name;
	m_values.clear();
	m_edits.clear();
	m_base.reset();
	m_shaderChanged = false;
	m_originalText = game->files.readText("materials/" + name.toStdString() + ".vmt").value_or(std::string());
	m_isPatch = false;
	m_generatedBy.clear();
	// deploy.bat's own materials start with its marker: "// mapbase-fidelity: generated by deploy.ps1 (parallax) ..."
	if (const auto marker = QString::fromStdString(m_originalText.substr(0, m_originalText.find('\n'))); marker.startsWith("// mapbase-fidelity: generated")) {
		const auto open = marker.indexOf('('), close = marker.indexOf(')');
		m_generatedBy = open > 0 && close > open ? marker.mid(open + 1, close - open - 1) : QStringLiteral("deploy.bat");
	}
	try {
		const auto root = parseKeyValues(m_originalText);
		for (const auto& child : root.children) {
			if (child.block) {
				m_isPatch = iequals(child.key, "patch");
				if (m_isPatch) {
					m_base = loadMaterial(game->files, normalizeMaterialName(child.get("include")));
				}
				break;
			}
		}
	} catch (const std::exception&) {
	}
	if (material) {
		m_shader = QString::fromStdString(material->shader);
		for (const auto& [key, val] : material->params) {
			m_values[QString::fromStdString(key)] = QString::fromStdString(val);
		}
	} else {
		m_shader = "LightmappedGeneric";
	}
	m_title->setText(tr("<span style='font-size:13pt'><b>%1</b></span>").arg(name.toHtmlEscaped()));
	const auto mount = game->files.locate("materials/" + name.toStdString() + ".vmt");
	QString where;
	if (mount && game->files.mounts()[*mount].kind == Mount::Kind::Vpk) {
		where = tr("From %1 - Save puts an edited copy into the mod (it then replaces the game's).")
		            .arg(QString::fromStdString(displayPath(game->files.mounts()[*mount].path.filename())));
	} else if (!m_originalText.empty()) {
		where = QFileInfo::exists(savePath(name)) ? tr("In the mod: %1").arg(QDir::toNativeSeparators(savePath(name)))
		                                         : tr("From the game's folders - Save puts an edited copy into the mod.");
	} else {
		where = tr("Not found (%1) - Save creates it in the mod.").arg(QString::fromStdString(error));
	}
	if (m_isPatch) {
		where += tr(" Patch material: the kind comes from the material it includes.");
	}
	// Long paths wrap at their separators.
	m_where->setText(where.replace('/', QStringLiteral("/\u200b")).replace('\\', QStringLiteral("\\\u200b")));
	m_notice->setText(m_generatedBy.isEmpty() ? QString()
	                                          : tr("Made by deploy.bat (%1). Saving makes it the mod's own: deploy.bat then leaves it "
	                                               "alone. Delete the file to get the generated one back.").arg(m_generatedBy));
	m_notice->setVisible(!m_generatedBy.isEmpty());
	m_kindBox->setEnabled(!m_isPatch);
	m_save->setEnabled(false);
	m_saveAs->setEnabled(true);
	m_status->clear();
	m_baseImage = QImage();
	loadValuesIntoFields();
	loadHeightSource();
	refreshPreview(true);
}

void MaterialEditor::updateSections() {
	const bool open = !m_name.isEmpty();
	const Kind k = kind();
	const bool fidelity = fidelityShaders();
	const bool surface = k == Kind::Wall || k == Kind::Blend || k == Kind::Model;
	m_texturesBox->setVisible(open);
	m_bumpBox->setVisible(open && k != Kind::Water && k != Kind::Unlit);
	m_reliefBox->setVisible(open && (k == Kind::Wall || k == Kind::Blend));
	m_envBox->setVisible(open && surface);
	m_alphaBox->setVisible(open && k != Kind::Water);
	m_detailBox->setVisible(open && k != Kind::Water);
	m_waterBox->setVisible(open && k == Kind::Water);
	m_liquidBox->setVisible(open && k == Kind::Model);
	m_rows["$basetexture2"]->setVisible(k == Kind::Blend);
	if (auto* label = qobject_cast<QFormLayout*>(m_texturesBox->layout())->labelForField(m_rows["$basetexture2"])) label->setVisible(k == Kind::Blend);
	const auto showRow = [](QGroupBox* box, QWidget* row, bool on) {
		row->setVisible(on);
		if (auto* label = qobject_cast<QFormLayout*>(box->layout())->labelForField(row)) label->setVisible(on);
	};
	showRow(m_texturesBox, m_rows["$basetexture"], k != Kind::Water);
	showRow(m_detailBox, m_rows["$phongexponent"], m_phong->isChecked());
	showRow(m_detailBox, m_rows["$phongboost"], m_phong->isChecked());

	// mapbase-fidelity switches: only with its shaders in the mod.
	const bool relief = m_shader.compare("Fidelity_Parallax", Qt::CaseInsensitive) == 0 ||
	                    m_shader.compare("Fidelity_WorldVertexTransition", Qt::CaseInsensitive) == 0;
	m_relief->setEnabled(fidelity || relief);
	for (auto* label : m_fidelityMissing) label->setVisible(!fidelity);
	for (auto* w : std::initializer_list<QWidget*>{m_sliders["$parallaxdepth"]->parentWidget(), m_sliders["$parallaxao"]->parentWidget(),
	                                               m_sliders["$parallaxfade"]->parentWidget(), m_heightLabel->parentWidget()}) {
		showRow(m_reliefBox, w, relief);
	}
	m_fromNormals->setEnabled(!value("$bumpmap").isEmpty());
	m_zonesButton->setEnabled(m_heightSource.has_value());
	m_invert->setEnabled(m_heightSource.has_value());
	const bool fidelityWater = m_shader.compare("Water_Fidelity", Qt::CaseInsensitive) == 0;
	m_fidelityWater->setEnabled(fidelity || fidelityWater);
	showRow(m_waterBox, m_rows["$fidelityglint"], fidelityWater);
	showRow(m_waterBox, m_rows["$fidelityfoam"], fidelityWater);
	const bool liquid = m_shader.compare("Fidelity_Liquid", Qt::CaseInsensitive) == 0;
	m_liquid->setEnabled(fidelity || liquid);
	for (const char* key : {"$liquidcolor", "$liquidalpha", "fillMode", "$fillmin", "$fillmax", "$wobble", "$viscosity", "$glasscolor",
	                        "$glassalpha", "$splash", "$spill"}) {
		showRow(m_liquidBox, m_rows[key], liquid);
	}
	const bool fixedFill = liquid && !value("$fill").isEmpty() && value("$fill").toDouble() >= 0;
	showRow(m_liquidBox, m_rows["$fill"], fixedFill);
	showRow(m_liquidBox, m_rows["$fillmin"], liquid && !fixedFill);
	showRow(m_liquidBox, m_rows["$fillmax"], liquid && !fixedFill);
	m_shaderName->setText(open ? tr("Shader: %1").arg(m_shader) : QString());
}

void MaterialEditor::loadValuesIntoFields() {
	m_loading = true;
	const QString base = baseShader();
	int kindIndex = m_kindBox->findData(base, Qt::UserRole, Qt::MatchFixedString);
	if (kindIndex < 0) {
		m_kindBox->addItem(base, base);   // a shader the list does not know: kept as it is
		kindIndex = m_kindBox->count() - 1;
	}
	m_kindBox->setCurrentIndex(kindIndex);
	for (auto& [key, field] : m_textureFields) field->setText(value(key));
	for (auto& [key, field] : m_colorFields) field->setText(value(key));
	for (auto& [key, slider] : m_sliders) {
		const double minimum = slider->property("valueMin").toDouble(), maximum = slider->property("valueMax").toDouble();
		const double v = value(key).isEmpty() ? slider->property("fallback").toDouble() : value(key).toDouble();
		slider->setValue(int(std::clamp((v - minimum) / (maximum - minimum), 0.0, 1.0) * 1000 + 0.5));
		if (auto* label = slider->parentWidget()->findChild<QLabel*>()) {
			label->setText(QString::number(v, 'f', slider->property("decimals").toInt()));
		}
	}
	m_surface->setCurrentText(value("$surfaceprop"));
	m_ssbump->setChecked(value("$ssbump") == "1");
	const QString envmap = value("$envmap");
	m_reflection->setCurrentIndex(envmap.isEmpty() ? 0 : (envmap.compare("env_cubemap", Qt::CaseInsensitive) == 0 ? 1 : 2));
	m_mask->setCurrentIndex(value("$basealphaenvmapmask") == "1" ? 2 : value("$normalmapalphaenvmapmask") == "1" ? 3 : !value("$envmapmask").isEmpty() ? 1 : 0);
	m_transparency->setCurrentIndex(value("$translucent") == "1" ? 1 : value("$alphatest") == "1" ? 2 : value("$additive") == "1" ? 3 : 0);
	m_nocull->setChecked(value("$nocull") == "1");
	m_selfillum->setChecked(value("$selfillum") == "1");
	m_detailScale->setValue(value("$detailscale").isEmpty() ? 4.0 : value("$detailscale").toDouble());
	m_detailBlend->setCurrentIndex(std::clamp(value("$detailblendmode").toInt(), 0, 2));
	m_phong->setChecked(value("$phong") == "1");
	m_relief->setChecked(m_shader.compare("Fidelity_Parallax", Qt::CaseInsensitive) == 0 ||
	                     m_shader.compare("Fidelity_WorldVertexTransition", Qt::CaseInsensitive) == 0);
	m_invert->setChecked(m_heightInverted);
	m_fidelityWater->setChecked(m_shader.compare("Water_Fidelity", Qt::CaseInsensitive) == 0);
	m_liquid->setChecked(m_shader.compare("Fidelity_Liquid", Qt::CaseInsensitive) == 0);
	m_fillMode->setCurrentIndex(!value("$fill").isEmpty() && value("$fill").toDouble() >= 0 ? 1 : 0);
	m_splash->setChecked(value("$splash") == "1");
	m_spill->setChecked(value("$spill") == "1");
	m_loading = false;
	fillOtherTable();
	updateSections();
}

void MaterialEditor::fillOtherTable() {
	m_loading = true;
	m_other->setRowCount(0);
	for (const auto& [key, val] : m_values) {
		if (COVERED.contains(key)) continue;
		const int row = m_other->rowCount();
		m_other->insertRow(row);
		auto* keyItem = new QTableWidgetItem(key);
		keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
		m_other->setItem(row, 0, keyItem);
		m_other->setItem(row, 1, new QTableWidgetItem(val));
	}
	m_other->resizeColumnToContents(0);
	m_loading = false;
}

void MaterialEditor::refreshPreview(bool reloadTextures) {
	MaterialPreview::Settings s;
	const Kind k = kind();
	s.mode = k == Kind::Water ? MaterialPreview::Mode::Water
	                          : (m_shader.compare("Fidelity_Liquid", Qt::CaseInsensitive) == 0 ? MaterialPreview::Mode::Liquid : MaterialPreview::Mode::Surface);
	if (s.mode == MaterialPreview::Mode::Liquid && m_shape->currentIndex() == int(MaterialPreview::Shape::Plane)) {
		m_shape->setCurrentIndex(int(MaterialPreview::Shape::Cube));   // a container, not a wall
	}
	s.ssbump = value("$ssbump") == "1";
	s.envmap = !value("$envmap").isEmpty() && k != Kind::Water;
	s.maskMode = value("$basealphaenvmapmask") == "1" ? 2 : value("$normalmapalphaenvmapmask") == "1" ? 3 : !value("$envmapmask").isEmpty() ? 1 : 0;
	s.alphaMode = value("$translucent") == "1" ? 1 : value("$alphatest") == "1" ? 2 : value("$additive") == "1" ? 3 : 0;
	s.selfillum = value("$selfillum") == "1";
	s.phong = value("$phong") == "1";
	if (!value("$envmaptint").isEmpty()) s.envTint = parseColor(value("$envmaptint"));
	if (!value("$selfillumtint").isEmpty()) s.selfillumTint = parseColor(value("$selfillumtint"));
	if (!value("$detailscale").isEmpty()) s.detailScale = value("$detailscale").toFloat();
	s.detailBlend = value("$detailblendmode").toInt();
	if (!value("$phongboost").isEmpty()) s.phongBoost = value("$phongboost").toFloat();
	if (!value("$phongexponent").isEmpty()) s.phongExponent = value("$phongexponent").toFloat();
	if (!value("$alphatestreference").isEmpty()) s.alphaRef = value("$alphatestreference").toFloat();
	const bool relief = m_shader.compare("Fidelity_Parallax", Qt::CaseInsensitive) == 0 || m_shader.compare("Fidelity_WorldVertexTransition", Qt::CaseInsensitive) == 0;
	s.parallaxDepth = relief ? (value("$parallaxdepth").isEmpty() ? 0.02f : value("$parallaxdepth").toFloat()) : 0.f;
	if (!value("$parallaxao").isEmpty()) s.parallaxAO = value("$parallaxao").toFloat();
	s.waterGlint = m_shader.compare("Water_Fidelity", Qt::CaseInsensitive) == 0 ? (value("$fidelityglint").isEmpty() ? 1.f : value("$fidelityglint").toFloat()) : 0.f;
	if (!value("$reflecttint").isEmpty()) s.reflectTint = parseColor(value("$reflecttint"));
	if (!value("$reflectamount").isEmpty()) s.reflectAmount = std::clamp(value("$reflectamount").toFloat(), 0.f, 2.f);
	if (!value("$fogcolor").isEmpty()) s.fogColor = parseColor(value("$fogcolor"), s.fogColor);
	if (!value("$liquidcolor").isEmpty()) s.liquidColor = parseColor(value("$liquidcolor"), s.liquidColor);
	if (!value("$liquidalpha").isEmpty()) s.liquidAlpha = value("$liquidalpha").toFloat();
	if (!value("$fill").isEmpty() && value("$fill").toDouble() >= 0) s.fill = value("$fill").toFloat();
	if (!value("$glasscolor").isEmpty()) s.glassColor = parseColor(value("$glasscolor"), s.glassColor);
	if (!value("$glassalpha").isEmpty()) s.glassAlpha = value("$glassalpha").toFloat();
	m_preview->setSettings(s);
	if (!reloadTextures) return;
	auto* game = m_editor->game();
	if (!game) return;
	std::map<QString, std::string> wanted;
	for (const char* key : {"$basetexture", "$bumpmap", "$envmapmask", "$detail", "$normalmap"}) {
		if (!value(key).isEmpty()) wanted[key] = value(key).toStdString();
	}
	const GameFileSystem* fs = &game->files;
	m_textureWatcher.setFuture(QtConcurrent::run([fs, wanted] {
		std::map<QString, QImage> images;
		for (const auto& [key, name] : wanted) {
			if (auto image = loadTexture(*fs, name, 512)) images[key] = toImage(*image);
		}
		return images;
	}));
}

QString MaterialEditor::savePath(const QString& name) const {
	const auto* entry = m_editor->gameEntry();
	return entry ? fromPath(entry->gameDir) + "/materials/" + name + ".vmt" : QString();
}

bool MaterialEditor::save(const QString& asName) {
	auto* game = m_editor->game();
	if (!game || m_name.isEmpty()) return false;
	const QString name = asName.isEmpty() ? m_name : QString::fromStdString(normalizeMaterialName(asName.toStdString()));
	if (name.contains("..") || name.isEmpty()) {
		m_status->setText(tr("Not a usable material name"));
		return false;
	}
	const QString gameDir = fromPath(m_editor->gameEntry()->gameDir);
	const auto writeFile = [&](const QString& path, const char* data, qint64 size) {
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data, size) != size) {
			m_status->setText(tr("Could not write %1").arg(QDir::toNativeSeparators(path)));
			return false;
		}
		file.close();
		game->files.noteWrittenFile(toPath(path));
		return true;
	};
	// A new or changed height map is written next to the material (I8, as deploy.bat's).
	const bool relief = m_shader.compare("Fidelity_Parallax", Qt::CaseInsensitive) == 0 || m_shader.compare("Fidelity_WorldVertexTransition", Qt::CaseInsensitive) == 0;
	if (relief && m_heightSource && (m_heightDirty || value("$parallaxmap").isEmpty() || !asName.isEmpty())) {
		const auto heights = currentHeights();
		const auto vtf = heights ? encodeHeightVtf(*heights) : std::vector<std::byte>{};
		const QString heightName = name + "_relief";
		if (vtf.empty() || !writeFile(gameDir + "/materials/" + heightName + ".vtf", reinterpret_cast<const char*>(vtf.data()), qint64(vtf.size()))) {
			if (vtf.empty()) m_status->setText(tr("The height map could not be written"));
			return false;
		}
		setValue("$parallaxmap", heightName);
	}
	std::string text;
	try {
		if (!asName.isEmpty() || m_originalText.empty()) {
			// A new material: everything as it is now (a patch is written out in full).
			std::vector<MaterialEdit> params;
			for (const auto& [key, val] : m_values) params.push_back({key.toStdString(), val.toStdString()});
			text = newMaterialText(m_shader.toStdString(), params);
		} else {
			std::vector<MaterialEdit> edits;
			for (const auto& [key, val] : m_edits) {
				edits.push_back({key.toStdString(), val ? std::optional<std::string>(val->toStdString()) : std::nullopt});
			}
			// deploy.bat's marker goes with the comments: from now on the material is the mod's own.
			text = editMaterialText(m_originalText, m_isPatch ? std::string() : m_shader.toStdString(), edits, m_base ? &*m_base : nullptr);
		}
	} catch (const std::exception& e) {
		m_status->setText(tr("The material could not be read: %1").arg(QString::fromUtf8(e.what())));
		return false;
	}
	const QString path = savePath(name);
	if (!writeFile(path, text.data(), qint64(text.size()))) {
		return false;
	}
	emit materialSaved(name);
	openMaterial(name);   // read back what the game will read
	m_status->setText(tr("Saved %1").arg(QDir::toNativeSeparators(path)));
	return true;
}

} // namespace sme::app
