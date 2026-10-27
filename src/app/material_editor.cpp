// material_editor.cpp - Material editor dock (see material_editor.h).

#include "material_editor.h"

#include "editor.h"
#include "material_preview.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
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
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStringListModel>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <set>

#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/keyvalues.h>
#include <sme/text.h>
#include <sme/texture.h>

namespace sme::app {

namespace {

// Keys the fields above edit; the table lists every other parameter.
const std::set<QString> COVERED = {"$basetexture", "$basetexture2", "$bumpmap", "$ssbump", "$envmap", "$envmaptint", "$envmapmask",
                                   "$basealphaenvmapmask", "$normalmapalphaenvmapmask", "$translucent", "$alphatest", "$additive",
                                   "$nocull", "$selfillum", "$detail", "$detailscale", "$detailblendmode", "$phong", "$phongexponent",
                                   "$phongboost", "$surfaceprop"};

const QStringList SURFACES = {"default", "concrete", "brick", "rock", "metal", "metal_box", "metalpanel", "metalgrate", "metalvent",
                              "chainlink", "wood", "wood_box", "wood_crate", "wood_plank", "wood_panel", "dirt", "grass", "gravel",
                              "sand", "mud", "snow", "ice", "tile", "plaster", "glass", "computer", "carpet", "cardboard",
                              "plastic", "rubber", "paper", "porcelain", "water", "slime", "quicksand", "ladder", "foliage"};

QVector3D parseColor(const QString& text) {
	QString t = text;
	t.remove('[').remove(']').remove('{').remove('}');
	const auto parts = t.split(' ', Qt::SkipEmptyParts);
	if (parts.size() < 3) return {1, 1, 1};
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

} // namespace

MaterialEditor::MaterialEditor(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	auto* outer = new QHBoxLayout(this);
	outer->setContentsMargins(4, 4, 4, 4);
	auto* split = new QSplitter(Qt::Horizontal);
	outer->addWidget(split);

	// Left: the fields.
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* fields = new QWidget;
	auto* v = new QVBoxLayout(fields);
	m_title = new QLabel(tr("<b>No material</b> - right-click a material in the Asset Browser: Edit material"));
	m_title->setWordWrap(true);
	m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
	v->addWidget(m_title);
	m_where = new QLabel;
	m_where->setWordWrap(true);
	m_where->setStyleSheet("color: #8a90a0;");
	v->addWidget(m_where);
	scroll->setWidget(fields);
	split->addWidget(scroll);
	buildFields();
	v->addStretch(1);

	// Right: preview, light and shape, save.
	auto* right = new QWidget;
	auto* r = new QVBoxLayout(right);
	r->setContentsMargins(4, 0, 0, 0);
	m_preview = new MaterialPreview;
	r->addWidget(m_preview, 1);
	auto* choices = new QHBoxLayout;
	m_shape = new QComboBox;
	m_shape->addItems({tr("Wall"), tr("Cube"), tr("Sphere")});
	m_lighting = new QComboBox;
	m_lighting->addItems({tr("Sun (outdoors)"), tr("Lamp (indoors, warm)"), tr("Night (dim, cold)"), tr("Unlit (texture only)")});
	m_lighting->setToolTip(tr("Right drag in the preview moves the light"));
	choices->addWidget(m_shape);
	choices->addWidget(m_lighting, 1);
	r->addLayout(choices);
	auto* buttons = new QHBoxLayout;
	m_save = new QPushButton(tr("Save"));
	m_saveAs = new QPushButton(tr("Save as new ..."));
	m_save->setEnabled(false);
	m_saveAs->setEnabled(false);
	buttons->addWidget(m_save);
	buttons->addWidget(m_saveAs);
	r->addLayout(buttons);
	m_status = new QLabel;
	m_status->setWordWrap(true);
	m_status->setStyleSheet("color: #8a90a0;");
	r->addWidget(m_status);
	split->addWidget(right);
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
		const QString keys[MaterialPreview::SlotCount] = {"$basetexture", "$bumpmap", "$envmapmask", "$detail"};
		for (int slot = 0; slot < MaterialPreview::SlotCount; ++slot) {
			const auto it = images.find(keys[slot]);
			m_preview->setTexture(static_cast<MaterialPreview::Slot>(slot), it == images.end() ? QImage() : it->second);
		}
		emit previewUpdated();
	});
	connect(editor, &Editor::gameChanged, this, [this] {
		m_name.clear();
		m_values.clear();
		m_edits.clear();
		m_title->setText(tr("<b>No material</b>"));
		m_save->setEnabled(false);
		m_saveAs->setEnabled(false);
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

void MaterialEditor::buildFields() {
	auto* layout = qobject_cast<QVBoxLayout*>(m_title->parentWidget()->layout());   // the fields column
	const auto group = [&](const QString& title) {
		auto* box = new QGroupBox(title);
		auto* form = new QFormLayout(box);
		layout->addWidget(box);
		return form;
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

	auto* basic = group(tr("Surface"));
	m_shaderBox = new QComboBox;
	m_shaderBox->setEditable(true);
	m_shaderBox->addItems({"LightmappedGeneric", "VertexLitGeneric", "UnlitGeneric", "WorldVertexTransition", "UnlitTwoTexture"});
	m_shaderBox->setToolTip(tr("LightmappedGeneric: walls and floors. VertexLitGeneric: models. WorldVertexTransition: two blended "
	                           "textures on displacements. UnlitGeneric: no lighting (screens, sky)."));
	connect(m_shaderBox, &QComboBox::currentTextChanged, this, [this](const QString& text) {
		if (m_loading || text == m_shader) return;
		m_shader = text;
		m_save->setEnabled(!m_name.isEmpty());
		refreshPreview(false);
	});
	basic->addRow(tr("Shader"), m_shaderBox);
	basic->addRow(tr("Texture"), textureField("$basetexture", tr("folder/texture (without .vtf)")));
	basic->addRow(tr("Blend texture"), textureField("$basetexture2", tr("second texture (WorldVertexTransition)")));
	m_surface = new QComboBox;
	m_surface->setObjectName("surfaceprop");
	m_surface->setEditable(true);
	m_surface->addItems(SURFACES);
	m_surface->setToolTip(tr("Footsteps, bullet impacts and physics (scripts/surfaceproperties.txt)"));
	connect(m_surface, &QComboBox::currentTextChanged, this, [this](const QString& text) {
		if (!m_loading) setValue("$surfaceprop", text.trimmed());
	});
	basic->addRow(tr("Surface property"), m_surface);

	auto* bump = group(tr("Normal map"));
	bump->addRow(tr("Normal map"), textureField("$bumpmap", tr("folder/texture_normal")));
	m_ssbump = check("$ssbump", tr("Self-shadowed (ssbump)"), tr("The normal map is an ssbump (made with height2ssbump)"));
	bump->addRow(m_ssbump);

	auto* env = group(tr("Reflections"));
	m_reflection = new QComboBox;
	m_reflection->setObjectName("reflection");
	m_reflection->addItems({tr("None"), tr("env_cubemap (the nearest cubemap)"), tr("Custom cubemap texture")});
	connect(m_reflection, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (m_loading) return;
		if (index == 0) setValue("$envmap", "");
		else if (index == 1) setValue("$envmap", "env_cubemap");
		else if (value("$envmap").isEmpty() || value("$envmap") == "env_cubemap") setValue("$envmap", "editor/cubemap");
	});
	env->addRow(tr("Reflection"), m_reflection);
	auto* tintRow = new QHBoxLayout;
	m_envTint = new QLineEdit;
	m_envTint->setPlaceholderText(tr("[1 1 1] - darker: [.3 .3 .3]"));
	connect(m_envTint, &QLineEdit::editingFinished, this, [this] {
		if (m_envTint->text().trimmed() != value("$envmaptint")) setValue("$envmaptint", m_envTint->text().trimmed());
	});
	auto* pick = new QToolButton;
	pick->setText(QStringLiteral("■"));
	connect(pick, &QToolButton::clicked, this, [this] {
		const QVector3D c = parseColor(value("$envmaptint").isEmpty() ? "[1 1 1]" : value("$envmaptint"));
		const QColor color = QColorDialog::getColor(QColor::fromRgbF(std::clamp(c.x(), 0.f, 1.f), std::clamp(c.y(), 0.f, 1.f), std::clamp(c.z(), 0.f, 1.f)), this);
		if (color.isValid()) {
			setValue("$envmaptint", QStringLiteral("[%1 %2 %3]").arg(color.redF(), 0, 'f', 2).arg(color.greenF(), 0, 'f', 2).arg(color.blueF(), 0, 'f', 2));
			loadValuesIntoFields();
		}
	});
	tintRow->addWidget(m_envTint, 1);
	tintRow->addWidget(pick);
	env->addRow(tr("Strength / tint"), tintRow);
	m_mask = new QComboBox;
	m_mask->addItems({tr("Everywhere"), tr("Mask texture ($envmapmask)"), tr("Texture's alpha ($basealphaenvmapmask)"),
	                  tr("Normal map's alpha ($normalmapalphaenvmapmask)")});
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
	env->addRow(tr("Reflects where"), m_mask);
	env->addRow(tr("Mask texture"), textureField("$envmapmask", tr("folder/texture_mask")));
	m_parallaxNote = new QLabel(tr("Parallax-corrected (Mapbase): reflections line up with the room when the cubemap that "
	                               "captures this surface has a room box (select the env_cubemap: Add room box). VBSP adds "
	                               "$envmapparallax to the material itself."));
	m_parallaxNote->setWordWrap(true);
	m_parallaxNote->setStyleSheet("color: #8a90a0;");
	env->addRow(m_parallaxNote);

	auto* alpha = group(tr("Transparency"));
	m_transparency = new QComboBox;
	m_transparency->addItems({tr("Opaque"), tr("See-through ($translucent: glass, smoke)"), tr("Cut out ($alphatest: fences, leaves)"),
	                          tr("Glowing ($additive: light beams)")});
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
	m_nocull = check("$nocull", tr("Both sides ($nocull)"));
	alpha->addRow(m_nocull);
	m_selfillum = check("$selfillum", tr("Glows by itself where the texture's alpha is white ($selfillum)"));
	alpha->addRow(m_selfillum);

	auto* detail = group(tr("Detail and shine"));
	detail->addRow(tr("Detail texture"), textureField("$detail", tr("detail/noise_detail_01")));
	m_detailScale = new QDoubleSpinBox;
	m_detailScale->setRange(0.01, 64);
	m_detailScale->setValue(4);
	m_detailScale->setKeyboardTracking(false);
	connect(m_detailScale, &QDoubleSpinBox::valueChanged, this, [this](double scale) {
		if (!m_loading) setValue("$detailscale", QString::number(scale));
	});
	detail->addRow(tr("Detail scale"), m_detailScale);
	m_detailBlend = new QComboBox;
	m_detailBlend->addItems({tr("Multiply (0)"), tr("Add (1)"), tr("Over the texture (2)")});
	connect(m_detailBlend, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (!m_loading) setValue("$detailblendmode", index == 0 ? "" : QString::number(index));
	});
	detail->addRow(tr("Detail blend"), m_detailBlend);
	m_phong = check("$phong", tr("Shiny highlights ($phong)"), tr("Models (VertexLitGeneric); Mapbase also on brushes. Mask: the normal map's alpha"));
	detail->addRow(m_phong);
	m_phongExponent = new QDoubleSpinBox;
	m_phongExponent->setRange(1, 255);
	m_phongExponent->setValue(5);
	m_phongExponent->setKeyboardTracking(false);
	connect(m_phongExponent, &QDoubleSpinBox::valueChanged, this, [this](double v) {
		if (!m_loading) setValue("$phongexponent", QString::number(v));
	});
	detail->addRow(tr("Highlight size (exponent)"), m_phongExponent);
	m_phongBoost = new QDoubleSpinBox;
	m_phongBoost->setRange(0, 64);
	m_phongBoost->setValue(1);
	m_phongBoost->setKeyboardTracking(false);
	connect(m_phongBoost, &QDoubleSpinBox::valueChanged, this, [this](double v) {
		if (!m_loading) setValue("$phongboost", QString::number(v));
	});
	detail->addRow(tr("Highlight strength"), m_phongBoost);

	// Everything else, as text.
	auto* otherBox = new QGroupBox(tr("Other parameters"));
	auto* o = new QVBoxLayout(otherBox);
	m_other = new QTableWidget;
	m_other->setObjectName("otherParams");
	m_other->setColumnCount(2);
	m_other->setHorizontalHeaderLabels({tr("Parameter"), tr("Value")});
	m_other->horizontalHeader()->setStretchLastSection(true);
	m_other->verticalHeader()->setVisible(false);
	m_other->setMinimumHeight(120);
	connect(m_other, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
		if (m_loading || item->column() != 1) return;
		setValue(m_other->item(item->row(), 0)->text(), item->text());
	});
	o->addWidget(m_other);
	auto* addRow = new QHBoxLayout;
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
	o->addLayout(addRow);
	layout->addWidget(otherBox);
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
	const bool textureKey = key == "$basetexture" || key == "$bumpmap" || key == "$envmapmask" || key == "$detail";
	refreshPreview(textureKey);
	if (!COVERED.contains(key) && !m_loading) fillOtherTable();
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
	m_originalText = game->files.readText("materials/" + name.toStdString() + ".vmt").value_or(std::string());
	m_isPatch = false;
	try {
		const auto root = parseKeyValues(m_originalText);
		for (const auto& child : root.children) {
			if (child.block) {
				m_isPatch = iequals(child.key, "patch");
				if (m_isPatch) {
					const auto include = child.get("include");
					m_base = loadMaterial(game->files, normalizeMaterialName(include));
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
	m_title->setText(tr("<b>%1</b> (%2)").arg(name.toHtmlEscaped(), m_shader));
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
		where += tr(" Patch material: the shader comes from the material it includes.");
	}
	m_where->setText(where);
	m_shaderBox->setEnabled(!m_isPatch);
	m_save->setEnabled(false);
	m_saveAs->setEnabled(true);
	m_status->clear();
	loadValuesIntoFields();
	refreshPreview(true);
}

void MaterialEditor::loadValuesIntoFields() {
	m_loading = true;
	m_shaderBox->setCurrentText(m_shader);
	for (auto& [key, field] : m_textureFields) {
		field->setText(value(key));
	}
	m_surface->setCurrentText(value("$surfaceprop"));
	m_ssbump->setChecked(value("$ssbump") == "1");
	const QString envmap = value("$envmap");
	m_reflection->setCurrentIndex(envmap.isEmpty() ? 0 : (envmap.compare("env_cubemap", Qt::CaseInsensitive) == 0 ? 1 : 2));
	m_envTint->setText(value("$envmaptint"));
	m_mask->setCurrentIndex(value("$basealphaenvmapmask") == "1" ? 2 : value("$normalmapalphaenvmapmask") == "1" ? 3 : !value("$envmapmask").isEmpty() ? 1 : 0);
	m_transparency->setCurrentIndex(value("$translucent") == "1" ? 1 : value("$alphatest") == "1" ? 2 : value("$additive") == "1" ? 3 : 0);
	m_nocull->setChecked(value("$nocull") == "1");
	m_selfillum->setChecked(value("$selfillum") == "1");
	m_detailScale->setValue(value("$detailscale").isEmpty() ? 4.0 : value("$detailscale").toDouble());
	m_detailBlend->setCurrentIndex(std::clamp(value("$detailblendmode").toInt(), 0, 2));
	m_phong->setChecked(value("$phong") == "1");
	m_phongExponent->setValue(value("$phongexponent").isEmpty() ? 5.0 : value("$phongexponent").toDouble());
	m_phongBoost->setValue(value("$phongboost").isEmpty() ? 1.0 : value("$phongboost").toDouble());
	m_loading = false;
	fillOtherTable();
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
	s.ssbump = value("$ssbump") == "1";
	s.envmap = !value("$envmap").isEmpty();
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
	m_preview->setSettings(s);
	if (!reloadTextures) return;
	auto* game = m_editor->game();
	if (!game) return;
	std::map<QString, std::string> wanted;
	for (const char* key : {"$basetexture", "$bumpmap", "$envmapmask", "$detail"}) {
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
			text = editMaterialText(m_originalText, m_isPatch ? std::string() : m_shader.toStdString(), edits, m_base ? &*m_base : nullptr);
		}
	} catch (const std::exception& e) {
		m_status->setText(tr("The material could not be read: %1").arg(QString::fromUtf8(e.what())));
		return false;
	}
	const QString path = savePath(name);
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(text.data(), qint64(text.size())) != qint64(text.size())) {
		m_status->setText(tr("Could not write %1").arg(QDir::toNativeSeparators(path)));
		return false;
	}
	file.close();
	game->files.noteWrittenFile(toPath(path));
	m_status->setText(tr("Saved %1").arg(QDir::toNativeSeparators(path)));
	emit materialSaved(name);
	openMaterial(name);   // read back what the game will read
	m_status->setText(tr("Saved %1").arg(QDir::toNativeSeparators(path)));
	return true;
}

} // namespace sme::app
