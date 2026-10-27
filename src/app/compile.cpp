// compile.cpp - Map compile pipeline and dialogs (see compile.h).

#include "compile.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <sme/checks.h>
#include <sme/compile_log.h>

namespace sme::app {

namespace {

/// On Linux/macOS the Windows compile tools run through wine if it is installed.
std::pair<QString, QStringList> wrap(const QString& program, const QStringList& arguments) {
#ifndef _WIN32
	if (program.endsWith(".exe", Qt::CaseInsensitive)) {
		const QString wine = QStandardPaths::findExecutable("wine");
		if (!wine.isEmpty()) {
			return {wine, QStringList{program} + arguments};
		}
	}
#endif
	return {program, arguments};
}

} // namespace

std::vector<CompileRunner::Step> CompileRunner::plan(const QString& vmfPath, const QString& gameDir, const GameSettings& settings, const CompileOptions& options) {
	std::vector<Step> steps;
	const QFileInfo vmf(vmfPath);
	const QString base = vmf.absolutePath() + "/" + vmf.completeBaseName();
	if (options.vbsp) {
		steps.push_back({"VBSP", settings.vbsp, {"-game", gameDir, base}});
	}
	if (options.vvis) {
		QStringList args{"-game", gameDir};
		if (options.preset == CompileOptions::Preset::Fast) args << "-fast";
		steps.push_back({"VVIS", settings.vvis, args << base});
	}
	if (options.vrad) {
		QStringList args{"-game", gameDir};
		if (options.preset == CompileOptions::Preset::Fast) args << "-bounce" << "2" << "-noextra";
		if (options.preset == CompileOptions::Preset::Final) args << "-final";
		if (options.hdr) args << "-both";
		steps.push_back({"VRAD", settings.vrad, args << base});
	}
	if (options.copy) {
		steps.push_back({"Copy", {}, {base + ".bsp", gameDir + "/maps/" + vmf.completeBaseName() + ".bsp"}, false, true});
	}
	if (options.run && !settings.gameExe.isEmpty()) {
		QStringList args = settings.launchOptions.split(' ', Qt::SkipEmptyParts);
		args << "-game" << gameDir << "+map" << vmf.completeBaseName();
		steps.push_back({"Game", settings.gameExe, args, true});
	}
	return steps;
}

QStringList CompileRunner::describe(const QString& vmfPath, const QString& gameDir, const GameSettings& settings, const CompileOptions& options) {
	QStringList lines;
	for (const auto& step : plan(vmfPath, gameDir, settings, options)) {
		if (step.copy) {
			lines << QStringLiteral("copy \"%1\" -> \"%2\"").arg(step.arguments[0], step.arguments[1]);
		} else {
			lines << QStringLiteral("\"%1\" %2").arg(step.program, step.arguments.join(' '));
		}
	}
	return lines;
}

QStringList CompileRunner::stepNames() const {
	QStringList names;
	for (const auto& step : m_steps) {
		names << step.name;
	}
	return names;
}

void CompileRunner::start(const QString& vmfPath, const QString& gameDir, const GameSettings& settings, const CompileOptions& options) {
	if (running()) {
		return;
	}
	m_steps = plan(vmfPath, gameDir, settings, options);
	m_index = 0;
	m_current = -1;
	m_vmfPath = vmfPath;
	m_gameDir = gameDir;
	m_leaked = false;
	m_cancelled = false;
	emit started(vmfPath, stepNames());
	// An old pointfile would show a stale leak.
	QFile::remove(QFileInfo(vmfPath).absolutePath() + "/" + QFileInfo(vmfPath).completeBaseName() + ".lin");
	next();
}

void CompileRunner::cancel() {
	if (!m_process) {
		return;
	}
	m_cancelled = true;
	m_process->kill();   // finished() reports the cancel
}

void CompileRunner::fail(const QString& text) {
	emit output(text, QColor(255, 110, 110));
	emit problem(m_current, text, 2, {});
	emit stepFinished(m_current, false, m_stepClock.elapsed());
	for (int i = m_current + 1; i < static_cast<int>(m_steps.size()); ++i) {
		emit stepSkipped(i, tr("not run"));
	}
	emit finished(false);
}

void CompileRunner::handleLine(const QString& raw) {
	const QString line = raw.trimmed();
	if (line.isEmpty()) {
		return;
	}
	QColor color;
	if (const auto message = classifyCompileLine(line.toStdString())) {
		if (message->leak) {
			m_leaked = true;
		}
		const int severity = message->severity == CompileMessage::Severity::Error ? 2 : (message->severity == CompileMessage::Severity::Warning ? 1 : 0);
		color = severity == 2 ? QColor(255, 110, 110) : QColor(255, 200, 100);
		if (severity > 0) {
			emit problem(m_current, line, severity, QString::fromStdString(message->explanation));
		}
	}
	emit output(line, color);
}

void CompileRunner::next() {
	if (m_current >= 0 && m_current < static_cast<int>(m_steps.size()) && m_index > static_cast<std::size_t>(m_current)) {
		// The previous step ended well.
		emit stepFinished(m_current, true, m_stepClock.elapsed());
	}
	if (m_index >= m_steps.size()) {
		emit output(tr("Compile finished."), QColor(120, 220, 120));
		emit finished(true);
		return;
	}
	m_current = static_cast<int>(m_index);
	const Step step = m_steps[m_index++];
	m_stepClock.start();
	m_phase.clear();
	m_partial.clear();
	if (step.copy) {
		emit stepStarted(m_current, tr("copy to %1").arg(step.arguments[1]));
		QDir().mkpath(QFileInfo(step.arguments[1]).absolutePath());
		QFile::remove(step.arguments[1]);
		if (!QFile::copy(step.arguments[0], step.arguments[1])) {
			fail(tr("Could not copy %1 to %2").arg(step.arguments[0], step.arguments[1]));
			return;
		}
		emit output(tr("Copied the map to %1").arg(step.arguments[1]), {});
		next();
		return;
	}
	if (step.name == "VVIS" && m_leaked) {
		emit output(tr("Skipping VVIS and VRAD: the map leaks (see Problems)."), QColor(255, 180, 80));
		for (int i = m_current; i < static_cast<int>(m_steps.size()); ++i) {
			emit stepSkipped(i, tr("map leaks"));
		}
		emit finished(false);
		return;
	}
	if (step.program.isEmpty() || (!QFileInfo::exists(step.program) && QStandardPaths::findExecutable(step.program).isEmpty())) {
		emit stepStarted(m_current, step.program);
		fail(tr("%1 not found: \"%2\" - set the path in the game settings (⚙).").arg(step.name, step.program));
		return;
	}
	const auto [program, arguments] = wrap(step.program, step.arguments);
	const QString command = QStringLiteral("\"%1\" %2").arg(program, arguments.join(' '));
	emit stepStarted(m_current, command);
	emit output(QStringLiteral("> ") + command, QColor(140, 180, 255));
	if (step.detached) {
		if (!QProcess::startDetached(program, arguments, QFileInfo(step.program).absolutePath())) {
			fail(tr("Could not start the game."));
			return;
		}
		next();
		return;
	}
	m_process = new QProcess(this);
	m_process->setProcessChannelMode(QProcess::MergedChannels);
	m_process->setWorkingDirectory(QFileInfo(step.program).absolutePath());
	connect(m_process, &QProcess::readyRead, this, [this] {
		m_partial += QString::fromLocal8Bit(m_process->readAll()).remove('\r');
		const auto newline = m_partial.lastIndexOf('\n');
		if (newline >= 0) {
			for (const auto& line : m_partial.left(newline).split('\n')) {
				if (const auto phase = compilePhase(line.toStdString()); !phase.empty()) {
					m_phase = QString::fromStdString(phase);
				}
				if (const auto percent = compileProgress(line.toStdString())) {
					emit stepProgress(m_current, m_phase, *percent);
				}
				handleLine(line);
			}
			m_partial = m_partial.mid(newline + 1);
		}
		// Progress dots arrive without a line break ("PortalFlow: 0...1...2").
		if (const auto phase = compilePhase(m_partial.toStdString()); !phase.empty()) {
			m_phase = QString::fromStdString(phase);
		}
		if (const auto percent = compileProgress(m_partial.toStdString())) {
			emit stepProgress(m_current, m_phase, *percent);
		}
	});
	connect(m_process, &QProcess::finished, this, [this, step](int code, QProcess::ExitStatus status) {
		handleLine(m_partial);
		m_partial.clear();
		m_process->deleteLater();
		m_process = nullptr;
		if (m_cancelled) {
			fail(tr("Compile cancelled."));
			return;
		}
		if (step.name == "VBSP") {
			const QFileInfo vmf(m_vmfPath);
			QFile lin(vmf.absolutePath() + "/" + vmf.completeBaseName() + ".lin");
			if (lin.open(QIODevice::ReadOnly)) {
				m_leaked = true;
				const QByteArray text = lin.readAll();
				emit leak(readPointfile(std::string_view(text.constData(), static_cast<std::size_t>(text.size()))));
			}
		}
		if (status != QProcess::NormalExit || code != 0) {
			fail(tr("%1 failed (exit code %2).").arg(step.name).arg(code));
			return;
		}
		next();
	});
	connect(m_process, &QProcess::errorOccurred, this, [this, step](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart && m_process) {
			// finished() never comes for a program that did not start.
			m_process->deleteLater();
			m_process = nullptr;
			fail(tr("%1 could not be started.").arg(step.name));
		}
	});
	m_process->start(program, arguments);
}

// --- Compile dialog -------------------------------------------------------------------

CompileDialog::CompileDialog(const QString& vmfPath, const QString& gameDir, const GameSettings& settings, QWidget* parent)
	: QDialog(parent), m_vmfPath(vmfPath), m_gameDir(gameDir), m_settings(settings) {
	setWindowTitle(tr("Compile and run"));
	auto* layout = new QVBoxLayout(this);
	auto* form = new QFormLayout;
	m_preset = new QComboBox;
	m_preset->addItem(tr("Fast - quick look (VVIS -fast, VRAD 2 bounces)"));
	m_preset->addItem(tr("Normal"));
	m_preset->addItem(tr("Final - best lighting (VRAD -final)"));
	m_preset->setCurrentIndex(1);
	form->addRow(tr("Quality"), m_preset);
	m_vbsp = new QCheckBox(tr("VBSP - geometry"));
	m_vvis = new QCheckBox(tr("VVIS - visibility"));
	m_vrad = new QCheckBox(tr("VRAD - lighting"));
	m_hdr = new QCheckBox(tr("HDR lighting too (-both)"));
	m_copy = new QCheckBox(tr("Copy the BSP into the game's maps folder"));
	m_run = new QCheckBox(tr("Start the game with the map"));
	for (auto* check : {m_vbsp, m_vvis, m_vrad, m_copy, m_run}) {
		check->setChecked(true);
		layout->addWidget(check);
	}
	layout->addWidget(m_hdr);
	layout->insertLayout(0, form);
	m_commands = new QPlainTextEdit;
	m_commands->setReadOnly(true);
	m_commands->setMinimumHeight(110);
	layout->addWidget(new QLabel(tr("Commands:")));
	layout->addWidget(m_commands);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Compile"));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	for (auto* check : {m_vbsp, m_vvis, m_vrad, m_copy, m_run, m_hdr}) {
		connect(check, &QCheckBox::toggled, this, &CompileDialog::updatePreview);
	}
	connect(m_preset, &QComboBox::currentIndexChanged, this, &CompileDialog::updatePreview);
	updatePreview();
	resize(640, 420);
}

CompileOptions CompileDialog::options() const {
	CompileOptions o;
	o.preset = static_cast<CompileOptions::Preset>(m_preset->currentIndex());
	o.vbsp = m_vbsp->isChecked();
	o.vvis = m_vvis->isChecked();
	o.vrad = m_vrad->isChecked();
	o.copy = m_copy->isChecked();
	o.run = m_run->isChecked();
	o.hdr = m_hdr->isChecked();
	return o;
}

void CompileDialog::updatePreview() {
	m_commands->setPlainText(CompileRunner::describe(m_vmfPath, m_gameDir, m_settings, options()).join('\n'));
}

// --- Game settings dialog ---------------------------------------------------------------

GameSettingsDialog::GameSettingsDialog(Editor* editor, const QString& gameKey, QWidget* parent) : QDialog(parent) {
	setWindowTitle(tr("Game settings - %1").arg(gameKey));
	const GameSettings current = editor->gameSettings(gameKey);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("<b>Entity definitions (FGD)</b> - loaded in this order; later files override earlier ones.")));
	m_fgds = new QListWidget;
	m_fgds->addItems(current.fgds);
	layout->addWidget(m_fgds);
	auto* fgdButtons = new QHBoxLayout;
	auto* add = new QPushButton(tr("Add FGD ..."));
	auto* remove = new QPushButton(tr("Remove"));
	auto* up = new QPushButton(tr("Up"));
	auto* down = new QPushButton(tr("Down"));
	for (auto* b : {add, remove, up, down}) {
		fgdButtons->addWidget(b);
	}
	fgdButtons->addStretch(1);
	layout->addLayout(fgdButtons);
	connect(add, &QPushButton::clicked, this, [this] {
		const auto files = QFileDialog::getOpenFileNames(this, tr("Add FGD"), {}, tr("FGD files (*.fgd)"));
		m_fgds->addItems(files);
	});
	connect(remove, &QPushButton::clicked, this, [this] { delete m_fgds->currentItem(); });
	connect(up, &QPushButton::clicked, this, [this] {
		const int row = m_fgds->currentRow();
		if (row > 0) {
			m_fgds->insertItem(row - 1, m_fgds->takeItem(row));
			m_fgds->setCurrentRow(row - 1);
		}
	});
	connect(down, &QPushButton::clicked, this, [this] {
		const int row = m_fgds->currentRow();
		if (row >= 0 && row + 1 < m_fgds->count()) {
			m_fgds->insertItem(row + 1, m_fgds->takeItem(row));
			m_fgds->setCurrentRow(row + 1);
		}
	});

	auto* form = new QFormLayout;
	const auto pathRow = [this, form](const QString& label, const QString& value, bool directory) {
		auto* edit = new QLineEdit(value);
		auto* browse = new QPushButton(tr("..."));
		browse->setMaximumWidth(32);
		auto* row = new QWidget;
		auto* h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		h->addWidget(edit, 1);
		h->addWidget(browse);
		connect(browse, &QPushButton::clicked, this, [this, edit, directory] {
			const QString path = directory ? QFileDialog::getExistingDirectory(this, {}, edit->text())
			                               : QFileDialog::getOpenFileName(this, {}, QFileInfo(edit->text()).absolutePath());
			if (!path.isEmpty()) {
				edit->setText(path);
			}
		});
		form->addRow(label, row);
		return edit;
	};
	m_vbsp = pathRow(tr("VBSP"), current.vbsp, false);
	m_vvis = pathRow(tr("VVIS"), current.vvis, false);
	m_vrad = pathRow(tr("VRAD"), current.vrad, false);
	m_gameExe = pathRow(tr("Game executable"), current.gameExe, false);
	m_launch = new QLineEdit(current.launchOptions);
	form->addRow(tr("Launch options"), m_launch);
	m_mapDir = pathRow(tr("Map sources (.vmf)"), current.mapDir, true);
	layout->addLayout(form);
	auto* hint = new QLabel(tr("Mapbase: use its own compile tools (Mapbase Compile Tools, next to hammer.exe). "
	                           "Black Mesa: tools and bms.fgd are in Black Mesa/bin."));
	hint->setWordWrap(true);
	hint->setProperty("role", "description");
	layout->addWidget(hint);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	resize(720, 520);
}

GameSettings GameSettingsDialog::settings() const {
	GameSettings s;
	for (int i = 0; i < m_fgds->count(); ++i) {
		s.fgds << m_fgds->item(i)->text();
	}
	s.vbsp = m_vbsp->text();
	s.vvis = m_vvis->text();
	s.vrad = m_vrad->text();
	s.gameExe = m_gameExe->text();
	s.launchOptions = m_launch->text();
	s.mapDir = m_mapDir->text();
	return s;
}

} // namespace sme::app
