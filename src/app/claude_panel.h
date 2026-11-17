// claude_panel.h - Project › Claude: switches the Claude integration on and off (claude_service.h),
// chooses what Claude may do (read only, or read and change materials; ask before each change),
// shows the fixed exchange folder, connects Claude Desktop (or shows the line for Claude Code),
// lists every call Claude made and undoes Claude's changes one by one.

#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QRadioButton;
class QTreeWidget;

namespace sme::app {

class ClaudeService;
struct ClaudeActivity;
class ToggleSwitch;

class ClaudePanel : public QWidget {
	Q_OBJECT

public:
	explicit ClaudePanel(ClaudeService* service, QWidget* parent = nullptr);

private:
	void refreshState();
	void addActivity(const ClaudeActivity& activity);
	void connectDesktop();

	ClaudeService* m_service;
	ToggleSwitch* m_enabled;
	QLabel* m_status;
	QRadioButton* m_read;
	QRadioButton* m_change;
	ToggleSwitch* m_ask;
	QLabel* m_folder;
	QTreeWidget* m_activity;
	QPushButton* m_undo;
};

} // namespace sme::app
