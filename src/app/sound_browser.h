// sound_browser.h - Sounds tab: every sound script entry (with its files), every sound file
// and every soundscape of the game, searchable and draggable into the map
// (sound -> ambient_generic, soundscape -> env_soundscape). A click plays the sound (own
// decoder, waveform with loop point and playhead); sound script entries can be edited and
// new ones saved into the mod's own sound script; soundscapes have their own editor.

#pragma once

#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <memory>

#include <sme/assets.h>
#include <sme/audio.h>
#include <sme/soundscape.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QStackedWidget;
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

/// Waveform of the decoded sound with its loop point and the playhead; a click seeks.
class SoundWaveform : public QWidget {
	Q_OBJECT

public:
	explicit SoundWaveform(QWidget* parent = nullptr);
	void setAudio(std::shared_ptr<const DecodedAudio> audio);
	void setPlayhead(double seconds);   ///< < 0: hidden

signals:
	void seekRequested(double seconds);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	std::shared_ptr<const DecodedAudio> m_audio;
	std::vector<std::pair<float, float>> m_peaks;
	double m_playhead = -1;
};

class SoundBrowser : public QWidget {
	Q_OBJECT

public:
	enum Kind { ScriptSound = 1, FileSound, SoundscapeEntry };

	explicit SoundBrowser(Editor* editor, QWidget* parent = nullptr);
	~SoundBrowser() override;
	[[nodiscard]] QTreeWidget* tree() const { return m_tree; }
	/// The sound shown in the player (null until one was decoded).
	[[nodiscard]] std::shared_ptr<const DecodedAudio> currentAudio() const { return m_audio; }
	/// Sound script file of the mod that "Save" writes ("scripts/game_sounds_<mod>.txt").
	[[nodiscard]] QString modScriptPath() const;
	/// Soundscape file of the mod that the soundscape editor saves into ("scripts/soundscapes_<mod>.txt").
	[[nodiscard]] QString modSoundscapePath() const;
	[[nodiscard]] SoundscapeEditor* soundscapeEditor() const { return m_scapeEditor; }
	/// Starts a new soundscape in the editor.
	void newSoundscape();

signals:
	void indexed();
	void audioLoaded();

private:
	void reload();
	void fill();
	void applyFilter();
	void showItem(QTreeWidgetItem* item);
	void useItem(QTreeWidgetItem* item);
	/// Decodes `file` (below sound/) in the background, shows it and plays it if `play`.
	void load(const QString& file, bool play);
	void playCurrent();
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
	QTreeWidget* m_tree;
	QLabel* m_info;
	// Player
	QLabel* m_title;
	SoundWaveform* m_waveform;
	QPushButton* m_play;
	QCheckBox* m_loop;
	QCheckBox* m_playOnClick;
	QSlider* m_volume;
	QLabel* m_details;
	QString m_file;                                ///< Shown sound below sound/
	std::shared_ptr<const DecodedAudio> m_audio;
	int m_loadGeneration = 0;
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
