// class_picker.h - Picks an entity class from a search list: a button that shows the class and
// opens a popup with a search field, the classes related to the current one on top (prop_static
// -> prop_physics, prop_dynamic ...) and all others below. Typing filters, the arrow keys move,
// Enter takes the class, Esc closes. The mouse wheel never changes the class by accident.

#pragma once

#include <QPushButton>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;

namespace sme {
class EntityCatalog;
}

namespace sme::app {

/// Class picker button with its search popup (see the file comment).
class ClassPicker : public QPushButton {
	Q_OBJECT

public:
	/// `solid`: offer brush classes (else point classes).
	ClassPicker(const EntityCatalog* catalog, bool solid, QWidget* parent = nullptr);

	/// The class shown on the button (empty: several classes, `placeholder` is shown).
	void setClassName(const QString& name, const QString& placeholder = {});
	[[nodiscard]] QString className() const { return m_class; }

	/// Opens the search list (also what a click does).
	void openList();
	/// The open list (nullptr while closed), for tests.
	[[nodiscard]] QListWidget* list() const { return m_list; }
	[[nodiscard]] QLineEdit* searchField() const { return m_search; }

signals:
	/// A class was chosen in the list.
	void classChosen(const QString& name);

private:
	void fill(const QString& text);
	void choose();
	bool eventFilter(QObject* watched, QEvent* event) override;

	const EntityCatalog* m_catalog;
	bool m_solid;
	QString m_class;
	QStringList m_all;            ///< Every class of the right kind, sorted
	QWidget* m_popup = nullptr;
	QLineEdit* m_search = nullptr;
	QListWidget* m_list = nullptr;
	QLabel* m_about = nullptr;    ///< Description of the highlighted class
};

} // namespace sme::app
