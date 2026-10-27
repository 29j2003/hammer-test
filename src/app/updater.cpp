// updater.cpp - Release check, download and install (see updater.h).

#include "updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>

#include <sme/version.h>

namespace sme::app {

namespace {

constexpr int TIMEOUT_MS = 20000;

// Waits for the editor to close, unpacks the new version over the old one and starts it.
constexpr const char* INSTALL_SCRIPT = R"PS(param([string]$Zip, [string]$Target, [int]$ProcessId, [string]$Exe)
$ErrorActionPreference = 'Stop'
try {
	Wait-Process -Id $ProcessId -Timeout 120 -ErrorAction SilentlyContinue
	$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('sme-update-' + [guid]::NewGuid())
	Expand-Archive -LiteralPath $Zip -DestinationPath $tmp -Force
	Copy-Item -Path (Join-Path $tmp '*') -Destination $Target -Recurse -Force
	Remove-Item -LiteralPath $tmp -Recurse -Force
	Remove-Item -LiteralPath $Zip -Force
} catch {
	Add-Type -AssemblyName PresentationFramework
	[System.Windows.MessageBox]::Show("The update could not be installed: $($_.Exception.Message)`nDownload it from the release page instead.", 'Mapsmith') | Out-Null
}
Start-Process -FilePath $Exe
)PS";

} // namespace

std::optional<ReleaseInfo> parseRelease(const QByteArray& json) {
	const auto object = QJsonDocument::fromJson(json).object();
	QString tag = object.value("tag_name").toString();
	if (tag.isEmpty() || object.value("draft").toBool() || object.value("prerelease").toBool()) {
		return std::nullopt;
	}
	ReleaseInfo release;
	release.version = tag.startsWith('v', Qt::CaseInsensitive) ? tag.mid(1) : tag;
	release.notes = object.value("body").toString();
	release.page = QUrl(object.value("html_url").toString());
	for (const auto& value : object.value("assets").toArray()) {
		const auto asset = value.toObject();
		const QString name = asset.value("name").toString();
		const QUrl url(asset.value("browser_download_url").toString());
		if (name.endsWith("-windows.zip", Qt::CaseInsensitive)) {
			release.zipName = name;
			release.zipUrl = url;
		} else if (name.endsWith(".sha256", Qt::CaseInsensitive) || name.compare("SHA256SUMS", Qt::CaseInsensitive) == 0) {
			release.checksumUrl = url;
		}
	}
	return release;
}

Updater::Updater(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {
	QNetworkProxyFactory::setUseSystemConfiguration(true);   // works behind the proxy the system uses
}

QNetworkReply* Updater::get(const QUrl& url) {
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mapsmith/%1").arg(QCoreApplication::applicationVersion()));
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setTransferTimeout(TIMEOUT_MS);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	return m_network->get(request);
}

void Updater::check() {
	auto* reply = get(QUrl(QString::fromLatin1(LATEST_RELEASE)));
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		reply->deleteLater();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status == 404) {
			m_available.reset();
			emit upToDate();   // no release published yet
			return;
		}
		if (reply->error() != QNetworkReply::NoError) {
			emit checkFailed(reply->errorString());
			return;
		}
		const auto release = parseRelease(reply->readAll());
		if (!release) {
			emit checkFailed(tr("The release information could not be read"));
			return;
		}
		if (compareVersions(release->version.toStdString(), QCoreApplication::applicationVersion().toStdString()) > 0) {
			m_available = release;
			emit updateAvailable(*release);
		} else {
			m_available.reset();
			emit upToDate();
		}
	});
}

void Updater::download(const ReleaseInfo& release) {
	if (release.zipUrl.isEmpty() || release.checksumUrl.isEmpty()) {
		emit downloadFailed(tr("This release has no Windows package with a checksum - download it from the release page"));
		return;
	}
	// The checksum first (small), then the package.
	auto* sums = get(release.checksumUrl);
	connect(sums, &QNetworkReply::finished, this, [this, sums, release] {
		sums->deleteLater();
		if (sums->error() != QNetworkReply::NoError) {
			emit downloadFailed(sums->errorString());
			return;
		}
		const auto expected = checksumFor(sums->readAll().toStdString(), release.zipName.toStdString());
		if (!expected) {
			emit downloadFailed(tr("The checksum file does not list %1").arg(release.zipName));
			return;
		}
		const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/Mapsmith-update";
		QDir().mkpath(dir);
		auto file = std::make_shared<QFile>(dir + "/" + release.zipName);
		if (!file->open(QIODevice::WriteOnly)) {
			emit downloadFailed(file->errorString());
			return;
		}
		auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
		m_download = get(release.zipUrl);
		connect(m_download, &QNetworkReply::readyRead, this, [this, file, hash] {
			const QByteArray chunk = m_download->readAll();
			hash->addData(chunk);
			file->write(chunk);
		});
		connect(m_download, &QNetworkReply::downloadProgress, this, &Updater::downloadProgress);
		connect(m_download, &QNetworkReply::finished, this, [this, file, hash, expected] {
			auto* reply = m_download;
			m_download = nullptr;
			reply->deleteLater();
			const QByteArray rest = reply->readAll();
			hash->addData(rest);
			file->write(rest);
			file->close();
			if (reply->error() != QNetworkReply::NoError) {
				file->remove();
				emit downloadFailed(reply->error() == QNetworkReply::OperationCanceledError ? tr("Cancelled") : reply->errorString());
				return;
			}
			if (QString::fromLatin1(hash->result().toHex()) != QString::fromStdString(*expected)) {
				file->remove();
				emit downloadFailed(tr("The download is damaged (checksum does not match) - nothing was changed"));
				return;
			}
			emit downloaded(file->fileName());
		});
	});
}

void Updater::cancelDownload() {
	if (m_download) {
		m_download->abort();
	}
}

bool Updater::canInstall(QString* reason) {
#ifdef _WIN32
	const QFileInfo dir(QCoreApplication::applicationDirPath());
	QFile probe(dir.absoluteFilePath() + "/.update-test");
	if (!probe.open(QIODevice::WriteOnly)) {
		if (reason) *reason = QObject::tr("The editor's folder (%1) is not writable").arg(dir.absoluteFilePath());
		return false;
	}
	probe.close();
	probe.remove();
	return true;
#else
	if (reason) *reason = QObject::tr("Automatic installing works on Windows; on this system build the new version or download it");
	return false;
#endif
}

bool Updater::install(const QString& zipPath, QString* error) {
	const QString script = QFileInfo(zipPath).absolutePath() + "/install-update.ps1";
	QFile file(script);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(INSTALL_SCRIPT) < 0) {
		if (error) *error = file.errorString();
		return false;
	}
	file.close();
	const QStringList arguments{"-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", QDir::toNativeSeparators(script),
	                            "-Zip", QDir::toNativeSeparators(zipPath), "-Target", QDir::toNativeSeparators(QCoreApplication::applicationDirPath()),
	                            "-ProcessId", QString::number(QCoreApplication::applicationPid()),
	                            "-Exe", QDir::toNativeSeparators(QCoreApplication::applicationFilePath())};
	if (!QProcess::startDetached(QStringLiteral("powershell"), arguments)) {
		if (error) *error = QObject::tr("PowerShell could not be started");
		return false;
	}
	return true;
}

} // namespace sme::app
