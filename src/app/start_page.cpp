// start_page.cpp - The page Mapsmith opens with (see start_page.h).

#include "start_page.h"

#include <QCheckBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace sme::app {

namespace {

constexpr QSize PICTURE(320, 180);

/// "sourcemods/mymod" -> "mymod", "Half-Life 2/hl2" -> "hl2": the part one recognises.
QString gameName(const std::string& key) {
	const QString text = QString::fromStdString(key);
	return text.section('/', -1);
}

QString when(std::int64_t time) {
	const QDateTime moment = QDateTime::fromSecsSinceEpoch(time);
	const qint64 days = moment.daysTo(QDateTime::currentDateTime());
	if (days <= 0) return QObject::tr("today, %1").arg(moment.time().toString(QStringLiteral("HH:mm")));
	if (days == 1) return QObject::tr("yesterday");
	return QLocale().toString(moment.date(), QLocale::ShortFormat);
}

QPixmap projectPicture(const QString& mapPath, const QSize& size) {
	QImage image(projectPicturePath(mapPath));
	if (image.isNull()) {
		// No picture yet: a calm placeholder with the map's first letter.
		image = QImage(size, QImage::Format_RGB32);
		image.fill(QColor(40, 42, 46));
	}
	return QPixmap::fromImage(image.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation).copy(QRect(QPoint(0, 0), size)));
}

QFrame* card(const QString& name) {
	auto* frame = new QFrame;
	frame->setObjectName(name);
	frame->setProperty("role", "card");
	return frame;
}

} // namespace

std::vector<RecentProject> loadRecentProjects() {
	auto list = parseRecentProjects(QSettings().value(RECENT_PROJECTS_KEY).toString().toStdString());
	std::erase_if(list, [](const RecentProject& p) { return !QFileInfo::exists(QString::fromStdString(p.mapPath)); });
	return list;
}

QString projectPicturePath(const QString& mapPath) {
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/pictures");
	const QByteArray hash = QCryptographicHash::hash(QDir::cleanPath(mapPath).toLower().toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
	return dir + QStringLiteral("/") + QString::fromLatin1(hash) + QStringLiteral(".jpg");
}

void rememberRecentProject(const QString& gameKey, const QString& mapPath, const QImage& picture) {
	if (mapPath.isEmpty()) return;
	auto list = parseRecentProjects(QSettings().value(RECENT_PROJECTS_KEY).toString().toStdString());
	list = rememberProject(std::move(list), {gameKey.toStdString(), QDir::cleanPath(mapPath).toStdString(), QDateTime::currentSecsSinceEpoch()});
	QSettings().setValue(RECENT_PROJECTS_KEY, QString::fromStdString(writeRecentProjects(list)));
	if (!picture.isNull()) {
		const QString path = projectPicturePath(mapPath);
		QDir().mkpath(QFileInfo(path).absolutePath());
		picture.scaled(PICTURE * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation).save(path, "JPG", 85);
	}
}

bool StartPage::showAtStartup() {
	return QSettings().value(QStringLiteral("start/show"), true).toBool();
}

StartPage::StartPage(QWidget* parent) : QWidget(parent) {
	setObjectName("startPage");
	auto* outer = new QHBoxLayout(this);
	outer->addStretch(1);
	auto* column = new QVBoxLayout;
	column->setSpacing(14);
	outer->addLayout(column, 6);
	outer->addStretch(1);
	column->addStretch(1);

	auto* title = new QLabel(tr("Mapsmith"));
	QFont big = title->font();
	big.setPointSizeF(big.pointSizeF() * 2.2);
	big.setBold(true);
	title->setFont(big);
	column->addWidget(title);
	auto* subtitle = new QLabel(tr("Pick up where you left off, or start something new."));
	subtitle->setProperty("role", "description");
	column->addWidget(subtitle);

	auto* row = new QHBoxLayout;
	row->setSpacing(14);
	column->addLayout(row);

	// Continue: the last project, big.
	m_continueCard = card("startContinueCard");
	auto* cv = new QVBoxLayout(m_continueCard);
	cv->setContentsMargins(14, 14, 14, 14);
	m_continuePicture = new QLabel;
	m_continuePicture->setFixedSize(PICTURE);
	m_continuePicture->setScaledContents(true);
	cv->addWidget(m_continuePicture);
	m_continueTitle = new QLabel;
	QFont bold = m_continueTitle->font();
	bold.setPointSizeF(bold.pointSizeF() * 1.25);
	bold.setBold(true);
	m_continueTitle->setFont(bold);
	cv->addWidget(m_continueTitle);
	m_continueDetail = new QLabel;
	m_continueDetail->setProperty("role", "description");
	cv->addWidget(m_continueDetail);
	m_continue = new QPushButton(tr("Continue"));
	m_continue->setObjectName("startContinue");
	m_continue->setProperty("accent", true);
	m_continue->setMinimumHeight(40);
	cv->addWidget(m_continue);
	row->addWidget(m_continueCard, 1);

	// Start new: the guided way to a Mapbase mod with a first map.
	auto* fresh = card("startNewCard");
	auto* nv = new QVBoxLayout(fresh);
	nv->setContentsMargins(14, 14, 14, 14);
	auto* freshTitle = new QLabel(tr("Start something new"));
	freshTitle->setFont(bold);
	nv->addWidget(freshTitle);
	auto* freshText = new QLabel(tr("A mod of your own, built on Half-Life 2 with Mapbase, with Mapsmith's graphics and sound features "
	                                "installed - and a first room to build from, open and ready to edit."));
	freshText->setWordWrap(true);
	freshText->setProperty("role", "description");
	nv->addWidget(freshText);
	nv->addStretch(1);
	auto* startNew = new QPushButton(tr("Start new project"));
	startNew->setObjectName("startNew");
	startNew->setProperty("accent", true);
	startNew->setMinimumHeight(40);
	nv->addWidget(startNew);
	auto* openMap = new QPushButton(tr("Open a map ..."));
	openMap->setObjectName("startOpenMap");
	nv->addWidget(openMap);
	row->addWidget(fresh, 1);

	// Recent projects.
	m_recentTitle = new QLabel(tr("Recent"));
	m_recentTitle->setProperty("role", "heading");
	column->addWidget(m_recentTitle);
	m_recent = new QListWidget;
	m_recent->setObjectName("startRecent");
	m_recent->setIconSize(QSize(96, 54));
	m_recent->setMinimumHeight(180);
	m_recent->setToolTip(tr("Double-click (or Enter) opens the map in its mod"));
	column->addWidget(m_recent, 1);

	auto* bottom = new QHBoxLayout;
	m_showAtStartup = new QCheckBox(tr("Show at startup"));
	m_showAtStartup->setObjectName("startShowAtStartup");
	m_showAtStartup->setChecked(showAtStartup());
	auto* skip = new QPushButton(tr("Go to the editor"));
	skip->setObjectName("startSkip");
	bottom->addWidget(m_showAtStartup);
	bottom->addStretch(1);
	bottom->addWidget(skip);
	column->addLayout(bottom);
	column->addStretch(1);

	connect(m_continue, &QPushButton::clicked, this, [this] {
		if (!m_projects.empty()) emit openProject(QString::fromStdString(m_projects.front().gameKey), QString::fromStdString(m_projects.front().mapPath));
	});
	const auto openItem = [this](QListWidgetItem* item) {
		if (item) emit openProject(item->data(Qt::UserRole + 1).toString(), item->data(Qt::UserRole).toString());
	};
	connect(m_recent, &QListWidget::itemActivated, this, openItem);
	connect(startNew, &QPushButton::clicked, this, &StartPage::newProjectRequested);
	connect(openMap, &QPushButton::clicked, this, &StartPage::openMapRequested);
	connect(skip, &QPushButton::clicked, this, &StartPage::closeRequested);
	connect(m_showAtStartup, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("start/show"), on); });
	refresh();
}

void StartPage::refresh() {
	m_projects = loadRecentProjects();
	m_showAtStartup->setChecked(showAtStartup());
	m_continueCard->setVisible(!m_projects.empty());
	if (!m_projects.empty()) {
		const auto& last = m_projects.front();
		const QString map = QString::fromStdString(last.mapPath);
		m_continuePicture->setPixmap(projectPicture(map, PICTURE));
		m_continueTitle->setText(QFileInfo(map).completeBaseName());
		m_continueDetail->setText(tr("%1  ·  %2").arg(gameName(last.gameKey), when(last.time)));
		m_continue->setToolTip(QDir::toNativeSeparators(map));
	}
	m_recent->clear();
	for (std::size_t i = 1; i < m_projects.size(); ++i) {
		const auto& project = m_projects[i];
		const QString map = QString::fromStdString(project.mapPath);
		auto* item = new QListWidgetItem(QIcon(projectPicture(map, QSize(96, 54))),
		                                 tr("%1   -   %2   -   %3").arg(QFileInfo(map).completeBaseName(), gameName(project.gameKey), when(project.time)),
		                                 m_recent);
		item->setData(Qt::UserRole, map);
		item->setData(Qt::UserRole + 1, QString::fromStdString(project.gameKey));
		item->setToolTip(QDir::toNativeSeparators(map));
	}
	m_recentTitle->setVisible(m_recent->count() > 0);
	m_recent->setVisible(m_recent->count() > 0);
}

} // namespace sme::app
