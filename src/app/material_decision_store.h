// material_decision_store.h - The open mod's material decisions (<mod>/cfg/mapsmith_materials.txt,
// sme/material_decisions.h) for the whole editor: read when a game opens and again when the file
// changes on disk (edited by hand), changed through Mapsmith's controls (Parallax: Auto / On / Off,
// Depth; Bump map: Auto / Generate / Off, Strength; Load material decisions ...), and announced so Game enhancements, the building builder
// and the material editor follow.

#pragma once

#include <QFileSystemWatcher>
#include <QMenu>
#include <QObject>
#include <QStringList>

#include <functional>
#include <optional>

#include <sme/material_decisions.h>

namespace sme::app {

class Editor;

class MaterialDecisionStore : public QObject {
	Q_OBJECT

public:
	explicit MaterialDecisionStore(Editor* editor);

	/// The decisions of the open mod (empty without a game).
	[[nodiscard]] const MaterialDecisions& decisions() const { return m_decisions; }
	/// What the file says about one material.
	[[nodiscard]] MaterialDecision lookup(const QString& material) const;
	/// The file's path ("" without a game).
	[[nodiscard]] QString filePath() const;
	/// Changes the decisions and writes the file (false: no game or it could not be written).
	bool change(const std::function<void(MaterialDecisions&)>& edit);
	/// Parallax for these materials: a choice (nullopt: keep) and a depth (nullopt: keep; 0: remove the
	/// line's depth). Auto without depth removes the parallax keys.
	bool setParallax(const QStringList& materials, std::optional<ParallaxChoice> choice, std::optional<double> depth = std::nullopt);
	/// Bump map for these materials: a choice (nullopt: keep; Auto removes the key) and a strength
	/// (nullopt: keep; 1 or less than 0.1: remove the line's strength).
	bool setBump(const QStringList& materials, std::optional<BumpChoice> choice, std::optional<double> strength = std::nullopt);
	/// Reads the file again (after it was edited outside).
	void reload();

signals:
	/// The decisions changed (read again or changed here).
	void changed();
	/// The file has problems (line: message), after reading it.
	void problemsFound(const QStringList& problems);

private:
	void watch();
	Editor* m_editor;
	MaterialDecisions m_decisions;
	QFileSystemWatcher m_watcher;
	QString m_lastWritten;   ///< Text this store wrote last (the watcher's own change is no news)
};

/// Fills a menu with the parallax choice for these materials: Auto / On / Off (the first
/// material's choice checked), "Depth ..." and "Usual depth" - each written to the decision file.
void fillParallaxMenu(QMenu* menu, Editor* editor, const QStringList& materials);
/// The same for the generated bump map: Auto / Generate / Off and "Strength ..." / "Usual strength".
void fillBumpMenu(QMenu* menu, Editor* editor, const QStringList& materials);

} // namespace sme::app
