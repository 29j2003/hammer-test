// material_decision_store.cpp - The open mod's material decisions (see material_decision_store.h).

#include "material_decision_store.h"

#include "editor.h"

#include <QActionGroup>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>

#include <sme/text.h>

namespace sme::app {

MaterialDecisionStore::MaterialDecisionStore(Editor* editor) : m_editor(editor) {
	connect(editor, &Editor::gameChanged, this, &MaterialDecisionStore::reload);
	connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
		QFile file(path);
		const QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
		if (text == m_lastWritten) {
			watch();   // our own write (editors replace the file: watch it again)
			return;
		}
		reload();
	});
	connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
		// The file appeared (or went) while only its folder was watched.
		if (QFileInfo::exists(filePath()) != m_watcher.files().contains(filePath())) reload();
	});
}

QString MaterialDecisionStore::filePath() const {
	const auto* entry = m_editor->gameEntry();
	if (!entry || entry->gameDir.empty()) return {};
	return QString::fromStdString(materialDecisionsFile(entry->gameDir).string());
}

MaterialDecision MaterialDecisionStore::lookup(const QString& material) const {
	return m_decisions.lookup(material.toStdString());
}

void MaterialDecisionStore::watch() {
	if (!m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
	if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
	const QString path = filePath();
	if (path.isEmpty()) return;
	if (QFileInfo::exists(path)) m_watcher.addPath(path);
	const QString folder = QFileInfo(path).absolutePath();
	if (QFileInfo::exists(folder)) m_watcher.addPath(folder);
}

void MaterialDecisionStore::reload() {
	const auto* entry = m_editor->gameEntry();
	m_decisions = entry ? loadMaterialDecisions(entry->gameDir) : MaterialDecisions{};
	{
		QFile file(filePath());
		m_lastWritten = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
	}
	watch();
	if (!m_decisions.problems().empty()) {
		QStringList problems;
		for (const auto& p : m_decisions.problems()) problems << tr("line %1: %2").arg(p.line).arg(QString::fromStdString(p.message));
		emit problemsFound(problems);
	}
	emit changed();
}

bool MaterialDecisionStore::change(const std::function<void(MaterialDecisions&)>& edit) {
	const auto* entry = m_editor->gameEntry();
	if (!entry || entry->gameDir.empty()) return false;
	MaterialDecisions next = m_decisions;
	edit(next);
	if (next.serialize() == m_decisions.serialize()) return true;
	m_lastWritten = QString::fromStdString(next.serialize());
	if (!saveMaterialDecisions(entry->gameDir, next)) {
		emit m_editor->message(tr("Could not write %1").arg(filePath()));
		return false;
	}
	m_decisions = std::move(next);
	watch();
	emit changed();
	return true;
}

bool MaterialDecisionStore::setParallax(const QStringList& materials, std::optional<ParallaxChoice> choice, std::optional<double> depth) {
	if (materials.isEmpty()) return false;
	return change([&](MaterialDecisions& d) {
		for (const QString& material : materials) {
			d.edit(material.toStdString(), [&](MaterialDecision& m) {
				if (choice) {
					if (*choice == ParallaxChoice::Auto) m.parallax.reset();
					else m.parallax = *choice;
				}
				if (depth) {
					if (*depth <= 0) m.depth.reset();
					else m.depth = *depth;
				}
				if (choice == ParallaxChoice::Auto && !depth) m.depth.reset();
			});
		}
	});
}

void fillParallaxMenu(QMenu* menu, Editor* editor, const QStringList& materials) {
	if (!menu || materials.isEmpty()) return;
	auto* store = editor->materialDecisions();
	menu->setObjectName("parallaxMenu");
	const MaterialDecision current = store->lookup(materials.front());
	const ParallaxChoice choice = current.parallax.value_or(ParallaxChoice::Auto);
	const bool game = editor->gameEntry() != nullptr;
	auto* group = new QActionGroup(menu);
	const struct {
		ParallaxChoice choice;
		QString text, tip, name;
	} items[] = {
		{ParallaxChoice::Auto, QCoreApplication::translate("Parallax", "Auto (Mapsmith decides)"),
		 QCoreApplication::translate("Parallax", "By name, look and pattern: windows, signs and smooth walls stay flat, fine bricks get less depth"), "parallaxAuto"},
		{ParallaxChoice::On, QCoreApplication::translate("Parallax", "On"),
		 QCoreApplication::translate("Parallax", "Always relief - also where Mapsmith would not give it (from the pattern, else from the brightness)"), "parallaxOn"},
		{ParallaxChoice::Off, QCoreApplication::translate("Parallax", "Off"), QCoreApplication::translate("Parallax", "Never relief: the wall stays flat"), "parallaxOff"},
	};
	for (const auto& item : items) {
		auto* action = menu->addAction(item.text);
		action->setObjectName(item.name);
		action->setToolTip(item.tip);
		action->setCheckable(true);
		action->setChecked(item.choice == choice);
		action->setEnabled(game);
		group->addAction(action);
		QObject::connect(action, &QAction::triggered, store, [store, materials, c = item.choice] { store->setParallax(materials, c); });
	}
	menu->setToolTipsVisible(true);
	menu->addSeparator();
	auto* depth = menu->addAction(current.depth ? QCoreApplication::translate("Parallax", "Depth %1 ...").arg(*current.depth)
	                                            : QCoreApplication::translate("Parallax", "Depth ..."));
	depth->setObjectName("parallaxDepth");
	depth->setEnabled(game);
	depth->setToolTip(QCoreApplication::translate("Parallax", "0.006 is shallow (bricks), 0.01 the usual; small bricks swim with deep relief"));
	QObject::connect(depth, &QAction::triggered, store, [store, materials, now = current.depth.value_or(0.006)] {
		bool ok = false;
		const double value = QInputDialog::getDouble(QApplication::activeWindow(), QCoreApplication::translate("Parallax", "Parallax depth"),
		                                             QCoreApplication::translate("Parallax", "Depth of the relief (0.006 shallow, 0.01 the usual):"), now,
		                                             0.0005, 0.1, 4, &ok, Qt::WindowFlags(), 0.0005);
		if (ok) store->setParallax(materials, std::nullopt, value);
	});
	if (current.depth) {
		auto* reset = menu->addAction(QCoreApplication::translate("Parallax", "Usual depth (Mapsmith decides)"));
		reset->setObjectName("parallaxDepthAuto");
		QObject::connect(reset, &QAction::triggered, store, [store, materials] { store->setParallax(materials, std::nullopt, 0.0); });
	}
}

} // namespace sme::app
