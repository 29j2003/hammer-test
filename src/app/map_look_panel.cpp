// map_look_panel.cpp - Map Look dock (see map_look_panel.h).

#include "map_look_panel.h"

#include "editor.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

#include <sme/fidelity_look.h>
#include <sme/text.h>

namespace sme::app {

namespace {

constexpr int STEPS = 1000;

QString formatValue(double value, int decimals) {
	QString text = QString::number(value, 'f', decimals);
	if (text.contains('.')) {
		while (text.endsWith('0')) text.chop(1);
		if (text.endsWith('.')) text.chop(1);
	}
	return text;
}

/// Inside the map, where the two helper entities go: above the player start, else the middle of the brushes.
Vec3 helperPlace(const MapDocument& doc) {
	for (const auto& entity : doc.entities) {
		if (iequals(entity.classname(), "info_player_start")) return entity.origin() + Vec3{0, 0, 48};
	}
	Bounds box;
	for (const auto& solid : doc.world.solids) {
		for (const auto& side : solid.sides) {
			for (const auto& p : side.planePoints) box.add(p);
		}
	}
	return box.empty() ? Vec3{} : box.center();
}

} // namespace

MapLookPanel::MapLookPanel(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	outer->addWidget(scroll);
	auto* content = new QWidget;
	auto* v = new QVBoxLayout(content);
	v->setContentsMargins(10, 10, 10, 10);
	v->setSpacing(10);
	scroll->setWidget(content);

	m_note = new QLabel;
	m_note->setWordWrap(true);
	m_note->setProperty("role", "hint");
	v->addWidget(m_note);
	m_preview = new QCheckBox(tr("Show it in the lighting preview"));
	m_preview->setObjectName("lookPreview");
	connect(m_preview, &QCheckBox::toggled, m_editor, [this](bool on) {
		if (!m_refreshing) m_editor->setFidelityLook(on);
	});
	v->addWidget(m_preview);

	QString group;
	QFormLayout* form = nullptr;
	for (const auto& setting : lookSettings()) {
		if (QString::fromStdString(setting.group) != group) {
			group = QString::fromStdString(setting.group);
			auto* box = new QGroupBox(tr(setting.group.c_str()));
			form = new QFormLayout(box);
			form->setContentsMargins(10, 12, 10, 8);
			form->setHorizontalSpacing(10);
			form->setVerticalSpacing(6);
			form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
			form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
			v->addWidget(box);
		}
		auto* row = new QWidget;
		auto* h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(6);
		auto* slider = new QSlider(Qt::Horizontal);
		slider->setRange(0, STEPS);
		slider->setObjectName(QString::fromStdString(setting.cvar));
		auto* spin = new QDoubleSpinBox;
		spin->setRange(setting.minimum, setting.maximum);
		spin->setDecimals(setting.decimals);
		spin->setSingleStep(std::pow(10.0, -setting.decimals) * 5);
		spin->setKeyboardTracking(false);
		spin->setFixedWidth(64);
		auto* reset = new QToolButton;
		reset->setText(QStringLiteral("↺"));
		reset->setAutoRaise(true);
		reset->setToolTip(tr("Back to the mod's own value"));
		h->addWidget(slider, 1);
		h->addWidget(spin);
		h->addWidget(reset);
		auto* label = new QLabel(tr(setting.label.c_str()));
		label->setToolTip(QStringLiteral("%1\n%2").arg(tr(setting.description.c_str()), QString::fromStdString(setting.cvar)));
		slider->setToolTip(label->toolTip());
		form->addRow(label, row);
		const std::string cvar = setting.cvar;
		const double minimum = setting.minimum, maximum = setting.maximum;
		const int decimals = setting.decimals;
		connect(slider, &QSlider::sliderPressed, this, [this] {
			m_dragging = true;
			m_dragCommitted = false;
		});
		connect(slider, &QSlider::sliderReleased, this, [this] { m_dragging = false; });
		connect(slider, &QSlider::valueChanged, this, [=, this](int position) {
			if (m_refreshing) return;
			const double value = minimum + (maximum - minimum) * position / STEPS;
			const double rounded = std::round(value * std::pow(10.0, decimals)) / std::pow(10.0, decimals);
			setValue(cvar, rounded, m_dragging && m_dragCommitted);
			if (m_dragging) m_dragCommitted = true;
		});
		connect(spin, &QDoubleSpinBox::valueChanged, this, [=, this](double value) {
			if (!m_refreshing) setValue(cvar, value);
		});
		connect(reset, &QToolButton::clicked, this, [=, this] { resetValue(cvar); });
		m_rows[cvar] = {slider, spin, reset, label};
	}
	v->addStretch(1);
	connect(editor, &Editor::documentChanged, this, &MapLookPanel::refresh);
	connect(editor, &Editor::gameChanged, this, &MapLookPanel::refresh);
	connect(editor, &Editor::settingsChanged, this, &MapLookPanel::refresh);
	refresh();
}

void MapLookPanel::apply(std::map<std::string, std::string> values, bool merge) {
	auto change = setMapLookValues(m_editor->doc(), values, helperPlace(m_editor->doc()));
	if (merge) {
		m_editor->commitMerged(std::move(change));
	} else {
		m_editor->commit(std::move(change));
	}
}

void MapLookPanel::setValue(const std::string& cvar, double value, bool merge) {
	const auto* setting = findLookSetting(cvar);
	if (!setting) return;
	auto values = mapLookValues(m_editor->doc());
	const QString text = formatValue(value, setting->decimals);
	if (values.contains(cvar) && QString::fromStdString(values[cvar]) == text) return;
	values[cvar] = text.toStdString();
	apply(std::move(values), merge);
}

void MapLookPanel::resetValue(const std::string& cvar) {
	auto values = mapLookValues(m_editor->doc());
	if (values.erase(cvar) == 0) return;
	apply(std::move(values), false);
}

void MapLookPanel::refresh() {
	m_refreshing = true;
	const auto values = mapLookValues(m_editor->doc());
	m_preview->setChecked(m_editor->fidelityLook());
	m_note->setText(m_editor->hasFidelity()
	                    ? tr("What mapbase-fidelity shows in this map. The map sets these when it loads (a logic_auto sends them to a "
	                         "point_clientcommand - both are kept by the editor). Grey values are the mod's own.")
	                    : tr("This mod does not have mapbase-fidelity: the values only show in the preview here. Players see them once the "
	                         "mod has it (Project > Features > Install features)."));
	for (const auto& setting : lookSettings()) {
		auto& row = m_rows.at(setting.cvar);
		const bool own = values.contains(setting.cvar);
		const double value = m_editor->lookValue(setting.cvar);
		row.spin->setValue(value);
		const double span = setting.maximum - setting.minimum;
		row.slider->setValue(span > 0 ? int(std::lround((value - setting.minimum) / span * STEPS)) : 0);
		row.reset->setEnabled(own);
		QFont font = row.label->font();
		font.setBold(own);
		row.label->setFont(font);
		row.spin->setProperty("inherited", !own);
		row.spin->setStyleSheet(own ? QString() : QStringLiteral("color: palette(mid);"));
	}
	m_refreshing = false;
}

} // namespace sme::app
