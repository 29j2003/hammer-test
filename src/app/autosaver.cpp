// autosaver.cpp - Autosaves and crash recovery (see autosaver.h).

#include "autosaver.h"

#include "editor.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <sme/autosave.h>

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
}

void Autosaver::endSession() {
	m_sessionOpen = false;
	QFile::remove(markerPath());
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

bool offerRecovery(Editor* editor, QWidget* parent, const RecoveryOffer& offer) {
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
	auto* buttons = new QDialogButtonBox;
	auto* restore = buttons->addButton(QObject::tr("Restore selected"), QDialogButtonBox::AcceptRole);
	restore->setProperty("accent", true);
	buttons->addButton(offer.mapPath.isEmpty() || offer.afterCrash ? QObject::tr("Ignore") : QObject::tr("Open the file as it is"), QDialogButtonBox::RejectRole);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
	layout->addWidget(buttons);
	dialog.resize(520, 320);
	if (dialog.exec() != QDialog::Accepted || !list->currentItem()) {
		return false;
	}
	QString error;
	if (!editor->openRecovered(list->currentItem()->data(Qt::UserRole).toString(), offer.mapPath, &error)) {
		QMessageBox::warning(parent, QObject::tr("Restore an autosave"), QObject::tr("Could not open the autosave:\n%1").arg(error));
		return false;
	}
	return true;
}

bool openMapWithRecovery(Editor* editor, QWidget* parent, const QString& path, QString* error) {
	if (const auto offer = recoveryForMap(path); offer && offerRecovery(editor, parent, *offer)) {
		return true;
	}
	return editor->openMap(path, error);
}

} // namespace sme::app
