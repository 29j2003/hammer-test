// source_control_panel.cpp - Source control panel (see source_control_panel.h).

#include "source_control_panel.h"

#include "editor.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <sme/game.h>
#include <sme/git_status.h>

namespace sme::app {

namespace {

enum Page { RepositoryPage = 0, MessagePage = 1 };

/// Section with a small upper-case heading.
QGroupBox* card(const QString& title) {
	auto* box = new QGroupBox(title.toUpper());
	box->setProperty("role", "section");
	return box;
}

QPushButton* button(const QString& text, const QString& name, const QString& tip) {
	auto* b = new QPushButton(text);
	b->setObjectName(name);
	b->setToolTip(tip);
	return b;
}

QString settingsKey(const QString& gameKey) {
	return QStringLiteral("games/%1/repository").arg(QString(gameKey).replace('/', '_'));
}

QColor stateColor(char letter) {
	switch (letter) {
		case 'A': return QColor(92, 207, 122);
		case 'D': return QColor(235, 100, 90);
		case 'R': return QColor(110, 170, 240);
		case 'U': return QColor(255, 80, 80);
		case '?': return QColor(150, 150, 150);
		default: return QColor(240, 180, 70);
	}
}

QString stateName(char letter) {
	switch (letter) {
		case 'A': return SourceControlPanel::tr("added");
		case 'D': return SourceControlPanel::tr("deleted");
		case 'R': return SourceControlPanel::tr("renamed");
		case 'U': return SourceControlPanel::tr("conflict - fix it, then commit");
		case '?': return SourceControlPanel::tr("new, not tracked yet");
		default: return SourceControlPanel::tr("changed");
	}
}

/// A Visual Studio solution near the top of the folder (Mapbase keeps games.sln in sp/src).
QString findSolution(const QString& root) {
	QStringList level{root};
	for (int depth = 0; depth < 4 && !level.isEmpty(); ++depth) {
		QStringList next;
		for (const auto& folder : level) {
			const QDir dir(folder);
			const auto solutions = dir.entryList({QStringLiteral("*.sln")}, QDir::Files, QDir::Name);
			if (!solutions.isEmpty()) return dir.filePath(solutions.front());
			for (const auto& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
				if (!sub.startsWith('.') && next.size() < 200) next << dir.filePath(sub);
			}
		}
		level = next;
	}
	return {};
}

} // namespace

QString SourceControlPanel::gitProgram() {
	return QStandardPaths::findExecutable(QStringLiteral("git"));
}

SourceControlPanel::SourceControlPanel(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	setObjectName("sourceControl");
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(12, 10, 12, 10);
	v->setSpacing(10);

	// Repository: where, which branch, the remote and the network buttons.
	auto* repo = card(tr("Repository"));
	auto* rv = new QVBoxLayout(repo);
	auto* folderRow = new QHBoxLayout;
	m_folderLabel = new QLabel;
	m_folderLabel->setObjectName("scmFolder");
	m_folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_folderLabel->setMinimumWidth(80);
	m_folderLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	folderRow->addWidget(m_folderLabel, 1);
	auto* choose = button(tr("Choose folder ..."), "scmChoose", tr("The repository to work with, e.g. the mod's source code"));
	auto* openFolder = button(tr("Open folder"), "scmOpenFolder", tr("Show the folder in the file manager"));
	auto* refreshButton = button(tr("Refresh"), "scmRefresh", tr("Read the changes again"));
	folderRow->addWidget(choose);
	folderRow->addWidget(openFolder);
	folderRow->addWidget(refreshButton);
	rv->addLayout(folderRow);
	auto* branchRow = new QHBoxLayout;
	m_branchLabel = new QLabel;
	m_branchLabel->setObjectName("scmBranch");
	m_branchLabel->setTextFormat(Qt::RichText);
	m_remoteLabel = new QLabel;
	m_remoteLabel->setObjectName("scmRemote");
	m_remoteLabel->setProperty("role", "description");
	m_remoteLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_openRemote = button(tr("Open on GitHub"), "scmOpenRemote", tr("Open the repository's page in the browser"));
	branchRow->addWidget(m_branchLabel);
	branchRow->addWidget(m_remoteLabel, 1);
	branchRow->addWidget(m_openRemote);
	rv->addLayout(branchRow);
	auto* actionRow = new QHBoxLayout;
	m_fetch = button(tr("Fetch"), "scmFetch", tr("Ask the remote what is new (changes nothing here)"));
	m_pull = button(tr("Pull"), "scmPull", tr("Get the new commits from the remote and merge them"));
	m_push = button(tr("Push"), "scmPush", tr("Send your commits to the remote"));
	m_openSolution = button(tr("Open in Visual Studio"), "scmSolution", tr("Open the solution of the source code"));
	m_runUpdate = button(tr("Run update.bat"), "scmUpdate", tr("Build and install everything after a pull (the repository's own script)"));
	for (auto* b : {m_fetch, m_pull, m_push}) actionRow->addWidget(b);
	actionRow->addStretch(1);
	actionRow->addWidget(m_openSolution);
	actionRow->addWidget(m_runUpdate);
	rv->addLayout(actionRow);
	v->addWidget(repo);

	m_pages = new QStackedWidget;
	// Changes and commit | history.
	auto* split = new QSplitter(Qt::Horizontal);
	split->setChildrenCollapsible(false);
	auto* changes = card(tr("Changes"));
	auto* cv = new QVBoxLayout(changes);
	m_all = new QCheckBox(tr("All"));
	m_all->setObjectName("scmAll");
	m_all->setChecked(true);
	cv->addWidget(m_all);
	m_changes = new QTreeWidget;
	m_changes->setObjectName("scmChanges");
	m_changes->setHeaderLabels({tr("File"), tr("State")});
	m_changes->setRootIsDecorated(false);
	m_changes->header()->setStretchLastSection(false);
	m_changes->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_changes->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_changes->setToolTip(tr("Checked files go into the next commit; double-click opens a file"));
	cv->addWidget(m_changes, 1);
	m_message = new QPlainTextEdit;
	m_message->setObjectName("scmMessage");
	m_message->setPlaceholderText(tr("What did you change? (commit message)"));
	m_message->setMaximumHeight(80);
	cv->addWidget(m_message);
	auto* commitRow = new QHBoxLayout;
	commitRow->addStretch(1);
	m_commit = button(tr("Commit"), "scmCommit", tr("Save the checked files as a new commit (local; Push sends it)"));
	m_commit->setProperty("accent", true);
	commitRow->addWidget(m_commit);
	cv->addLayout(commitRow);
	split->addWidget(changes);
	auto* history = card(tr("History"));
	auto* hv = new QVBoxLayout(history);
	m_history = new QTreeWidget;
	m_history->setObjectName("scmHistory");
	m_history->setHeaderLabels({tr("Commit"), tr("Message"), tr("Author"), tr("When")});
	m_history->setRootIsDecorated(false);
	m_history->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_history->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_history->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	m_history->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
	m_history->header()->setStretchLastSection(false);
	hv->addWidget(m_history);
	split->addWidget(history);
	split->setStretchFactor(0, 2);
	split->setStretchFactor(1, 3);
	m_pages->addWidget(split);
	// Not a repository / no git.
	auto* message = new QWidget;
	auto* mv = new QVBoxLayout(message);
	m_stateMessage = new QLabel;
	m_stateMessage->setObjectName("scmState");
	m_stateMessage->setWordWrap(true);
	m_stateMessage->setAlignment(Qt::AlignCenter);
	m_init = button(tr("Initialize a repository here"), "scmInit", tr("git init: start tracking this folder"));
	mv->addStretch(1);
	mv->addWidget(m_stateMessage);
	auto* initRow = new QHBoxLayout;
	initRow->addStretch(1);
	initRow->addWidget(m_init);
	initRow->addStretch(1);
	mv->addLayout(initRow);
	mv->addStretch(2);
	m_pages->addWidget(message);
	v->addWidget(m_pages, 1);

	auto* output = card(tr("Output"));
	auto* ov = new QVBoxLayout(output);
	m_log = new QPlainTextEdit;
	m_log->setObjectName("scmLog");
	m_log->setReadOnly(true);
	m_log->setMaximumHeight(110);
	m_log->setMaximumBlockCount(500);
	ov->addWidget(m_log);
	v->addWidget(output);

	connect(choose, &QPushButton::clicked, this, [this] {
		const QString folder = QFileDialog::getExistingDirectory(this, tr("Repository folder"), m_folder);
		if (!folder.isEmpty()) setRepository(folder);
	});
	connect(openFolder, &QPushButton::clicked, this, [this] {
		if (!m_folder.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder));
	});
	connect(refreshButton, &QPushButton::clicked, this, &SourceControlPanel::refresh);
	connect(m_openRemote, &QPushButton::clicked, this, [this] {
		const auto url = remoteWebUrl(m_remoteUrl.toStdString());
		if (!url.empty()) QDesktopServices::openUrl(QUrl(QString::fromStdString(url)));
	});
	connect(m_fetch, &QPushButton::clicked, this, [this] { network({"fetch", "--prune"}); });
	connect(m_pull, &QPushButton::clicked, this, [this] { network({"pull", "--no-rebase"}); });
	connect(m_push, &QPushButton::clicked, this, [this] {
		// The first push of a branch also sets its upstream.
		if (m_upstream.isEmpty() && !m_branch.isEmpty()) {
			network({"push", "-u", "origin", m_branch});
		} else {
			network({"push"});
		}
	});
	connect(m_openSolution, &QPushButton::clicked, this, [this] {
		if (!m_solution.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_solution));
	});
	connect(m_runUpdate, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_WIN
		// Its own console window, so the build output stays visible.
		QProcess::startDetached(QStringLiteral("cmd.exe"), {"/c", "start", "update", "cmd", "/k", "update.bat"}, m_root);
		log(tr("Started update.bat in its own window"));
#endif
	});
	connect(m_init, &QPushButton::clicked, this, [this] {
		run({"init"}, [this](int code, const QByteArray&, const QByteArray&) {
			emit commandFinished(code == 0);
			refresh();
		}, true);
	});
	connect(m_all, &QCheckBox::clicked, this, [this](bool on) {
		for (int i = 0; i < m_changes->topLevelItemCount(); ++i) {
			m_changes->topLevelItem(i)->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
		}
	});
	connect(m_changes, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
		const QString path = QDir(m_root).filePath(item->data(0, Qt::UserRole).toString());
		if (QFileInfo::exists(path)) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
	});
	connect(m_commit, &QPushButton::clicked, this, &SourceControlPanel::commit);
	connect(editor, &Editor::gameChanged, this, &SourceControlPanel::loadForGame);
	loadForGame();
}

void SourceControlPanel::loadForGame() {
	QString folder;
	if (!m_editor->gameKey().isEmpty()) {
		folder = QSettings().value(settingsKey(m_editor->gameKey())).toString();
	}
	if (folder.isEmpty()) {
		if (const auto* entry = m_editor->gameEntry()) folder = fromPath(entry->gameDir);   // the mod folder itself
	}
	m_folder = folder;
	refresh();
}

void SourceControlPanel::setRepository(const QString& folder) {
	m_folder = QDir::cleanPath(folder);
	if (!m_editor->gameKey().isEmpty()) {
		QSettings().setValue(settingsKey(m_editor->gameKey()), m_folder);
	}
	refresh();
}

void SourceControlPanel::showState(int page, const QString& message) {
	m_pages->setCurrentIndex(page);
	m_stateMessage->setText(message);
	updateTools();
}

void SourceControlPanel::updateTools() {
	const bool repo = !m_root.isEmpty() && m_pages->currentIndex() == RepositoryPage;
	const bool idle = !busy();
	const bool remote = !m_remoteUrl.isEmpty();
	m_fetch->setEnabled(repo && idle && remote);
	m_pull->setEnabled(repo && idle && remote);
	m_push->setEnabled(repo && idle && remote && !m_branch.isEmpty());
	m_commit->setEnabled(repo && idle);
	m_openRemote->setVisible(!remoteWebUrl(m_remoteUrl.toStdString()).empty());
	m_openSolution->setVisible(repo && !m_solution.isEmpty());
#ifdef Q_OS_WIN
	m_runUpdate->setVisible(repo && QFileInfo::exists(QDir(m_root).filePath("update.bat")));
#else
	m_runUpdate->setVisible(false);
#endif
}

void SourceControlPanel::refresh() {
	m_folderLabel->setText(m_folder.isEmpty() ? tr("No folder chosen") : QDir::toNativeSeparators(m_folder));
	m_folderLabel->setToolTip(QDir::toNativeSeparators(m_folder));
	if (gitProgram().isEmpty()) {
		m_root.clear();
		m_init->hide();
		m_branchLabel->clear();
		m_remoteLabel->clear();
		showState(MessagePage, tr("Git is not installed. Get it from git-scm.com, then press Refresh."));
		return;
	}
	if (m_folder.isEmpty() || !QFileInfo(m_folder).isDir()) {
		m_root.clear();
		m_init->hide();
		showState(MessagePage, tr("Choose the folder of a repository (Choose folder ...)."));
		return;
	}
	if (m_refreshing) return;   // one read at a time; a running one picks up the newest state anyway
	m_refreshing = true;
	m_root.clear();
	run({"rev-parse", "--show-toplevel"}, [this](int code, const QByteArray& out, const QByteArray&) {
		if (code != 0) {
			m_refreshing = false;
			m_branch.clear();
			m_remoteUrl.clear();
			m_branchLabel->clear();
			m_remoteLabel->clear();
			m_init->show();
			showState(MessagePage, tr("%1 is not a git repository.").arg(QDir::toNativeSeparators(m_folder)));
			emit refreshed();
			return;
		}
		m_root = QDir::cleanPath(QString::fromUtf8(out).trimmed());
		m_solution = findSolution(m_root);
		run({"status", "--porcelain=v2", "--branch", "-z", "--untracked-files=normal"}, [this](int code, const QByteArray& out, const QByteArray& err) {
			const auto status = code == 0 ? parseGitStatus(std::string_view(out.constData(), static_cast<std::size_t>(out.size()))) : std::nullopt;
			if (!status) {
				log(QString::fromUtf8(err).trimmed(), true);
				return;
			}
			m_branch = QString::fromStdString(status->branch);
			m_upstream = QString::fromStdString(status->upstream);
			QString branch = status->detached ? tr("(no branch)") : m_branch;
			QString text = QStringLiteral("<b>%1</b>").arg(branch.toHtmlEscaped());
			if (!m_upstream.isEmpty()) {
				if (status->ahead) text += QStringLiteral("  <span style='color:#5ccf7a'>↑%1</span>").arg(status->ahead);
				if (status->behind) text += QStringLiteral("  <span style='color:#f0b040'>↓%1</span>").arg(status->behind);
				if (!status->ahead && !status->behind) text += QStringLiteral("  <span style='color:#8a8a8a'>%1</span>").arg(tr("up to date"));
			}
			m_branchLabel->setText(text);
			m_branchLabel->setToolTip(m_upstream.isEmpty() ? tr("This branch has no remote branch yet (Push creates it)")
			                                               : tr("Follows %1 - ↑ commits to push, ↓ commits to pull").arg(m_upstream));
			// Keep what was unchecked.
			QSet<QString> unchecked;
			for (int i = 0; i < m_changes->topLevelItemCount(); ++i) {
				auto* item = m_changes->topLevelItem(i);
				if (item->checkState(0) == Qt::Unchecked) unchecked << item->data(0, Qt::UserRole).toString();
			}
			m_changes->clear();
			constexpr int LIMIT = 3000;
			int shown = 0;
			for (const auto& file : status->files) {
				if (file.kind == GitFileChange::Kind::Ignored) continue;
				if (++shown > LIMIT) break;
				const QString path = QString::fromStdString(file.path);
				const char letter = file.letter();
				auto* item = new QTreeWidgetItem({path, QString(QChar(letter))});
				item->setData(0, Qt::UserRole, path);
				item->setData(0, Qt::UserRole + 1, QString::fromStdString(file.originalPath));
				item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
				item->setCheckState(0, unchecked.contains(path) ? Qt::Unchecked : Qt::Checked);
				item->setForeground(1, stateColor(letter));
				item->setToolTip(0, file.originalPath.empty() ? stateName(letter) : tr("renamed from %1").arg(QString::fromStdString(file.originalPath)));
				item->setToolTip(1, stateName(letter));
				m_changes->addTopLevelItem(item);
			}
			if (shown > LIMIT) log(tr("More than %1 changed files - only the first are listed (a .gitignore may help)").arg(LIMIT));
			m_all->setChecked(unchecked.isEmpty());
		});
		run({"log", "-n", "30", "--format=" + QString::fromLatin1(gitLogFormat().data(), int(gitLogFormat().size()))},
		    [this](int code, const QByteArray& out, const QByteArray&) {
			    m_history->clear();
			    if (code != 0) return;   // no commits yet
			    for (const auto& commit : parseGitLog(std::string_view(out.constData(), static_cast<std::size_t>(out.size())))) {
				    auto* item = new QTreeWidgetItem({QString::fromStdString(commit.hash), QString::fromStdString(commit.subject),
				                                      QString::fromStdString(commit.author), QString::fromStdString(commit.when)});
				    item->setToolTip(1, QString::fromStdString(commit.subject));
				    m_history->addTopLevelItem(item);
			    }
		    });
		run({"config", "--get", "remote.origin.url"}, [this](int code, const QByteArray& out, const QByteArray&) {
			m_remoteUrl = code == 0 ? QString::fromUtf8(out).trimmed() : QString();
			const auto web = remoteWebUrl(m_remoteUrl.toStdString());
			m_remoteLabel->setText(m_remoteUrl.isEmpty() ? tr("no remote (origin)") : QString::fromStdString(web.empty() ? m_remoteUrl.toStdString() : web));
			m_refreshing = false;
			showState(RepositoryPage);
			emit refreshed();
		});
	});
}

void SourceControlPanel::run(const QStringList& args, Done done, bool logged) {
	m_jobs.push({args, std::move(done), logged});
	if (!m_process) startNext();
}

void SourceControlPanel::startNext() {
	if (m_jobs.empty()) {
		setBusy(false);
		return;
	}
	Job job = std::move(m_jobs.front());
	m_jobs.pop();
	auto* process = new QProcess(this);
	m_process = process;
	setBusy(true);
	auto env = QProcessEnvironment::systemEnvironment();
	env.insert("GIT_TERMINAL_PROMPT", "0");   // never wait for a password on a hidden console
	env.insert("GIT_OPTIONAL_LOCKS", "0");
	env.insert("LC_ALL", "C");
	process->setProcessEnvironment(env);
	process->setWorkingDirectory(m_root.isEmpty() ? m_folder : m_root);
	QStringList args{"-c", "core.quotepath=false", "-c", "color.ui=false"};
	args << job.args;
	if (job.logged) log(QStringLiteral("> git %1").arg(job.args.join(' ')));
	const bool logged = job.logged;
	connect(process, &QProcess::finished, this, [this, process, done = std::move(job.done), logged](int code, QProcess::ExitStatus status) {
		const QByteArray out = process->readAllStandardOutput();
		const QByteArray err = process->readAllStandardError();
		const int exit = status == QProcess::NormalExit ? code : -1;
		if (logged) {
			for (const auto& text : {QString::fromUtf8(out).trimmed(), QString::fromUtf8(err).trimmed()}) {
				if (!text.isEmpty()) log(text, exit != 0);
			}
		}
		process->deleteLater();
		m_process = nullptr;
		if (done) done(exit, out, err);
		startNext();
	});
	connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart) return;
		log(tr("Could not start git: %1").arg(process->errorString()), true);
		process->deleteLater();
		m_process = nullptr;
		m_refreshing = false;
		std::queue<Job>().swap(m_jobs);
		setBusy(false);
	});
	process->start(gitProgram(), args);
}

void SourceControlPanel::setBusy(bool busy) {
	Q_UNUSED(busy);
	updateTools();
}

void SourceControlPanel::log(const QString& text, bool error) {
	if (text.isEmpty()) return;
	m_log->appendHtml(QStringLiteral("<span style='color:%1'>%2</span>")
	                      .arg(error ? QStringLiteral("#ff8a7a") : QStringLiteral("#c8c8c8"), text.toHtmlEscaped().replace('\n', "<br>")));
	m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

QStringList SourceControlPanel::checkedPaths() const {
	QStringList paths;
	for (int i = 0; i < m_changes->topLevelItemCount(); ++i) {
		const auto* item = m_changes->topLevelItem(i);
		if (item->checkState(0) != Qt::Checked) continue;
		paths << item->data(0, Qt::UserRole).toString();
		const QString original = item->data(0, Qt::UserRole + 1).toString();
		if (!original.isEmpty()) paths << original;   // a rename is both paths
	}
	return paths;
}

void SourceControlPanel::commit() {
	const QString message = m_message->toPlainText().trimmed();
	const QStringList paths = checkedPaths();
	if (paths.isEmpty()) {
		log(tr("Nothing to commit: check the files that belong to it"), true);
		return;
	}
	if (message.isEmpty()) {
		log(tr("Write what you changed first (commit message)"), true);
		m_message->setFocus();
		return;
	}
	QStringList add{"add", "-A", "--"};
	add << paths;
	run(add, [this, paths, message](int code, const QByteArray&, const QByteArray&) {
		if (code != 0) {
			emit commandFinished(false);
			return;
		}
		// Only the checked files, even when others were staged before.
		QStringList commitArgs{"commit", "-m", message, "--"};
		commitArgs << paths;
		run(commitArgs, [this](int code, const QByteArray&, const QByteArray&) {
			if (code == 0) m_message->clear();
			emit commandFinished(code == 0);
			refresh();
		}, true);
	}, true);
}

void SourceControlPanel::network(const QStringList& args) {
	log(tr("Contacting %1 ...").arg(m_remoteUrl.isEmpty() ? tr("the remote") : QString::fromStdString(remoteWebUrl(m_remoteUrl.toStdString()))));
	run(args, [this](int code, const QByteArray&, const QByteArray&) {
		emit commandFinished(code == 0);
		refresh();
	}, true);
}

} // namespace sme::app
