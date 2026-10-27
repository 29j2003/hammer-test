// soundscape_editor.h - Soundscape editor of the Sounds page: name, room effect
// (DSP), fade-in and the rules as cards - a looping sound, random sounds every few seconds,
// another soundscape inside this one. Each sound can be heard right there; Save puts the
// soundscape into the mod's own script (the browser does the writing).

#pragma once

#include <QStringList>
#include <QWidget>

#include <functional>

#include <sme/soundscape.h>

class QComboBox;
class QCompleter;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace sme::app {

class SoundscapeEditor : public QWidget {
	Q_OBJECT

public:
	explicit SoundscapeEditor(QWidget* parent = nullptr);

	/// Shows a soundscape; `file` is where it was read from, `inMod` whether that file is the mod's own.
	void setSoundscape(const SoundscapeScript& scape, const QString& file, bool inMod);
	/// An empty soundscape with a name to change, ready for rules.
	void newSoundscape(const QString& suggestedName);
	/// The soundscape as edited so far.
	[[nodiscard]] const SoundscapeScript& soundscape() const { return m_scape; }
	/// Names offered for "soundscape inside" rules.
	void setSoundscapeNames(const QStringList& names);
	/// Sound files (below sound/) offered while typing a sound.
	void setSoundFiles(const QStringList& files);
	/// The sound the browser shows right now ("+ Shown" adds it).
	void setShownFile(std::function<QString()> shown) { m_shownFile = std::move(shown); }
	void setStatus(const QString& text);
	[[nodiscard]] QString status() const;

signals:
	void previewRequested(const QString& file);
	void saveRequested(const sme::SoundscapeScript& scape);
	void deleteRequested(const QString& name);

private:
	void rebuildRules();
	QWidget* makeRuleCard(int index);
	void addRule(SoundscapeRule::Kind kind);

	SoundscapeScript m_scape;
	QStringList m_names;
	QCompleter* m_fileCompleter;
	std::function<QString()> m_shownFile;
	QLineEdit* m_name;
	QComboBox* m_dsp;
	QLineEdit* m_fade;
	QLabel* m_source;
	QVBoxLayout* m_rules;
	QLabel* m_status;
	QPushButton* m_save;
	QPushButton* m_delete;
};

} // namespace sme::app
