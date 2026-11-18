// particle_editor.cpp - The particle system editor of the Particles page (see particle_editor.h).

#include "particle_editor.h"

#include "material_field.h"

#include <sme/particle_edit.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace sme::app {

namespace {

struct Group {
	const char* key;
	const char* title;
	const char* tip;
};
constexpr Group GROUPS[] = {
	{"emitters", "Emitters", "When and how many particles are made"},
	{"initializers", "Initializers", "What every new particle starts with"},
	{"operators", "Operators", "How the particles move and change while they live"},
	{"forces", "Forces", "Pushes on the particles"},
	{"constraints", "Constraints", "Limits on the particles"},
	{"renderers", "Renderers", "How the particles are drawn"},
};

QLabel* label(const QString& text, const char* role) {
	auto* l = new QLabel(text);
	l->setProperty("role", role);
	return l;
}

QString materialShown(const std::string& value) {
	QString s = QString::fromStdString(value);
	if (s.endsWith(QLatin1String(".vmt"), Qt::CaseInsensitive)) s.chop(4);
	return s;
}

PcfValue* findValue(PcfValues& values, const std::string& key) {
	for (auto& [k, v] : values) {
		if (k == key) return &v;
	}
	return nullptr;
}

} // namespace

ParticleEditor::ParticleEditor(Editor* editor, AssetBrowser* assets, QWidget* parent) : QWidget(parent), m_editor(editor), m_assets(assets) {
	setObjectName("particleEditor");
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(8, 4, 12, 8);
	v->setSpacing(6);
	auto* top = new QHBoxLayout;
	top->addWidget(label(tr("EDIT SYSTEM"), "sectionHeader"), 1);
	auto* save = new QPushButton(tr("Save to mod"));
	save->setObjectName("particleSave");
	save->setToolTip(tr("Writes the system into the mod's own particle file and its particle list, so maps and models can use it"));
	auto* close = new QPushButton(tr("Close"));
	close->setObjectName("particleEditClose");
	top->addWidget(save);
	top->addWidget(close);
	v->addLayout(top);
	connect(save, &QPushButton::clicked, this, &ParticleEditor::saveRequested);
	connect(close, &QPushButton::clicked, this, &ParticleEditor::closeRequested);

	auto* nameRow = new QHBoxLayout;
	nameRow->addWidget(new QLabel(tr("Name")));
	m_name = new QLineEdit;
	m_name->setObjectName("particleName");
	m_name->setToolTip(tr("The name maps and models use (info_particle_system, model effects). Letters, digits, _ - ."));
	nameRow->addWidget(m_name, 1);
	v->addLayout(nameRow);
	connect(m_name, &QLineEdit::textEdited, this, [this](const QString& text) {
		const auto name = particleSystemName(text.toStdString());
		if (name.empty()) return;
		m_system.name = name;
		emitChanged();
	});
	m_noteLabel = label(QString(), "description");
	m_noteLabel->setWordWrap(true);
	v->addWidget(m_noteLabel);

	m_cards = new QWidget;
	m_cardsLayout = new QVBoxLayout(m_cards);
	m_cardsLayout->setContentsMargins(0, 0, 0, 0);
	m_cardsLayout->setSpacing(6);
	v->addWidget(m_cards);
	v->addStretch(1);

	m_rebuildSoon.setSingleShot(true);
	m_rebuildSoon.setInterval(0);
	connect(&m_rebuildSoon, &QTimer::timeout, this, &ParticleEditor::rebuild);
}

void ParticleEditor::setSystem(PcfSystem system, const QString& note) {
	m_system = std::move(system);
	m_note = note;
	m_name->setText(QString::fromStdString(m_system.name));
	m_noteLabel->setText(note);
	m_noteLabel->setVisible(!note.isEmpty());
	rebuild();
}

std::vector<PcfFunction>* ParticleEditor::list(const QString& group) {
	if (group == QLatin1String("emitters")) return &m_system.emitters;
	if (group == QLatin1String("initializers")) return &m_system.initializers;
	if (group == QLatin1String("operators")) return &m_system.operators;
	if (group == QLatin1String("renderers")) return &m_system.renderers;
	if (group == QLatin1String("forces")) return &m_system.forces;
	if (group == QLatin1String("constraints")) return &m_system.constraints;
	return nullptr;
}

bool ParticleEditor::setValueText(const QString& group, int index, const QString& key, const QString& text) {
	PcfValues* values = nullptr;
	if (group == QLatin1String("properties")) {
		values = &m_system.properties;
	} else if (auto* parts = list(group); parts && index >= 0 && index < int(parts->size())) {
		values = &(*parts)[std::size_t(index)].values;
	}
	if (!values) return false;
	PcfValue* value = findValue(*values, key.toStdString());
	if (!value) return false;
	std::string input = text.toStdString();
	if (key == QLatin1String("material") && std::holds_alternative<std::string>(*value)) {
		// The field shows material names; the file keeps them with .vmt, in lower case.
		QString m = text.trimmed().toLower().replace('\\', '/');
		if (!m.isEmpty() && !m.endsWith(QLatin1String(".vmt"))) m += QLatin1String(".vmt");
		input = m.toStdString();
	}
	auto parsed = pcfValueFromText(input, *value);
	if (!parsed) return false;
	*value = std::move(*parsed);
	emitChanged();
	return true;
}

bool ParticleEditor::addPart(const QString& group, const QString& functionName) {
	auto* parts = list(group);
	if (!parts) return false;
	for (const auto& info : particleFunctionCatalog()) {
		if (QString::fromStdString(info.group) == group && QString::fromStdString(info.name) == functionName) {
			parts->push_back({info.name, info.defaults});
			emitChanged();
			m_rebuildSoon.start();
			return true;
		}
	}
	return false;
}

void ParticleEditor::removePart(const QString& group, int index) {
	auto* parts = list(group);
	if (!parts || index < 0 || index >= int(parts->size())) return;
	parts->erase(parts->begin() + index);
	emitChanged();
	m_rebuildSoon.start();
}

void ParticleEditor::emitChanged() {
	emit changed();
}

QWidget* ParticleEditor::valueRow(const QString& group, int index, const std::string& key, const PcfValue& value) {
	auto* row = new QWidget;
	auto* h = new QHBoxLayout(row);
	h->setContentsMargins(0, 0, 0, 0);
	auto* name = label(QString::fromStdString(key), "valueKey");
	name->setMinimumWidth(90);
	name->setWordWrap(true);
	h->addWidget(name, 2);
	const QString qkey = QString::fromStdString(key);
	if (const auto* flag = std::get_if<bool>(&value)) {
		auto* check = new QCheckBox;
		check->setChecked(*flag);
		connect(check, &QCheckBox::toggled, this, [this, group, index, qkey](bool on) { setValueText(group, index, qkey, on ? "1" : "0"); });
		h->addWidget(check, 3);
		return row;
	}
	if (key == "material" && std::holds_alternative<std::string>(value)) {
		auto* field = new MaterialField(QStringLiteral("particleMaterial"), m_editor);
		field->setAssets(m_assets);
		field->setText(materialShown(std::get<std::string>(value)));
		connect(field->lineEdit(), &QLineEdit::textChanged, this, [this, group, index, qkey](const QString& text) { setValueText(group, index, qkey, text); });
		h->addWidget(field, 3);
		return row;
	}
	auto* edit = new QLineEdit(QString::fromStdString(pcfValueText(value)));
	edit->setObjectName(QStringLiteral("particleValue_%1").arg(qkey));
	if (std::holds_alternative<PcfColor>(value)) edit->setToolTip(tr("Red green blue alpha, 0 to 255"));
	else if (std::holds_alternative<Vec3>(value)) edit->setToolTip(tr("Three numbers: x y z"));
	connect(edit, &QLineEdit::textEdited, this, [this, edit, group, index, qkey](const QString& text) {
		const bool ok = setValueText(group, index, qkey, text);
		edit->setStyleSheet(ok ? QString() : QStringLiteral("QLineEdit { border: 1px solid #c0504d; }"));
		edit->setToolTip(ok ? QString() : tr("This does not fit the value (it keeps its last good value)."));
	});
	h->addWidget(edit, 3);
	if (const auto* color = std::get_if<PcfColor>(&value)) {
		auto* pick = new QToolButton;
		pick->setText(QStringLiteral("■"));
		pick->setStyleSheet(QStringLiteral("QToolButton { color: rgb(%1,%2,%3); }").arg(color->r).arg(color->g).arg(color->b));
		pick->setToolTip(tr("Choose a colour"));
		connect(pick, &QToolButton::clicked, this, [this, edit, pick, group, index, qkey] {
			const auto now = pcfValueFromText(edit->text().toStdString(), PcfValue(PcfColor{}));
			const auto c = now ? std::get<PcfColor>(*now) : PcfColor{};
			const QColor chosen = QColorDialog::getColor(QColor(c.r, c.g, c.b, c.a), this, tr("Particle colour"), QColorDialog::ShowAlphaChannel);
			if (!chosen.isValid()) return;
			const QString text = QStringLiteral("%1 %2 %3 %4").arg(chosen.red()).arg(chosen.green()).arg(chosen.blue()).arg(chosen.alpha());
			edit->setText(text);
			setValueText(group, index, qkey, text);
			pick->setStyleSheet(QStringLiteral("QToolButton { color: rgb(%1,%2,%3); }").arg(chosen.red()).arg(chosen.green()).arg(chosen.blue()));
		});
		h->addWidget(pick);
	}
	return row;
}

void ParticleEditor::rebuild() {
	// The cards are built anew only when parts come or go (a value edit keeps its field).
	auto* fresh = new QWidget;
	auto* v = new QVBoxLayout(fresh);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(6);
	const auto cardFrame = [](const QString& title, QWidget** body, QPushButton** remove) {
		auto* frame = new QFrame;
		frame->setObjectName("particleCard");
		auto* layout = new QVBoxLayout(frame);
		layout->setContentsMargins(10, 8, 10, 8);
		auto* head = new QHBoxLayout;
		head->addWidget(label(title, "cardTitle"), 1);
		if (remove) {
			*remove = new QPushButton(QObject::tr("Remove"));
			(*remove)->setObjectName("particleRemovePart");
			head->addWidget(*remove);
		}
		layout->addLayout(head);
		*body = frame;
		return frame;
	};

	v->addWidget(label(tr("SYSTEM"), "sectionHeader"));
	{
		QWidget* body = nullptr;
		auto* frame = cardFrame(tr("Own values"), &body, nullptr);
		for (const auto& [key, value] : m_system.properties) frame->layout()->addWidget(valueRow(QStringLiteral("properties"), -1, key, value));
		v->addWidget(frame);
	}
	for (const auto& group : GROUPS) {
		const QString key = QString::fromLatin1(group.key);
		auto* parts = list(key);
		auto* header = label(tr(group.title).toUpper() + QStringLiteral("  ·  %1").arg(parts->size()), "sectionHeader");
		header->setToolTip(tr(group.tip));
		v->addWidget(header);
		for (int i = 0; i < int(parts->size()); ++i) {
			QWidget* body = nullptr;
			QPushButton* remove = nullptr;
			const auto& part = (*parts)[std::size_t(i)];
			auto* frame = cardFrame(QString::fromStdString(part.functionName), &body, &remove);
			for (const auto& info : particleFunctionCatalog()) {
				if (info.name == part.functionName && !info.description.empty()) {
					auto* d = label(QString::fromStdString(info.description), "description");
					d->setWordWrap(true);
					frame->layout()->addWidget(d);
					break;
				}
			}
			for (const auto& [k, value] : part.values) frame->layout()->addWidget(valueRow(key, i, k, value));
			connect(remove, &QPushButton::clicked, this, [this, key, i] { removePart(key, i); });
			v->addWidget(frame);
		}
		// Add one of the common parts of this group.
		QStringList names;
		for (const auto& info : particleFunctionCatalog()) {
			if (info.group == group.key) names << QString::fromStdString(info.name);
		}
		if (!names.isEmpty()) {
			auto* addRow = new QHBoxLayout;
			auto* choice = new QComboBox;
			choice->setObjectName(QStringLiteral("particleAdd_%1").arg(key));
			choice->addItems(names);
			auto* add = new QPushButton(tr("Add"));
			addRow->addWidget(choice, 1);
			addRow->addWidget(add);
			connect(add, &QPushButton::clicked, this, [this, key, choice] { addPart(key, choice->currentText()); });
			v->addLayout(addRow);
		}
	}
	if (!m_system.children.empty()) {
		v->addWidget(label(tr("CHILDREN  ·  %1").arg(m_system.children.size()), "sectionHeader"));
		for (int i = 0; i < int(m_system.children.size()); ++i) {
			const auto& child = m_system.children[std::size_t(i)];
			auto* row = new QHBoxLayout;
			row->addWidget(label(QString::fromStdString(child.name) + (child.delay > 0 ? tr("  (after %1 s)").arg(child.delay) : QString()), "valueText"), 1);
			auto* remove = new QPushButton(tr("Remove"));
			connect(remove, &QPushButton::clicked, this, [this, i] {
				if (i < int(m_system.children.size())) m_system.children.erase(m_system.children.begin() + i);
				emitChanged();
				m_rebuildSoon.start();
			});
			row->addWidget(remove);
			v->addLayout(row);
		}
		auto* note = label(tr("Children are saved along with the system (the game needs them in the same file)."), "description");
		note->setWordWrap(true);
		v->addWidget(note);
	}
	m_cardsLayout->addWidget(fresh);
	if (m_cardsLayout->count() > 1) {
		auto* old = m_cardsLayout->takeAt(0);
		if (old->widget()) old->widget()->deleteLater();
		delete old;
	}
}

} // namespace sme::app
