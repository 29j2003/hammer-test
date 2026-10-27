// particle_browser.cpp - Particles tab (see particle_browser.h).

#include "particle_browser.h"

#include "asset_browser.h"
#include "editor.h"

#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMimeData>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace sme::app {

namespace {

/// Tree whose particle systems can be dragged into the 3D view.
class ParticleTree : public QTreeWidget {
public:
	using QTreeWidget::QTreeWidget;

protected:
	QStringList mimeTypes() const override { return {"application/x-sme-particle"}; }
	QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
		if (items.isEmpty() || items.first()->childCount() > 0) {
			return nullptr;
		}
		auto* mime = new QMimeData;
		mime->setData("application/x-sme-particle", items.first()->text(0).toUtf8());
		mime->setText(items.first()->text(0));
		return mime;
	}
};

} // namespace

ParticleBrowser::ParticleBrowser(Editor* editor, AssetBrowser* assets, QWidget* parent) : QWidget(parent), m_editor(editor), m_assets(assets) {
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(4, 4, 4, 4);
	m_search = new QLineEdit;
	m_search->setPlaceholderText(tr("Search particle systems (fire, smoke, spark ...)"));
	m_search->setClearButtonEnabled(true);
	v->addWidget(m_search);
	m_tree = new ParticleTree;
	m_tree->setHeaderHidden(true);
	m_tree->setDragEnabled(true);
	m_tree->setDragDropMode(QAbstractItemView::DragOnly);
	m_tree->setUniformRowHeights(true);
	v->addWidget(m_tree, 1);
	m_info = new QLabel(tr("No game loaded"));
	m_info->setWordWrap(true);
	v->addWidget(m_info);
	connect(m_search, &QLineEdit::textChanged, this, &ParticleBrowser::filter);
	connect(assets, &AssetBrowser::materialsIndexed, this, &ParticleBrowser::fill);
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
		if (!item || item->childCount() > 0) {
			return;
		}
		std::vector<int> targets;
		for (const int id : m_editor->selection().entities) {
			if (const auto* entity = m_editor->doc().findEntity(id); entity && entity->classname() == "info_particle_system") {
				targets.push_back(id);
			}
		}
		if (targets.empty()) {
			emit m_editor->message(tr("Drag the particle system into the 3D view, or select an info_particle_system to set it"));
			return;
		}
		m_editor->commit(edit::setKeyValue(m_editor->doc(), targets, "effect_name", item->text(0).toStdString()));
	});
}

void ParticleBrowser::fill() {
	m_tree->clear();
	QMap<QString, QTreeWidgetItem*> files;
	for (const auto& system : m_assets->particles()) {
		const QString file = QString::fromStdString(system.file);
		auto it = files.find(file);
		if (it == files.end()) {
			auto* group = new QTreeWidgetItem(m_tree, {file.section('/', -1)});
			group->setToolTip(0, file);
			group->setFlags(group->flags() & ~Qt::ItemIsDragEnabled);
			it = files.insert(file, group);
		}
		auto* item = new QTreeWidgetItem(*it, {QString::fromStdString(system.name)});
		item->setToolTip(0, file);
	}
	m_info->setText(m_assets->particles().empty()
	                    ? tr("No particle systems found (particles/*.pcf)")
	                    : tr("%1 particle systems in %2 files - drag one into the map (info_particle_system). Particles only show in the game.")
	                          .arg(m_assets->particles().size())
	                          .arg(files.size()));
	filter(m_search->text());
}

void ParticleBrowser::filter(const QString& text) {
	const QStringList words = text.toLower().split(' ', Qt::SkipEmptyParts);
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		auto* group = m_tree->topLevelItem(i);
		int visible = 0;
		for (int j = 0; j < group->childCount(); ++j) {
			auto* item = group->child(j);
			bool show = true;
			for (const auto& word : words) show = show && item->text(0).toLower().contains(word);
			item->setHidden(!show);
			visible += show ? 1 : 0;
		}
		group->setHidden(visible == 0);
		group->setExpanded(!words.isEmpty());
	}
}

} // namespace sme::app
