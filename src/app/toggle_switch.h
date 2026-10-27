// toggle_switch.h - An on/off switch (a pill with a sliding knob) for yes/no keys and flags,
// like modern property panels. It is a QCheckBox underneath, so it
// toggles with a click or the space key and reports `toggled` like any checkbox.

#pragma once

#include <QCheckBox>

namespace sme::app {

/// On/off switch (see the file comment).
class ToggleSwitch : public QCheckBox {
	Q_OBJECT

public:
	explicit ToggleSwitch(QWidget* parent = nullptr);
	/// Size of the pill.
	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
	void paintEvent(QPaintEvent* event) override;
	[[nodiscard]] bool hitButton(const QPoint& pos) const override;
};

} // namespace sme::app
