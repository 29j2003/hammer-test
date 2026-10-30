// shortcut_dialog.cpp - Keyboard shortcut editor (see shortcut_dialog.h).

#include "shortcut_dialog.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace sme::app {

namespace {

/// What the list shows for a command: its menu text (without the "&" of mnemonics), else its name.
QString commandLabel(const QAction* action) {
	QString text = action->text();
	text.remove(QRegularExpression(QStringLiteral("&(?!&)")));
	text = text.trimmed();
	return text.isEmpty() ? action->objectName() : text;
}

QList<QAction*> commands(QWidget* owner) {
	QList<QAction*> out;
	QSet<QString> seen;
	for (auto* a : owner->findChildren<QAction*>()) {
		const QString name = a->objectName();
		// Qt's own helper actions (the clear button of a search field: "_q_qlineeditclearaction") are no commands.
		if (name.isEmpty() || name.startsWith(QLatin1String("_q_")) || a->isSeparator() || seen.contains(name)) continue;
		seen.insert(name);
		out << a;
	}
	std::sort(out.begin(), out.end(), [](QAction* a, QAction* b) { return commandLabel(a).compare(commandLabel(b), Qt::CaseInsensitive) < 0; });
	return out;
}

/// Every action with this name (some commands exist twice, e.g. in a menu and a tool bar).
QList<QAction*> actionsNamed(QWidget* owner, const QString& name) {
	QList<QAction*> out;
	for (auto* a : owner->findChildren<QAction*>()) {
		if (a->objectName() == name) out << a;
	}
	return out;
}

} // namespace

ShortcutDialog::ShortcutDialog(QWidget* owner, QWidget* parent) : QDialog(parent), m_owner(owner) {
	setWindowTitle(tr("Shortcuts"));
	resize(560, 640);
	auto* v = new QVBoxLayout(this);
	m_search = new QLineEdit;
	m_search->setPlaceholderText(tr("Search commands ..."));
	m_search->setClearButtonEnabled(true);
	v->addWidget(m_search);
	m_table = new QTableWidget(0, 3);
	m_table->setHorizontalHeaderLabels({tr("Command"), tr("Shortcut"), QString()});
	m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	m_table->verticalHeader()->hide();
	v->addWidget(m_table, 1);
	m_conflicts = new QLabel;
	m_conflicts->setWordWrap(true);
	m_conflicts->setStyleSheet("color: #ffb070;");
	v->addWidget(m_conflicts);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	v->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
		for (int row = 0; row < m_table->rowCount(); ++row) {
			m_table->setRowHidden(row, !m_table->item(row, 0)->text().contains(text, Qt::CaseInsensitive));
		}
	});
	fill();
}

void ShortcutDialog::fill() {
	const auto list = commands(m_owner);
	m_table->setRowCount(static_cast<int>(list.size()));
	int row = 0;
	for (auto* action : list) {
		const QString name = action->objectName();
		auto* label = new QTableWidgetItem(commandLabel(action));
		label->setFlags(label->flags() & ~Qt::ItemIsEditable);
		label->setToolTip(action->toolTip());
		m_table->setItem(row, 0, label);
		auto* edit = new QKeySequenceEdit(action->shortcut());
		m_table->setCellWidget(row, 1, edit);
		auto* reset = new QPushButton(tr("Default"));
		reset->setToolTip(tr("Back to %1").arg(action->property("defaultShortcut").toString()));
		m_table->setCellWidget(row, 2, reset);
		const auto store = [this, name](const QKeySequence& key) {
			for (auto* a : actionsNamed(m_owner, name)) a->setShortcut(key);
			QSettings().setValue("shortcuts/" + name, key.toString(QKeySequence::PortableText));
			checkConflicts();
		};
		connect(edit, &QKeySequenceEdit::editingFinished, this, [edit, store] { store(edit->keySequence()); });
		connect(reset, &QPushButton::clicked, this, [this, edit, action, name] {
			const QKeySequence key(action->property("defaultShortcut").toString(), QKeySequence::PortableText);
			edit->setKeySequence(key);
			for (auto* a : actionsNamed(m_owner, name)) a->setShortcut(key);
			QSettings().remove("shortcuts/" + name);
			checkConflicts();
		});
		++row;
	}
	checkConflicts();
}

void ShortcutDialog::checkConflicts() {
	QMap<QString, QStringList> byKey;
	for (auto* action : commands(m_owner)) {
		const QString key = action->shortcut().toString(QKeySequence::NativeText);
		if (!key.isEmpty()) byKey[key] << commandLabel(action);
	}
	QStringList lines;
	for (auto it = byKey.cbegin(); it != byKey.cend(); ++it) {
		if (it.value().size() > 1) {
			lines << tr("%1 is used by: %2").arg(it.key(), it.value().join(", "));
		}
	}
	m_conflicts->setText(lines.isEmpty() ? QString() : tr("Conflicts (the key does nothing until fixed):\n%1").arg(lines.join('\n')));
}

void ShortcutDialog::applySaved(QWidget* owner) {
	QSettings settings;
	settings.beginGroup("shortcuts");
	for (const auto& name : settings.childKeys()) {
		const QKeySequence key(settings.value(name).toString(), QKeySequence::PortableText);
		for (auto* a : actionsNamed(owner, name)) a->setShortcut(key);
	}
}

} // namespace sme::app
