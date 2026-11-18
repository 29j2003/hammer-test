// updater.h - Updates from the GitHub releases of this project: checks the newest release,
// downloads the Windows zip, verifies its SHA-256 against the release's checksum file and
// installs it with a small PowerShell helper that waits for the editor to close, replaces
// the files and starts the new version.
//
// Test builds: every push the CI builds is also published as the pre-release "testing" (one
// package "Mapsmith-<version>-test.<run>-windows.zip" with its checksum, replaced on each push).
// With "Get test builds" on (the default in a test build itself, off in a release) the check also
// looks there and offers a newer test build. Releases stay what "releases/latest" gives: GitHub
// never counts a pre-release as the latest release.
//
// The editor's own network access: one or two requests to api.github.com per check (at start, can be
// turned off) and the download when the mapper says yes. Nothing is sent about the user or the
// maps. (The only other network use is the system's git, when the mapper presses Fetch, Pull
// or Push on the Source control tab.)

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace sme::app {

/// A release on GitHub: version, notes, and where its Windows package and checksum are.
struct ReleaseInfo {
	QString version;       ///< "0.3.0" (tag without "v")
	QString notes;         ///< Release text
	QUrl page;             ///< Release page in the browser
	QUrl zipUrl;           ///< Windows package
	QString zipName;
	QUrl checksumUrl;      ///< "<zip>.sha256" or a SHA256SUMS file
	long build = 0;        ///< Number of a test build, 0 for a release
	/// "0.3.0", or "0.2.0 (test build 133)".
	[[nodiscard]] QString label() const;
};

/// Reads the GitHub API answer for a release (nullopt if it has no version). With `testBuild` it reads
/// the pre-release "testing" instead: its version and build come from its package's name.
[[nodiscard]] std::optional<ReleaseInfo> parseRelease(const QByteArray& json, bool testBuild = false);
/// Whether `release` is newer than the editor with this version and build number (0 = a release).
[[nodiscard]] bool newerThan(const ReleaseInfo& release, const QString& version, long build);

/// Checks for, downloads and installs updates (see the file comment).
class Updater : public QObject {
	Q_OBJECT

public:
	static constexpr const char* LATEST_RELEASE = "https://api.github.com/repos/29j2003/hammer-test/releases/latest";
	static constexpr const char* TEST_BUILD = "https://api.github.com/repos/29j2003/hammer-test/releases/tags/testing";

	/// This editor's test build number (0: a release or a local build).
	[[nodiscard]] static long ownBuild();
	/// "0.2.0" or "0.2.0 (test build 133)".
	[[nodiscard]] static QString ownLabel();
	/// Whether the check also offers test builds (Help > Get test builds).
	[[nodiscard]] static bool testBuilds();
	static void setTestBuilds(bool on);

	explicit Updater(QObject* parent = nullptr);

	/// Asks GitHub for the newest release (and test build, if on); answers with updateAvailable,
	/// upToDate or checkFailed.
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
	/// Answers to check() and download().
	void updateAvailable(const sme::app::ReleaseInfo& release);
	void upToDate();
	void checkFailed(const QString& error);
	void downloadProgress(qint64 received, qint64 total);
	void downloaded(const QString& zipPath);
	void downloadFailed(const QString& error);

private:
	QNetworkReply* get(const QUrl& url);
	/// Second step of check(): the test build, if it is newer.
	void checkTestBuild();

	QNetworkAccessManager* m_network;
	std::optional<ReleaseInfo> m_available;
	QNetworkReply* m_download = nullptr;
};

} // namespace sme::app
