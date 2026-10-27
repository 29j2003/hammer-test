// updater.h - Updates from the GitHub releases of this project: checks the newest release,
// downloads the Windows zip, verifies its SHA-256 against the release's checksum file and
// installs it with a small PowerShell helper that waits for the editor to close, replaces
// the files and starts the new version.
//
// The only network access of the editor: one request to api.github.com per check (at
// start, can be turned off) and the download when the mapper says yes. Nothing is sent
// about the user or the maps.

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace sme::app {

struct ReleaseInfo {
	QString version;       ///< "0.3.0" (tag without "v")
	QString notes;         ///< Release text
	QUrl page;             ///< Release page in the browser
	QUrl zipUrl;           ///< Windows package
	QString zipName;
	QUrl checksumUrl;      ///< "<zip>.sha256" or a SHA256SUMS file
};

/// Reads the GitHub API answer for a release (nullopt if it has no version).
[[nodiscard]] std::optional<ReleaseInfo> parseRelease(const QByteArray& json);

class Updater : public QObject {
	Q_OBJECT

public:
	static constexpr const char* LATEST_RELEASE = "https://api.github.com/repos/29j2003/hammer-test/releases/latest";

	explicit Updater(QObject* parent = nullptr);

	/// Asks GitHub for the newest release; answers with updateAvailable, upToDate or checkFailed.
	void check();
	/// Downloads and verifies the package; answers with downloaded or downloadFailed.
	void download(const ReleaseInfo& release);
	void cancelDownload();
	/// The newest release found by the last check, if it is newer than this editor.
	[[nodiscard]] const std::optional<ReleaseInfo>& available() const { return m_available; }

	/// Installing in place works on Windows when the editor's folder is writable.
	[[nodiscard]] static bool canInstall(QString* reason = nullptr);
	/// Starts the helper that replaces the files once the editor has closed. The caller quits.
	[[nodiscard]] static bool install(const QString& zipPath, QString* error = nullptr);

signals:
	void updateAvailable(const sme::app::ReleaseInfo& release);
	void upToDate();
	void checkFailed(const QString& error);
	void downloadProgress(qint64 received, qint64 total);
	void downloaded(const QString& zipPath);
	void downloadFailed(const QString& error);

private:
	QNetworkReply* get(const QUrl& url);

	QNetworkAccessManager* m_network;
	std::optional<ReleaseInfo> m_available;
	QNetworkReply* m_download = nullptr;
};

} // namespace sme::app
