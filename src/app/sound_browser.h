// sound_browser.h - Sounds tab: every sound script entry (with its files), every sound file
// and every soundscape of the game, searchable, playable and draggable into the map
// (sound -> ambient_generic, soundscape -> env_soundscape).

#pragma once

#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>

#include <atomic>

#include <sme/assets.h>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace sme::app {

class Editor;
class SoundPlayer;

/// Result of the background indexing.
struct SoundIndex {
	std::vector<SoundScriptEntry> scripts;
	std::vector<Soundscape> soundscapes;
	std::vector<std::string> files;       ///< Below sound/ ("ambient/wind1.wav")
};

class SoundBrowser : public QWidget {
	Q_OBJECT

public:
	enum Kind { ScriptSound = 1, FileSound, SoundscapeEntry };

	explicit SoundBrowser(Editor* editor, QWidget* parent = nullptr);
	~SoundBrowser() override;
	[[nodiscard]] QTreeWidget* tree() const { return m_tree; }

signals:
	void indexed();

private:
	void reload();
	void fill();
	void applyFilter();
	void showItem(QTreeWidgetItem* item);
	void useItem(QTreeWidgetItem* item);
	void play(QTreeWidgetItem* item);

	Editor* m_editor;
	SoundPlayer* m_player;
	QLineEdit* m_search;
	QComboBox* m_kind;
	QPushButton* m_play;
	QTreeWidget* m_tree;
	QLabel* m_info;
	QTimer m_filterTimer;
	SoundIndex m_index;
	QFutureWatcher<SoundIndex> m_watcher;
	std::atomic<int> m_generation{0};
};

} // namespace sme::app
