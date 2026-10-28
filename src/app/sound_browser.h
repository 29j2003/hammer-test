// sound_browser.h - Sounds page and dock: every sound script entry (with its files), every sound file
// and every soundscape of the game, sorted into categories (weapons, NPCs, ambient, ...) and by
// folder, searchable and draggable into the map (sound -> ambient_generic, soundscape ->
// env_soundscape). A click plays the sound (own decoder, waveform with loop point and playhead).
// The shown sound can be edited (select a range on the waveform: trim, cut, fades, gain, speed,
// sample rate, loop point, with undo) and saved as a WAV into the mod; sound script entries can be
// edited and saved into the mod's own sound script; soundscapes have their own editor.

#pragma once

#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <memory>

#include <sme/assets.h>
#include <sme/audio.h>
#include <sme/sound_edit.h>
#include <sme/soundscape.h>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace sme::app {

class Editor;
class SoundPlayer;
class SoundscapeEditor;

/// Result of the background indexing.
struct SoundIndex {
	std::vector<SoundScriptEntry> scripts;
	std::vector<Soundscape> soundscapes;
	std::vector<std::string> files;       ///< Below sound/ ("ambient/wind1.wav")
};

/// Waveform of the decoded sound with its loop point, the playhead and a selected range: a click
/// plays from there, dragging selects frames (Shift extends the selection).
class SoundWaveform : public QWidget {
	Q_OBJECT

public:
	explicit SoundWaveform(QWidget* parent = nullptr);
	/// Sound to draw (null clears). `keepSelection` keeps the selected range (clamped) after an edit.
	void setAudio(std::shared_ptr<const DecodedAudio> audio, bool keepSelection = false);
	void setPlayhead(double seconds);   ///< < 0: hidden
	/// Selected frames (empty: nothing selected, the tools work on the whole sound).
	[[nodiscard]] FrameRange selection() const { return m_selection; }
	void setSelection(FrameRange range);

signals:
	/// Click in the waveform: play from there.
	void seekRequested(double seconds);
	void selectionChanged();

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	[[nodiscard]] std::size_t frameAt(double x) const;
	[[nodiscard]] double xOf(std::size_t frame) const;

	std::shared_ptr<const DecodedAudio> m_audio;
	std::vector<std::pair<float, float>> m_peaks;
	double m_playhead = -1;
	FrameRange m_selection;
	std::size_t m_anchor = 0;     ///< Frame where the drag started
	double m_pressX = -1;         ///< Mouse x at the press (-1: no drag)
	bool m_dragging = false;
};

/// The Sounds page and dock (see the file comment).
class SoundBrowser : public QWidget {
	Q_OBJECT

public:
	enum Kind { ScriptSound = 1, FileSound, SoundscapeEntry };
	/// Category list entries besides the sound categories (their index into SOUND_CATEGORIES).
	enum { AllCategories = -1, SoundscapesCategory = 100 };

	explicit SoundBrowser(Editor* editor, QWidget* parent = nullptr);
	~SoundBrowser() override;
	/// The tree: one branch per category, inside it folders with script sounds and sound files.
	[[nodiscard]] QTreeWidget* tree() const { return m_tree; }
	/// The category list beside it ("All sounds", Weapons, ..., Soundscapes).
	[[nodiscard]] QListWidget* categories() const { return m_categories; }
	[[nodiscard]] SoundWaveform* waveform() const { return m_waveform; }
	/// Shows the item with this name (switching to its category) and makes it current; null when
	/// there is none. `kind` 0 accepts any kind.
	QTreeWidgetItem* reveal(const QString& name, int kind = 0);
	/// The sound shown in the player, with the edits made to it (null until one was decoded).
	[[nodiscard]] std::shared_ptr<const DecodedAudio> currentAudio() const { return m_audio; }
	/// True when the shown sound has edits that are not saved.
	[[nodiscard]] bool edited() const { return !m_undo.empty() && !m_savedAfterEdit; }
	/// Applies an edit to the shown sound (on the waveform's selection) with undo.
	void applyEdit(const QString& what, const std::function<DecodedAudio(const DecodedAudio&, FrameRange)>& edit);
	void undoEdit();
	void redoEdit();
	/// Writes the shown (edited) sound as a 16-bit WAV to sound/<relative> in the mod (never into
	/// the game; an existing file of the mod only after asking unless `confirmed`). With
	/// `scriptEntry` it also goes into the sound script entry form and is saved there.
	bool saveEdited(const QString& relative, bool scriptEntry, bool confirmed = false);
	/// Sound script file of the mod that "Save" writes ("scripts/game_sounds_<mod>.txt").
	[[nodiscard]] QString modScriptPath() const;
	/// Soundscape file of the mod that the soundscape editor saves into ("scripts/soundscapes_<mod>.txt").
	[[nodiscard]] QString modSoundscapePath() const;
	[[nodiscard]] SoundscapeEditor* soundscapeEditor() const { return m_scapeEditor; }
	/// Starts a new soundscape in the editor.
	void newSoundscape();

signals:
	/// Background indexing finished / a sound was decoded and shown.
	void indexed();
	void audioLoaded();
	/// The dock's "Edit this sound" button: the Sounds page should be shown (it has room for the tools).
	void pageRequested();
	/// "Import ...": bring the mapper's own files into the mod (ImportDialog).
	void importRequested();

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	void reload();
	void fill();
	void fillCategories();
	void applyFilter();
	/// Category of the selected entry in the list (AllCategories, SoundscapesCategory or an index).
	[[nodiscard]] int selectedCategory() const;
	void showItem(QTreeWidgetItem* item);
	void useItem(QTreeWidgetItem* item);
	/// Decodes `file` (below sound/) in the background, shows it and plays it if `play`.
	void load(const QString& file, bool play);
	void playCurrent();
	void playSelection();
	/// Shows `audio` as the current sound (after an edit, undo or redo) and updates the labels.
	void showEdited(std::shared_ptr<const DecodedAudio> audio);
	void updateEditState();
	[[nodiscard]] QString describe(const DecodedAudio& audio) const;
	void editEntry(const SoundScriptEntry& entry);
	void saveEntry();
	void editSoundscape(const Soundscape& scape);
	void saveSoundscape(const SoundscapeScript& scape);
	void deleteSoundscape(const QString& name);
	/// Text of a file in the mod's folder ("" if missing) / writes one (folders made as needed).
	[[nodiscard]] std::string readModFile(const QString& relative) const;
	bool writeModFile(const QString& relative, const std::string& text);

	Editor* m_editor;
	SoundPlayer* m_player;
	QLineEdit* m_search;
	QComboBox* m_kind;
	QListWidget* m_categories;
	QTreeWidget* m_tree;
	QLabel* m_info;
	// Player
	QSplitter* m_rightSplit;
	QLabel* m_title;
	SoundWaveform* m_waveform;
	QPushButton* m_play;
	QPushButton* m_playSelection;
	QCheckBox* m_loop;
	QCheckBox* m_playOnClick;
	QSlider* m_volume;
	QLabel* m_details;
	QString m_file;                                ///< Shown sound below sound/
	std::shared_ptr<const DecodedAudio> m_audio;
	int m_loadGeneration = 0;
	double m_playOffset = 0;                       ///< Seconds where the playing copy starts (selection)
	bool m_firstFill = true;
	// Editing the shown sound
	QWidget* m_tools;                              ///< Hidden in the low dock (see resizeEvent)
	QPushButton* m_editHint;                       ///< Shown instead: leads to the Sounds page
	QLabel* m_selectionLabel;
	QToolButton* m_undoButton;
	QToolButton* m_redoButton;
	QDoubleSpinBox* m_gain;
	QSpinBox* m_speed;
	QComboBox* m_rate;
	QLineEdit* m_saveName;
	QCheckBox* m_saveScript;
	QPushButton* m_saveWav;
	std::vector<std::shared_ptr<const DecodedAudio>> m_undo, m_redo;
	bool m_savedAfterEdit = false;
	// Sound script editor
	QLineEdit* m_entryName;
	QComboBox* m_channel;
	QLineEdit* m_entryVolume;
	QLineEdit* m_pitch;
	QComboBox* m_soundlevel;
	QListWidget* m_waves;
	QPushButton* m_save;
	// Right column: the sound script entry or the soundscape editor
	QStackedWidget* m_editors;
	QWidget* m_entryBox;
	SoundscapeEditor* m_scapeEditor;
	QString m_reselect;                            ///< Soundscape to select again after re-reading

	QTimer m_filterTimer;
	SoundIndex m_index;
	QFutureWatcher<SoundIndex> m_watcher;
	std::atomic<int> m_generation{0};
};

} // namespace sme::app
