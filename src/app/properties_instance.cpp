// properties_instance.cpp - The "Instance" box of the properties panel for a func_instance:
// which .vmf it shows (with a browse button and whether it was found), the $variables it fills
// in (replace01..NN) as a small table, the parameters the instance offers, and buttons to read
// the file again or to collapse the instance into the map. Part of PropertiesPanel, kept in its
// own file so the instance feature stays in one place.

#include "properties_panel.h"

#include "editor.h"
#include "ui_helpers.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <sme/instances.h>
#include <sme/text.h>

namespace sme::app {

namespace {

/// The instance's parameters ("$var type default" of its func_instance_parms), for the hint.
QStringList instanceParameters(const MapDocument& instance) {
	QStringList out;
	for (const auto& entity : instance.entities) {
		if (!iequals(entity.classname(), "func_instance_parms")) continue;
		for (const auto& [key, value] : entity.keyValues) {
			if (toLower(key).starts_with("parm") && !value.empty()) out << QString::fromStdString(value);
		}
	}
	return out;
}

} // namespace

void PropertiesPanel::buildInstanceBox(QVBoxLayout* layout, const Entity& funcInstance) {
	auto* box = new QGroupBox(tr("Instance"));
	box->setObjectName("instanceBox");
	auto* v = new QVBoxLayout(box);
	const int id = funcInstance.id;
	const auto settings = instanceSettings(funcInstance);
	const auto file = m_editor->instanceFile(funcInstance);

	// The file, relative to the map (like Hammer writes it).
	auto* fileRow = new QHBoxLayout;
	auto* fileEdit = new QLineEdit(QString::fromStdString(settings.file));
	fileEdit->setObjectName("instanceFile");
	fileEdit->setPlaceholderText(tr("instances/my_instance.vmf"));
	auto* browse = new QPushButton(tr("Browse ..."));
	fileRow->addWidget(fileEdit, 1);
	fileRow->addWidget(browse);
	v->addLayout(fileRow);
	const auto setFile = [this, id](const QString& text) {
		m_editor->commit(edit::setKeyValue(m_editor->doc(), {id}, "file", text.trimmed().replace('\\', '/').toStdString()));
	};
	connect(fileEdit, &QLineEdit::editingFinished, this, [fileEdit, setFile, old = fileEdit->text()] {
		if (fileEdit->text() != old) setFile(fileEdit->text());
	});
	connect(browse, &QPushButton::clicked, this, [this, setFile] {
		const auto folders = m_editor->instanceFolders();
		const QString start = folders.empty() ? QString() : fromPath(folders.front());
		const QString path = QFileDialog::getOpenFileName(this, tr("Instance"), start, tr("Valve map (*.vmf)"));
		if (path.isEmpty()) return;
		// Relative to the first search folder that holds it (the map's folder first).
		QString relative = path;
		for (const auto& folder : folders) {
			const QDir dir(fromPath(folder));
			const QString candidate = dir.relativeFilePath(path);
			if (!candidate.startsWith("..")) {
				relative = candidate;
				break;
			}
		}
		setFile(relative);
	});

	std::shared_ptr<const MapDocument> instance;
	if (settings.file.empty()) {
		v->addWidget(noteLabel(tr("Pick the .vmf this instance shows. Its brushes and entities are drawn in place (read-only, cyan edges); "
		                     "VBSP merges them when compiling.")));
	} else if (!file) {
		auto* missing = noteLabel(tr("Not found next to the map or in the game's map folders."));
		missing->setProperty("role", "warning");
		v->addWidget(missing);
	} else {
		InstanceCache cache;
		std::string error;
		instance = cache.load(*file, &error);
		auto* found = noteLabel(instance ? tr("Shown from %1").arg(QDir::toNativeSeparators(fromPath(*file)))
		                            : tr("Could not read %1: %2").arg(fromPath(*file), QString::fromStdString(error)));
		found->setObjectName("instanceStatus");
		v->addWidget(found);
	}

	// $variables: replace01..NN as a table (an empty last row adds one).
	auto* table = new QTableWidget(0, 2);
	table->setObjectName("instanceReplacements");
	table->setHorizontalHeaderLabels({tr("Variable"), tr("Value")});
	table->horizontalHeader()->setStretchLastSection(true);
	table->verticalHeader()->setVisible(false);
	auto rows = settings.replacements;
	rows.push_back({});
	for (const auto& r : rows) {
		const int row = table->rowCount();
		table->insertRow(row);
		table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(r.variable)));
		table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(r.value)));
	}
	table->setMinimumHeight(table->verticalHeader()->defaultSectionSize() * std::min<int>(table->rowCount() + 1, 6) + 4);
	connect(table, &QTableWidget::itemChanged, this, [this, id, table] {
		std::vector<InstanceReplacement> list;
		for (int row = 0; row < table->rowCount(); ++row) {
			const auto* variable = table->item(row, 0);
			const auto* value = table->item(row, 1);
			const QString name = variable ? variable->text().trimmed() : QString();
			if (name.isEmpty()) continue;
			list.push_back({name.toStdString(), value ? value->text().trimmed().toStdString() : std::string()});
		}
		// After the edit has closed: the commit rebuilds this panel.
		QTimer::singleShot(0, this, [this, id, list] {
			Transaction t(m_editor->doc(), "Instance variables");
			t.touchEntity(id);
			if (auto* entity = m_editor->doc().findEntity(id)) setInstanceReplacements(*entity, list);
			m_editor->commit(t.finish());
		});
	});
	v->addWidget(new QLabel(tr("Variables")));
	v->addWidget(table);
	if (instance) {
		const auto parameters = instanceParameters(*instance);
		if (!parameters.isEmpty()) {
			v->addWidget(noteLabel(tr("This instance offers: %1").arg(parameters.join(QStringLiteral(" · ")))));
		}
	}

	auto* buttons = new QHBoxLayout;
	auto* reload = new QPushButton(tr("Read file again"));
	reload->setObjectName("instanceReload");
	reload->setToolTip(tr("After the instance's .vmf was changed and saved"));
	auto* collapse = new QPushButton(tr("Collapse instance"));
	collapse->setObjectName("instanceCollapse");
	collapse->setToolTip(tr("Put the instance's brushes and entities into this map instead of the func_instance (Ctrl+Z undoes)"));
	collapse->setEnabled(instance != nullptr);
	connect(reload, &QPushButton::clicked, m_editor, &Editor::reloadInstances);
	connect(collapse, &QPushButton::clicked, this, [this, id] { m_editor->collapseInstance(id); });
	buttons->addWidget(reload);
	buttons->addWidget(collapse);
	buttons->addStretch(1);
	v->addLayout(buttons);
	layout->addWidget(box);
}

} // namespace sme::app
