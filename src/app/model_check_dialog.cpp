// model_check_dialog.cpp - "Check model" (see model_check_dialog.h).

#include "model_check_dialog.h"

#include "editor.h"
#include "ui_helpers.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <set>

#include <sme/assets.h>
#include <sme/text.h>

namespace sme::app {

std::vector<ModelIssue> runModelCheck(const Editor* editor, const QString& model, bool withSoundsAndParticles) {
	const auto* game = editor ? editor->game() : nullptr;
	if (!game) return {};
	ModelCheckContext context;
	std::set<std::string> sounds, particles;
	if (withSoundsAndParticles) {
		for (const auto& name : soundScriptNames(game->files)) sounds.insert(toLower(name));
		for (const auto& system : particleSystems(game->files)) particles.insert(toLower(system.name));
		context.soundNames = &sounds;
		context.particleNames = &particles;
	}
	return checkModel(game->files, model.toStdString(), context);
}

ModelCheckDialog::ModelCheckDialog(Editor* editor, const QString& model, QWidget* parent) : QDialog(parent), m_editor(editor), m_model(model) {
	setObjectName("modelCheckDialog");
	setWindowTitle(tr("Check model - %1").arg(model.section('/', -1)));
	setAttribute(Qt::WA_DeleteOnClose);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(noteLabel(tr("What would make %1 fail or look wrong in the game: files, checksums, limits, materials, sounds and effects.").arg(model)));
	m_summary = new QLabel(tr("Checking ..."));
	m_summary->setObjectName("modelCheckSummary");
	layout->addWidget(m_summary);
	m_list = new QTreeWidget;
	m_list->setObjectName("modelCheckList");
	m_list->setHeaderLabels({tr("Problem"), tr("What to do")});
	m_list->setRootIsDecorated(false);
	m_list->setWordWrap(true);
	m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	layout->addWidget(m_list, 1);
	auto* buttons = new QDialogButtonBox;
	auto* again = buttons->addButton(tr("Check again"), QDialogButtonBox::ActionRole);
	again->setObjectName("modelCheckAgain");
	auto* copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
	buttons->addButton(QDialogButtonBox::Close);
	connect(again, &QPushButton::clicked, this, &ModelCheckDialog::recheck);
	connect(copy, &QPushButton::clicked, this, [this] {
		QStringList lines{m_model + ": " + m_summary->text()};
		for (const auto& issue : m_issues) lines << QStringLiteral("- %1 (%2)").arg(qs(issue.message), qs(issue.fix));
		QApplication::clipboard()->setText(lines.join('\n'));
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	connect(&m_watcher, &QFutureWatcher<std::vector<ModelIssue>>::finished, this, [this] {
		showIssues(m_watcher.result());
		emit checked();
	});
	resize(760, 420);
	recheck();
}

void ModelCheckDialog::recheck() {
	if (m_watcher.isRunning()) return;
	m_summary->setText(tr("Checking ..."));
	const Editor* editor = m_editor;
	const QString model = m_model;
	m_watcher.setFuture(QtConcurrent::run([editor, model] { return runModelCheck(editor, model); }));
}

void ModelCheckDialog::showIssues(const std::vector<ModelIssue>& issues) {
	m_issues = issues;
	std::stable_sort(m_issues.begin(), m_issues.end(), [](const ModelIssue& a, const ModelIssue& b) { return int(a.level) < int(b.level); });
	m_list->clear();
	for (const auto& issue : m_issues) {
		const QString level = issue.level == ModelIssue::Level::Error ? tr("Error") : issue.level == ModelIssue::Level::Warning ? tr("Warning") : tr("Note");
		auto* item = new QTreeWidgetItem({level + ": " + qs(issue.message), qs(issue.fix)});
		item->setData(0, Qt::UserRole, qs(issue.code));
		item->setForeground(0, issue.level == ModelIssue::Level::Error ? QColor(255, 120, 110) : issue.level == ModelIssue::Level::Warning ? QColor(240, 190, 90)
		                                                                                                                                      : QColor(170, 175, 185));
		item->setToolTip(0, qs(issue.message));
		item->setToolTip(1, qs(issue.fix));
		m_list->addTopLevelItem(item);
	}
	m_summary->setText(qs(modelCheckSummary(m_issues)));
}

ModelCheckDialog* openModelCheckDialog(Editor* editor, const QString& model, QWidget* parent) {
	if (!editor || model.isEmpty()) return nullptr;
	auto* dialog = new ModelCheckDialog(editor, model, parent);
	dialog->show();
	return dialog;
}

} // namespace sme::app
