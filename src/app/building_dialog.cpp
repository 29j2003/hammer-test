// building_dialog.cpp - "Build buildings" with a live preview (see building_dialog.h).

#include "building_dialog.h"

#include "asset_browser.h"
#include "dialog_fit.h"
#include "editor.h"
#include "material_field.h"
#include "model_cache.h"
#include "ui_helpers.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

#include <sme/material.h>
#include <sme/text.h>
#include <sme/texture.h>

namespace sme::app {

namespace {

QDoubleSpinBox* units(QWidget* parent, const char* name, double lo, double hi, double value, double step, const QString& suffix = QObject::tr(" units")) {
	auto* box = new QDoubleSpinBox(parent);
	box->setObjectName(name);
	box->setRange(lo, hi);
	box->setDecimals(0);
	box->setSingleStep(step);
	box->setSuffix(suffix);
	box->setValue(value);
	return box;
}

QSpinBox* count(QWidget* parent, const char* name, int lo, int hi, int value) {
	auto* box = new QSpinBox(parent);
	box->setObjectName(name);
	box->setRange(lo, hi);
	box->setValue(value);
	return box;
}

QComboBox* choices(QWidget* parent, const char* name, const QStringList& items) {
	auto* box = new QComboBox(parent);
	box->setObjectName(name);
	box->addItems(items);
	return box;
}

QCheckBox* check(QWidget* parent, const char* name, const QString& text, bool on) {
	auto* box = new QCheckBox(text, parent);
	box->setObjectName(name);
	box->setChecked(on);
	return box;
}

/// A titled group of fields in one of the Advanced sections.
QWidget* page(QWidget* section, const QString& title, QFormLayout*& form) {
	auto* w = new QGroupBox(title, section);
	form = new QFormLayout(w);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	section->layout()->addWidget(w);
	return w;
}

/// "How many windows" from many to few: about one window column every so many units (0: every one).
constexpr std::array<double, 9> WINDOW_SPACINGS{0, 96, 128, 160, 176, 224, 288, 384, 512};
constexpr double DEFAULT_WINDOW_SPACING = 176;

/// The slider step nearest a spacing.
int windowAmountFor(double spacing) {
	int best = 0;
	for (std::size_t i = 1; i < WINDOW_SPACINGS.size(); ++i) {
		if (std::abs(WINDOW_SPACINGS[i] - spacing) < std::abs(WINDOW_SPACINGS[static_cast<std::size_t>(best)] - spacing)) best = static_cast<int>(i);
	}
	return best;
}

/// Where "Read as" keeps the role a user set for each material.
const char* const ROLE_SETTINGS = "buildings/materialRoles";

/// The material's key: lower case, forward slashes.
std::string materialKey(const std::string& material) {
	return toLower(normalizeMaterialName(trim(material)));
}

const char* const PART_NAMES[] = {QT_TRANSLATE_NOOP("BuildingDialog", "Walls"),   QT_TRANSLATE_NOOP("BuildingDialog", "Ground floor"),
                                  QT_TRANSLATE_NOOP("BuildingDialog", "Windows"), QT_TRANSLATE_NOOP("BuildingDialog", "Doors"),
                                  QT_TRANSLATE_NOOP("BuildingDialog", "Trim"),    QT_TRANSLATE_NOOP("BuildingDialog", "Cornices"),
                                  QT_TRANSLATE_NOOP("BuildingDialog", "Roof"),    QT_TRANSLATE_NOOP("BuildingDialog", "Interior")};

const char* const ANCHOR_NAMES[] = {QT_TRANSLATE_NOOP("BuildingDialog", "In each window"), QT_TRANSLATE_NOOP("BuildingDialog", "Below windows"),
                                    QT_TRANSLATE_NOOP("BuildingDialog", "Above windows"),  QT_TRANSLATE_NOOP("BuildingDialog", "At doors"),
                                    QT_TRANSLATE_NOOP("BuildingDialog", "Corners"),        QT_TRANSLATE_NOOP("BuildingDialog", "Wall bay (replaces it)"),
                                    QT_TRANSLATE_NOOP("BuildingDialog", "Along the base"), QT_TRANSLATE_NOOP("BuildingDialog", "On the roof"),
                                    QT_TRANSLATE_NOOP("BuildingDialog", "Along the roof edge")};

/// The Models list takes models dropped from the asset browser.
class PropDropFilter : public QObject {
public:
	PropDropFilter(QObject* parent, std::function<void(const QString&)> dropped) : QObject(parent), m_dropped(std::move(dropped)) {}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override {
		if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
			auto* drag = static_cast<QDragMoveEvent*>(event);
			if (drag->mimeData()->hasFormat("application/x-sme-model") || drag->mimeData()->hasText()) {
				drag->acceptProposedAction();
				return true;
			}
		} else if (event->type() == QEvent::Drop) {
			auto* drop = static_cast<QDropEvent*>(event);
			QString model = drop->mimeData()->hasFormat("application/x-sme-model") ? QString::fromUtf8(drop->mimeData()->data("application/x-sme-model"))
			                                                                        : drop->mimeData()->text();
			model = model.trimmed();
			if (model.endsWith(".mdl", Qt::CaseInsensitive)) m_dropped(model);
			drop->acceptProposedAction();
			return true;
		}
		return QObject::eventFilter(watched, event);
	}

private:
	std::function<void(const QString&)> m_dropped;
};

enum PropColumn { ColSlot, ColModel, ColEvery, ColChance, ColWhen, ColOut, ColRemove };
enum SideColumn { ColSide, ColLength, ColNow, ColState, ColWindows, ColPattern, ColDoors };

} // namespace

BuildingDialog::BuildingDialog(Editor* editor, std::function<QString()> browserModel, QWidget* parent)
    : QDialog(parent), m_editor(editor), m_browserModel(std::move(browserModel)) {
	setObjectName("buildingDialog");
	setWindowTitle(tr("Build buildings"));
	setModal(false);
	auto* outer = new QVBoxLayout(this);
	auto* intro = new QLabel(tr("Each selected brush becomes a building (no interiors). Brushes that touch form a row: the sides they share get no facade."), this);
	intro->setWordWrap(true);
	outer->addWidget(intro);
	const BuildingOptions d = defaultBuildingOptions();
	// --- The simple page: a style and the few things that matter -------------------------
	{
		auto* simple = new QFormLayout;
		simple->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		m_style = choices(this, "buildingStyle", {tr("Custom (keep my settings)"), tr("Plaster block"), tr("Brick tenement"), tr("Panel block"), tr("Shop row"),
		                                           tr("Industrial")});
		m_style->setToolTip(tr("City 17 / Eastern bloc looks: materials of the game found by their names, floors, flat bands, the roof and details. Pick one, "
		                       "then change what you like"));
		simple->addRow(tr("Style"), m_style);
		m_simpleFloors = count(this, "simpleFloors", 1, 100, d.floors);
		m_simpleFloors->setToolTip(tr("All floors, the ground floor included (the height follows; Advanced has other ways)"));
		simple->addRow(tr("Floors"), m_simpleFloors);
		m_simpleFacade = new MaterialField("simpleFacade", editor, this);
		m_simpleFacade->setToolTip(tr("The walls. A texture with its windows in it (HL2's building textures) is fitted to the floors: no windows are built"));
		simple->addRow(tr("Facade"), m_simpleFacade);
		{
			// What the facade's picture is: the windows found in it outlined, one line, "Read as".
			auto* row = new QWidget(this);
			auto* h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			m_facadePicture = new QLabel(row);
			m_facadePicture->setObjectName("simpleFacadeWindows");
			m_facadePicture->setFixedSize(72, 72);
			m_facadePicture->setAlignment(Qt::AlignCenter);
			m_facadePicture->setToolTip(tr("The facade texture; the windows Mapsmith found in it are outlined"));
			h->addWidget(m_facadePicture);
			auto* v = new QVBoxLayout;
			m_facadeDecision = new QLabel(row);
			m_facadeDecision->setObjectName("simpleFacadeDecision");
			m_facadeDecision->setWordWrap(true);
			m_facadeDecision->setToolTip(tr("What Mapsmith reads the facade texture as - it decides where the texture goes, its scale (whole floors and "
			                                "windows) and whether windows are built"));
			v->addWidget(m_facadeDecision);
			auto* readAs = new QHBoxLayout;
			readAs->addWidget(new QLabel(tr("Read as"), row));
			m_facadeRole = choices(row, "simpleFacadeRole", {tr("Automatic (from the picture)"), tr("Facade with windows"), tr("Plain wall"), tr("Trim strip"),
			                                                 tr("Roof"), tr("Shop front"), tr("Unsuitable")});
			m_facadeRole->setToolTip(tr("If the picture was read wrong: what this texture is (remembered for this material)"));
			readAs->addWidget(m_facadeRole, 1);
			v->addLayout(readAs);
			h->addLayout(v, 1);
			simple->addRow(QString(), row);
		}
		m_simpleRoof = new MaterialField("simpleRoof", editor, this);
		simple->addRow(tr("Roof"), m_simpleRoof);
		m_simpleWindows = choices(this, "simpleWindows", {tr("From the facade texture"), tr("Built into the wall"), tr("None")});
		m_simpleWindows->setToolTip(tr("From the facade texture: the wall texture already shows the windows - it is fitted so they line up with the floors. "
		                               "Built into the wall: shallow openings with one window texture each"));
		simple->addRow(tr("Windows"), m_simpleWindows);
		m_windowAmount = new QSlider(Qt::Horizontal, this);
		m_windowAmount->setObjectName("simpleWindowAmount");
		m_windowAmount->setRange(0, static_cast<int>(WINDOW_SPACINGS.size()) - 1);
		m_windowAmount->setPageStep(1);
		m_windowAmount->setTickPosition(QSlider::TicksBelow);
		m_windowAmount->setToolTip(tr("Facade textures: only every few painted windows stay, plain wall strips cover the others. Built windows: fewer per floor"));
		{
			auto* row = new QWidget(this);
			auto* h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			h->addWidget(new QLabel(tr("Many"), row));
			h->addWidget(m_windowAmount, 1);
			h->addWidget(new QLabel(tr("Few"), row));
			m_windowAmountLabel = new QLabel(row);
			m_windowAmountLabel->setObjectName("simpleWindowAmountLabel");
			m_windowAmountLabel->setMinimumWidth(m_windowAmountLabel->fontMetrics().horizontalAdvance(tr("one every 512 units")));
			h->addWidget(m_windowAmountLabel);
			simple->addRow(tr("How many windows"), row);
		}
		m_detail = choices(this, "buildingDetail", {tr("None"), tr("Some"), tr("Lots")});
		m_detail->setToolTip(tr("Drainpipes, gutters, and the game's vents, AC units, antennas and railings on roofs and walls"));
		simple->addRow(tr("Detail"), m_detail);
		m_simpleCondition = new QSlider(Qt::Horizontal, this);
		m_simpleCondition->setObjectName("simpleCondition");
		m_simpleCondition->setRange(0, 100);
		m_simpleCondition->setToolTip(tr("Intact ... destroyed: lost floors, a jagged top, broken windows, rubble"));
		{
			auto* row = new QWidget(this);
			auto* h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			h->addWidget(new QLabel(tr("Intact"), row));
			h->addWidget(m_simpleCondition, 1);
			h->addWidget(new QLabel(tr("Destroyed"), row));
			simple->addRow(tr("Condition"), row);
		}
		m_simpleSeed = count(this, "simpleSeed", 1, 999999, 1);
		m_simpleSeed->setToolTip(tr("Another number gives other random choices (which floors, which props)"));
		{
			auto* row = new QWidget(this);
			auto* h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			h->addWidget(m_simpleSeed, 1);
			auto* pick = new QPushButton(tr("Change a side ..."), row);
			pick->setObjectName("buildingPickSideSimple");
			pick->setToolTip(tr("Click next to a wall in the 3D or top view to set that side: facade, plain wall, open or nodraw"));
			connect(pick, &QPushButton::clicked, this, [this] {
				m_editor->requestScenePoint(tr("Buildings: click next to a side - Esc stops"), [this](const Vec3& point) { pickSideAt(point); });
			});
			h->addWidget(pick);
			simple->addRow(tr("Seed"), row);
		}
		outer->addLayout(simple);
	}
	m_advanced = new QToolButton(this);
	m_advanced->setObjectName("buildingAdvanced");
	m_advanced->setCheckable(true);
	m_advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	m_advanced->setArrowType(Qt::RightArrow);
	m_advanced->setText(tr("Advanced"));
	m_advanced->setAutoRaise(true);
	outer->addWidget(m_advanced);
	m_tabs = new QTabWidget(this);
	m_tabs->setObjectName("buildingTabs");
	outer->addWidget(m_tabs, 1);
	std::array<QWidget*, 3> sections{};
	{
		const QString titles[] = {tr("Shape && sides"), tr("Facade"), tr("Materials, models && variation")};
		for (std::size_t i = 0; i < sections.size(); ++i) {
			auto* scroll = new QScrollArea(m_tabs);
			scroll->setWidgetResizable(true);
			scroll->setFrameShape(QFrame::NoFrame);
			sections[i] = new QWidget(scroll);
			new QVBoxLayout(sections[i]);
			scroll->setWidget(sections[i]);
			m_tabs->addTab(scroll, titles[i]);
		}
	}
	m_tabs->hide();
	connect(m_advanced, &QToolButton::toggled, this, [this](bool on) {
		m_tabs->setVisible(on);
		m_advanced->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
		QSettings().setValue("buildings/advanced", on);
		if (on) {
			// Room for the sections, within the screen.
			const QRect area = availableWindowArea(this);
			resize(std::max(width(), std::min(760, area.width() - 32)), std::max(height(), std::min(860, area.height() - 48)));
		} else {
			adjustSize();
		}
	});
	QFormLayout* form = nullptr;

	// --- Shape ---------------------------------------------------------------
	page(sections[0], tr("Shape"), form);
	m_editInfo = new QLabel(this);
	m_editInfo->setObjectName("buildingEditInfo");
	m_editInfo->setWordWrap(true);
	m_editInfo->hide();
	form->addRow(m_editInfo);
	m_sizeRow = new QWidget(this);
	{
		auto* h = new QHBoxLayout(m_sizeRow);
		h->setContentsMargins(0, 0, 0, 0);
		m_sizeLength = units(m_sizeRow, "buildingSizeLength", 32, 65536, 512, 16);
		m_sizeLength->setToolTip(tr("Along its longest side"));
		m_sizeDepth = units(m_sizeRow, "buildingSizeDepth", 32, 65536, 256, 16);
		m_sizeDepth->setToolTip(tr("Across it"));
		h->addWidget(new QLabel(tr("Length"), m_sizeRow));
		h->addWidget(m_sizeLength, 1);
		h->addWidget(new QLabel(tr("Depth"), m_sizeRow));
		h->addWidget(m_sizeDepth, 1);
		m_unlink = new QPushButton(tr("Unlink (plain brushes)"), m_sizeRow);
		m_unlink->setObjectName("buildingUnlink");
		m_unlink->setToolTip(tr("Turns the selected buildings into plain brushes and props: they can be edited brush by brush, but no longer with this dialog"));
		connect(m_unlink, &QPushButton::clicked, this, [this] {
			m_finished = true;
			m_editor->clearBrushPreview();
			m_editor->setPropPreviewItems({});
			m_editor->unlinkSelectedBuildings();
			QDialog::reject();
		});
		h->addWidget(m_unlink);
	}
	m_sizeLabel = new QLabel(tr("Size"), this);
	form->addRow(m_sizeLabel, m_sizeRow);
	m_sizeRow->hide();
	m_sizeLabel->hide();
	m_mode = choices(this, "buildingMode", {tr("Block (solid, facades outside)"), tr("Facade only (walls on the chosen sides)"), tr("One model per lot")});
	m_mode->setToolTip(tr("Block: a solid mass that seals, the facades in front. Facade only: a wall slab behind each chosen side - one front, a U of three "
	                      "(a backdrop) ... Model: each lot becomes one building model of the game"));
	form->addRow(tr("Build"), m_mode);
	m_facadeThickness = units(this, "buildingFacadeThickness", 8, 1024, d.facadeThickness, 8);
	m_facadeThickness->setToolTip(tr("Facade only: how thick each wall is"));
	form->addRow(tr("Wall thickness"), m_facadeThickness);
	m_returnDepth = units(this, "buildingReturn", 0, 4096, d.returnDepth, 16);
	m_returnDepth->setToolTip(tr("Facade only: where a wall ends at an open side it turns the corner this far (0: a straight end)"));
	form->addRow(tr("Return at open ends"), m_returnDepth);
	m_lotModel = new QLineEdit(this);
	m_lotModel->setObjectName("buildingLotModel");
	m_lotModel->setPlaceholderText(tr("models/props_buildings/row_res_1.mdl"));
	{
		auto* row = new QWidget(this);
		auto* h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		h->addWidget(m_lotModel, 1);
		auto* use = new QToolButton(row);
		use->setObjectName("buildingLotModelUse");
		use->setText("...");
		use->setToolTip(tr("Take the model selected in the asset browser"));
		connect(use, &QToolButton::clicked, this, [this] {
			if (m_browserModel) {
				if (const QString picked = m_browserModel(); !picked.isEmpty()) m_lotModel->setText(picked);
			}
		});
		h->addWidget(use);
		form->addRow(tr("Lot model"), row);
	}
	m_sidesPreset = choices(this, "buildingSidesPreset", {tr("All outer sides"), tr("Sides facing the street"), tr("Front only"), tr("Front and the two beside it")});
	m_sidesPreset->setToolTip(tr("Which sides get a facade while a side is on Auto. The front is the longest side facing the street; a side against another "
	                             "brush of the map does not face the street"));
	form->addRow(tr("Facades on"), m_sidesPreset);
	m_sides = new QTreeWidget(this);
	m_sides->setObjectName("buildingSides");
	m_sides->setColumnCount(7);
	m_sides->setHeaderLabels({tr("Side"), tr("Length"), tr("Now"), tr("State"), tr("Windows"), tr("Pattern"), tr("Doors")});
	m_sides->setRootIsDecorated(false);
	m_sides->setMinimumHeight(140);
	m_sides->setToolTip(tr("Every side of every lot. State: Auto (the preset decides), Facade, Plain wall (no openings), Open (facade only: no wall), Nodraw. "
	                       "Windows / Doors: -1 takes the building's setting; Pattern: x window, . wall, repeated"));
	form->addRow(m_sides);
	m_pickSide = new QPushButton(tr("Pick side in the view"), this);
	m_pickSide->setObjectName("buildingPickSide");
	m_pickSide->setToolTip(tr("Click next to a side in the 3D or top view: it is selected in the list"));
	connect(m_pickSide, &QPushButton::clicked, this, [this] {
		m_editor->requestScenePoint(tr("Buildings: click next to a side - Esc stops"), [this](const Vec3& point) { pickSideAt(point); });
	});
	form->addRow(m_pickSide);

	// --- Floors & windows ----------------------------------------------------
	page(sections[1], tr("Floors && windows"), form);
	m_heightMode = choices(this, "buildingHeightMode", {tr("From the brush"), tr("Set the height"), tr("From the floors")});
	m_heightMode->setToolTip(tr("From the brush or a height you set: as many floors as fit. From the floors: the height follows (ground floor + floors above + the top)"));
	form->addRow(tr("Height"), m_heightMode);
	m_height = units(this, "buildingHeight", 64, 16384, d.height, 16);
	form->addRow(tr("Building height"), m_height);
	m_floors = count(this, "buildingFloors", 1, 100, d.floors);
	m_floors->setToolTip(tr("All floors, the ground floor included"));
	form->addRow(tr("Floors"), m_floors);
	m_floorHeight = units(this, "buildingFloorHeight", 32, 1024, d.floorHeight, 8);
	m_floorHeight->setToolTip(tr("128 matches the game's broken facade panels and many building textures"));
	form->addRow(tr("Floor height"), m_floorHeight);
	m_groundHeight = units(this, "buildingGroundHeight", 32, 1024, d.groundFloorHeight, 8);
	form->addRow(tr("Ground floor height"), m_groundHeight);
	m_topWall = units(this, "buildingTopWall", 0, 512, d.topWall, 8);
	m_topWall->setToolTip(tr("Wall above the top floor's windows"));
	form->addRow(tr("Wall above the top floor"), m_topWall);
	m_windowsFromTexture = check(this, "buildingWindowsFromTexture", tr("The facade texture has the windows in it (build none)"), false);
	form->addRow(m_windowsFromTexture);
	m_textureFloors = count(this, "buildingTextureFloors", 0, 16, 0);
	m_textureFloors->setSpecialValueText(tr("from its size"));
	m_textureFloors->setToolTip(tr("Floors one repeat of the facade texture spans (0: from its size - a 512 texture is one 128-unit floor)"));
	form->addRow(tr("Facade texture: floors per repeat"), m_textureFloors);
	m_wholeTiles = check(this, "buildingWholeTiles", tr("Each side a whole number of texture repeats wide (scale changes at most 10 %)"), true);
	form->addRow(m_wholeTiles);
	m_windowStyle = choices(this, "buildingWindowStyle", {tr("Recessed (panel at the back)"), tr("Panels on the wall (fewer brushes)"), tr("None")});
	form->addRow(tr("Windows"), m_windowStyle);
	m_layout = choices(this, "buildingLayout", {tr("A number per side"), tr("By spacing")});
	m_layout->setCurrentIndex(1);
	form->addRow(tr("How many"), m_layout);
	m_windowSpacing = units(this, "buildingWindowSpacing", 0, 4096, DEFAULT_WINDOW_SPACING, 16);
	m_windowSpacing->setSpecialValueText(tr("every window"));
	m_windowSpacing->setToolTip(tr("About one window column every so many units along a wall. Facade textures: the painted windows in between are "
	                               "covered by plain wall strips. Built windows (By spacing): the wall between them follows"));
	form->addRow(tr("One window every"), m_windowSpacing);
	m_windowCount = count(this, "buildingWindowCount", 0, 64, d.windowsPerSide);
	m_windowCount->setToolTip(tr("Windows per floor on each facade side (A number per side)"));
	form->addRow(tr("Windows per side"), m_windowCount);
	m_windowWidth = units(this, "buildingWindowWidth", 8, 1024, d.windowWidth, 8);
	form->addRow(tr("Window width"), m_windowWidth);
	m_windowHeight = units(this, "buildingWindowHeight", 8, 1024, d.windowHeight, 8);
	form->addRow(tr("Window height"), m_windowHeight);
	m_sill = units(this, "buildingSillHeight", 0, 512, d.sillHeight, 8);
	m_sill->setToolTip(tr("From the floor to the window's bottom"));
	form->addRow(tr("Sill height"), m_sill);
	m_gap = units(this, "buildingWindowGap", 8, 1024, d.windowGap, 8);
	m_gap->setToolTip(tr("By spacing: wall between two windows"));
	form->addRow(tr("Between windows"), m_gap);
	m_margin = units(this, "buildingCornerMargin", 0, 1024, d.cornerMargin, 8);
	m_margin->setToolTip(tr("Wall kept at each end of a side"));
	form->addRow(tr("At the corners"), m_margin);
	m_depth = units(this, "buildingWindowDepth", 3, 128, d.windowDepth, 2);
	m_depth->setToolTip(tr("How deep the windows sit in the wall"));
	form->addRow(tr("Window depth"), m_depth);
	m_pattern = new QLineEdit(d.pattern.c_str(), this);
	m_pattern->setObjectName("buildingPattern");
	m_pattern->setToolTip(tr("x: window, . : wall, repeated over the columns (\"x.x\" leaves every second out). Several patterns with ';' "
	                         "between them: each building gets one at random"));
	form->addRow(tr("Pattern"), m_pattern);
	m_sills = check(this, "buildingSills", tr("Sills under the windows"), d.sills);
	form->addRow(m_sills);
	m_backing = check(this, "buildingBacking", tr("Interior behind the glass"), d.windowBacking);
	m_backing->setToolTip(tr("A plate with the interior material behind each window: see-through glass shows it (without it the game would show the void "
	                         "through the glass)"));
	form->addRow(m_backing);

	// --- Ground floor --------------------------------------------------------
	page(sections[1], tr("Ground floor"), form);
	m_groundLevel = choices(this, "buildingGroundLevel", {tr("At street level"), tr("Raised (with steps)"), tr("Half sunk")});
	m_groundLevel->setToolTip(tr("Raised: the ground floor lifted (Hochparterre), steps up to each door (never more than 18 units each, players walk "
	                             "up them), small basement windows below. Half sunk: its floor below the street"));
	form->addRow(tr("Ground floor"), m_groundLevel);
	m_raise = units(this, "buildingRaise", 0, 256, d.groundRaise, 8);
	form->addRow(tr("Raised / sunk by"), m_raise);
	m_basement = check(this, "buildingBasement", tr("Basement windows (raised)"), d.basementWindows);
	form->addRow(m_basement);
	m_groundWindows = choices(this, "buildingGroundWindows", {tr("Like the floors above"), tr("Shop windows"), tr("None")});
	form->addRow(tr("Windows"), m_groundWindows);
	m_shopWidth = units(this, "buildingShopWidth", 16, 1024, d.shopWidth, 8);
	form->addRow(tr("Shop window width"), m_shopWidth);
	m_shopHeight = units(this, "buildingShopHeight", 16, 1024, d.shopHeight, 8);
	form->addRow(tr("Shop window height"), m_shopHeight);
	m_doorSides = choices(this, "buildingDoorSides", {tr("On the front"), tr("On every facade"), tr("None")});
	form->addRow(tr("Doors"), m_doorSides);
	m_doorCount = count(this, "buildingDoorCount", 0, 32, d.doorsPerSide);
	form->addRow(tr("Doors per side"), m_doorCount);
	m_doorPlace = choices(this, "buildingDoorPlace", {tr("Middle"), tr("Spread out"), tr("At the start"), tr("At the end")});
	form->addRow(tr("Door place"), m_doorPlace);
	m_doorWidth = units(this, "buildingDoorWidth", 16, 512, d.doorWidth, 8);
	form->addRow(tr("Door width"), m_doorWidth);
	m_doorHeight = units(this, "buildingDoorHeight", 48, 512, d.doorHeight, 8);
	form->addRow(tr("Door height"), m_doorHeight);
	m_stepDepth = units(this, "buildingStepDepth", 8, 64, d.stepDepth, 2);
	form->addRow(tr("Step depth"), m_stepDepth);

	// --- Roof & bands --------------------------------------------------------
	page(sections[1], tr("Roof && bands"), form);
	m_roof = choices(this, "buildingRoof", {tr("Flat"), tr("Gable"), tr("Hip")});
	form->addRow(tr("Roof"), m_roof);
	m_parapet = units(this, "buildingParapet", 0, 256, d.parapetHeight, 8);
	m_parapet->setToolTip(tr("Flat roof: the wall round it (0: none)"));
	form->addRow(tr("Parapet"), m_parapet);
	m_roofAngle = units(this, "buildingRoofAngle", 5, 75, d.roofAngle, 5, QString::fromUtf8("°"));
	form->addRow(tr("Roof angle"), m_roofAngle);
	m_overhang = units(this, "buildingOverhang", 0, 128, d.roofOverhang, 4);
	form->addRow(tr("Overhang"), m_overhang);
	m_cornices = check(this, "buildingCornices", tr("Cornices between the floors"), d.cornices);
	form->addRow(m_cornices);
	m_corniceHeight = units(this, "buildingCorniceHeight", 2, 64, d.corniceHeight, 2);
	form->addRow(tr("Cornice height"), m_corniceHeight);
	m_corniceDepth = units(this, "buildingCorniceDepth", 1, 64, d.corniceDepth, 2);
	form->addRow(tr("Cornice depth"), m_corniceDepth);
	m_topCornice = check(this, "buildingTopCornice", tr("Cornice at the top"), d.topCornice);
	form->addRow(m_topCornice);
	m_plinth = check(this, "buildingPlinth", tr("Plinth at the bottom"), d.plinth);
	form->addRow(m_plinth);
	m_plinthHeight = units(this, "buildingPlinthHeight", 2, 128, d.plinthHeight, 2);
	form->addRow(tr("Plinth height"), m_plinthHeight);
	m_foundation = check(this, "buildingFoundation", tr("Reach down to displacement ground"), d.foundation);
	m_foundation->setToolTip(tr("A lot on displacement terrain gets a foundation down to the lowest ground under it: no gap, nothing floats"));
	form->addRow(m_foundation);
	m_wallDisp = check(this, "buildingWallDisp", tr("Plain walls bulge (displacement)"), d.wallDisplacement);
	m_wallDisp->setToolTip(tr("Plain walls get a displacement surface on their own thin brushes (four-cornered, lightmap scale that VBSP accepts)"));
	form->addRow(m_wallDisp);
	m_roofDisp = check(this, "buildingRoofDisp", tr("Flat roof sags (displacement)"), d.roofDisplacement);
	form->addRow(m_roofDisp);
	m_dispPower = choices(this, "buildingDispPower", {"2", "3", "4"});
	m_dispPower->setCurrentIndex(1);
	form->addRow(tr("Displacement power"), m_dispPower);
	m_dispStrength = units(this, "buildingDispStrength", 1, 64, d.displacementStrength, 1);
	form->addRow(tr("Displacement strength"), m_dispStrength);

	// --- Materials -----------------------------------------------------------
	{
		auto* w = new QGroupBox(tr("Materials"), sections[2]);
		auto* v = new QVBoxLayout(w);
		auto* materials = new QFormLayout;
		v->addLayout(materials);
		const char* names[] = {"buildingFacadeMaterial", "buildingGroundMaterial", "buildingWindowMaterial", "buildingBrokenMaterial", "buildingDoorMaterial",
		                       "buildingTrimMaterial",   "buildingCorniceMaterial", "buildingRoofMaterial",  "buildingInteriorMaterial"};
		const QString labels[] = {tr("Walls"), tr("Ground floor"), tr("Windows"), tr("Broken windows"), tr("Doors"), tr("Trim (reveals, sills, steps)"),
		                          tr("Cornices and plinth"), tr("Roof"), tr("Interior (seen through damage)")};
		for (std::size_t i = 0; i < m_materials.size(); ++i) {
			m_materials[i] = new MaterialField(names[i], editor, w);
			if (i > 0) m_materials[i]->setPlaceholderText(i == 1 ? tr("the walls'") : tr("the trim's / walls'"));
			materials->addRow(labels[i], m_materials[i]);
			if (i == 0 || i == 1 || i == 6 || i == 7) {
				// What the picture is read as (walls, ground floor, cornices, roof).
				m_materialNotes[i] = new QLabel(w);
				m_materialNotes[i]->setObjectName(QString("materialNote%1").arg(i));
				m_materialNotes[i]->setWordWrap(true);
				m_materialNotes[i]->setEnabled(false);   // grey: a note, not a field
				materials->addRow(QString(), m_materialNotes[i]);
			}
			connect(m_materials[i], &MaterialField::textChanged, this, [this] {
				showDecisions();
				m_soon.start();
			});
		}
		m_pipeMaterial = new MaterialField("buildingPipeMaterial", editor, w);
		m_pipeMaterial->setPlaceholderText(tr("the trim's"));
		materials->addRow(tr("Drainpipes and gutters"), m_pipeMaterial);
		connect(m_pipeMaterial, &MaterialField::textChanged, this, [this] { m_soon.start(); });
		m_plainMaterial = new MaterialField("buildingPlainMaterial", editor, w);
		m_plainMaterial->setPlaceholderText(tr("the facade texture's plain part"));
		m_plainMaterial->setToolTip(tr("Fewer windows on a facade texture: the strips over the windows left out get this (empty: a plain part of the "
		                               "facade texture, stretched)"));
		materials->addRow(tr("Wall between windows"), m_plainMaterial);
		connect(m_plainMaterial, &MaterialField::textChanged, this, [this] { m_soon.start(); });
		m_rubbleMaterial = new MaterialField("buildingRubbleMaterial", editor, w);
		m_rubbleMaterial->setPlaceholderText(tr("the interior's"));
		m_rubbleMaterial->setToolTip(tr("Damaged buildings: the rubble heaps and the broken top"));
		materials->addRow(tr("Rubble"), m_rubbleMaterial);
		connect(m_rubbleMaterial, &MaterialField::textChanged, this, [this] { m_soon.start(); });
		auto* grid = new QGridLayout;
		v->addWidget(new QLabel(tr("Textures - each part on its own (fit: one tile per floor, per window bay, or once over each opening)"), w));
		v->addLayout(grid);
		const QStringList headers{tr("Part"), tr("Fit"), tr("Scale"), tr("Shift U"), tr("Shift V"), tr("Rotation")};
		for (int c = 0; c < headers.size(); ++c) grid->addWidget(new QLabel(headers[c], w), 0, c);
		for (std::size_t i = 0; i < m_textures.size(); ++i) {
			const int row = static_cast<int>(i) + 1;
			grid->addWidget(new QLabel(tr(PART_NAMES[i]), w), row, 0);
			auto& t = m_textures[i];
			const QString base = QString("buildingTexture%1").arg(i);
			t.fit = new QComboBox(w);
			t.fit->setObjectName(base + "Fit");
			t.fit->addItems({tr("Scale"), tr("Per floor"), tr("Per bay"), tr("Per opening"), tr("Whole tiles (floors)"), tr("Once over the band")});
			t.fit->setCurrentIndex(static_cast<int>(d.textures[i].fit));
			t.scale = new QDoubleSpinBox(w);
			t.scale->setObjectName(base + "Scale");
			t.scale->setRange(0.01, 16);
			t.scale->setDecimals(3);
			t.scale->setSingleStep(0.05);
			t.scale->setValue(d.textures[i].scale);
			t.shiftU = new QDoubleSpinBox(w);
			t.shiftU->setObjectName(base + "ShiftU");
			t.shiftU->setRange(-4096, 4096);
			t.shiftV = new QDoubleSpinBox(w);
			t.shiftV->setObjectName(base + "ShiftV");
			t.shiftV->setRange(-4096, 4096);
			t.rotation = new QDoubleSpinBox(w);
			t.rotation->setObjectName(base + "Rotation");
			t.rotation->setRange(-360, 360);
			t.rotation->setSingleStep(90);
			grid->addWidget(t.fit, row, 1);
			grid->addWidget(t.scale, row, 2);
			grid->addWidget(t.shiftU, row, 3);
			grid->addWidget(t.shiftV, row, 4);
			grid->addWidget(t.rotation, row, 5);
		}
		v->addStretch(1);
		sections[2]->layout()->addWidget(w);
	}

	// --- Models --------------------------------------------------------------
	{
		auto* w = new QGroupBox(tr("Models"), sections[2]);
		auto* v = new QVBoxLayout(w);
		auto* hint = new QLabel(tr("Drop models from the asset browser on the list, use \"Add\" (the browser's selected model) or type a path. Each row: where, "
		                           "every N-th place, the chance, when (always / only intact / only where damaged) and how far out from the wall."),
		                        w);
		hint->setWordWrap(true);
		v->addWidget(hint);
		m_props = new QTreeWidget(w);
		m_props->setObjectName("buildingProps");
		m_props->setColumnCount(7);
		m_props->setHeaderLabels({tr("Where"), tr("Model"), tr("Every"), tr("Chance"), tr("When"), tr("Out"), QString()});
		m_props->setRootIsDecorated(false);
		m_props->setMinimumHeight(160);
		m_props->setAcceptDrops(true);
		m_props->viewport()->setAcceptDrops(true);
		m_props->viewport()->installEventFilter(new PropDropFilter(this, [this](const QString& model) {
			const bool damaged = m_damage->value() > 0 && model.contains("broken", Qt::CaseInsensitive);
			addProp(model, damaged && model.contains("exterior", Qt::CaseInsensitive) ? FacadePropAnchor::Bay : FacadePropAnchor::BelowWindow);
		}));
		v->addWidget(m_props, 1);
		auto* row = new QHBoxLayout;
		auto* add = new QPushButton(tr("Add selected model"), w);
		add->setObjectName("buildingAddProp");
		connect(add, &QPushButton::clicked, this, [this] {
			const QString model = m_browserModel ? m_browserModel() : QString();
			addProp(model, FacadePropAnchor::BelowWindow);
		});
		auto* suggest = new QToolButton(w);
		suggest->setObjectName("buildingSuggestProps");
		suggest->setText(tr("Suggestions"));
		suggest->setPopupMode(QToolButton::InstantPopup);
		auto* menu = new QMenu(suggest);
		suggest->setMenu(menu);
		connect(menu, &QMenu::aboutToShow, this, [this, menu] {
			menu->clear();
			std::vector<std::string> names;
			if (m_assets) {
				for (const auto& m : m_assets->modelNames()) names.push_back(m.toStdString());
			}
			const bool damaged = m_damage->value() > 0;
			for (int a = 0; a <= static_cast<int>(FacadePropAnchor::RoofEdge); ++a) {
				const auto anchor = static_cast<FacadePropAnchor>(a);
				auto* sub = menu->addMenu(tr(ANCHOR_NAMES[a]));
				const auto list = buildingModelSuggestions(names, anchor, damaged || anchor == FacadePropAnchor::Bay);
				if (list.empty()) sub->addAction(tr("(none found in the game)"))->setEnabled(false);
				for (const auto& name : list) {
					const QString model = QString::fromStdString(name);
					sub->addAction(model, this, [this, model, anchor] { addProp(model, anchor); });
				}
			}
		});
		row->addWidget(add);
		row->addWidget(suggest);
		row->addStretch(1);
		v->addLayout(row);
		sections[2]->layout()->addWidget(w);
	}

	// --- Condition & variation -----------------------------------------------
	page(sections[2], tr("Condition && variation"), form);
	m_damage = new QDoubleSpinBox(this);
	m_damage->setObjectName("buildingDamage");
	m_damage->setRange(0, 100);
	m_damage->setSuffix(" %");
	m_damage->setSingleStep(10);
	m_damage->setToolTip(tr("0: intact. More: floors missing from the top, a jagged top, broken windows, broken facade models in bays and rubble at the "
	                        "base (the Models list's \"only where damaged\" rows); the inside shows the interior material"));
	form->addRow(tr("Damage"), m_damage);
	m_damageSeed = count(this, "buildingDamageSeed", 1, 999999, 1);
	form->addRow(tr("Damage seed"), m_damageSeed);
	m_seed = count(this, "buildingSeed", 1, 999999, 1);
	m_seed->setToolTip(tr("Random choices (floors, pattern, material per building, props' chance): the same seed gives the same buildings"));
	form->addRow(tr("Seed"), m_seed);
	m_floorVariation = count(this, "buildingFloorVariation", 0, 20, 0);
	m_floorVariation->setToolTip(tr("Each building gets up to this many floors more or fewer"));
	form->addRow(tr("Floors +-"), m_floorVariation);
	m_touching = check(this, "buildingTouching", tr("Brushes that touch are one building"), false);
	m_touching->setToolTip(tr("On: a curve or an L made of several brushes is one building - one wall material and pattern, the doors only on its longest "
	                          "front. Off: each brush is its own house (a row)"));
	form->addRow(m_touching);
	m_variants = new QLineEdit(this);
	m_variants->setObjectName("buildingVariants");
	m_variants->setToolTip(tr("More wall materials, ';' between them: each building gets one of these or the Walls material at random"));
	form->addRow(tr("More wall materials"), m_variants);
	m_lots = new QTreeWidget(this);
	m_lots->setObjectName("buildingLots");
	m_lots->setColumnCount(4);
	m_lots->setHeaderLabels({tr("Building"), tr("Floors"), tr("Height"), tr("Wall material")});
	m_lots->setRootIsDecorated(false);
	m_lots->setMinimumHeight(100);
	m_lots->setToolTip(tr("Per building: 0 / empty takes the settings above"));
	form->addRow(m_lots);

	m_note = new QLabel(this);
	m_note->setObjectName("buildingNote");
	m_note->setWordWrap(true);
	m_note->setMinimumHeight(48);
	outer->addWidget(m_note);
	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Build"));
	m_buttons->button(QDialogButtonBox::Ok)->setObjectName("buildingBuild");
	connect(m_buttons, &QDialogButtonBox::accepted, this, &BuildingDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &BuildingDialog::reject);
	outer->addWidget(m_buttons);

	for (auto* section : sections) static_cast<QVBoxLayout*>(section->layout())->addStretch(1);
	loadSettings();
	if (m_windowsFromTexture->isChecked()) {
		// Where the painted windows are is not kept between runs: read again from the picture.
		if (const auto facts = inspectMaterial(m_materials[0]->text().toStdString()); facts && facts->analysis.role == TextureRole::FacadeWindows) {
			m_textureWindows = facts->analysis.windows;
		}
	}
	syncSimple();
	showDecisions();
	// The simple page writes into the fields behind it (and follows them).
	connect(m_simpleFloors, &QSpinBox::valueChanged, this, [this](int n) {
		if (m_syncing) return;
		m_heightMode->setCurrentIndex(static_cast<int>(HeightMode::Floors));
		m_floors->setValue(n);
	});
	connect(m_simpleFacade, &MaterialField::textChanged, this, [this] {
		if (!m_syncing) m_materials[0]->setText(m_simpleFacade->text());
	});
	connect(m_simpleRoof, &MaterialField::textChanged, this, [this] {
		if (!m_syncing) m_materials[7]->setText(m_simpleRoof->text());
	});
	connect(m_simpleWindows, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (m_syncing) return;
		m_windowsFromTexture->setChecked(index == 0);
		if (index == 2) m_windowStyle->setCurrentIndex(static_cast<int>(WindowStyle::None));
		if (index == 1 && m_windowStyle->currentIndex() == static_cast<int>(WindowStyle::None)) m_windowStyle->setCurrentIndex(static_cast<int>(WindowStyle::Recessed));
		updateDetailProps();
	});
	connect(m_simpleCondition, &QSlider::valueChanged, this, [this](int v) {
		if (!m_syncing) m_damage->setValue(v);
	});
	connect(m_simpleSeed, &QSpinBox::valueChanged, this, [this](int v) {
		if (!m_syncing) m_seed->setValue(v);
	});
	for (auto* box : {m_floors}) connect(box, &QSpinBox::valueChanged, this, [this] { syncSimple(); });
	connect(m_seed, &QSpinBox::valueChanged, this, [this] { syncSimple(); });
	connect(m_heightMode, &QComboBox::currentIndexChanged, this, [this] { syncSimple(); });
	connect(m_windowStyle, &QComboBox::currentIndexChanged, this, [this] { syncSimple(); });
	connect(m_windowsFromTexture, &QCheckBox::toggled, this, [this] { syncSimple(); });
	connect(m_damage, &QDoubleSpinBox::valueChanged, this, [this] { syncSimple(); });
	connect(m_materials[7], &MaterialField::textChanged, this, [this] { syncSimple(); });
	connect(m_materials[0], &MaterialField::textChanged, this, [this] {
		// What the facade's picture is decides: a facade with its windows in it builds none, a plain
		// wall gets them built.
		if (!m_filling && !m_syncing) facadeChanged(true);
		syncSimple();
	});
	connect(m_facadeRole, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (m_syncing || m_filling) return;
		setMaterialRole(m_materials[0]->text(), static_cast<TextureRole>(index));
	});
	connect(m_windowAmount, &QSlider::valueChanged, this, [this](int index) {
		if (m_syncing) return;
		m_windowSpacing->setValue(WINDOW_SPACINGS[static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(WINDOW_SPACINGS.size()) - 1))]);
	});
	connect(m_windowSpacing, &QDoubleSpinBox::valueChanged, this, [this] { syncSimple(); });
	connect(m_floorHeight, &QDoubleSpinBox::valueChanged, this, [this] { showDecisions(); });
	connect(m_style, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (!m_filling && !m_syncing) applyStyle(static_cast<BuildingStyle>(index));
	});
	connect(m_detail, &QComboBox::currentIndexChanged, this, [this] {
		if (!m_filling && !m_syncing) updateDetailProps();
	});
	m_advanced->setChecked(QSettings().value("buildings/advanced", false).toBool());

	// Every change plans again soon (not on every key press).
	m_soon.setSingleShot(true);
	m_soon.setInterval(150);
	connect(&m_soon, &QTimer::timeout, this, &BuildingDialog::refresh);
	const auto later = [this] {
		if (!m_filling) m_soon.start();
	};
	// Every field: noted as changed (editing buildings), planned again soon.
	const auto watch = [this](QWidget* w) {
		return [this, w] {
			noteChanged(w->objectName());
			if (!m_filling) m_soon.start();
		};
	};
	for (auto* box : findChildren<QDoubleSpinBox*>()) connect(box, &QDoubleSpinBox::valueChanged, this, watch(box));
	for (auto* box : findChildren<QSpinBox*>()) connect(box, &QSpinBox::valueChanged, this, watch(box));
	for (auto* box : findChildren<QComboBox*>()) connect(box, &QComboBox::currentIndexChanged, this, watch(box));
	for (auto* box : findChildren<QCheckBox*>()) connect(box, &QCheckBox::toggled, this, watch(box));
	for (auto* edit : findChildren<QLineEdit*>()) connect(edit, &QLineEdit::textChanged, this, watch(edit));
	connect(&m_watcher, &QFutureWatcher<Editor::BuildingResult>::finished, this, &BuildingDialog::planFinished);
	connect(m_editor, &Editor::selectionChanged, this, later);
	connect(m_editor, &Editor::documentChanged, this, later);
	if (auto* cache = editor->models()) connect(cache, &ModelCache::modelReady, this, later);
	m_soon.start();
}

BuildingDialog::~BuildingDialog() {
	if (m_stop) m_stop->store(true);
	m_watcher.waitForFinished();
}

void BuildingDialog::syncSimple() {
	if (m_syncing) return;
	m_syncing = true;
	m_simpleFloors->setValue(m_floors->value());
	m_simpleFloors->setEnabled(m_heightMode->currentIndex() == static_cast<int>(HeightMode::Floors) || m_floors->isEnabled());
	m_simpleFacade->setText(m_materials[0]->text());
	m_simpleRoof->setText(m_materials[7]->text());
	m_simpleWindows->setCurrentIndex(m_windowsFromTexture->isChecked() ? 0 : (m_windowStyle->currentIndex() == static_cast<int>(WindowStyle::None) ? 2 : 1));
	m_simpleCondition->setValue(static_cast<int>(std::lround(m_damage->value())));
	m_simpleSeed->setValue(m_seed->value());
	m_windowAmount->setValue(windowAmountFor(m_windowSpacing->value()));
	m_windowAmountLabel->setText(m_windowSpacing->value() <= 0 ? tr("every window") : tr("one every %1 units").arg(m_windowSpacing->value()));
	m_syncing = false;
}

std::vector<std::string> BuildingDialog::gameMaterials() const {
	return m_assets ? m_assets->materialNames() : std::vector<std::string>{};
}

std::vector<std::string> BuildingDialog::gameModels() const {
	std::vector<std::string> names;
	if (m_assets) {
		for (const auto& m : m_assets->modelNames()) names.push_back(m.toStdString());
	}
	return names;
}

void BuildingDialog::applyStyle(BuildingStyle style) {
	if (style == BuildingStyle::Custom) return;
	MaterialInspector inspect;
	if (m_editor->game()) inspect = [this](const std::string& material) { return inspectMaterial(material); };
	const std::vector<std::string> materials = gameMaterials();
	BuildingOptions next = applyBuildingStyle(options(), style, materials, gameModels(), inspect);
	// The facade picked last time for this style (if the game has it).
	const std::string remembered = materialKey(QSettings().value(QString("buildings/facade/%1").arg(static_cast<int>(style))).toString().toStdString());
	bool own = false;
	if (!remembered.empty()) {
		for (const auto& name : materials) {
			if (materialKey(name) != remembered) continue;
			next.facadeMaterial = QString::fromStdString(name).toUpper().toStdString();
			if (const auto facts = inspect ? inspectMaterial(name) : std::nullopt) applyFacadeFacts(next, facts->analysis);
			else next.windowsFromTexture = materialHasWindows(name);
			own = true;
			break;
		}
	}
	setOptions(next);
	if (own) updateDetailProps();   // AC units under windows only where windows are built
	// Editing buildings: the style's settings apply to all of them.
	if (editing()) {
		for (const auto& pair : QString::fromStdString(encodeBuildingOptions(next)).split('&', Qt::SkipEmptyParts)) {
			const QString key = pair.section('=', 0, 0);
			if (!key.isEmpty()) m_changed.insert(key.toStdString());
		}
	}
	m_soon.start();
}

void BuildingDialog::updateDetailProps() {
	// The rows the Detail amount added go; the ones for the new amount come.
	for (int i = m_props->topLevelItemCount() - 1; i >= 0; --i) {
		auto* item = m_props->topLevelItem(i);
		if (item->data(ColSlot, Qt::UserRole).toBool()) delete item;
	}
	const bool windows = !m_windowsFromTexture->isChecked() && m_windowStyle->currentIndex() != static_cast<int>(WindowStyle::None);
	for (const auto& prop : detailProps(m_detail->currentIndex(), gameModels(), windows, m_damage->value() > 0)) {
		addProp(QString::fromStdString(prop.model), prop.anchor, &prop);
	}
	noteChanged("buildingProps");
	m_soon.start();
}

void BuildingDialog::setAssets(AssetBrowser* assets) {
	m_assets = assets;
	const std::vector<std::string> names = assets ? assets->materialNames() : std::vector<std::string>{};
	const BuildingMaterialRole roles[] = {BuildingMaterialRole::Facade, BuildingMaterialRole::Ground, BuildingMaterialRole::Window,
	                                      BuildingMaterialRole::BrokenWindow, BuildingMaterialRole::Door, BuildingMaterialRole::Trim,
	                                      BuildingMaterialRole::Trim, BuildingMaterialRole::Roof, BuildingMaterialRole::Interior};
	for (std::size_t i = 0; i < m_materials.size(); ++i) {
		m_materials[i]->setAssets(assets);
		QStringList list;
		for (const auto& name : buildingMaterialSuggestions(names, roles[i])) list << QString::fromStdString(name).toUpper();
		m_materials[i]->setSuggestions(list);
	}
	if (m_materials[0]->text().isEmpty() && !names.empty()) {
		const auto best = buildingMaterialSuggestions(names, BuildingMaterialRole::Facade, 1);
		if (!best.empty()) {
			m_filling = true;
			m_materials[0]->setText(QString::fromStdString(best.front()).toUpper());
			m_filling = false;
			facadeChanged(false);
		}
	}
	if (m_materials[2]->text().isEmpty() && !names.empty()) {
		const auto best = buildingMaterialSuggestions(names, BuildingMaterialRole::Window, 1);
		if (!best.empty()) m_materials[2]->setText(QString::fromStdString(best.front()).toUpper());
	}
	m_simpleFacade->setAssets(assets);
	m_plainMaterial->setAssets(assets);
	m_rubbleMaterial->setAssets(assets);
	m_simpleRoof->setAssets(assets);
	m_pipeMaterial->setAssets(assets);
	{
		QStringList facades, roofs, pipes;
		for (const auto& name : buildingMaterialSuggestions(names, BuildingMaterialRole::Facade, 12)) facades << QString::fromStdString(name).toUpper();
		// Facades with windows in their picture first (their names when there is no picture).
		for (const auto& name : names) {
			if (facades.size() >= 24) break;
			const QString upper = QString::fromStdString(name).toUpper();
			if (facades.contains(upper)) continue;
			const std::string lower = toLower(name);
			if (lower.find("building") == std::string::npos && lower.find("facade") == std::string::npos && lower.find("apartment") == std::string::npos &&
			    !materialHasWindows(name)) {
				continue;   // only plausible ones are read (each picture is loaded)
			}
			const auto facts = m_editor->game() ? inspectMaterial(name) : std::nullopt;
			const bool windowed = facts && facts->analysis.role != TextureRole::Unknown ? facts->analysis.role == TextureRole::FacadeWindows : materialHasWindows(name);
			if (windowed) facades.prepend(upper);
		}
		for (const auto& name : buildingMaterialSuggestions(names, BuildingMaterialRole::Roof)) roofs << QString::fromStdString(name).toUpper();
		for (const auto& name : names) {
			const std::string lower = toLower(name);
			if ((lower.find("pipe") != std::string::npos || lower.find("metal") != std::string::npos) && pipes.size() < 8) pipes << QString::fromStdString(name).toUpper();
		}
		m_simpleFacade->setSuggestions(facades);
		m_simpleRoof->setSuggestions(roofs);
		m_pipeMaterial->setSuggestions(pipes);
	}
	// The first time: a City 17 look right away; later: the Detail rows from this game's models.
	if (assets && m_fresh && !editing()) {
		m_fresh = false;
		m_style->setCurrentIndex(static_cast<int>(BuildingStyle::PlasterBlock));   // applies it
	} else if (assets) {
		updateDetailProps();
	}
	syncSimple();
	showDecisions();
}

std::optional<MaterialFacts> BuildingDialog::inspectMaterial(const std::string& material) const {
	const std::string key = materialKey(material);
	if (key.empty()) return std::nullopt;
	auto it = m_facts.find(key);
	if (it == m_facts.end()) {
		std::optional<MaterialFacts> facts;
		if (const LoadedGame* game = m_editor->game()) {
			if (const auto vmt = loadMaterial(game->files, key)) {
				MaterialFacts f;
				f.shader = vmt->shader;
				const std::string texture = normalizeMaterialName(vmt->previewTexture());
				if (const auto data = texture.empty() ? std::nullopt : game->files.read("materials/" + texture + ".vtf")) {
					if (const auto info = readTextureInfo(*data)) {
						f.width = info->width;
						f.height = info->height;
					}
					// A small mip is enough to find windows (and cheap): read once per material.
					if (const auto image = decodeTexture(*data, 256)) {
						f.analysis = analyzeTexture(*image, key, f.width, f.height);
						QImage picture(reinterpret_cast<const uchar*>(image->pixels.data()), image->width, image->height, image->width * 4, QImage::Format_RGBA8888);
						m_pictures[key] = picture.copy();
					}
				}
				facts = f;
			}
		}
		it = m_facts.emplace(key, facts).first;
	}
	std::optional<MaterialFacts> result = it->second;
	if (result) {
		const QVariantMap roles = QSettings().value(ROLE_SETTINGS).toMap();
		const TextureRole own = textureRoleFromKey(roles.value(QString::fromStdString(key)).toString().toStdString());
		if (own != TextureRole::Unknown && own != result->analysis.role) {
			result->analysis.role = own;
			result->analysis.reason = "set with Read as";
			if (own != TextureRole::FacadeWindows && own != TextureRole::ShopFront) {
				result->analysis.windows.clear();
				result->analysis.hasWindows = false;
			}
		}
	}
	return result;
}

void BuildingDialog::setMaterialRole(const QString& material, TextureRole role) {
	const std::string key = materialKey(material.toStdString());
	if (key.empty()) return;
	QSettings settings;
	QVariantMap roles = settings.value(ROLE_SETTINGS).toMap();
	if (role == TextureRole::Unknown) roles.remove(QString::fromStdString(key));
	else roles.insert(QString::fromStdString(key), QString::fromLatin1(textureRoleKey(role)));
	settings.setValue(ROLE_SETTINGS, roles);
	if (materialKey(m_materials[0]->text().toStdString()) == key) facadeChanged(false);
	else showDecisions();
}

QString BuildingDialog::facadeDecision() const {
	return m_facadeDecision->text();
}

void BuildingDialog::facadeChanged(bool byUser) {
	const std::string facade = m_materials[0]->text().trimmed().toUpper().toStdString();
	const bool before = m_windowsFromTexture->isChecked();
	const auto facts = facade.empty() ? std::nullopt : inspectMaterial(facade);
	if (facts && facts->analysis.role != TextureRole::Unknown) {
		// The picture decides: windows from the texture (where they are, floors per tile) or built.
		BuildingOptions o;
		applyFacadeFacts(o, facts->analysis);
		m_windowsFromTexture->setChecked(o.windowsFromTexture);
		m_textureFloors->setValue(o.textureFloors);
		m_textureWindows = o.textureWindows;
	} else {
		m_windowsFromTexture->setChecked(materialHasWindows(facade));   // no picture: its name
		m_textureWindows.clear();
	}
	noteChanged("buildingTextureWindows");
	const bool windowed = m_windowsFromTexture->isChecked();
	if (windowed != before) {
		if (!windowed && m_windowStyle->currentIndex() == static_cast<int>(WindowStyle::None)) m_windowStyle->setCurrentIndex(static_cast<int>(WindowStyle::Recessed));
		updateDetailProps();
	}
	if (byUser && !facade.empty() && m_style->currentIndex() != static_cast<int>(BuildingStyle::Custom)) {
		QSettings().setValue(QString("buildings/facade/%1").arg(m_style->currentIndex()), QString::fromStdString(facade));   // the style starts with it next time
	}
	showDecisions();
	m_soon.start();
}

void BuildingDialog::showDecisions() {
	const double floorHeight = m_floorHeight->value();
	const auto line = [&](const QString& material) -> QString {
		if (material.trimmed().isEmpty()) return {};
		const auto facts = inspectMaterial(material.toStdString());
		if (!facts) return m_editor->game() ? tr("Not found in the game") : QString();
		if (facts->analysis.role == TextureRole::Unknown) return tr("No picture - judged by its name");
		QString text = QString::fromStdString(facts->analysis.describe(floorHeight));
		if (facts->analysis.reason == "set with Read as") text += tr(" (set with Read as)");
		return text;
	};
	for (std::size_t i = 0; i < m_materialNotes.size(); ++i) {
		if (m_materialNotes[i]) m_materialNotes[i]->setText(line(m_materials[i]->text()));
	}
	const QString facade = m_materials[0]->text();
	const auto facts = facade.trimmed().isEmpty() ? std::nullopt : inspectMaterial(facade.toStdString());
	m_facadeDecision->setText(facade.trimmed().isEmpty() ? tr("No facade material") : line(facade));
	// The picture, the windows found in it outlined.
	QPixmap pixmap;
	if (const auto found = m_pictures.find(materialKey(facade.toStdString())); found != m_pictures.end() && !found->second.isNull()) {
		pixmap = QPixmap::fromImage(found->second.scaled(m_facadePicture->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
		if (facts) {
			QPainter painter(&pixmap);
			painter.setPen(QPen(QColor(255, 210, 40), 2));
			for (const auto& w : facts->analysis.windows) {
				for (const double shift : {0.0, -1.0}) {
					if (w.u1 + shift <= 0) continue;
					painter.drawRect(QRectF((w.u0 + shift) * pixmap.width(), w.v0 * pixmap.height(), (w.u1 - w.u0) * pixmap.width(), (w.v1 - w.v0) * pixmap.height()));
				}
			}
		}
	}
	m_facadePicture->setPixmap(pixmap);
	// "Read as" shows the role a user set (Automatic otherwise).
	{
		const QVariantMap roles = QSettings().value(ROLE_SETTINGS).toMap();
		const TextureRole own = textureRoleFromKey(roles.value(QString::fromStdString(materialKey(facade.toStdString()))).toString().toStdString());
		const bool was = m_syncing;
		m_syncing = true;
		m_facadeRole->setCurrentIndex(static_cast<int>(own));
		m_syncing = was;
		m_facadeRole->setEnabled(facts.has_value() && !facade.trimmed().isEmpty());
	}
	// Windows: "Built into the wall" makes no sense over painted windows, "From the facade texture"
	// none on a plain wall ("Read as" changes what the texture is).
	if (auto* model = qobject_cast<QStandardItemModel*>(m_simpleWindows->model())) {
		const TextureRole role = facts ? facts->analysis.role : TextureRole::Unknown;
		const bool painted = role == TextureRole::FacadeWindows;
		const bool plain = role != TextureRole::Unknown && !painted;
		if (auto* item = model->item(0)) {
			item->setEnabled(!plain);
			item->setToolTip(plain ? tr("The facade texture has no windows in it - set Read as: Facade with windows if it has") : QString());
		}
		if (auto* item = model->item(1)) {
			item->setEnabled(!painted);
			item->setToolTip(painted ? tr("The facade texture already has windows - set Read as: Plain wall to build them anyway") : QString());
		}
	}
}

void BuildingDialog::addProp(const QString& modelPath, FacadePropAnchor anchor, const FacadeProp* values) {
	QString model = modelPath.trimmed();
	model.replace('\\', '/');
	auto* item = new QTreeWidgetItem(m_props);
	auto* slot = new QComboBox(m_props);
	slot->setObjectName("buildingPropSlot");
	for (const char* name : ANCHOR_NAMES) slot->addItem(tr(name));
	slot->setCurrentIndex(static_cast<int>(anchor));
	auto* path = new QLineEdit(model, m_props);
	path->setObjectName("buildingPropModel");
	path->setPlaceholderText(tr("models/....mdl"));
	auto* every = new QSpinBox(m_props);
	every->setObjectName("buildingPropEvery");
	every->setRange(1, 64);
	auto* chance = new QSpinBox(m_props);
	chance->setObjectName("buildingPropChance");
	chance->setRange(0, 100);
	chance->setValue(100);
	chance->setSuffix(" %");
	auto* when = new QComboBox(m_props);
	when->setObjectName("buildingPropWhen");
	when->addItems({tr("Always"), tr("Only intact"), tr("Only where damaged")});
	if (anchor == FacadePropAnchor::Bay || model.contains("broken", Qt::CaseInsensitive) || model.contains("debris", Qt::CaseInsensitive)) when->setCurrentIndex(2);
	auto* out = new QDoubleSpinBox(m_props);
	out->setObjectName("buildingPropOut");
	out->setRange(-128, 128);
	out->setDecimals(0);
	auto* remove = new QToolButton(m_props);
	remove->setText(QString::fromUtf8("✕"));
	remove->setToolTip(tr("Remove this row"));
	m_props->setItemWidget(item, ColSlot, slot);
	m_props->setItemWidget(item, ColModel, path);
	m_props->setItemWidget(item, ColEvery, every);
	m_props->setItemWidget(item, ColChance, chance);
	m_props->setItemWidget(item, ColWhen, when);
	m_props->setItemWidget(item, ColOut, out);
	if (values) {
		every->setValue(values->every);
		chance->setValue(static_cast<int>(std::lround(values->chance)));
		when->setCurrentIndex(static_cast<int>(values->when));
		out->setValue(values->wallOffset);
	}
	m_props->setItemWidget(item, ColRemove, remove);
	item->setData(ColSlot, Qt::UserRole, values && values->automatic);
	if (values && values->automatic) item->setToolTip(ColModel, tr("Added by the Detail amount"));
	const auto later = [this] {
		noteChanged("buildingProps");
		if (!m_filling) m_soon.start();
	};
	connect(slot, &QComboBox::currentIndexChanged, this, later);
	connect(path, &QLineEdit::textChanged, this, later);
	connect(every, &QSpinBox::valueChanged, this, later);
	connect(chance, &QSpinBox::valueChanged, this, later);
	connect(when, &QComboBox::currentIndexChanged, this, later);
	connect(out, &QDoubleSpinBox::valueChanged, this, later);
	connect(remove, &QToolButton::clicked, this, [this, item] {
		delete item;
		noteChanged("buildingProps");
		m_soon.start();
	});
	noteChanged("buildingProps");
	m_soon.start();
}

void BuildingDialog::noteChanged(const QString& name) {
	if (m_filling || name.isEmpty()) return;
	if (name == "buildingSizeLength" || name == "buildingSizeDepth") {
		m_sizeTouched = true;
		return;
	}
	QString key = name;
	if (name.startsWith("buildingProp")) key = "buildingProps";
	if (name.startsWith("buildingSide") && name != "buildingSidesPreset") key = "buildingSides";
	if (name.startsWith("buildingLot") && name != "buildingLotModel") key = "buildingLots";
	m_changed.insert(key.toStdString());
}

std::optional<std::pair<double, double>> BuildingDialog::newSize() const {
	if (m_editIds.size() != 1 || !m_sizeTouched) return std::nullopt;
	return std::make_pair(m_sizeLength->value(), m_sizeDepth->value());
}

void BuildingDialog::followSelection() {
	const auto records = m_editor->selectedBuildings();
	std::vector<int> ids;
	for (const auto& r : records) ids.push_back(r.id);
	if (ids == m_editIds) return;
	m_editIds = ids;
	m_changed.clear();
	m_sizeTouched = false;
	if (records.empty()) {
		m_editInfo->hide();
		m_sizeRow->hide();
		m_sizeLabel->hide();
		m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Build"));
		return;
	}
	// The (first) building's settings into the fields: changes apply to all of them.
	setOptions(records.front().options);
	const auto [length, depth] = outlineSize(records.front().outline);
	m_filling = true;
	m_sizeLength->setValue(std::round(length));
	m_sizeDepth->setValue(std::round(depth));
	m_filling = false;
	m_sizeLength->setEnabled(records.size() == 1);
	m_sizeDepth->setEnabled(records.size() == 1);
	m_editInfo->setText(records.size() == 1
	                        ? tr("Editing the selected building: its settings are in the fields. Change any of them, its size or height - Apply rebuilds it (one undo step).")
	                        : tr("Editing %n buildings: the fields show the first one's settings. What you change applies to all of them; what you leave "
	                             "stays each one's own.",
	                             nullptr, int(records.size())));
	m_editInfo->show();
	m_sizeRow->show();
	m_sizeLabel->show();
	m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
}

void BuildingDialog::setOptions(const BuildingOptions& o) {
	m_filling = true;
	m_style->setCurrentIndex(static_cast<int>(o.style));
	m_windowsFromTexture->setChecked(o.windowsFromTexture);
	m_textureFloors->setValue(o.textureFloors);
	m_textureWindows = o.textureWindows;
	m_windowSpacing->setValue(o.windowSpacing);
	m_plainMaterial->setText(QString::fromStdString(o.plainMaterial));
	m_rubbleMaterial->setText(QString::fromStdString(o.rubbleMaterial));
	m_wholeTiles->setChecked(o.fitWholeTiles);
	m_detail->setCurrentIndex(std::clamp(o.detail, 0, 2));
	m_pipeMaterial->setText(QString::fromStdString(o.pipeMaterial));
	m_mode->setCurrentIndex(static_cast<int>(o.mode));
	m_facadeThickness->setValue(o.facadeThickness);
	m_returnDepth->setValue(o.returnDepth);
	m_lotModel->setText(QString::fromStdString(o.lotModel));
	m_sidesPreset->setCurrentIndex(static_cast<int>(o.sides));
	m_heightMode->setCurrentIndex(static_cast<int>(o.heightMode));
	m_height->setValue(o.height);
	m_floors->setValue(o.floors);
	m_floorHeight->setValue(o.floorHeight);
	m_groundHeight->setValue(o.groundFloorHeight);
	m_topWall->setValue(o.topWall);
	m_windowStyle->setCurrentIndex(static_cast<int>(o.windowStyle));
	m_layout->setCurrentIndex(o.layout == WindowLayout::Count ? 0 : 1);
	m_windowCount->setValue(o.windowsPerSide);
	m_windowWidth->setValue(o.windowWidth);
	m_windowHeight->setValue(o.windowHeight);
	m_sill->setValue(o.sillHeight);
	m_gap->setValue(o.windowGap);
	m_margin->setValue(o.cornerMargin);
	m_depth->setValue(o.windowDepth);
	m_pattern->setText(QString::fromStdString(o.pattern));
	m_sills->setChecked(o.sills);
	m_backing->setChecked(o.windowBacking);
	m_groundLevel->setCurrentIndex(static_cast<int>(o.groundLevel));
	m_raise->setValue(o.groundRaise);
	m_basement->setChecked(o.basementWindows);
	m_groundWindows->setCurrentIndex(static_cast<int>(o.groundWindows));
	m_shopWidth->setValue(o.shopWidth);
	m_shopHeight->setValue(o.shopHeight);
	m_doorSides->setCurrentIndex(static_cast<int>(o.doorSides));
	m_doorCount->setValue(o.doorsPerSide);
	m_doorPlace->setCurrentIndex(static_cast<int>(o.doorPlace));
	m_doorWidth->setValue(o.doorWidth);
	m_doorHeight->setValue(o.doorHeight);
	m_stepDepth->setValue(o.stepDepth);
	m_roof->setCurrentIndex(static_cast<int>(o.roof));
	m_parapet->setValue(o.parapetHeight);
	m_roofAngle->setValue(o.roofAngle);
	m_overhang->setValue(o.roofOverhang);
	m_cornices->setChecked(o.cornices);
	m_corniceHeight->setValue(o.corniceHeight);
	m_corniceDepth->setValue(o.corniceDepth);
	m_topCornice->setChecked(o.topCornice);
	m_plinth->setChecked(o.plinth);
	m_plinthHeight->setValue(o.plinthHeight);
	m_foundation->setChecked(o.foundation);
	m_wallDisp->setChecked(o.wallDisplacement);
	m_roofDisp->setChecked(o.roofDisplacement);
	m_dispPower->setCurrentIndex(std::clamp(o.displacementPower, 2, 4) - 2);
	m_dispStrength->setValue(o.displacementStrength);
	const std::string* materials[] = {&o.facadeMaterial, &o.groundMaterial, &o.windowMaterial, &o.brokenWindowMaterial, &o.doorMaterial,
	                                  &o.trimMaterial,   &o.corniceMaterial, &o.roofMaterial,  &o.interiorMaterial};
	for (std::size_t i = 0; i < m_materials.size(); ++i) m_materials[i]->setText(QString::fromStdString(*materials[i]));
	for (std::size_t i = 0; i < m_textures.size(); ++i) {
		m_textures[i].fit->setCurrentIndex(static_cast<int>(o.textures[i].fit));
		m_textures[i].scale->setValue(o.textures[i].scale);
		m_textures[i].shiftU->setValue(o.textures[i].shiftU);
		m_textures[i].shiftV->setValue(o.textures[i].shiftV);
		m_textures[i].rotation->setValue(o.textures[i].rotation);
	}
	m_props->clear();
	for (const auto& prop : o.props) addProp(QString::fromStdString(prop.model), prop.anchor, &prop);
	m_damage->setValue(o.damage * 100);
	m_damageSeed->setValue(static_cast<int>(o.damageSeed));
	m_seed->setValue(static_cast<int>(o.seed));
	m_floorVariation->setValue(o.floorVariation);
	m_touching->setChecked(o.touchingIsOne);
	QStringList variants;
	for (const auto& v : o.facadeVariants) variants << QString::fromStdString(v);
	m_variants->setText(variants.join("; "));
	m_sideSettings = o.sideSettings;
	m_lotSettings = o.lotSettings;
	m_sides->clear();
	m_lots->clear();
	m_filling = false;
	syncSimple();
	showDecisions();
	m_soon.start();
}

BuildingOptions BuildingDialog::options() const {
	BuildingOptions o = defaultBuildingOptions();
	o.style = static_cast<BuildingStyle>(m_style->currentIndex());
	o.windowsFromTexture = m_windowsFromTexture->isChecked();
	o.textureFloors = m_textureFloors->value();
	o.textureWindows = m_textureWindows;
	o.windowSpacing = m_windowSpacing->value();
	o.plainMaterial = m_plainMaterial->text().trimmed().toUpper().toStdString();
	o.rubbleMaterial = m_rubbleMaterial->text().trimmed().toUpper().toStdString();
	o.fitWholeTiles = m_wholeTiles->isChecked();
	o.detail = m_detail->currentIndex();
	o.pipeMaterial = m_pipeMaterial->text().trimmed().toUpper().toStdString();
	o.mode = static_cast<BuildingMode>(m_mode->currentIndex());
	o.facadeThickness = m_facadeThickness->value();
	o.returnDepth = m_returnDepth->value();
	o.lotModel = m_lotModel->text().trimmed().replace('\\', '/').toStdString();
	o.sides = static_cast<BuildingSides>(m_sidesPreset->currentIndex());
	o.sideSettings = m_sideSettings;
	o.lotSettings = m_lotSettings;
	o.heightMode = static_cast<HeightMode>(m_heightMode->currentIndex());
	o.height = m_height->value();
	o.floors = m_floors->value();
	o.floorHeight = m_floorHeight->value();
	o.groundFloorHeight = m_groundHeight->value();
	o.topWall = m_topWall->value();
	o.windowStyle = static_cast<WindowStyle>(m_windowStyle->currentIndex());
	o.layout = m_layout->currentIndex() == 0 ? WindowLayout::Count : WindowLayout::Spacing;
	o.windowsPerSide = m_windowCount->value();
	o.windowWidth = m_windowWidth->value();
	o.windowHeight = m_windowHeight->value();
	o.sillHeight = m_sill->value();
	o.windowGap = m_gap->value();
	o.cornerMargin = m_margin->value();
	o.windowDepth = m_depth->value();
	o.pattern = m_pattern->text().trimmed().toStdString();
	o.sills = m_sills->isChecked();
	o.windowBacking = m_backing->isChecked();
	o.groundLevel = static_cast<GroundLevel>(m_groundLevel->currentIndex());
	o.groundRaise = m_raise->value();
	o.basementWindows = m_basement->isChecked();
	o.groundWindows = static_cast<GroundWindows>(m_groundWindows->currentIndex());
	o.shopWidth = m_shopWidth->value();
	o.shopHeight = m_shopHeight->value();
	o.doorSides = static_cast<DoorSides>(m_doorSides->currentIndex());
	o.doorsPerSide = m_doorCount->value();
	o.doorPlace = static_cast<DoorPlace>(m_doorPlace->currentIndex());
	o.doorWidth = m_doorWidth->value();
	o.doorHeight = m_doorHeight->value();
	o.stepDepth = m_stepDepth->value();
	o.roof = static_cast<BuildingRoof>(m_roof->currentIndex());
	o.parapetHeight = m_parapet->value();
	o.roofAngle = m_roofAngle->value();
	o.roofOverhang = m_overhang->value();
	o.cornices = m_cornices->isChecked();
	o.corniceHeight = m_corniceHeight->value();
	o.corniceDepth = m_corniceDepth->value();
	o.topCornice = m_topCornice->isChecked();
	o.plinth = m_plinth->isChecked();
	o.plinthHeight = m_plinthHeight->value();
	o.foundation = m_foundation->isChecked();
	o.wallDisplacement = m_wallDisp->isChecked();
	o.roofDisplacement = m_roofDisp->isChecked();
	o.displacementPower = m_dispPower->currentIndex() + 2;
	o.displacementStrength = m_dispStrength->value();
	std::string* materials[] = {&o.facadeMaterial, &o.groundMaterial, &o.windowMaterial, &o.brokenWindowMaterial, &o.doorMaterial,
	                            &o.trimMaterial,   &o.corniceMaterial, &o.roofMaterial,  &o.interiorMaterial};
	for (std::size_t i = 0; i < m_materials.size(); ++i) *materials[i] = m_materials[i]->text().trimmed().toUpper().toStdString();
	for (std::size_t i = 0; i < m_textures.size(); ++i) {
		auto& t = o.textures[i];
		t.fit = static_cast<TextureFit>(m_textures[i].fit->currentIndex());
		t.scale = m_textures[i].scale->value();
		t.shiftU = m_textures[i].shiftU->value();
		t.shiftV = m_textures[i].shiftV->value();
		t.rotation = m_textures[i].rotation->value();
	}
	for (int i = 0; i < m_props->topLevelItemCount(); ++i) {
		auto* item = m_props->topLevelItem(i);
		const auto* slot = qobject_cast<QComboBox*>(m_props->itemWidget(item, ColSlot));
		const auto* path = qobject_cast<QLineEdit*>(m_props->itemWidget(item, ColModel));
		const auto* every = qobject_cast<QSpinBox*>(m_props->itemWidget(item, ColEvery));
		const auto* chance = qobject_cast<QSpinBox*>(m_props->itemWidget(item, ColChance));
		const auto* when = qobject_cast<QComboBox*>(m_props->itemWidget(item, ColWhen));
		const auto* out = qobject_cast<QDoubleSpinBox*>(m_props->itemWidget(item, ColOut));
		if (!slot || !path || !every || !chance || !when || !out) continue;
		FacadeProp prop;
		prop.model = path->text().trimmed().replace('\\', '/').toStdString();
		if (prop.model.empty()) continue;
		prop.anchor = static_cast<FacadePropAnchor>(slot->currentIndex());
		prop.every = every->value();
		prop.chance = chance->value();
		prop.when = static_cast<PropCondition>(when->currentIndex());
		prop.wallOffset = out->value();
		prop.automatic = item->data(ColSlot, Qt::UserRole).toBool();
		// Awnings over the shop windows; AC units and the like on the floors above; in windows: all.
		prop.groundFloor = prop.anchor == FacadePropAnchor::AboveWindow || prop.anchor == FacadePropAnchor::Window || prop.when == PropCondition::Damaged;
		prop.upperFloors = prop.anchor != FacadePropAnchor::AboveWindow || prop.when == PropCondition::Damaged;
		o.props.push_back(prop);
	}
	o.damage = m_damage->value() / 100.0;
	o.damageSeed = static_cast<std::uint32_t>(m_damageSeed->value());
	o.seed = static_cast<std::uint32_t>(m_seed->value());
	o.floorVariation = m_floorVariation->value();
	o.touchingIsOne = m_touching->isChecked();
	for (const auto& part : m_variants->text().split(';', Qt::SkipEmptyParts)) {
		const QString m = part.trimmed().toUpper();
		if (!m.isEmpty()) o.facadeVariants.push_back(m.toStdString());
	}
	return o;
}

QString BuildingDialog::note() const {
	return m_note->text();
}

bool BuildingDialog::planning() const {
	return m_running.has_value() || m_again;
}

void BuildingDialog::waitForPlan() {
	for (int round = 0; round < 50; ++round) {
		if (m_soon.isActive()) {
			m_soon.stop();
			refresh();
		}
		if (!m_running && !m_again && !m_soon.isActive()) return;
		m_watcher.waitForFinished();
		QCoreApplication::processEvents();
	}
}

void BuildingDialog::refresh() {
	if (m_finished) return;
	auto* ok = m_buttons->button(QDialogButtonBox::Ok);
	followSelection();
	auto job = m_editor->buildingJob(options(), m_changed, newSize());
	if (job.solids != m_previewSolids || job.serial != m_previewSerial) {
		// Another selection or map: the old preview goes at once.
		if (job.solids != m_previewSolids && job.edits.empty()) {
			m_sideSettings.clear();
			m_lotSettings.clear();
			job.options.sideSettings.clear();
			job.options.lotSettings.clear();
		}
		m_editor->clearBrushPreview();
		m_editor->setPropPreviewItems({});
		m_shown.reset();
		m_shownKey.clear();
		ok->setEnabled(false);
		m_previewSolids = job.solids;
		m_previewSerial = job.serial;
		if (!job.solids.empty()) m_note->setText(tr("Planning the buildings ..."));
	}
	if (m_running) {
		m_again = true;
		if (m_stop) m_stop->store(true);
		return;
	}
	const std::string key = Editor::buildingJobKey(job);
	if (m_shown && key == m_shownKey && m_editor->buildingJobCurrent(m_shown->first)) return;
	m_stop = std::make_shared<std::atomic<bool>>(false);
	job.options.cancelled = [stop = m_stop] { return stop->load(); };
	m_running = job;
	m_watcher.setFuture(QtConcurrent::run([job] { return Editor::runBuildingJob(job); }));
}

void BuildingDialog::planFinished() {
	if (!m_running) return;
	const Editor::BuildingJob job = std::move(*m_running);
	m_running.reset();
	if (m_finished) return;
	const Editor::BuildingResult result = m_watcher.result();
	if (m_again || !m_editor->buildingJobCurrent(job) || job.solids != m_editor->selection().solids) {
		m_again = false;
		refresh();
		return;
	}
	m_editor->noteBuildingDone(job, result);
	showPlan(job, result);
}

void BuildingDialog::showPlan(const Editor::BuildingJob& job, const Editor::BuildingResult& result) {
	auto* ok = m_buttons->button(QDialogButtonBox::Ok);
	const auto& plan = result.plan;
	m_shown = std::make_pair(job, result);
	m_shownKey = Editor::buildingJobKey(job);
	if (!result.error.empty()) {
		m_editor->clearBrushPreview();
		m_editor->setPropPreviewItems({});
		m_note->setText(tr("The buildings could not be planned (%1). Change a setting or the selection; the map is unchanged.").arg(qs(result.error)));
		ok->setEnabled(false);
		return;
	}
	if (!plan.ok() && plan.removeSolids.empty()) {
		m_editor->clearBrushPreview();
		m_editor->setPropPreviewItems({});
		QStringList lines{tr("Select the brushes to build on: each brush becomes one building (its outline seen from above and its height).")};
		for (const auto& note : plan.notes) lines << qs(note);
		m_note->setText(lines.join('\n'));
		ok->setEnabled(false);
		return;
	}
	m_editor->setBrushPreview(plan.resultSolids(), plan.replacedRefs());
	std::vector<std::pair<QString, PropPlacement>> ghosts;
	for (const auto& prop : plan.props) ghosts.emplace_back(qs(prop.model), prop.placement);
	m_editor->setPropPreviewItems(std::move(ghosts));
	fillSides(plan);
	fillLots(plan);
	QStringList lines;
	int floors = 0;
	for (const auto& b : plan.buildings) floors = std::max(floors, b.floors);
	lines << tr("%n building(s), up to %1 floors: %2 brushes, %3 windows, %4 doors", nullptr, int(plan.buildings.size()))
	             .arg(floors)
	             .arg(plan.brushCount())
	             .arg(plan.windows)
	             .arg(plan.doors) +
	             (plan.props.empty() ? QString('.') : ", " + tr("%n prop(s).", nullptr, int(plan.props.size())));
	if (plan.displacements > 0) lines << tr("%n displacement(s) on their own brushes.", nullptr, plan.displacements);
	for (const auto& note : plan.notes) lines << qs(note);
	m_note->setText(lines.join('\n'));
	ok->setEnabled(true);
}

void BuildingDialog::fillSides(const BuildingPlan& plan) {
	static const char* const stateNames[] = {QT_TR_NOOP("Auto"), QT_TR_NOOP("Facade"), QT_TR_NOOP("Plain wall"), QT_TR_NOOP("Open"), QT_TR_NOOP("Nodraw")};
	m_filling = true;
	// The rows stay while the sides are the same; their "Now" column follows each plan.
	const bool same = m_sides->topLevelItemCount() == static_cast<int>(plan.sides.size());
	if (!same) m_sides->clear();
	for (std::size_t i = 0; i < plan.sides.size(); ++i) {
		const auto& s = plan.sides[i];
		const std::pair<int, int> key{s.lot, s.side};
		QTreeWidgetItem* item = same ? m_sides->topLevelItem(static_cast<int>(i)) : new QTreeWidgetItem(m_sides);
		QString label = tr("Building %1 side %2").arg(s.lot + 1).arg(s.side + 1);
		if (s.front) label += ' ' + tr("(front)");
		item->setText(ColSide, label);
		item->setText(ColLength, QString::number(std::lround(s.length)));
		QString now = tr(stateNames[static_cast<int>(s.state)]);
		if (s.shared) now += ", " + tr("shared");
		if (s.blocked) now += ", " + tr("against a brush");
		if (s.state == SideState::Facade) now += ", " + tr("%n window(s)/floor", nullptr, s.windowsPerFloor);
		item->setText(ColNow, now);
		item->setData(ColSide, Qt::UserRole, s.lot);
		item->setData(ColLength, Qt::UserRole, s.side);
		if (same) continue;
		const SideSettings current = m_sideSettings.contains(key) ? m_sideSettings[key] : SideSettings{};
		auto* state = new QComboBox(m_sides);
		state->setObjectName("buildingSideState");
		for (const char* name : stateNames) state->addItem(tr(name));
		state->setCurrentIndex(static_cast<int>(current.state));
		auto* windows = new QSpinBox(m_sides);
		windows->setObjectName("buildingSideWindows");
		windows->setRange(-1, 64);
		windows->setValue(current.windows);
		windows->setSpecialValueText(tr("as set"));
		auto* pattern = new QLineEdit(QString::fromStdString(current.pattern), m_sides);
		pattern->setObjectName("buildingSidePattern");
		pattern->setPlaceholderText(tr("as set"));
		auto* doors = new QSpinBox(m_sides);
		doors->setObjectName("buildingSideDoors");
		doors->setRange(-1, 32);
		doors->setValue(current.doors);
		doors->setSpecialValueText(tr("as set"));
		m_sides->setItemWidget(item, ColState, state);
		m_sides->setItemWidget(item, ColWindows, windows);
		m_sides->setItemWidget(item, ColPattern, pattern);
		m_sides->setItemWidget(item, ColDoors, doors);
		const auto store = [this, key, state, windows, pattern, doors] {
			if (m_filling) return;
			SideSettings settings;
			settings.state = static_cast<SideState>(state->currentIndex());
			settings.windows = windows->value();
			settings.pattern = pattern->text().trimmed().toStdString();
			settings.doors = doors->value();
			if (settings.state == SideState::Auto && settings.windows < 0 && settings.pattern.empty() && settings.doors < 0) {
				m_sideSettings.erase(key);
			} else {
				m_sideSettings[key] = settings;
			}
			m_soon.start();
		};
		connect(state, &QComboBox::currentIndexChanged, this, store);
		connect(windows, &QSpinBox::valueChanged, this, store);
		connect(pattern, &QLineEdit::textChanged, this, store);
		connect(doors, &QSpinBox::valueChanged, this, store);
	}
	m_sides->resizeColumnToContents(ColSide);
	m_filling = false;
}

void BuildingDialog::fillLots(const BuildingPlan& plan) {
	m_filling = true;
	const bool same = m_lots->topLevelItemCount() == static_cast<int>(plan.buildings.size());
	if (!same) m_lots->clear();
	for (std::size_t i = 0; i < plan.buildings.size(); ++i) {
		const auto& b = plan.buildings[i];
		QTreeWidgetItem* item = same ? m_lots->topLevelItem(static_cast<int>(i)) : new QTreeWidgetItem(m_lots);
		item->setText(0, tr("Building %1 (%n floor(s), top %2)", nullptr, b.floors).arg(i + 1).arg(std::lround(b.top - b.bottom)));
		if (same) continue;
		const int lot = static_cast<int>(i);
		const LotSettings current = m_lotSettings.contains(lot) ? m_lotSettings[lot] : LotSettings{};
		auto* floors = new QSpinBox(m_lots);
		floors->setObjectName("buildingLotFloors");
		floors->setRange(0, 100);
		floors->setValue(current.floors);
		floors->setSpecialValueText(tr("as set"));
		auto* height = new QDoubleSpinBox(m_lots);
		height->setObjectName("buildingLotHeight");
		height->setRange(0, 16384);
		height->setDecimals(0);
		height->setSingleStep(16);
		height->setValue(current.height);
		height->setSpecialValueText(tr("as set"));
		auto* material = new QLineEdit(QString::fromStdString(current.facadeMaterial), m_lots);
		material->setObjectName("buildingLotMaterial");
		material->setPlaceholderText(tr("as set"));
		m_lots->setItemWidget(item, 1, floors);
		m_lots->setItemWidget(item, 2, height);
		m_lots->setItemWidget(item, 3, material);
		const auto store = [this, lot, floors, height, material] {
			if (m_filling) return;
			LotSettings settings;
			settings.floors = floors->value();
			settings.height = height->value();
			settings.facadeMaterial = material->text().trimmed().toUpper().toStdString();
			if (settings.floors == 0 && settings.height <= 0 && settings.facadeMaterial.empty()) {
				m_lotSettings.erase(lot);
			} else {
				m_lotSettings[lot] = settings;
			}
			m_soon.start();
		};
		connect(floors, &QSpinBox::valueChanged, this, store);
		connect(height, &QDoubleSpinBox::valueChanged, this, store);
		connect(material, &QLineEdit::textChanged, this, store);
	}
	m_lots->resizeColumnToContents(0);
	m_filling = false;
}

void BuildingDialog::pickSideAt(const Vec3& point) {
	if (!m_shown) return;
	const int index = m_shown->second.plan.sideAt(point, 96);
	if (index < 0 || index >= m_sides->topLevelItemCount()) {
		m_note->setText(tr("No side there - click next to a building's wall."));
		return;
	}
	m_tabs->setCurrentIndex(0);
	auto* item = m_sides->topLevelItem(index);
	m_sides->setCurrentItem(item);
	m_sides->scrollToItem(item);
	// A small menu right there: what this side gets.
	const auto& side = m_shown->second.plan.sides[static_cast<std::size_t>(index)];
	const std::pair<int, int> key{side.lot, side.side};
	QMenu menu(this);
	menu.setObjectName("buildingSideMenu");
	menu.addSection(tr("Building %1 side %2").arg(side.lot + 1).arg(side.side + 1));
	const std::pair<SideState, QString> states[] = {{SideState::Auto, tr("Auto (the style decides)")}, {SideState::Facade, tr("Facade")},
	                                                 {SideState::Plain, tr("Plain wall")},          {SideState::Open, tr("Open (facade only)")},
	                                                 {SideState::Nodraw, tr("Nodraw")}};
	const SideState current = m_sideSettings.contains(key) ? m_sideSettings[key].state : SideState::Auto;
	for (const auto& [state, label] : states) {
		auto* action = menu.addAction(label);
		action->setCheckable(true);
		action->setChecked(state == current);
		action->setData(static_cast<int>(state));
	}
	if (!m_pickMenuQuiet) {
		if (auto* chosen = menu.exec(QCursor::pos())) setSideState(key, static_cast<SideState>(chosen->data().toInt()));
	}
	if (auto* state = m_sides->itemWidget(item, ColState)) state->setFocus();
}

void BuildingDialog::setSideState(const std::pair<int, int>& key, SideState state) {
	SideSettings settings = m_sideSettings.contains(key) ? m_sideSettings[key] : SideSettings{};
	settings.state = state;
	if (settings.state == SideState::Auto && settings.windows < 0 && settings.pattern.empty() && settings.doors < 0) {
		m_sideSettings.erase(key);
	} else {
		m_sideSettings[key] = settings;
	}
	// The side list shows it too.
	for (int i = 0; i < m_sides->topLevelItemCount(); ++i) {
		auto* item = m_sides->topLevelItem(i);
		if (item->data(ColSide, Qt::UserRole).toInt() == key.first && item->data(ColLength, Qt::UserRole).toInt() == key.second) {
			if (auto* box = qobject_cast<QComboBox*>(m_sides->itemWidget(item, ColState))) {
				m_filling = true;
				box->setCurrentIndex(static_cast<int>(state));
				m_filling = false;
			}
		}
	}
	noteChanged("buildingSides");
	m_soon.start();
}

void BuildingDialog::loadSettings() {
	// Every field remembers what it was last time (by its name).
	QSettings settings;
	settings.beginGroup("buildings");
	m_fresh = !settings.contains("buildingStyle");
	for (auto* box : findChildren<QDoubleSpinBox*>()) {
		if (!box->objectName().isEmpty() && settings.contains(box->objectName())) box->setValue(settings.value(box->objectName()).toDouble());
	}
	for (auto* box : findChildren<QSpinBox*>()) {
		if (!box->objectName().isEmpty() && settings.contains(box->objectName())) box->setValue(settings.value(box->objectName()).toInt());
	}
	for (auto* box : findChildren<QComboBox*>()) {
		if (!box->objectName().isEmpty() && settings.contains(box->objectName())) box->setCurrentIndex(settings.value(box->objectName()).toInt());
	}
	for (auto* box : findChildren<QCheckBox*>()) {
		if (!box->objectName().isEmpty() && settings.contains(box->objectName())) box->setChecked(settings.value(box->objectName()).toBool());
	}
	for (auto* edit : findChildren<QLineEdit*>()) {
		if (!edit->objectName().isEmpty() && settings.contains(edit->objectName())) edit->setText(settings.value(edit->objectName()).toString());
	}
	const auto props = settings.value("props").toStringList();
	for (const auto& line : props) {
		const auto parts = line.split('|');
		if (parts.size() < 2) continue;
		FacadeProp values;
		values.automatic = parts.size() > 2 && parts[2] == "auto";
		if (values.automatic) continue;   // the Detail amount adds those again
		addProp(parts[1], static_cast<FacadePropAnchor>(std::clamp(parts[0].toInt(), 0, static_cast<int>(FacadePropAnchor::RoofEdge))));
	}
	settings.endGroup();
}

void BuildingDialog::saveSettings() const {
	QSettings settings;
	settings.beginGroup("buildings");
	for (auto* box : findChildren<QDoubleSpinBox*>()) {
		if (box->objectName().startsWith("building") && !box->objectName().startsWith("buildingProp") && !box->objectName().startsWith("buildingLot")) {
			settings.setValue(box->objectName(), box->value());
		}
	}
	for (auto* box : findChildren<QSpinBox*>()) {
		if (box->objectName().startsWith("building") && !box->objectName().startsWith("buildingProp") && !box->objectName().startsWith("buildingSide") &&
		    !box->objectName().startsWith("buildingLot")) {
			settings.setValue(box->objectName(), box->value());
		}
	}
	for (auto* box : findChildren<QComboBox*>()) {
		if (box->objectName().startsWith("building") && !box->objectName().startsWith("buildingProp") && !box->objectName().startsWith("buildingSide")) {
			settings.setValue(box->objectName(), box->currentIndex());
		}
	}
	for (auto* box : findChildren<QCheckBox*>()) {
		if (box->objectName().startsWith("building")) settings.setValue(box->objectName(), box->isChecked());
	}
	for (auto* edit : findChildren<QLineEdit*>()) {
		if (edit->objectName().startsWith("building") && !edit->objectName().startsWith("buildingProp") && !edit->objectName().startsWith("buildingSide") &&
		    edit->objectName() != "buildingLotMaterial") {
			settings.setValue(edit->objectName(), edit->text());
		}
	}
	QStringList props;
	for (const auto& prop : options().props) {
		props << QString::number(static_cast<int>(prop.anchor)) + '|' + QString::fromStdString(prop.model) + (prop.automatic ? "|auto" : "");
	}
	settings.setValue("props", props);
	settings.endGroup();
}

void BuildingDialog::accept() {
	m_soon.stop();
	saveSettings();
	const auto wanted = m_editor->buildingJob(options(), m_changed, newSize());
	const std::string key = Editor::buildingJobKey(wanted);
	QApplication::setOverrideCursor(Qt::WaitCursor);
	if (m_running) {
		if (!m_again && Editor::buildingJobKey(*m_running) == key) {
			m_watcher.waitForFinished();
			m_shown = std::make_pair(*m_running, m_watcher.result());
			m_shownKey = key;
		} else if (m_stop) {
			m_stop->store(true);
		}
		m_running.reset();
	}
	m_finished = true;
	m_editor->cancelScenePoint();
	if (m_shown && m_shownKey == key && m_editor->buildingJobCurrent(m_shown->first)) {
		m_editor->applyBuildingJob(m_shown->first, m_shown->second);   // as previewed
	} else {
		m_editor->applyBuildings(options(), m_changed, newSize());   // planned now
	}
	QApplication::restoreOverrideCursor();
	QDialog::accept();
}

void BuildingDialog::done(int result) {
	m_soon.stop();
	if (m_stop) m_stop->store(true);
	if (!m_finished) {
		m_finished = true;
		m_editor->cancelScenePoint();
		m_editor->clearBrushPreview();
		m_editor->setPropPreviewItems({});
	}
	QDialog::done(result);
}

} // namespace sme::app
