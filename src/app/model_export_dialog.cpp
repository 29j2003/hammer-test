// model_export_dialog.cpp - Exporting a model for editing (see model_export_dialog.h).

#include "model_export_dialog.h"

#include "editor.h"
#include "ui_helpers.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

#include <sme/game.h>
#include <sme/model_export.h>

namespace sme::app {

ModelExportDialog::ModelExportDialog(Editor* editor, const QString& model, const QStringList& sequences, QWidget* parent)
    : QDialog(parent), m_editor(editor), m_model(model), m_sequences(sequences) {
	setObjectName("modelExportDialog");
	setWindowTitle(sequences.isEmpty() ? tr("Export for editing") : tr("Export animation"));
	auto* layout = new QVBoxLayout(this);
	layout->setSpacing(10);

	auto* title = new QLabel(model, this);
	title->setProperty("role", "pageTitle");
	title->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(title);
	layout->addWidget(noteLabel(sequences.isEmpty()
	                                ? tr("Writes the model as .smd files and a .qc: edit them in Blender (Source Tools), compile with studiomdl.")
	                                : tr("Writes %1 as an .smd with the model's reference and a .qc.").arg(sequences.join(", "))));

	auto* folderRow = new QHBoxLayout;
	folderRow->addWidget(new QLabel(tr("Folder"), this));
	m_folder = new QLineEdit(this);
	m_folder->setObjectName("modelExportFolder");
	if (const auto* entry = editor->gameEntry()) {
		m_folder->setText(QString::fromStdWString(modelExportFolder(entry->gameDir, model.toStdString()).wstring()));
	}
	folderRow->addWidget(m_folder, 1);
	auto* change = new QPushButton(tr("Change ..."), this);
	folderRow->addWidget(change);
	layout->addLayout(folderRow);

	m_animations = new QCheckBox(sequences.isEmpty() ? tr("Animations") : tr("Only these animations"), this);
	m_animations->setObjectName("modelExportAnimations");
	m_animations->setChecked(true);
	m_animations->setEnabled(sequences.isEmpty());
	m_animations->setToolTip(tr("Every animation of the model as its own .smd in anims/ (animations of included models stay included)"));
	m_physics = new QCheckBox(tr("Collision model"), this);
	m_physics->setObjectName("modelExportPhysics");
	m_physics->setChecked(sequences.isEmpty());
	m_materials = new QCheckBox(tr("Materials and textures (with .png pictures)"), this);
	m_materials->setObjectName("modelExportMaterials");
	m_materials->setChecked(sequences.isEmpty());
	for (auto* box : {m_animations, m_physics, m_materials}) layout->addWidget(box);

	m_progress = new QProgressBar(this);
	m_progress->setObjectName("modelExportProgress");
	m_progress->setTextVisible(false);
	m_progress->setMaximumHeight(6);
	m_progress->hide();
	layout->addWidget(m_progress);
	m_result = new QLabel(this);
	m_result->setObjectName("modelExportResult");
	m_result->setWordWrap(true);
	m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_result->hide();
	layout->addWidget(m_result);

	auto* buttons = new QHBoxLayout;
	m_open = new QPushButton(tr("Open folder"), this);
	m_open->setObjectName("modelExportOpen");
	m_open->hide();
	buttons->addWidget(m_open);
	buttons->addStretch(1);
	m_close = new QPushButton(tr("Close"), this);
	m_export = new QPushButton(tr("Export"), this);
	m_export->setObjectName("modelExportStart");
	m_export->setProperty("accent", true);
	m_export->setDefault(true);
	buttons->addWidget(m_close);
	buttons->addWidget(m_export);
	layout->addLayout(buttons);

	m_poll = new QTimer(this);
	m_poll->setInterval(100);
	connect(m_poll, &QTimer::timeout, this, [this] {
		m_progress->setRange(0, std::max(1, m_total->load()));
		m_progress->setValue(m_done->load());
	});
	connect(change, &QPushButton::clicked, this, [this] {
		const QString dir = QFileDialog::getExistingDirectory(this, tr("Export to"), m_folder->text());
		if (!dir.isEmpty()) m_folder->setText(dir);
	});
	connect(m_export, &QPushButton::clicked, this, &ModelExportDialog::exportNow);
	connect(m_close, &QPushButton::clicked, this, [this] {
		if (busy()) stop();
		else reject();
	});
	connect(m_open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder->text())); });
	connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
		m_poll->stop();
		showResult(m_watcher.result());
	});
	setMinimumWidth(560);
	resize(sizeHint().expandedTo(QSize(560, 0)) + QSize(0, 40));   // room for the result line
	// The export reads the game's files: it stops before they go away.
	connect(editor, &Editor::gameAboutToChange, this, &ModelExportDialog::stop);
}

ModelExportDialog::~ModelExportDialog() {
	stop();
}

QString ModelExportDialog::folder() const {
	return m_folder->text();
}

void ModelExportDialog::setFolder(const QString& folder) {
	m_folder->setText(folder);
}

bool ModelExportDialog::busy() const {
	return m_watcher.isRunning();
}

QString ModelExportDialog::resultText() const {
	return m_result->text();
}

void ModelExportDialog::stop() {
	if (!m_watcher.isRunning()) return;
	m_cancel->store(true);
	m_watcher.waitForFinished();
}

bool ModelExportDialog::exportNow() {
	const auto* game = m_editor->game();
	if (busy() || !game || m_folder->text().trimmed().isEmpty()) return false;
	ModelExportOptions options;
	for (const auto& s : m_sequences) options.sequences.push_back(s.toStdString());
	options.animations = m_animations->isChecked();
	options.physics = m_physics->isChecked();
	options.materials = m_materials->isChecked();
	options.pictures = options.materials;
	const GameFileSystem* files = &game->files;
	const std::string model = m_model.toStdString();
	std::filesystem::path folder = QDir::toNativeSeparators(m_folder->text().trimmed()).toStdWString();
	if (folder.is_relative()) {
		// A typed "foo" means inside the mod, not next to wherever Mapsmith was started from.
		const auto* entry = m_editor->gameEntry();
		if (!entry) return false;
		folder = entry->gameDir / folder;
		m_folder->setText(QString::fromStdWString(folder.wstring()));
	}
	m_cancel->store(false);
	m_done->store(0);
	m_total->store(1);
	auto cancel = m_cancel;
	auto done = m_done;
	auto total = m_total;
	// Taking apart fills the first half of the bar, writing the second.
	const auto progress = [cancel, done, total](int half, int d, int t) {
		total->store(200);
		done->store(half * 100 + (t > 0 ? std::clamp(d * 100 / t, 0, 100) : 0));
		return !cancel->load();
	};
	m_export->setEnabled(false);
	m_open->hide();
	m_result->hide();
	m_progress->setValue(0);
	m_progress->show();
	m_close->setText(tr("Cancel"));
	m_poll->start();
	m_watcher.setFuture(QtConcurrent::run([files, model, folder, options, progress]() {
		Result result;
		std::string error;
		auto exported = decompileModel(*files, model, options, &error, [&](int d, int t) { return progress(0, d, t); });
		if (!exported) {
			result.message = QString::fromStdString(error);
			return result;
		}
		const auto written = writeModelExport(*files, *exported, folder, &error, [&](int d, int t) { return progress(1, d, t); });
		if (!written) {
			result.message = QString::fromStdString(error);
			return result;
		}
		result.ok = true;
		result.files = int(written->size());
		for (const auto& note : exported->notes) result.notes << QString::fromStdString(note);
		return result;
	}));
	return true;
}

void ModelExportDialog::showResult(const Result& result) {
	m_progress->hide();
	m_export->setEnabled(true);
	m_close->setText(tr("Close"));
	if (result.ok) {
		m_result->setText(tr("✓ %n file(s) written.", nullptr, result.files));
		m_result->setToolTip(result.notes.join("\n"));
		m_open->show();
	} else {
		m_result->setText(tr("✗ Not exported: %1").arg(result.message == "stopped" ? tr("stopped") : result.message));
		m_result->setToolTip({});
	}
	m_result->show();
	emit exported(result.ok);
}

} // namespace sme::app
