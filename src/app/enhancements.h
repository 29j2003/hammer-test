// enhancements.h - "Game enhancements": the extra effects of the mod's graphics add-on
// (mapbase-fidelity: liquid in bottles, sway in the wind, water with sun glint and foam) on
// every material they are made for. GameEnhancements keeps the mod's materials in line with
// its settings (<mod>/cfg/mapsmith_enhancements.txt), in the background, whenever a mod with
// the add-on is opened or a switch changes (rules and files: sme/fidelity_effects.h).
// EnhancementSwitches shows the switches of one model (Object Properties, Models page);
// EnhancementsSection is the mod-wide place (Project > Overview).

#pragma once

#include <QFutureWatcher>
#include <QGroupBox>
#include <QStringList>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <sme/fidelity_effects.h>

class QLabel;
class QPushButton;
class QTreeWidget;
class QVBoxLayout;

namespace sme::app {

class Editor;
class ToggleSwitch;

/// Keeps the mod's enhancement materials in line with its settings (see the file comment).
class GameEnhancements : public QObject {
	Q_OBJECT

public:
	explicit GameEnhancements(Editor* editor);
	~GameEnhancements() override;

	/// A game with mapbase-fidelity is open: its effects are made.
	[[nodiscard]] bool available() const;
	/// The first look at the game's materials is done (which ones get an effect).
	[[nodiscard]] bool ready() const { return m_ready; }
	/// Materials are being written.
	[[nodiscard]] bool busy() const { return m_running; }
	[[nodiscard]] const EnhancementSettings& settings() const { return m_settings; }

	/// Switches an effect on or off for the whole mod.
	void setEffectEnabled(GameEffect effect, bool on);
	/// Leaves a model ("models/x/y.mdl") or a material out of an effect, or takes it back.
	void setExcluded(GameEffect effect, const QString& item, bool excluded);

	/// The effects made for some material of this model ("models/x/y.mdl").
	[[nodiscard]] std::vector<GameEffect> effectsFor(const QString& model) const;
	/// The model is not left out itself, but a material of it is (shared with a left-out model).
	[[nodiscard]] bool sharesExcludedMaterial(GameEffect effect, const QString& model) const;
	/// Materials the effect is made for, and how many of them have it now.
	[[nodiscard]] int candidateCount(GameEffect effect) const;
	[[nodiscard]] int activeCount(GameEffect effect) const;
	/// Materials the mod has its own version of (never touched).
	[[nodiscard]] const QStringList& keptOwn() const { return m_keptOwn; }
	/// One line about the last run ("Up to date ...", "Writing materials ...").
	[[nodiscard]] QString status() const;

	/// Brings the mod's materials in line with the settings (in the background).
	void sync();

signals:
	/// Settings, counts or status changed.
	void changed();
	/// These materials were written (the views read them again).
	void materialsChanged(const QStringList& names);

private:
	struct Result {
		int generation = 0;
		bool scanned = false;   ///< `candidates` is new
		EffectCandidates candidates;
		std::map<GameEffect, std::set<std::string>> excluded;
		EnhancementPlan plan;
		QStringList written;
		QStringList failed;
	};
	void gameChanged();
	void finished();
	void save();
	[[nodiscard]] std::vector<std::string> materialsOf(const QString& model) const;

	Editor* m_editor;
	std::filesystem::path m_modDir;
	EnhancementSettings m_settings;
	EffectCandidates m_candidates;
	std::map<GameEffect, std::set<std::string>> m_candidateSets;
	std::map<GameEffect, std::set<std::string>> m_excluded;   ///< Materials left out, per effect
	std::map<GameEffect, int> m_active;
	mutable std::map<QString, std::vector<std::string>> m_modelMaterials;
	QStringList m_keptOwn;
	QStringList m_failed;
	int m_lastWritten = 0;
	bool m_ready = false;
	bool m_again = false;
	bool m_running = false;
	int m_generation = 0;   ///< Results of an older game are dropped
	QFutureWatcher<Result> m_watcher;
};

/// The switches of one model: one per effect made for its materials. Hidden when none applies
/// or the mod has no mapbase-fidelity.
class EnhancementSwitches : public QGroupBox {
	Q_OBJECT

public:
	explicit EnhancementSwitches(GameEnhancements* enhancements, QWidget* parent = nullptr);
	/// The model shown ("models/x/y.mdl", empty = none).
	void setModel(const QString& model);
	[[nodiscard]] QString model() const { return m_model; }

private:
	void refresh();

	GameEnhancements* m_enhancements;
	QString m_model;
	QVBoxLayout* m_rows;
};

/// Project > Overview: the mod-wide switches, the list of what is left out, the status.
class EnhancementsSection : public QGroupBox {
	Q_OBJECT

public:
	explicit EnhancementsSection(GameEnhancements* enhancements, QWidget* parent = nullptr);

private:
	void refresh();

	GameEnhancements* m_enhancements;
	QLabel* m_intro;
	QWidget* m_body;
	std::map<GameEffect, ToggleSwitch*> m_switches;
	std::map<GameEffect, QLabel*> m_counts;
	QTreeWidget* m_excluded;
	QPushButton* m_remove;
	QLabel* m_status;
};

} // namespace sme::app
