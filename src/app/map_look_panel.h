// map_look_panel.h - "Map Look" dock: the look values of mapbase-fidelity a map sets when it
// loads (drifting clouds, light shafts, colour grading, water), with sliders that change the
// lighting preview at once. Values the map does not set show the mod's own (cfg/fidelity.cfg).

#pragma once

#include <QWidget>

#include <map>
#include <string>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QToolButton;
class QVBoxLayout;

namespace sme::app {

class Editor;

class MapLookPanel : public QWidget {
	Q_OBJECT

public:
	explicit MapLookPanel(Editor* editor, QWidget* parent = nullptr);
	/// Sets one value in the map (one undo step; slider drags fold into one).
	void setValue(const std::string& cvar, double value, bool merge = false);
	/// Back to the mod's own value (removes it from the map).
	void resetValue(const std::string& cvar);

private:
	struct Row {
		QSlider* slider;
		QDoubleSpinBox* spin;
		QToolButton* reset;
		QLabel* label;
	};
	void refresh();
	void apply(std::map<std::string, std::string> values, bool merge);

	Editor* m_editor;
	QCheckBox* m_preview;
	QLabel* m_note;
	std::map<std::string, Row> m_rows;
	bool m_refreshing = false;
	bool m_dragging = false;
	bool m_dragCommitted = false;
};

} // namespace sme::app
