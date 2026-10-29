// compile.cpp - Map compile pipeline and dialogs (see compile.h).

#include "compile.h"
#include "process_guard.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QTreeWidget>
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

CompileOptions loadCompileOptions(const QString& gameKey) {
	const QString text = QSettings().value("compile/" + QString(gameKey).replace('/', '_')).toString();
	return text.isEmpty() ? CompileOptions{} : CompileOptions::parse(text.toStdString());
}

void saveCompileOptions(const QString& gameKey, const CompileOptions& options) {
	QSettings().setValue("compile/" + QString(gameKey).replace('/', '_'), QString::fromStdString(options.serialize()));
}

namespace {

/// The cubemaps in a compiled map file (nullopt if it is missing or unreadable).
std::optional<BspCubemaps> readMapCubemaps(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
	const QByteArray data = file.readAll();
	return readBspCubemaps(std::as_bytes(std::span(data.constData(), static_cast<std::size_t>(data.size()))), QFileInfo(path).completeBaseName().toStdString());
}

} // namespace

std::vector<CompileRunner::Step> CompileRunner::plan(const QString& vmfPath, const QString& gameDir, const GameSettings& settings, const CompileOptions& options) {
	std::vector<Step> steps;
	const QFileInfo vmf(vmfPath);
	const QString base = vmf.absolutePath() + "/" + vmf.completeBaseName();
	for (const auto& command : compileCommands(options, base.toStdString(), gameDir.toStdString(), settings.launchOptions.toStdString())) {
		Step step;
		step.name = QString::fromStdString(command.name);
		for (const auto& argument : command.arguments) {
			step.arguments << QString::fromStdString(argument);
		}
		step.detached = command.detached;
		switch (command.tool) {
			case CompileTool::Vbsp: step.program = settings.vbsp; break;
			case CompileTool::Vvis: step.program = settings.vvis; break;
			case CompileTool::Vrad: step.program = settings.vrad; break;
			case CompileTool::Copy: step.copy = true; break;
			case CompileTool::Cubemaps:
				step.program = settings.gameExe;
				step.cubemapsInto = QDir(gameDir).filePath(QStringLiteral("maps/%1.bsp").arg(vmf.completeBaseName()));
				break;
			case CompileTool::Game:
				if (settings.gameExe.isEmpty()) continue;   // no game set: compile only
				step.program = settings.gameExe;
				break;
		}
		steps.push_back(std::move(step));
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

CompileRunner::~CompileRunner() {
	stopOwnProcesses(this);
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
		color = severity == 2 ? QColor(255, 110, 110) : (severity == 1 ? QColor(255, 200, 100) : QColor(140, 150, 165));
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
		fail(step.program.isEmpty() ? tr("%1: no program set - set the path in the game settings (⚙).").arg(step.name)
		                            : tr("%1 not found: \"%2\" - set the path in the game settings (⚙).").arg(step.name, step.program));
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
	if (!step.cubemapsInto.isEmpty()) {
		m_cubemapsBefore = readMapCubemaps(step.cubemapsInto);
		m_mapTimeBefore = QFileInfo(step.cubemapsInto).lastModified();
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
		if (!step.cubemapsInto.isEmpty()) {
			// The game quits itself after -buildcubemaps with exit code 1: the map tells whether it worked.
			const QFileInfo map(step.cubemapsInto);
			const bool rewritten = map.exists() && (!m_mapTimeBefore.isValid() || map.lastModified() > m_mapTimeBefore);
			const auto verdict = judgeCubemapBuild(code, status != QProcess::NormalExit, m_cubemapsBefore, readMapCubemaps(step.cubemapsInto), rewritten);
			if (!verdict.ok) {
				fail(tr("%1 failed: %2").arg(step.name, QString::fromStdString(verdict.message)));
				return;
			}
			emit output(QString::fromStdString(verdict.message), QColor(130, 210, 140));
			next();
			return;
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

namespace {

constexpr CompileTool TOOLS[] = {CompileTool::Vbsp, CompileTool::Vvis, CompileTool::Vrad, CompileTool::Copy, CompileTool::Cubemaps, CompileTool::Game};

QString toolDescription(CompileTool tool) {
	switch (tool) {
		case CompileTool::Vbsp: return QObject::tr("Turns the brushes into a BSP. Always needed.");
		case CompileTool::Vvis: return QObject::tr("Works out what can be seen from where.");
		case CompileTool::Vrad: return QObject::tr("Bakes light maps, shadows and bounced light.");
		case CompileTool::Copy: return QObject::tr("Copies the BSP into the game's maps folder.");
		case CompileTool::Cubemaps: return QObject::tr("Starts the game, builds the reflections, closes it.");
		case CompileTool::Game: return QObject::tr("Starts the game with the map.");
	}
	return {};
}

QLabel* quiet(const QString& text) {
	auto* label = new QLabel(text);
	label->setWordWrap(true);
	label->setProperty("role", "description");
	return label;
}

/// A switch row: the label with a quiet explanation under it, the check box on the right.
QWidget* switchRow(const QString& title, const QString& note, QCheckBox* box) {
	auto* row = new QWidget;
	auto* h = new QHBoxLayout(row);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(10);
	auto* text = new QVBoxLayout;
	text->setSpacing(0);
	text->addWidget(new QLabel(title));
	if (!note.isEmpty()) text->addWidget(quiet(note));
	h->addLayout(text, 1);
	h->addWidget(box, 0, Qt::AlignVCenter);
	return row;
}

const QList<QSize> RESOLUTIONS = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};

} // namespace

CompileDialog::CompileDialog(const QString& vmfPath, const QString& gameDir, const GameSettings& settings, QWidget* parent, const QString& gameKey)
	: QDialog(parent), m_vmfPath(vmfPath), m_gameDir(gameDir), m_gameKey(gameKey), m_settings(settings),
	  m_options(gameKey.isEmpty() ? CompileOptions{} : loadCompileOptions(gameKey)) {
	setWindowTitle(tr("Compile and run"));
	setObjectName("compileDialog");
	// Segmented choices and switches: the selected part in the accent colour.
	setStyleSheet("QPushButton[segment=\"true\"] { border-radius: 0; padding: 3px 12px; min-width: 44px; }"
	              "QPushButton[segment=\"true\"]:checked { background: #e88c28; color: #1a1a1a; border-color: #e88c28; font-weight: bold; }"
	              "QFrame[role=\"card\"] QLabel#cardTitle { font-weight: bold; font-size: 10.5pt; }"
	              "QFrame[role=\"card\"] QLabel#cardTool { color: #8a909a; font-size: 8pt; }"
	              "QTreeWidget#parameters::item { padding: 3px 0; }"
	              "QListWidget#processes::item { padding: 3px 2px; }"
	              "QListWidget#processes::indicator { width: 13px; height: 13px; border: 1px solid #6a707a; border-radius: 3px; background: #1d1f22; }"
	              "QListWidget#processes::indicator:checked { background: #e88c28; border-color: #e88c28; }");
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 14, 16, 12);
	layout->setSpacing(12);

	// Top: preset and the two views.
	auto* top = new QHBoxLayout;
	top->setSpacing(8);
	top->addWidget(new QLabel(tr("Preset")));
	m_preset = new QComboBox;
	m_preset->setObjectName("preset");
	m_preset->addItem(tr("Fast - a quick look"));
	m_preset->addItem(tr("Normal"));
	m_preset->addItem(tr("Final - best light"));
	m_preset->addItem(tr("Custom"));
	m_preset->setToolTip(tr("Fast: VVIS -fast, VRAD with 2 bounces. Final: VRAD -final. Custom: your own choice below."));
	top->addWidget(m_preset);
	top->addStretch(1);
	auto* viewButtons = new QHBoxLayout;
	viewButtons->setSpacing(0);
	top->addLayout(viewButtons);
	auto* views = new QButtonGroup(this);
	m_simpleButton = new QPushButton(tr("Simple"));
	m_simpleButton->setObjectName("simpleView");
	m_advancedButton = new QPushButton(tr("Advanced"));
	m_advancedButton->setObjectName("advancedView");
	for (auto* b : {m_simpleButton, m_advancedButton}) {
		b->setCheckable(true);
		b->setProperty("segment", true);
		views->addButton(b);
		viewButtons->addWidget(b);
	}
	layout->addLayout(top);

	m_pages = new QStackedWidget;
	m_pages->addWidget(buildSimple());
	m_pages->addWidget(buildAdvanced());
	layout->addWidget(m_pages, 1);

	// Bottom: what will run, as command lines.
	auto* commandHeader = new QHBoxLayout;
	auto* commandTitle = new QLabel(tr("Command lines"));
	commandTitle->setProperty("role", "key");
	commandHeader->addWidget(commandTitle);
	commandHeader->addStretch(1);
	auto* copy = new QPushButton(tr("Copy"));
	copy->setToolTip(tr("Copy the command lines"));
	commandHeader->addWidget(copy);
	layout->addLayout(commandHeader);
	m_commands = new QPlainTextEdit;
	m_commands->setObjectName("commandPreview");
	m_commands->setReadOnly(true);
	m_commands->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	m_commands->setFixedHeight(96);
	layout->addWidget(m_commands);
	connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(commandText()); });

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* compile = buttons->button(QDialogButtonBox::Ok);
	compile->setText(tr("Compile"));
	compile->setProperty("accent", true);
	compile->setDefault(true);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		if (!m_gameKey.isEmpty()) saveCompileOptions(m_gameKey, m_options);
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	connect(m_preset, &QComboBox::activated, this, [this](int index) {
		if (index == static_cast<int>(CompileOptions::Preset::Custom)) return;
		m_options.applyPreset(static_cast<CompileOptions::Preset>(index));
		loadIntoWidgets();
		changed();
	});
	connect(m_simpleButton, &QPushButton::clicked, this, [this] { setAdvanced(false); });
	connect(m_advancedButton, &QPushButton::clicked, this, [this] { setAdvanced(true); });

	loadIntoWidgets();
	selectTool(0);
	setAdvanced(QSettings().value("compile/advanced", false).toBool());
	changed();
	resize(1000, 680);
}

QFrame* CompileDialog::card(const QString& title, const QString& tool, const QString& description, QCheckBox** toggle) {
	auto* frame = new QFrame;
	frame->setProperty("role", "card");
	auto* v = new QVBoxLayout(frame);
	v->setContentsMargins(14, 12, 14, 14);
	v->setSpacing(8);
	auto* head = new QHBoxLayout;
	head->setSpacing(6);
	auto* name = new QLabel(title);
	name->setObjectName("cardTitle");
	head->addWidget(name);
	if (!tool.isEmpty()) {
		auto* toolLabel = new QLabel(tool);
		toolLabel->setObjectName("cardTool");
		head->addWidget(toolLabel, 0, Qt::AlignBottom);
	}
	head->addStretch(1);
	if (toggle) {
		*toggle = new QCheckBox;
		(*toggle)->setToolTip(tr("Run this step"));
		head->addWidget(*toggle);
	}
	v->addLayout(head);
	v->addWidget(quiet(description));
	return frame;
}

QWidget* CompileDialog::segmented(const QString& name, const QStringList& labels, QButtonGroup** group) {
	auto* row = new QWidget;
	auto* h = new QHBoxLayout(row);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(10);
	h->addWidget(new QLabel(name), 1);
	auto* buttons = new QHBoxLayout;
	buttons->setSpacing(0);
	*group = new QButtonGroup(row);
	for (int i = 0; i < labels.size(); ++i) {
		auto* b = new QPushButton(labels[i]);
		b->setCheckable(true);
		b->setProperty("segment", true);
		(*group)->addButton(b, i);
		buttons->addWidget(b);
	}
	h->addLayout(buttons);
	connect(*group, &QButtonGroup::idClicked, this, [this] {
		if (!m_loading) changed();
	});
	return row;
}

QWidget* CompileDialog::buildSimple() {
	auto* page = new QWidget;
	auto* grid = new QGridLayout(page);
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setHorizontalSpacing(12);
	grid->setVerticalSpacing(12);

	auto* geometry = card(tr("Geometry"), "VBSP", tr("Turns the brushes into a BSP. Always runs."), nullptr);
	m_detail = new QCheckBox;
	m_detail->setObjectName("detail");
	m_water = new QCheckBox;
	m_water->setObjectName("water");
	geometry->layout()->addWidget(switchRow(tr("Detail brushes"), tr("func_detail geometry"), m_detail));
	geometry->layout()->addWidget(switchRow(tr("Water"), tr("water brushes"), m_water));

	auto* visibility = card(tr("Visibility"), "VVIS", tr("Works out what the engine can see from where."), &m_vvis);
	m_vvis->setObjectName("vvis");
	visibility->layout()->addWidget(segmented(tr("Quality"), {tr("Fast"), tr("Full")}, &m_visQuality));

	auto* lighting = card(tr("Lighting"), "VRAD", tr("Bakes light maps, shadows and bounced light."), &m_vrad);
	m_vrad->setObjectName("vrad");
	lighting->layout()->addWidget(segmented(tr("Light level"), {tr("LDR"), tr("HDR"), tr("Both")}, &m_lightLevel));
	lighting->layout()->addWidget(segmented(tr("Quality"), {tr("Fast"), tr("Normal"), tr("Final")}, &m_lightQuality));
	m_bothWarning = new QLabel(tr("Both takes about twice as long - pick one unless the mod lets players switch HDR."));
	m_bothWarning->setWordWrap(true);
	m_bothWarning->setProperty("role", "warning");
	lighting->layout()->addWidget(m_bothWarning);

	auto* cubemaps = card(tr("Cubemaps"), tr("buildcubemaps"), tr("Starts the game, builds the reflections of the env_cubemaps into the map, then closes it."), &m_cubemaps);
	m_cubemaps->setObjectName("cubemaps");
	cubemaps->layout()->addWidget(segmented(tr("Build for"), {tr("LDR"), tr("HDR"), tr("Both")}, &m_cubemapLevel));
	m_cubemapNote = quiet(tr("Needs \"Copy to the game's maps folder\" (the game loads the map from there)."));
	cubemaps->layout()->addWidget(m_cubemapNote);

	auto* after = card(tr("After compiling"), {}, tr("What happens once every step went well."), nullptr);
	m_copy = new QCheckBox;
	m_copy->setObjectName("copy");
	m_run = new QCheckBox;
	m_run->setObjectName("run");
	after->layout()->addWidget(switchRow(tr("Copy to the game's maps folder"), QFileInfo(m_gameDir).fileName() + "/maps", m_copy));
	after->layout()->addWidget(switchRow(tr("Run the game"), tr("loads the map when it starts"), m_run));
	m_cheats = new QCheckBox;
	m_cheats->setObjectName("cheats");
	m_cheats->setToolTip(tr("+sv_cheats 1: impulse 101 (all weapons and ammo), noclip and god work while you test the map"));
	after->layout()->addWidget(switchRow(tr("Cheats"), tr("+sv_cheats 1 (impulse 101, noclip)"), m_cheats));

	auto* window = card(tr("Game window"), {}, tr("How the game starts (also for building cubemaps)."), nullptr);
	m_console = new QCheckBox;
	m_console->setObjectName("console");
	m_windowed = new QCheckBox;
	m_windowed->setObjectName("windowed");
	m_novid = new QCheckBox;
	m_novid->setObjectName("novid");
	window->layout()->addWidget(switchRow(tr("Console"), tr("-console"), m_console));
	window->layout()->addWidget(switchRow(tr("Windowed"), tr("-windowed"), m_windowed));
	auto* resolutionRow = new QWidget;
	auto* rh = new QHBoxLayout(resolutionRow);
	rh->setContentsMargins(0, 0, 0, 0);
	rh->setSpacing(10);
	rh->addWidget(new QLabel(tr("Resolution")), 1);
	m_resolution = new QComboBox;
	m_resolution->setObjectName("resolution");
	for (const QSize& size : RESOLUTIONS) {
		m_resolution->addItem(QStringLiteral("%1 x %2").arg(size.width()).arg(size.height()), size);
	}
	m_resolution->addItem(tr("Custom ..."), QSize(-1, -1));
	m_resolution->addItem(tr("The game's own setting"), QSize(0, 0));
	rh->addWidget(m_resolution);
	window->layout()->addWidget(resolutionRow);
	m_customSize = new QWidget;
	auto* ch = new QHBoxLayout(m_customSize);
	ch->setContentsMargins(0, 0, 0, 0);
	ch->setSpacing(6);
	ch->addStretch(1);
	m_width = new QSpinBox;
	m_height = new QSpinBox;
	for (auto* spin : {m_width, m_height}) {
		spin->setRange(320, 16384);
		spin->setKeyboardTracking(false);
	}
	ch->addWidget(m_width);
	ch->addWidget(new QLabel(QStringLiteral("x")));
	ch->addWidget(m_height);
	window->layout()->addWidget(m_customSize);
	window->layout()->addWidget(switchRow(tr("Skip the intro video"), tr("-novid"), m_novid));

	for (auto* frame : {geometry, visibility, lighting, cubemaps, after, window}) {
		qobject_cast<QVBoxLayout*>(frame->layout())->addStretch(1);
	}
	grid->setRowStretch(2, 1);   // cards as tall as their content, the room below stays empty
	grid->addWidget(geometry, 0, 0);
	grid->addWidget(visibility, 0, 1);
	grid->addWidget(lighting, 0, 2);
	grid->addWidget(cubemaps, 1, 0);
	grid->addWidget(after, 1, 1);
	grid->addWidget(window, 1, 2);
	for (int c = 0; c < 3; ++c) grid->setColumnStretch(c, 1);

	for (auto* box : {m_detail, m_water, m_vvis, m_vrad, m_cubemaps, m_copy, m_run, m_cheats, m_console, m_windowed, m_novid}) {
		connect(box, &QCheckBox::toggled, this, [this] {
			if (!m_loading) changed();
		});
	}
	connect(m_resolution, &QComboBox::activated, this, [this] {
		const QSize size = m_resolution->currentData().toSize();
		if (size.width() > 0) {
			m_loading = true;
			m_width->setValue(size.width());
			m_height->setValue(size.height());
			m_loading = false;
		}
		changed();
	});
	for (auto* spin : {m_width, m_height}) {
		connect(spin, &QSpinBox::valueChanged, this, [this] {
			if (!m_loading) changed();
		});
	}
	return page;
}

QWidget* CompileDialog::buildAdvanced() {
	auto* page = new QWidget;
	auto* h = new QHBoxLayout(page);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(12);

	// Left: the steps, each can be switched off.
	auto* left = new QVBoxLayout;
	left->setSpacing(6);
	auto* processesTitle = new QLabel(tr("Steps"));
	processesTitle->setProperty("role", "key");
	left->addWidget(processesTitle);
	m_processes = new QListWidget;
	m_processes->setObjectName("processes");
	m_processes->setFixedWidth(170);
	for (const CompileTool tool : TOOLS) {
		auto* item = new QListWidgetItem(QString::fromLatin1(compileToolName(tool).data(), static_cast<int>(compileToolName(tool).size())));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setToolTip(toolDescription(tool));
		m_processes->addItem(item);
	}
	left->addWidget(m_processes, 1);
	left->addWidget(quiet(tr("Unticked steps keep their parameters and are skipped.")));
	h->addLayout(left);

	// Middle: the parameters the selected tool knows.
	auto* middle = new QVBoxLayout;
	middle->setSpacing(6);
	m_toolTitle = new QLabel;
	m_toolTitle->setProperty("role", "key");
	middle->addWidget(m_toolTitle);
	m_search = new QLineEdit;
	m_search->setObjectName("parameterSearch");
	m_search->setPlaceholderText(tr("Find a parameter ..."));
	m_search->setClearButtonEnabled(true);
	middle->addWidget(m_search);
	m_parameters = new QTreeWidget;
	m_parameters->setObjectName("parameters");
	m_parameters->setColumnCount(2);
	m_parameters->setHeaderLabels({tr("Parameter"), tr("What it does")});
	m_parameters->setRootIsDecorated(false);
	m_parameters->setWordWrap(true);
	m_parameters->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_parameters->header()->setStretchLastSection(true);
	m_parameters->setColumnWidth(0, 190);
	m_parameters->setToolTip(tr("Double-click adds the parameter"));
	middle->addWidget(m_parameters, 1);
	middle->addWidget(quiet(tr("Double-click or + adds a parameter; values can be changed in the list on the right.")));
	h->addLayout(middle, 3);

	// Right: what is added for this tool, and a parameter of your own.
	auto* right = new QVBoxLayout;
	right->setSpacing(6);
	auto* addedTitle = new QLabel(tr("Added"));
	addedTitle->setProperty("role", "key");
	right->addWidget(addedTitle);
	m_added = new QListWidget;
	m_added->setObjectName("added");
	m_added->setToolTip(tr("Double-click to change a value; Delete removes"));
	right->addWidget(m_added, 1);
	auto* remove = new QPushButton(tr("Remove"));
	right->addWidget(remove);
	auto* customRow = new QHBoxLayout;
	customRow->setSpacing(6);
	m_custom = new QLineEdit;
	m_custom->setObjectName("customParameter");
	m_custom->setPlaceholderText(tr("Own parameter, e.g. -threads 8"));
	auto* addCustom = new QPushButton(tr("+ Add"));
	customRow->addWidget(m_custom, 1);
	customRow->addWidget(addCustom);
	right->addLayout(customRow);
	h->addLayout(right, 2);

	connect(m_processes, &QListWidget::currentRowChanged, this, &CompileDialog::selectTool);
	connect(m_processes, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
		if (m_loading) return;
		m_options.setRuns(TOOLS[m_processes->row(item)], item->checkState() == Qt::Checked);
		loadIntoWidgets();
		changed();
	});
	connect(m_search, &QLineEdit::textChanged, this, &CompileDialog::fillParameters);
	connect(m_parameters, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { addParameter(item->data(0, Qt::UserRole).toString()); });
	connect(m_added, &QListWidget::itemChanged, this, [this] {
		if (m_loading) return;
		auto& extra = m_options.extraFor(m_tool);
		extra.clear();
		for (int i = 0; i < m_added->count(); ++i) {
			const QString text = m_added->item(i)->text().trimmed();
			if (!text.isEmpty()) extra.push_back(text.toStdString());
		}
		changed();
	});
	const auto removeCurrent = [this] {
		const int row = m_added->currentRow();
		auto& extra = m_options.extraFor(m_tool);
		if (row < 0 || row >= static_cast<int>(extra.size())) return;
		extra.erase(extra.begin() + row);
		fillAdded();
		changed();
	};
	connect(remove, &QPushButton::clicked, this, removeCurrent);
	auto* deleteKey = new QShortcut(QKeySequence::Delete, m_added, nullptr, nullptr, Qt::WidgetShortcut);
	connect(deleteKey, &QShortcut::activated, this, removeCurrent);
	const auto addOwn = [this] {
		addParameter(m_custom->text().trimmed());
		m_custom->clear();
	};
	connect(addCustom, &QPushButton::clicked, this, addOwn);
	connect(m_custom, &QLineEdit::returnPressed, this, addOwn);
	return page;
}

void CompileDialog::setAdvanced(bool advanced) {
	m_pages->setCurrentIndex(advanced ? 1 : 0);
	m_simpleButton->setChecked(!advanced);
	m_advancedButton->setChecked(advanced);
	QSettings().setValue("compile/advanced", advanced);
}

void CompileDialog::selectTool(int index) {
	if (index < 0 || index >= COMPILE_TOOL_COUNT) return;
	m_tool = TOOLS[index];
	if (m_processes->currentRow() != index) m_processes->setCurrentRow(index);
	fillParameters();
	fillAdded();
}

void CompileDialog::fillParameters() {
	const QString name = QString::fromLatin1(compileToolName(m_tool).data(), static_cast<int>(compileToolName(m_tool).size()));
	m_toolTitle->setText(QStringLiteral("%1  ·  %2").arg(name, toolDescription(m_tool)));
	m_parameters->clear();
	const QString filter = m_search->text().trimmed();
	for (const auto& parameter : knownParameters(m_tool)) {
		const QString text = QString::fromStdString(parameter.name) + (parameter.value.empty() ? QString() : " " + QString::fromStdString(parameter.value));
		const QString description = QString::fromStdString(parameter.description);
		if (!filter.isEmpty() && !text.contains(filter, Qt::CaseInsensitive) && !description.contains(filter, Qt::CaseInsensitive)) continue;
		auto* item = new QTreeWidgetItem(m_parameters, {text, description});
		item->setData(0, Qt::UserRole, text);
		item->setToolTip(1, description);
		auto* add = new QToolButton;
		add->setText(QStringLiteral("+"));
		add->setAutoRaise(true);
		add->setToolTip(tr("Add %1").arg(text));
		connect(add, &QToolButton::clicked, this, [this, text] { addParameter(text); });
		auto* cell = new QWidget;
		auto* ch = new QHBoxLayout(cell);
		ch->setContentsMargins(0, 0, 4, 0);
		ch->addWidget(new QLabel(text), 1);
		ch->addWidget(add);
		m_parameters->setItemWidget(item, 0, cell);
		item->setText(0, QString());
		item->setData(0, Qt::UserRole, text);
	}
	if (m_parameters->topLevelItemCount() == 0) {
		auto* none = new QTreeWidgetItem(m_parameters, {QString(), knownParameters(m_tool).empty() ? tr("This step has no parameters.") : tr("Nothing found.")});
		none->setFlags(Qt::NoItemFlags);
	}
}

void CompileDialog::fillAdded() {
	m_loading = true;
	m_added->clear();
	for (const auto& entry : m_options.extraFor(m_tool)) {
		auto* item = new QListWidgetItem(QString::fromStdString(entry));
		item->setFlags(item->flags() | Qt::ItemIsEditable);
		m_added->addItem(item);
	}
	m_loading = false;
}

void CompileDialog::addParameter(const QString& text) {
	if (text.isEmpty()) return;
	m_options.extraFor(m_tool).push_back(text.toStdString());
	fillAdded();
	m_added->setCurrentRow(m_added->count() - 1);
	changed();
}

void CompileDialog::loadIntoWidgets() {
	m_loading = true;
	const auto& o = m_options;
	m_preset->setCurrentIndex(static_cast<int>(o.preset()));
	m_detail->setChecked(o.detail);
	m_water->setChecked(o.water);
	m_vvis->setChecked(o.vvis);
	m_vrad->setChecked(o.vrad);
	m_cubemaps->setChecked(o.cubemaps);
	m_copy->setChecked(o.copy);
	m_run->setChecked(o.run);
	m_cheats->setChecked(o.cheats);
	m_console->setChecked(o.console);
	m_windowed->setChecked(o.windowed);
	m_novid->setChecked(o.novid);
	m_visQuality->button(o.visFast ? 0 : 1)->setChecked(true);
	m_lightLevel->button(static_cast<int>(o.light))->setChecked(true);
	m_lightQuality->button(static_cast<int>(o.lightQuality))->setChecked(true);
	m_cubemapLevel->button(static_cast<int>(o.cubemapLevel))->setChecked(true);
	int resolution = m_resolution->findData(QSize(o.width, o.height));
	if (resolution < 0) resolution = m_resolution->findData(QSize(-1, -1));
	m_resolution->setCurrentIndex(resolution);
	if (o.width > 0) {
		m_width->setValue(o.width);
		m_height->setValue(o.height);
	}
	for (int i = 0; i < COMPILE_TOOL_COUNT; ++i) {
		const CompileTool tool = TOOLS[i];
		// Cubemaps show their own switch (they still need the copy to run).
		const bool on = tool == CompileTool::Cubemaps ? o.cubemaps : o.runs(tool);
		m_processes->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
	}
	m_loading = false;
}

void CompileDialog::changed() {
	if (m_loading) return;
	auto& o = m_options;
	{
		// The simple view's widgets are the source; the step list of the advanced view writes
		// into the options and loads them into these widgets first.
		o.detail = m_detail->isChecked();
		o.water = m_water->isChecked();
		o.vvis = m_vvis->isChecked();
		o.vrad = m_vrad->isChecked();
		o.cubemaps = m_cubemaps->isChecked();
		o.copy = m_copy->isChecked();
		o.run = m_run->isChecked();
		o.cheats = m_cheats->isChecked();
		o.console = m_console->isChecked();
		o.windowed = m_windowed->isChecked();
		o.novid = m_novid->isChecked();
		o.visFast = m_visQuality->checkedId() == 0;
		o.light = static_cast<LightLevel>(std::max(0, m_lightLevel->checkedId()));
		o.lightQuality = static_cast<CompileQuality>(std::max(0, m_lightQuality->checkedId()));
		o.cubemapLevel = static_cast<LightLevel>(std::max(0, m_cubemapLevel->checkedId()));
		const QSize size = m_resolution->currentData().toSize();
		if (size.width() < 0) {
			o.width = m_width->value();
			o.height = m_height->value();
		} else {
			o.width = size.width();
			o.height = size.height();
		}
	}
	m_loading = true;
	m_preset->setCurrentIndex(static_cast<int>(o.preset()));
	for (int i = 0; i < COMPILE_TOOL_COUNT; ++i) {
		const bool on = TOOLS[i] == CompileTool::Cubemaps ? o.cubemaps : o.runs(TOOLS[i]);
		m_processes->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
	}
	m_loading = false;
	// What is not used is shown dimmed (it keeps its value).
	m_visQuality->buttons().front()->parentWidget()->setEnabled(o.vvis);
	m_lightLevel->buttons().front()->parentWidget()->setEnabled(o.vrad);
	m_lightQuality->buttons().front()->parentWidget()->setEnabled(o.vrad);
	m_bothWarning->setVisible(o.vrad && o.light == LightLevel::Both);
	m_cubemapLevel->buttons().front()->parentWidget()->setEnabled(o.cubemaps);
	m_cubemapNote->setVisible(o.cubemaps && !o.copy);
	m_customSize->setVisible(m_resolution->currentData().toSize().width() < 0);
	m_commands->setPlainText(commandText());
}

QString CompileDialog::commandText() const {
	return CompileRunner::describe(m_vmfPath, m_gameDir, m_settings, m_options).join('\n');
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
