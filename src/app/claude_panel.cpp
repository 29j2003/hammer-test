// claude_panel.cpp - The Project › Claude tab (see claude_panel.h).

#include "claude_panel.h"

#include "claude_protocol.h"
#include "claude_service.h"
#include "toggle_switch.h"
#include "ui_helpers.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace sme::app {

namespace {

QGroupBox* card(const QString& title, const char* name) {
	auto* box = new QGroupBox(title.toUpper());
	box->setProperty("role", "section");
	box->setObjectName(QString::fromLatin1(name));
	return box;
}

QLabel* description(const QString& text) {
	auto* label = new QLabel(text);
	label->setProperty("role", "description");
	label->setWordWrap(true);
	return label;
}

} // namespace

ClaudePanel::ClaudePanel(ClaudeService* service, QWidget* parent) : QWidget(parent), m_service(service) {
	setObjectName("claudePanel");
	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto* scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	outer->addWidget(scroll);
	auto* content = new QWidget(scroll->viewport());
	scroll->setWidget(content);
	auto* v = new QVBoxLayout(content);
	v->setContentsMargins(12, 10, 12, 10);
	v->setSpacing(10);
	limitReadableWidth(content, 1100);

	// On / off and what Claude may do.
	auto* main = card(tr("Claude"), "claudeMainCard");
	auto* mv = new QVBoxLayout(main);
	auto* row = new QHBoxLayout;
	m_enabled = new ToggleSwitch(main);
	m_enabled->setObjectName("claudeEnabled");
	m_enabled->setAccessibleName(tr("Let Claude use Mapsmith"));
	row->addWidget(m_enabled, 0, Qt::AlignTop);
	auto* text = new QVBoxLayout;
	auto* title = new QLabel(tr("Let Claude use Mapsmith"), main);
	title->setStyleSheet(QStringLiteral("font-weight: 600"));
	text->addWidget(title);
	text->addWidget(description(tr("Claude (in Claude Desktop or Claude Code on this computer) can look at your materials and change them - "
	                               "only through Mapsmith. It cannot run programs, open other folders or use the internet through Mapsmith. "
	                               "Off by default.")));
	m_status = new QLabel(main);
	m_status->setObjectName("claudeStatus");
	m_status->setWordWrap(true);
	text->addWidget(m_status);
	row->addLayout(text, 1);
	mv->addLayout(row);

	mv->addSpacing(4);
	m_read = new QRadioButton(tr("Read only - look at materials, problems, the log and the 3D view"), main);
	m_read->setObjectName("claudeRead");
	m_change = new QRadioButton(tr("Read and change materials - also the decision file, the mod's own material files and files in the exchange folder"), main);
	m_change->setObjectName("claudeChange");
	mv->addWidget(m_read);
	mv->addWidget(m_change);
	auto* askRow = new QHBoxLayout;
	m_ask = new ToggleSwitch(main);
	m_ask->setObjectName("claudeAsk");
	askRow->addWidget(m_ask);
	auto* askText = new QLabel(tr("Ask me before each change"), main);
	askRow->addWidget(askText, 1);
	mv->addLayout(askRow);
	v->addWidget(main);

	// The one folder Claude may read and write files in.
	auto* folder = card(tr("Exchange folder"), "claudeFolderCard");
	auto* fv = new QVBoxLayout(folder);
	fv->addWidget(description(tr("The only folder Claude can read and write files in (notes, lists, editor reports). Backups of everything "
	                             "Claude changes go to its \"backups\" folder.")));
	auto* folderRow = new QHBoxLayout;
	m_folder = new QLabel(folder);
	m_folder->setObjectName("claudeFolder");
	m_folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_folder->setText(QDir::toNativeSeparators(m_service->exchangeFolder()));
	folderRow->addWidget(m_folder, 1);
	auto* open = new QPushButton(tr("Open folder"), folder);
	open->setObjectName("claudeOpenFolder");
	connect(open, &QPushButton::clicked, this, [this] {
		QDir().mkpath(m_service->exchangeFolder());
		QDesktopServices::openUrl(QUrl::fromLocalFile(m_service->exchangeFolder()));
	});
	folderRow->addWidget(open);
	fv->addLayout(folderRow);
	v->addWidget(folder);

	// Connecting Claude.
	auto* connectCard = card(tr("Connect Claude"), "claudeConnectCard");
	auto* cv = new QVBoxLayout(connectCard);
	cv->addWidget(description(tr("Do this once. Claude Desktop then starts Mapsmith's bridge by itself; restart Claude Desktop afterwards. "
	                             "Claude uses your Claude account - Mapsmith needs no key.")));
	auto* buttons = new QHBoxLayout;
	auto* desktop = new QPushButton(tr("Connect Claude Desktop"), connectCard);
	desktop->setObjectName("claudeConnectDesktop");
	desktop->setProperty("accent", true);
	connect(desktop, &QPushButton::clicked, this, &ClaudePanel::connectDesktop);
	buttons->addWidget(desktop);
	auto* code = new QPushButton(tr("Copy setup for Claude Code"), connectCard);
	code->setObjectName("claudeCopyCode");
	connect(code, &QPushButton::clicked, this, [this, code] {
		QApplication::clipboard()->setText(claude::claudeCodeCommand(ClaudeService::bridgePath()));
		code->setText(tr("Copied - paste it in a terminal"));
	});
	buttons->addWidget(code);
	buttons->addStretch(1);
	cv->addLayout(buttons);
	v->addWidget(connectCard);

	// What Claude did.
	auto* activity = card(tr("Claude activity"), "claudeActivityCard");
	auto* av = new QVBoxLayout(activity);
	m_activity = new QTreeWidget(activity);
	m_activity->setObjectName("claudeActivity");
	m_activity->setHeaderLabels({tr("Time"), tr("Tool"), tr("What"), tr("Result")});
	m_activity->setRootIsDecorated(false);
	m_activity->setMinimumHeight(180);
	m_activity->header()->setSectionResizeMode(2, QHeaderView::Stretch);
	av->addWidget(m_activity);
	auto* undoRow = new QHBoxLayout;
	m_undo = new QPushButton(tr("Undo Claude's last change"), activity);
	m_undo->setObjectName("claudeUndo");
	connect(m_undo, &QPushButton::clicked, this, [this] {
		QString error;
		if (!m_service->undoLastChange(&error) && !error.isEmpty()) QMessageBox::warning(this, tr("Undo"), error);
	});
	undoRow->addWidget(m_undo);
	undoRow->addWidget(description(tr("Every change Claude makes keeps a backup; this puts them back one by one.")), 1);
	av->addLayout(undoRow);
	v->addWidget(activity);
	v->addStretch(1);

	for (const auto& a : m_service->activity()) addActivity(a);
	connect(m_enabled, &QCheckBox::toggled, m_service, &ClaudeService::setEnabled);
	connect(m_read, &QRadioButton::toggled, this, [this](bool on) {
		if (on) m_service->setAccess(ClaudeAccess::Read);
	});
	connect(m_change, &QRadioButton::toggled, this, [this](bool on) {
		if (on) m_service->setAccess(ClaudeAccess::Change);
	});
	connect(m_ask, &QCheckBox::toggled, m_service, &ClaudeService::setAskBeforeChange);
	connect(m_service, &ClaudeService::stateChanged, this, &ClaudePanel::refreshState);
	connect(m_service, &ClaudeService::activityAdded, this, &ClaudePanel::addActivity);
	refreshState();
}

void ClaudePanel::refreshState() {
	const QSignalBlocker b1(m_enabled), b2(m_read), b3(m_change), b4(m_ask);
	m_enabled->setChecked(m_service->enabled());
	m_read->setChecked(m_service->access() == ClaudeAccess::Read);
	m_change->setChecked(m_service->access() == ClaudeAccess::Change);
	m_ask->setChecked(m_service->askBeforeChange());
	m_status->setText(m_service->statusText());
	m_undo->setEnabled(m_service->canUndo());
	m_undo->setToolTip(m_service->canUndo() ? tr("Undo: %1").arg(m_service->undoText()) : QString());
}

void ClaudePanel::addActivity(const ClaudeActivity& a) {
	auto* item = new QTreeWidgetItem(m_activity, {a.time.toString(QStringLiteral("HH:mm:ss")), a.tool, a.what, a.result});
	if (!a.ok) item->setForeground(3, QColor(230, 140, 90));
	else if (a.change) item->setForeground(3, QColor(120, 200, 140));
	while (m_activity->topLevelItemCount() > 500) delete m_activity->takeTopLevelItem(0);
	m_activity->scrollToItem(item);
}

void ClaudePanel::connectDesktop() {
	const QStringList files = claude::claudeDesktopConfigFiles();
	const auto answer = QMessageBox::question(this, tr("Connect Claude Desktop"),
	                                          tr("Mapsmith adds itself to Claude Desktop's settings:\n%1\n\nThe other settings stay; a backup is kept "
	                                             "next to the file. Restart Claude Desktop afterwards.")
	                                              .arg(QDir::toNativeSeparators(files.join(QStringLiteral("\n")))));
	if (answer != QMessageBox::Yes) return;
	QString error;
	const QString changed = m_service->connectClaudeDesktop(&error);
	if (changed.isEmpty()) {
		QMessageBox::warning(this, tr("Connect Claude Desktop"), error);
		return;
	}
	QMessageBox::information(this, tr("Connect Claude Desktop"),
	                         tr("Done. Restart Claude Desktop, switch on \"Let Claude use Mapsmith\" here, and ask Claude about your materials.%1")
	                             .arg(m_service->enabled() ? QString() : tr("\n\n(It is still off.)")));
}

} // namespace sme::app
