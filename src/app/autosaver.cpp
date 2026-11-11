// autosaver.cpp - Autosaves and crash recovery (see autosaver.h).

#include "autosaver.h"

#include "crash_handler.h"

#include "editor.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QShortcutEvent>
#include <QStandardPaths>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>

#include <sme/autosave.h>
#include <sme/crash_log.h>

#include <memory>

namespace sme::app {

namespace {

QString appDataFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

AutosaveStamp stampOf(const QDateTime& time) {
	const QDate d = time.date();
	const QTime t = time.time();
	return {d.year(), d.month(), d.day(), t.hour(), t.minute(), t.second()};
}

QDateTime timeOf(const AutosaveStamp& stamp) {
	return QDateTime(QDate(stamp.year, stamp.month, stamp.day), QTime(stamp.hour, stamp.minute, stamp.second));
}

/// The autosaves of `mapPath` newer than the file; nullopt if there are none.
std::optional<RecoveryOffer> newerAutosaves(const QString& mapPath) {
	const auto list = listAutosaves(toPath(Autosaver::folderFor(mapPath)), Autosaver::mapNameFor(mapPath).toStdString());
	if (list.empty()) {
		return std::nullopt;
	}
	const QFileInfo file(mapPath);
	// A second of slack: the file system and the name both round to seconds.
	const QDateTime fileTime = !mapPath.isEmpty() && file.exists() ? file.lastModified().addSecs(1) : QDateTime();
	RecoveryOffer offer;
	offer.mapPath = mapPath;
	for (const auto& entry : list) {
		const QDateTime time = timeOf(entry.stamp);
		if (fileTime.isValid() && time <= fileTime) continue;
		offer.autosaves << fromPath(entry.path);
		offer.times << time;
	}
	if (offer.autosaves.isEmpty()) {
		return std::nullopt;
	}
	return offer;
}

} // namespace

Autosaver::Autosaver(Editor* editor, QObject* parent) : QObject(parent), m_editor(editor) {
	m_pool.setMaxThreadCount(1);
	m_savedRevision = editor->history().revision();
	connect(&m_timer, &QTimer::timeout, this, [this] { autosaveNow(); });
	setIntervalMinutes(intervalMinutes());
	// A new or opened map starts fresh; the marker follows the map's path.
	connect(editor, &Editor::documentReplaced, this, [this] { m_savedRevision = m_editor->history().revision(); });
	connect(editor, &Editor::documentChanged, this, [this] {
		if (m_sessionOpen && m_editor->filePath() != m_markerMap) updateMarker();
	});
	// The journal for a report after a crash: written half a second after it changed.
	m_journalWritten = editor->journalRevision();
	connect(&m_journalTimer, &QTimer::timeout, this, &Autosaver::writeJournal);
	m_journalTimer.start(500);
	// Edits and clicks go to the actions file at once (a crash right after them keeps them).
	connect(editor, &Editor::journalLine, this, [this](const QString& line) { noteAction(line); });
	QCoreApplication::instance()->installEventFilter(this);
	connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
		waitForWrites();
		endSession();
	});
	startSession();
}

Autosaver::~Autosaver() {
	waitForWrites();
}

int Autosaver::intervalMinutes() {
	return std::max(0, QSettings().value("autosave/minutes", 5).toInt());
}

void Autosaver::setIntervalMinutes(int minutes) {
	minutes = std::max(0, minutes);
	QSettings().setValue("autosave/minutes", minutes);
	if (minutes == 0) {
		m_timer.stop();
	} else {
		m_timer.start(minutes * 60 * 1000);
	}
}

int Autosaver::keepCount() {
	return std::max(1, QSettings().value("autosave/keep", 10).toInt());
}

QString Autosaver::folderFor(const QString& mapPath) {
	if (mapPath.isEmpty()) {
		return QDir(appDataFolder()).filePath("autosave");
	}
	return fromPath(autosaveFolder(toPath(mapPath)));
}

QString Autosaver::mapNameFor(const QString& mapPath) {
	return mapPath.isEmpty() ? QStringLiteral("untitled") : QFileInfo(mapPath).completeBaseName();
}

QString Autosaver::markerPath() {
	return QDir(appDataFolder()).filePath("session-open.txt");
}

QString Autosaver::journalPath() {
	return QDir(appDataFolder()).filePath("session-journal.txt");
}

QString Autosaver::actionsPath() {
	return QDir(appDataFolder()).filePath("session-actions.txt");
}

void Autosaver::noteAction(const QString& line) {
	if (!m_sessionOpen) return;
	if (!m_actions.isOpen() || m_actionLines >= 4000) {
		// Fresh at session start; when long, start over (the last lines are what a crash needs).
		const bool restart = m_actionLines > 0;
		m_actions.close();
		m_actions.setFileName(actionsPath());
		QDir().mkpath(QFileInfo(actionsPath()).absolutePath());
		if (!m_actions.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
		m_actionLines = 0;
		if (restart) m_actions.write("(earlier actions dropped)\n");
	}
	QString text = line;
	text.replace('\n', QStringLiteral(" | "));
	const QByteArray bytes = (QTime::currentTime().toString("HH:mm:ss.zzz") + ' ' + text + '\n').toUtf8();
	m_actions.write(bytes);
	m_actions.flush();   // into the OS now: it survives the process dying
	++m_actionLines;
}

bool Autosaver::eventFilter(QObject* watched, QEvent* event) {
	if (!m_sessionOpen) return false;
	const auto plain = [](QString text) { return text.remove('&').simplified(); };
	switch (event->type()) {
		case QEvent::MouseButtonRelease: {
			const auto* mouse = static_cast<QMouseEvent*>(event);
			if (mouse->button() != Qt::LeftButton && mouse->button() != Qt::RightButton) break;
			if (auto* button = qobject_cast<QAbstractButton*>(watched)) {
				if (!button->isEnabled() || !button->rect().contains(mouse->position().toPoint())) break;
				QString what = plain(button->text());
				if (what.isEmpty()) what = plain(button->toolTip()).left(60);
				QString part;
				if (auto* tool = qobject_cast<QToolButton*>(button); tool && tool->popupMode() == QToolButton::MenuButtonPopup) {
					// The small arrow at the right opens the menu, the rest runs the action.
					part = mouse->position().x() > tool->width() - 16 ? QStringLiteral(" (menu arrow)") : QStringLiteral(" (button)");
				}
				const QWidget* window = button->window();
				noteAction(QStringLiteral("click \"%1\"%2 [%3] in %4")
				               .arg(what, part, button->objectName(), window ? plain(window->windowTitle()).left(60) : QString()));
			} else if (auto* menu = qobject_cast<QMenu*>(watched)) {
				if (const QAction* action = menu->actionAt(mouse->position().toPoint())) {
					noteAction(QStringLiteral("menu \"%1\" > \"%2\"").arg(plain(menu->title()), plain(action->text())));
				}
			}
			break;
		}
		case QEvent::Shortcut:
			if (const auto* action = qobject_cast<QAction*>(watched)) {
				noteAction(QStringLiteral("shortcut %1 \"%2\"").arg(static_cast<QShortcutEvent*>(event)->key().toString(), plain(action->text())));
			}
			break;
		case QEvent::Show:
			if (const auto* dialog = qobject_cast<QDialog*>(watched)) {
				noteAction(QStringLiteral("dialog \"%1\" opened [%2]").arg(plain(dialog->windowTitle()), dialog->objectName()));
			}
			break;
		case QEvent::Hide:
			if (const auto* dialog = qobject_cast<QDialog*>(watched)) {
				noteAction(QStringLiteral("dialog \"%1\" closed (result %2)").arg(plain(dialog->windowTitle())).arg(dialog->result()));
			}
			break;
		default:
			break;
	}
	return false;
}

QString Autosaver::crashJournalPath() {
	return QDir(appDataFolder()).filePath("last-crash-journal.txt");
}

void Autosaver::writeJournal() {
	const unsigned revision = m_editor->journalRevision();
	if (!m_sessionOpen || revision == m_journalWritten) return;
	m_journalWritten = revision;
	auto text = std::make_shared<const std::string>("== Clicks and drags ==\n" + m_editor->pickJournal() + "\n== Edits ==\n" + m_editor->editJournal() +
	                                                "\n== Road and texture flow plans ==\n" + m_editor->planJournal());
	const QString path = journalPath();
	m_pool.start([text, path] {
		QDir().mkpath(QFileInfo(path).absolutePath());
		QSaveFile out(path);
		if (out.open(QIODevice::WriteOnly) && out.write(text->data(), static_cast<qint64>(text->size())) == static_cast<qint64>(text->size())) {
			out.commit();
		}
	});
}

QString Autosaver::autosaveNow(bool force) {
	const unsigned revision = m_editor->history().revision();
	if (!force && (!m_editor->modified() || revision == m_savedRevision)) {
		return {};
	}
	m_savedRevision = revision;
	const QString mapPath = m_editor->filePath();
	const QString folder = folderFor(mapPath);
	const QString mapName = mapNameFor(mapPath);
	const QString file = QDir(folder).filePath(QString::fromStdString(autosaveFileName(mapName.toStdString(), stampOf(QDateTime::currentDateTime()))));
	// Text now (the document may change right after), disk later.
	auto text = std::make_shared<const std::string>(m_editor->doc().serialize());
	const int keep = keepCount();
	m_pool.start([text, folder, file, mapName, keep] {
		QDir().mkpath(folder);
		QSaveFile out(file);
		if (!out.open(QIODevice::WriteOnly) || out.write(text->data(), static_cast<qint64>(text->size())) != static_cast<qint64>(text->size()) ||
		    !out.commit()) {
			return;
		}
		for (const auto& old : autosavesToRemove(listAutosaves(toPath(folder), mapName.toStdString()), static_cast<std::size_t>(keep))) {
			std::error_code ec;
			std::filesystem::remove(old, ec);
		}
	});
	emit m_editor->message(tr("Autosaved to %1").arg(QDir::toNativeSeparators(file)));
	return file;
}

void Autosaver::waitForWrites() {
	m_pool.waitForDone();
}

void Autosaver::startSession() {
	m_sessionOpen = true;
	updateMarker();
	m_actionLines = 0;
	m_actions.close();
	noteAction(QStringLiteral("session started"));
}

void Autosaver::endSession() {
	m_sessionOpen = false;
	m_actions.close();
	QFile::remove(markerPath());
	QFile::remove(journalPath());
	QFile::remove(actionsPath());
}

void Autosaver::updateMarker() {
	m_markerMap = m_editor->filePath();
	QDir().mkpath(QFileInfo(markerPath()).absolutePath());
	QSaveFile marker(markerPath());
	if (marker.open(QIODevice::WriteOnly)) {
		marker.write(m_markerMap.toUtf8());
		marker.commit();
	}
}

bool lastSessionCrashed() {
	return QFileInfo::exists(Autosaver::markerPath());
}

namespace {
QString g_lastCrashSummary;   ///< crashSummary() of the crash file read at start
} // namespace

QString lastCrashSummary() {
	return g_lastCrashSummary;
}

QString takeCrashJournal() {
	// What the crash handler wrote (where it crashed), the last actions (what led there), then the
	// journal (picks, edits, plans in detail).
	QString text;
	if (const QString crash = takeCrashFile(); !crash.isEmpty()) {
		text += QStringLiteral("== Crash ==\n") + crash + '\n';
		g_lastCrashSummary = QString::fromStdString(crashSummary(crash.toStdString()));
	}
	QFile actions(Autosaver::actionsPath());
	if (actions.open(QIODevice::ReadOnly)) {
		const QStringList lines = QString::fromUtf8(actions.readAll()).split('\n', Qt::SkipEmptyParts);
		actions.close();
		text += QStringLiteral("== Last actions ==\n") + lines.mid(std::max<qsizetype>(0, lines.size() - 200)).join('\n') + QStringLiteral("\n\n");
		QFile::remove(Autosaver::actionsPath());
	}
	QFile journal(Autosaver::journalPath());
	if (journal.open(QIODevice::ReadOnly)) {
		text += QString::fromUtf8(journal.readAll());
		journal.close();
	}
	QFile::remove(Autosaver::journalPath());
	if (text.isEmpty()) return {};
	QFile kept(Autosaver::crashJournalPath());
	if (kept.open(QIODevice::WriteOnly | QIODevice::Truncate)) kept.write(text.toUtf8());
	return text;
}

std::optional<RecoveryOffer> recoveryAfterCrash() {
	QFile marker(Autosaver::markerPath());
	if (!marker.open(QIODevice::ReadOnly)) {
		return std::nullopt;   // the last session ended normally (or there was none)
	}
	const QString mapPath = QString::fromUtf8(marker.readAll()).trimmed();
	auto offer = newerAutosaves(mapPath);
	if (offer) offer->afterCrash = true;
	return offer;
}

std::optional<RecoveryOffer> recoveryForMap(const QString& mapPath) {
	if (mapPath.isEmpty()) {
		return std::nullopt;
	}
	return newerAutosaves(mapPath);
}

bool offerRecovery(Editor* editor, QWidget* parent, const RecoveryOffer& offer, const std::function<void()>& makeReport) {
	QDialog dialog(parent);
	dialog.setWindowTitle(QObject::tr("Restore an autosave?"));
	auto* layout = new QVBoxLayout(&dialog);
	const QString map = offer.mapPath.isEmpty() ? QObject::tr("a map that was never saved") : QFileInfo(offer.mapPath).fileName();
	auto* text = new QLabel(offer.afterCrash
	                            ? QObject::tr("The editor did not close normally last time. There are autosaves of %1 that are newer than the "
	                                          "file. Restore one?")
	                                  .arg(map)
	                            : QObject::tr("There are autosaves of %1 that are newer than the file. Restore one instead of the file?").arg(map));
	text->setWordWrap(true);
	layout->addWidget(text);
	if (offer.afterCrash && !lastCrashSummary().isEmpty()) {
		auto* what = new QLabel(QObject::tr("What happened: %1").arg(lastCrashSummary()));
		what->setObjectName("crashSummary");
		what->setWordWrap(true);
		what->setTextInteractionFlags(Qt::TextSelectableByMouse);
		layout->addWidget(what);
	}
	auto* list = new QListWidget;
	list->setObjectName("autosaveList");
	for (int i = 0; i < offer.autosaves.size(); ++i) {
		auto* item = new QListWidgetItem(QObject::tr("%1  (%2)").arg(QLocale().toString(offer.times[i], QLocale::ShortFormat), QFileInfo(offer.autosaves[i]).fileName()));
		item->setData(Qt::UserRole, offer.autosaves[i]);
		item->setToolTip(QDir::toNativeSeparators(offer.autosaves[i]));
		list->addItem(item);
	}
	list->setCurrentRow(0);
	layout->addWidget(list);
	QCheckBox* report = nullptr;
	if (offer.afterCrash && makeReport) {
		report = new QCheckBox(QObject::tr("Also make an editor report about the crash (a file to send: your last clicks and edits before it)"));
		report->setObjectName("crashReport");
		layout->addWidget(report);
	}
	auto* buttons = new QDialogButtonBox;
	auto* restore = buttons->addButton(QObject::tr("Restore selected"), QDialogButtonBox::AcceptRole);
	restore->setProperty("accent", true);
	buttons->addButton(offer.mapPath.isEmpty() || offer.afterCrash ? QObject::tr("Ignore") : QObject::tr("Open the file as it is"), QDialogButtonBox::RejectRole);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
	layout->addWidget(buttons);
	dialog.resize(520, 320);
	const bool accepted = dialog.exec() == QDialog::Accepted && list->currentItem();
	const bool wantsReport = report && report->isChecked();
	bool restored = false;
	if (accepted) {
		QString error;
		restored = editor->openRecovered(list->currentItem()->data(Qt::UserRole).toString(), offer.mapPath, &error);
		if (!restored) QMessageBox::warning(parent, QObject::tr("Restore an autosave"), QObject::tr("Could not open the autosave:\n%1").arg(error));
	}
	if (wantsReport) makeReport();
	return restored;
}

void offerCrashReport(QWidget* parent, const std::function<void()>& makeReport) {
	QMessageBox box(parent);
	box.setObjectName("crashReportQuestion");
	box.setIcon(QMessageBox::Question);
	box.setWindowTitle(QObject::tr("Editor report"));
	box.setText(lastCrashSummary().isEmpty() ? QObject::tr("Mapsmith did not close normally last time.")
	                                          : QObject::tr("Mapsmith did not close normally last time.\n\nWhat happened: %1").arg(lastCrashSummary()));
	box.setInformativeText(QObject::tr("Make an editor report about it? It is a file to send that holds your last clicks and edits before the crash. "
	                                   "Nothing is sent anywhere."));
	auto* yes = box.addButton(QObject::tr("Make editor report ..."), QMessageBox::AcceptRole);
	box.addButton(QObject::tr("Not now"), QMessageBox::RejectRole);
	box.exec();
	if (box.clickedButton() == yes) makeReport();
}

bool openMapWithRecovery(Editor* editor, QWidget* parent, const QString& path, QString* error) {
	if (const auto offer = recoveryForMap(path); offer && offerRecovery(editor, parent, *offer)) {
		return true;
	}
	return editor->openMap(path, error);
}

} // namespace sme::app
