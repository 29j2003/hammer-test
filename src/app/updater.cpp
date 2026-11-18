// updater.cpp - Release check, download and install (see updater.h).

#include "updater.h"
#include "system_paths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include <sme/version.h>

namespace sme::app {

namespace {

constexpr int TIMEOUT_MS = 20000;
constexpr const char* RELEASE_FILES = "/29j2003/hammer-test/releases/download/";

// Waits for the editor to close, unpacks the new version over the old one and starts it.
constexpr const char* INSTALL_SCRIPT = R"PS(param([string]$Zip, [string]$Target, [int]$ProcessId, [string]$Exe)
$ErrorActionPreference = 'Stop'
try {
	Wait-Process -Id $ProcessId -Timeout 120 -ErrorAction SilentlyContinue
	$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('sme-update-' + [guid]::NewGuid())
	Expand-Archive -LiteralPath $Zip -DestinationPath $tmp -Force
	# Claude Desktop keeps Mapsmith's Claude bridge running: stop it (only the one from this folder), else its
	# .exe and the Qt DLLs it uses cannot be replaced. Claude Desktop starts it again.
	Get-Process -Name 'mapsmith-mcp' -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($Target, [System.StringComparison]::OrdinalIgnoreCase) } | Stop-Process -Force -ErrorAction SilentlyContinue
	Start-Sleep -Milliseconds 500
	Copy-Item -Path (Join-Path $tmp '*') -Destination $Target -Recurse -Force
	Remove-Item -LiteralPath $tmp -Recurse -Force
	Remove-Item -LiteralPath $Zip -Force
} catch {
	Add-Type -AssemblyName PresentationFramework
	[System.Windows.MessageBox]::Show("The update could not be installed: $($_.Exception.Message)`nDownload it from the release page instead.", 'Mapsmith') | Out-Null
}
Start-Process -FilePath $Exe
)PS";

/// Files of this project's releases only: https://github.com/<repo>/releases/download/... (GitHub
/// redirects from there to its file servers; the redirect may not leave https).
bool releaseFileUrl(const QUrl& url) {
	return url.scheme() == QLatin1String("https") && url.host().compare(QLatin1String("github.com"), Qt::CaseInsensitive) == 0 &&
	       url.path().startsWith(QLatin1String(RELEASE_FILES)) && !url.path().contains(QLatin1String(".."));
}

/// A plain file name for the download folder (no folder parts, nothing hidden).
bool plainFileName(const QString& name) {
	return !name.isEmpty() && !name.startsWith('.') && !name.contains('/') && !name.contains('\\') && !name.contains(':');
}

} // namespace

QString ReleaseInfo::label() const {
	return build > 0 ? QObject::tr("%1 (test build %2)").arg(version).arg(build) : version;
}

std::optional<ReleaseInfo> parseRelease(const QByteArray& json, bool testBuild) {
	const auto object = QJsonDocument::fromJson(json).object();
	QString tag = object.value("tag_name").toString();
	if (tag.isEmpty() || object.value("draft").toBool()) {
		return std::nullopt;
	}
	// A release is never a pre-release; the test build is only the pre-release "testing".
	if (testBuild ? (tag != QLatin1String("testing") || !object.value("prerelease").toBool()) : object.value("prerelease").toBool()) {
		return std::nullopt;
	}
	ReleaseInfo release;
	release.version = tag.startsWith('v', Qt::CaseInsensitive) ? tag.mid(1) : tag;
	release.notes = object.value("body").toString();
	// Opened in the browser: only a web page (a file:// link would start a program).
	const QUrl page(object.value("html_url").toString());
	if (page.scheme() == QLatin1String("https")) release.page = page;
	// A release has a checksum file per package (the installer's and the ZIP's, in any order):
	// the ZIP's own one, else a list of all (SHA256SUMS).
	QHash<QString, QUrl> checksums;
	QUrl sumsList;
	for (const auto& value : object.value("assets").toArray()) {
		const auto asset = value.toObject();
		const QString name = asset.value("name").toString();
		const QUrl url(asset.value("browser_download_url").toString());
		if (!plainFileName(name) || !releaseFileUrl(url)) continue;   // not one of this project's release files
		if (name.endsWith("-windows.zip", Qt::CaseInsensitive)) {
			const auto test = testPackage(name.toStdString());
			if (testBuild != test.has_value()) continue;   // a release takes no test package, and the other way round
			release.zipName = name;
			release.zipUrl = url;
			if (test) {
				release.version = QString::fromStdString(test->version);
				release.build = test->build;
			}
		} else if (name.endsWith(".sha256", Qt::CaseInsensitive)) {
			checksums.insert(name.toLower(), url);
		} else if (name.compare("SHA256SUMS", Qt::CaseInsensitive) == 0) {
			sumsList = url;
		}
	}
	release.checksumUrl = checksums.value((release.zipName + ".sha256").toLower(), sumsList);
	if (testBuild && release.build <= 0) {
		return std::nullopt;   // a test build without its package says nothing
	}
	return release;
}

bool newerThan(const ReleaseInfo& release, const QString& version, long build) {
	const int order = compareVersions(release.version.toStdString(), version.toStdString());
	if (release.build <= 0) {
		return order > 0;   // a release: only a higher version (a test build of it already has its code)
	}
	// A test build: not from an older version; newer than a release of the same version or an older test build.
	return order > 0 || (order == 0 && (build <= 0 || release.build > build));
}

long Updater::ownBuild() {
	return SME_BUILD_NUMBER;
}

QString Updater::ownLabel() {
	ReleaseInfo own;
	own.version = QCoreApplication::applicationVersion();
	own.build = ownBuild();
	return own.label();
}

bool Updater::testBuilds() {
	return QSettings().value("update/testBuilds", ownBuild() > 0).toBool();
}

void Updater::setTestBuilds(bool on) {
	QSettings().setValue("update/testBuilds", on);
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
			if (testBuilds()) {
				checkTestBuild();   // no release published yet
				return;
			}
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
		if (newerThan(*release, QCoreApplication::applicationVersion(), ownBuild())) {
			m_available = release;
			emit updateAvailable(*release);
		} else if (testBuilds()) {
			checkTestBuild();
		} else {
			m_available.reset();
			emit upToDate();
		}
	});
}

void Updater::checkTestBuild() {
	auto* reply = get(QUrl(QString::fromLatin1(TEST_BUILD)));
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		reply->deleteLater();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status == 404) {
			m_available.reset();
			emit upToDate();   // no test build right now (the CI replaces it for a moment on each push)
			return;
		}
		if (reply->error() != QNetworkReply::NoError) {
			emit checkFailed(reply->errorString());
			return;
		}
		const auto release = parseRelease(reply->readAll(), true);
		if (release && newerThan(*release, QCoreApplication::applicationVersion(), ownBuild())) {
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
	if (!QProcess::startDetached(systemProgram(QStringLiteral("powershell.exe")), arguments)) {
		if (error) *error = QObject::tr("PowerShell could not be started");
		return false;
	}
	return true;
}

} // namespace sme::app
