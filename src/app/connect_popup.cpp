// connect_popup.cpp - The Connect popup (see connect_popup.h).

#include "connect_popup.h"

#include "editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <sme/io_links.h>

namespace sme::app {

namespace {

QString describe(const Entity& entity) {
	const std::string name = entity.get("targetname");
	const QString cls = QString::fromStdString(entity.classname());
	return name.empty() ? cls : QStringLiteral("%1 \"%2\"").arg(cls, QString::fromStdString(name));
}

QComboBox* editableBox(const std::vector<std::string>& names, const char* objectName) {
	auto* combo = new QComboBox;
	combo->setObjectName(QString::fromLatin1(objectName));
	combo->setEditable(true);
	combo->setInsertPolicy(QComboBox::NoInsert);
	QStringList items;
	for (const auto& name : names) items << QString::fromStdString(name);
	combo->addItems(items);
	auto* completer = new QCompleter(items, combo);
	completer->setCaseSensitivity(Qt::CaseInsensitive);
	completer->setFilterMode(Qt::MatchContains);
	combo->setCompleter(completer);
	return combo;
}

} // namespace

ConnectPopup::ConnectPopup(Editor* editor, int sourceId, int targetId, QWidget* parent)
	: QDialog(parent, Qt::Popup), m_editor(editor), m_source(sourceId), m_target(targetId) {
	setObjectName("connectPopup");
	setAttribute(Qt::WA_DeleteOnClose);
	const auto& doc = editor->doc();
	const Entity* source = doc.findEntity(sourceId);
	const Entity* target = doc.findEntity(targetId);
	const auto* catalog = editor->entities();
	const std::string sourceClass = source ? source->classname() : std::string();
	const std::string targetClass = target ? target->classname() : std::string();
	const EntityClass* sourceDefinition = catalog ? catalog->find(sourceClass) : nullptr;
	const EntityClass* targetDefinition = catalog ? catalog->find(targetClass) : nullptr;

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(12, 10, 12, 10);
	auto* title = new QLabel(tr("<b>Connect</b> %1 <b>&rarr;</b> %2")
	                             .arg(source ? describe(*source).toHtmlEscaped() : QString(), target ? describe(*target).toHtmlEscaped() : QString()));
	layout->addWidget(title);

	m_suggestions = new QListWidget;
	m_suggestions->setObjectName("connectSuggestions");
	for (const auto& suggestion : suggestConnections(sourceClass, sourceDefinition, targetClass, targetDefinition)) {
		auto* item = new QListWidgetItem(QStringLiteral("%1  →  %2").arg(QString::fromStdString(suggestion.output), QString::fromStdString(suggestion.input)));
		item->setData(Qt::UserRole, QString::fromStdString(suggestion.output));
		item->setData(Qt::UserRole + 1, QString::fromStdString(suggestion.input));
		m_suggestions->addItem(item);
	}
	m_suggestions->setMaximumHeight(m_suggestions->sizeHintForRow(0) * std::min(6, std::max(1, m_suggestions->count())) + 6);
	layout->addWidget(m_suggestions);

	auto* form = new QFormLayout;
	m_output = editableBox(rankedOutputs(sourceClass, sourceDefinition), "connectOutput");
	m_input = editableBox(rankedInputs(targetClass, targetDefinition), "connectInput");
	m_parameter = new QLineEdit;
	m_parameter->setObjectName("connectParameter");
	m_parameter->setPlaceholderText(tr("none"));
	m_delay = new QDoubleSpinBox;
	m_delay->setObjectName("connectDelay");
	m_delay->setRange(0, 100000);
	m_delay->setDecimals(2);
	m_delay->setSuffix(tr(" s"));
	m_once = new QCheckBox(tr("Only once"));
	m_once->setObjectName("connectOnce");
	form->addRow(tr("When"), m_output);
	form->addRow(tr("do"), m_input);
	form->addRow(tr("Parameter"), m_parameter);
	form->addRow(tr("Delay"), m_delay);
	form->addRow(QString(), m_once);
	layout->addLayout(form);
	if (target && target->get("targetname").empty()) {
		auto* naming = new QLabel(tr("The target has no name yet - it gets one (like door_1)."));
		naming->setProperty("role", "description");
		naming->setWordWrap(true);
		layout->addWidget(naming);
	}

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Connect"));
	buttons->button(QDialogButtonBox::Ok)->setObjectName("connectAccept");
	connect(buttons, &QDialogButtonBox::accepted, this, &ConnectPopup::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	// A suggestion fills both boxes; the best one is chosen from the start.
	connect(m_suggestions, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
		if (!item) return;
		m_output->setEditText(item->data(Qt::UserRole).toString());
		m_input->setEditText(item->data(Qt::UserRole + 1).toString());
	});
	connect(m_suggestions, &QListWidget::itemDoubleClicked, this, &ConnectPopup::accept);
	if (m_suggestions->count() > 0) {
		m_suggestions->setCurrentRow(0);
	}
	m_suggestions->setFocus();
}

void ConnectPopup::accept() {
	Connection connection;
	connection.output = m_output->currentText().trimmed().toStdString();
	connection.input = m_input->currentText().trimmed().toStdString();
	connection.parameter = m_parameter->text().trimmed().toStdString();
	connection.delay = m_delay->value();
	connection.timesToFire = m_once->isChecked() ? 1 : -1;
	auto [change, name] = edit::connect(m_editor->doc(), m_source, m_target, connection);
	if (!name.empty()) {
		m_editor->commit(std::move(change));
		const Entity* source = m_editor->doc().findEntity(m_source);
		const int index = source ? int(source->connections.size()) - 1 : -1;
		emit m_editor->message(tr("Connected: %1 → %2.%3").arg(QString::fromStdString(connection.output), QString::fromStdString(name),
		                                                            QString::fromStdString(connection.input)));
		m_editor->showConnection(m_source, index);
	}
	QDialog::accept();
}

} // namespace sme::app
