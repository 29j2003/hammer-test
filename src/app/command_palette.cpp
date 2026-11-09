// command_palette.cpp - Command search (see command_palette.h).

#include "command_palette.h"

#include <QAction>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QVBoxLayout>

#include <sme/command_search.h>

namespace sme::app {

namespace {

QString plain(QString text) {
	return text.remove('&');
}

void collect(QMenu* menu, const QString& where, std::vector<Command>& out) {
	for (QAction* action : menu->actions()) {
		if (action->isSeparator()) continue;
		if (QMenu* sub = action->menu()) {
			collect(sub, where + QStringLiteral(" › ") + plain(sub->title()), out);
			continue;
		}
		const QString name = plain(action->text());
		if (name.isEmpty() || action->shortcut() == QKeySequence("Ctrl+K")) continue;   // not itself
		QPointer<QAction> guarded(action);
		out.push_back({name, where, action->shortcut().toString(QKeySequence::NativeText),
		               [guarded] {
			               if (guarded && guarded->isEnabled()) guarded->trigger();
		               },
		               action->isEnabled()});
	}
}

} // namespace

CommandPalette::CommandPalette(std::vector<Command> fixed, std::function<std::vector<Command>(const QString&)> more, QWidget* parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint), m_fixed(std::move(fixed)), m_more(std::move(more)) {
	setObjectName("commandPalette");
	setAttribute(Qt::WA_DeleteOnClose);
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	m_query = new QLineEdit(this);
	m_query->setObjectName("commandQuery");
	m_query->setPlaceholderText(tr("Search commands, tools, materials, models, entities ..."));
	m_query->setClearButtonEnabled(true);
	layout->addWidget(m_query);
	m_list = new QListWidget(this);
	m_list->setObjectName("commandList");
	m_list->setUniformItemSizes(true);
	layout->addWidget(m_list, 1);
	auto* hint = new QLabel(tr("Enter runs · ↑↓ choose · Esc closes"), this);
	hint->setProperty("role", "description");
	layout->addWidget(hint);
	m_query->installEventFilter(this);
	connect(m_query, &QLineEdit::textChanged, this, &CommandPalette::update);
	connect(m_query, &QLineEdit::returnPressed, this, &CommandPalette::runCurrent);
	connect(m_list, &QListWidget::itemActivated, this, [this] { runCurrent(); });
	resize(560, 420);
	update();
	m_query->setFocus();
}

std::vector<Command> CommandPalette::menuCommands(QMenuBar* bar) {
	std::vector<Command> out;
	for (QAction* action : bar->actions()) {
		if (QMenu* menu = action->menu()) collect(menu, plain(menu->title()), out);
	}
	return out;
}

QStringList CommandPalette::listed() const {
	QStringList names;
	for (const auto& command : m_shown) names << command.name;
	return names;
}

void CommandPalette::setQuery(const QString& query) {
	m_query->setText(query);
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event) {
	if (watched == m_query && event->type() == QEvent::KeyPress) {
		const auto* key = static_cast<QKeyEvent*>(event);
		const int row = m_list->currentRow();
		if (key->key() == Qt::Key_Down) {
			m_list->setCurrentRow(std::min(row + 1, m_list->count() - 1));
			return true;
		}
		if (key->key() == Qt::Key_Up) {
			m_list->setCurrentRow(std::max(row - 1, 0));
			return true;
		}
	}
	return QDialog::eventFilter(watched, event);
}

void CommandPalette::update() {
	const QString query = m_query->text().trimmed();
	std::vector<Command> all = m_fixed;
	if (m_more && query.size() >= 3) {
		for (auto& command : m_more(query)) all.push_back(std::move(command));
	}
	std::vector<std::string> names, wheres;
	names.reserve(all.size());
	wheres.reserve(all.size());
	for (const auto& command : all) {
		names.push_back(command.name.toStdString());
		wheres.push_back(command.where.toStdString());
	}
	m_shown.clear();
	m_list->clear();
	for (const std::size_t i : rankCommands(query.toStdString(), names, wheres, query.isEmpty() ? 400 : 60)) {
		const auto& command = all[i];
		QString text = command.name + QStringLiteral("   ·  ") + command.where;
		if (!command.shortcut.isEmpty()) text += QStringLiteral("   [") + command.shortcut + ']';
		auto* item = new QListWidgetItem(text, m_list);
		if (!command.enabled) item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
		m_shown.push_back(command);
	}
	if (m_list->count() > 0) m_list->setCurrentRow(0);
}

void CommandPalette::runCurrent() {
	const int row = m_list->currentRow();
	if (row < 0 || row >= int(m_shown.size())) return;
	const Command command = m_shown[static_cast<std::size_t>(row)];
	if (!command.enabled) return;
	close();
	if (command.run) command.run();
}

} // namespace sme::app
