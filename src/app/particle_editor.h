// particle_editor.h - Editing one particle system on the Particles page: its name, its own values
// and its parts (emitters, initializers, operators, renderers ...) as typed fields, parts added
// from a list of the common ones or removed, children shown. Every valid change goes to the
// preview at once (the browser shows what saving would write: write and read back); "Save to mod"
// writes it into the mod's own particle file (sme/particle_edit.h).

#pragma once

#include <QTimer>
#include <QWidget>

#include <sme/particles.h>

class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace sme::app {

class AssetBrowser;
class Editor;

/// The editor (see the file comment).
class ParticleEditor : public QWidget {
	Q_OBJECT

public:
	ParticleEditor(Editor* editor, AssetBrowser* assets, QWidget* parent = nullptr);

	/// Starts editing `system`. `note` says where it came from ("a copy of ... from the game").
	void setSystem(PcfSystem system, const QString& note);
	[[nodiscard]] const PcfSystem& system() const { return m_system; }
	/// Sets one value of the system's own values or of a part (`group` "properties", "emitters",
	/// ...; `index` of the part) from text, as the field would; false if the text does not fit.
	bool setValueText(const QString& group, int index, const QString& key, const QString& text);
	/// Adds a part from the catalog by name (tests, the "Add" buttons).
	bool addPart(const QString& group, const QString& functionName);
	void removePart(const QString& group, int index);
	/// The name field (tests).
	[[nodiscard]] QLineEdit* nameField() const { return m_name; }

signals:
	/// The system changed (a valid edit); the browser updates the preview.
	void changed();
	/// "Save to mod".
	void saveRequested();
	/// "Close" - back to looking at systems.
	void closeRequested();

private:
	void rebuild();
	[[nodiscard]] std::vector<PcfFunction>* list(const QString& group);
	[[nodiscard]] QWidget* valueRow(const QString& group, int index, const std::string& key, const PcfValue& value);
	void emitChanged();

	Editor* m_editor;
	AssetBrowser* m_assets;
	PcfSystem m_system;
	QString m_note;
	QLineEdit* m_name = nullptr;
	QLabel* m_noteLabel = nullptr;
	QWidget* m_cards = nullptr;
	QVBoxLayout* m_cardsLayout = nullptr;
	QTimer m_rebuildSoon;
};

} // namespace sme::app
