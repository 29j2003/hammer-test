// texts_panel.cpp - The Texts tab of the Project page (see texts_panel.h).

#include "texts_panel.h"

#include "asset_browser.h"
#include "editor.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <sme/game.h>
#include <sme/text.h>

namespace sme::app {

namespace {

QString qs(const std::string& text) {
	return QString::fromStdString(text);
}

std::string ss(const QString& text) {
	return text.toStdString();
}

const QColor MISSING(92, 46, 40);

/// Caption text as rich text for the preview (colours, bold, italic, line breaks).
QString captionHtml(const CaptionLine& line) {
	QString html;
	for (const auto& run : line.runs) {
		if (run.lineBreak) html += "<br>";
		QString part = qs(run.text).toHtmlEscaped().replace('\n', "<br>");
		if (run.bold) part = "<b>" + part + "</b>";
		if (run.italic) part = "<i>" + part + "</i>";
		html += QStringLiteral("<span style='color: rgb(%1,%2,%3)'>%4</span>").arg(run.r).arg(run.g).arg(run.b).arg(part);
	}
	return html;
}

} // namespace

TextsPanel::TextsPanel(Editor* editor, AssetBrowser* assets, QWidget* parent) : QWidget(parent), m_editor(editor), m_assets(assets) {
	setObjectName("textsPanel");
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(12, 10, 12, 8);
	layout->setSpacing(8);

	// Top: which file set, search, languages, save.
	auto* top = new QHBoxLayout;
	top->setSpacing(8);
	top->addWidget(new QLabel(tr("File")));
	m_setBox = new QComboBox;
	m_setBox->setObjectName("textsSet");
	m_setBox->setMinimumWidth(220);
	m_setBox->setToolTip(tr("Localization files of the mod (resource/<name>_<language>.txt); closecaption holds the captions"));
	top->addWidget(m_setBox);
	m_create = new QPushButton(tr("+ File ..."));
	m_create->setObjectName("textsCreate");
	m_create->setToolTip(tr("Start a new localization file for the mod's own texts or for captions"));
	top->addWidget(m_create);
	m_search = new QLineEdit;
	m_search->setObjectName("textsSearch");
	m_search->setPlaceholderText(tr("Search tokens and texts ..."));
	m_search->setClearButtonEnabled(true);
	top->addWidget(m_search, 1);
	m_onlyMissing = new QCheckBox(tr("Only missing translations"));
	m_onlyMissing->setObjectName("textsOnlyMissing");
	top->addWidget(m_onlyMissing);
	m_addLanguage = new QComboBox;
	m_addLanguage->setObjectName("textsAddLanguage");
	m_addLanguage->setToolTip(tr("Add a translation: a new column, saved as its own file"));
	top->addWidget(m_addLanguage);
	layout->addLayout(top);

	// Token buttons.
	auto* buttons = new QHBoxLayout;
	buttons->setSpacing(6);
	const auto button = [&](const QString& text, const QString& name, const QString& tip) {
		auto* b = new QPushButton(text);
		b->setObjectName(name);
		b->setToolTip(tip);
		buttons->addWidget(b);
		return b;
	};
	auto* add = button(tr("+ Token"), "textsAddToken", tr("A new token; use it as #Token in menus, entities and scripts"));
	auto* rename = button(tr("Rename"), "textsRename", tr("Rename the token in every language"));
	auto* remove = button(tr("Delete"), "textsDelete", tr("Delete the token in every language"));
	m_addCaption = button(tr("+ Caption for a sound ..."), "textsAddCaption", tr("A caption for a sound script entry: the game shows it when the sound plays"));
	buttons->addStretch(1);
	m_compile = button(tr("Compile captions"), "textsCompile", tr("Writes closecaption_<language>.dat - the file the game reads (no captioncompiler needed)"));
	m_save = button(tr("Save"), "textsSave", tr("Writes the files into the mod (UTF-16, as the game expects); a .bak keeps the first old version"));
	m_save->setProperty("accent", true);
	layout->addLayout(buttons);

	// Table above, editor and preview below.
	auto* split = new QSplitter(Qt::Vertical);
	split->setChildrenCollapsible(false);
	m_table = new QTableWidget;
	m_table->setObjectName("textsTable");
	m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->verticalHeader()->setVisible(false);
	m_table->verticalHeader()->setDefaultSectionSize(26);
	m_table->setWordWrap(false);
	m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
	split->addWidget(m_table);

	auto* bottom = new QWidget;
	auto* b = new QVBoxLayout(bottom);
	b->setContentsMargins(0, 6, 0, 0);
	b->setSpacing(6);
	m_editTitle = new QLabel;
	m_editTitle->setStyleSheet("font-weight: bold;");
	b->addWidget(m_editTitle);
	m_captionTools = new QWidget;
	auto* tools = new QHBoxLayout(m_captionTools);
	tools->setContentsMargins(0, 0, 0, 0);
	tools->setSpacing(4);
	const auto tool = [&](const QString& text, const QString& tip, std::function<void()> action) {
		auto* t = new QToolButton;
		t->setText(text);
		t->setToolTip(tip);
		connect(t, &QToolButton::clicked, this, action);
		tools->addWidget(t);
	};
	tool(tr("Colour ..."), tr("<clr:r,g,b> - the text after it in this colour"), [this] {
		const QColor color = QColorDialog::getColor(QColor(255, 220, 120), this, tr("Caption colour"));
		if (color.isValid()) insertTag(QStringLiteral("<clr:%1,%2,%3>").arg(color.red()).arg(color.green()).arg(color.blue()));
	});
	tool(tr("Bold"), tr("<B>...<B> - bold"), [this] { insertTag("<B>", "<B>"); });
	tool(tr("Italic"), tr("<I>...<I> - italic"), [this] { insertTag("<I>", "<I>"); });
	tool(tr("Line break"), tr("<cr> - a new line"), [this] { insertTag("<cr>"); });
	tool(tr("Sound effect"), tr("<sfx> - only shown with full closed captions (cc_subtitles 0), not with subtitles"), [this] { insertTag("<sfx>"); });
	tool(tr("Length ..."), tr("<len:N> - how many seconds the caption stays"), [this] {
		bool ok = false;
		const double seconds = QInputDialog::getDouble(this, tr("Caption length"), tr("Seconds:"), 3.0, 0.1, 60.0, 1, &ok);
		if (ok) insertTag(QStringLiteral("<len:%1>").arg(seconds));
	});
	tools->addStretch(1);
	b->addWidget(m_captionTools);
	m_edit = new QPlainTextEdit;
	m_edit->setObjectName("textsEdit");
	m_edit->setPlaceholderText(tr("Pick a text in the table to edit it here"));
	m_edit->setMaximumHeight(90);
	b->addWidget(m_edit);
	m_preview = new QLabel;
	m_preview->setObjectName("textsPreview");
	m_preview->setTextFormat(Qt::RichText);
	m_preview->setWordWrap(true);
	m_preview->setMinimumHeight(52);
	m_preview->setAlignment(Qt::AlignCenter);
	m_preview->setStyleSheet("background: rgba(0,0,0,170); border-radius: 6px; padding: 8px 14px; font-size: 11pt;");
	b->addWidget(m_preview);
	split->addWidget(bottom);
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 1);
	layout->addWidget(split, 1);

	m_status = new QLabel;
	m_status->setObjectName("textsStatus");
	m_status->setStyleSheet("color: #9a9a9a;");
	layout->addWidget(m_status);

	connect(m_setBox, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (m_filling || index < 0) return;
		if (m_modified && QMessageBox::question(this, tr("Texts"), tr("Drop the unsaved changes of %1?").arg(qs(m_set.name))) != QMessageBox::Yes) {
			const QSignalBlocker block(m_setBox);
			m_setBox->setCurrentIndex(m_setBox->findData(qs(m_set.name)));
			return;
		}
		showSet(m_setBox->itemData(index).toString());
	});
	connect(m_search, &QLineEdit::textChanged, this, &TextsPanel::applyFilter);
	connect(m_onlyMissing, &QCheckBox::toggled, this, &TextsPanel::applyFilter);
	connect(m_addLanguage, &QComboBox::activated, this, [this](int index) {
		if (index <= 0) return;
		addLanguage(m_addLanguage->itemData(index).toString());
		m_addLanguage->setCurrentIndex(0);
	});
	connect(add, &QPushButton::clicked, this, &TextsPanel::addToken);
	connect(rename, &QPushButton::clicked, this, &TextsPanel::renameToken);
	connect(remove, &QPushButton::clicked, this, &TextsPanel::deleteToken);
	connect(m_addCaption, &QPushButton::clicked, this, &TextsPanel::addCaptionForSound);
	connect(m_save, &QPushButton::clicked, this, [this] { save(); });
	connect(m_compile, &QPushButton::clicked, this, [this] { compileCaptions(); });
	connect(m_create, &QPushButton::clicked, this, [this] {
		const auto* entry = m_editor->gameEntry();
		if (!entry) return;
		const QString mod = qs(toLower(entry->gameDir.filename().string()));
		const QStringList choices = {tr("%1 - the mod's own texts (menu, hints, entity messages)").arg(mod), tr("closecaption - captions for sounds")};
		bool ok = false;
		const QString choice = QInputDialog::getItem(this, tr("New text file"), tr("Which file?"), choices, 0, false, &ok);
		if (!ok) return;
		const QString name = choice == choices[0] ? mod : QStringLiteral("closecaption");
		if (showSet(name)) return;   // already there
		LocalizationSet set;
		set.name = ss(name);
		set.languages["english"] = localizationPath(entry->gameDir, set.name, "english");
		m_sets.push_back(set);
		fillSetBox();
		m_set = set;
		m_files.clear();
		m_files["english"] = LocalizationFile{};
		{
			const QSignalBlocker block(m_setBox);
			m_setBox->setCurrentIndex(m_setBox->findData(name));
		}
		rebuildTable();
		setModified(true);
		setStatus(tr("New file %1 - it is written when you save.").arg(QDir(fromPath(entry->gameDir)).relativeFilePath(fromPath(set.languages["english"]))));
	});
	connect(m_table, &QTableWidget::currentCellChanged, this, [this] { updateEditor(); });
	connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
		if (m_filling || item->column() == 0 || item->column() > int(m_files.size())) return;
		const QString token = m_table->item(item->row(), 0)->text();
		const QString language = m_table->horizontalHeaderItem(item->column())->data(Qt::UserRole).toString();
		if (item->text() == text(token, language)) return;   // only the look changed (missing marks)
		setText(token, language, item->text());
	});
	connect(m_edit, &QPlainTextEdit::textChanged, this, [this] {
		if (m_filling) return;
		const QString token = currentToken();
		const QString language = currentLanguage();
		if (token.isEmpty() || language.isEmpty()) return;
		setText(token, language, m_edit->toPlainText());
		updatePreview();
	});
	connect(editor, &Editor::gameChanged, this, &TextsPanel::reload);
	reload();
}

void TextsPanel::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
	reloadIfUnchanged();   // the Menu tab or another program may have written the files
}

void TextsPanel::reloadIfUnchanged() {
	if (!m_modified) reload();
}

void TextsPanel::reload() {
	const auto* entry = m_editor->gameEntry();
	const QString shown = qs(m_set.name);
	m_sets = entry ? findLocalizationSets(entry->gameDir) : std::vector<LocalizationSet>{};
	fillSetBox();
	m_set = {};
	m_files.clear();
	setModified(false);
	setEnabled(entry != nullptr);
	if (!m_sets.empty() && !(shown.size() && showSet(shown))) {
		showSet(qs(m_sets.front().name));
	} else if (m_sets.empty()) {
		rebuildTable();
		setStatus(entry ? tr("The mod has no localization files yet - + File ... starts one.") : tr("No game chosen"));
	}
}

void TextsPanel::fillSetBox() {
	m_filling = true;
	m_setBox->clear();
	for (const auto& set : m_sets) {
		const QString label = set.captions() ? tr("%1  (captions, %2 languages)").arg(qs(set.name)).arg(set.languages.size())
		                                     : tr("%1  (%2 languages)").arg(qs(set.name)).arg(set.languages.size());
		m_setBox->addItem(label, qs(set.name));
	}
	m_filling = false;
}

bool TextsPanel::showSet(const QString& name) {
	const auto it = std::find_if(m_sets.begin(), m_sets.end(), [&](const auto& set) { return iequals(set.name, ss(name)); });
	if (it == m_sets.end()) return false;
	m_set = *it;
	m_files.clear();
	for (const auto& [language, path] : m_set.languages) {
		auto file = loadLocalizationFile(path);
		m_files[language] = file ? std::move(*file) : LocalizationFile{};
	}
	{
		const QSignalBlocker block(m_setBox);
		m_setBox->setCurrentIndex(m_setBox->findData(qs(m_set.name)));
	}
	setModified(false);
	rebuildTable();
	int tokens = 0;
	if (const auto english = m_files.find("english"); english != m_files.end()) tokens = int(english->second.tokens.size());
	setStatus(tr("%1: %2 tokens in %3 languages").arg(qs(m_set.name)).arg(tokens).arg(m_files.size()));
	return true;
}

QStringList TextsPanel::languages() const {
	QStringList out;
	if (m_files.count("english")) out << "english";
	for (const auto& [language, file] : m_files) {
		if (language != "english") out << qs(language);
	}
	return out;
}

QStringList TextsPanel::tokenOrder() const {
	QStringList order;
	QSet<QString> seen;
	for (const auto& language : languages()) {
		for (const auto& token : m_files.at(ss(language)).tokens) {
			const QString name = qs(token.token);
			if (!seen.contains(name.toLower())) {
				seen.insert(name.toLower());
				order << name;
			}
		}
	}
	return order;
}

bool TextsPanel::isCaptions() const {
	return m_set.captions();
}

void TextsPanel::rebuildTable() {
	m_filling = true;
	const QStringList langs = languages();
	const QString mod = m_editor->gameEntry() ? qs(m_editor->gameEntry()->gameDir.filename().string()) : QString();
	m_table->clear();
	m_table->setColumnCount(langs.size() + 2);
	auto* tokenHeader = new QTableWidgetItem(tr("Token"));
	m_table->setHorizontalHeaderItem(0, tokenHeader);
	for (int i = 0; i < langs.size(); ++i) {
		auto* header = new QTableWidgetItem(qs(languageDisplayName(ss(langs[i]))));
		header->setData(Qt::UserRole, langs[i]);
		m_table->setHorizontalHeaderItem(i + 1, header);
	}
	m_table->setHorizontalHeaderItem(langs.size() + 1, new QTableWidgetItem(tr("Used by")));
	const QStringList tokens = tokenOrder();
	m_table->setRowCount(tokens.size());
	for (int row = 0; row < tokens.size(); ++row) {
		auto* name = new QTableWidgetItem(tokens[row]);
		name->setFlags(name->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(row, 0, name);
		for (int i = 0; i < langs.size(); ++i) {
			const auto& file = m_files.at(ss(langs[i]));
			const int index = file.find(ss(tokens[row]));
			auto* cell = new QTableWidgetItem(index >= 0 ? qs(file.tokens[std::size_t(index)].text) : QString());
			if (index >= 0 && !file.tokens[std::size_t(index)].condition.empty()) cell->setToolTip(tr("Only %1").arg(qs(file.tokens[std::size_t(index)].condition)));
			m_table->setItem(row, i + 1, cell);
		}
		auto* use = new QTableWidgetItem(qs(menuUseOfToken(ss(tokens[row]), ss(mod))));
		use->setFlags(use->flags() & ~Qt::ItemIsEditable);
		use->setForeground(QColor(150, 150, 150));
		m_table->setItem(row, langs.size() + 1, use);
		updateRowLook(row);
	}
	// Token and "Used by" as wide as needed, the languages share the rest.
	auto* header = m_table->horizontalHeader();
	header->setStretchLastSection(false);
	header->setSectionResizeMode(0, QHeaderView::Interactive);
	m_table->setColumnWidth(0, 280);
	for (int i = 0; i < langs.size(); ++i) header->setSectionResizeMode(i + 1, QHeaderView::Stretch);
	header->setSectionResizeMode(langs.size() + 1, QHeaderView::Interactive);
	m_table->setColumnWidth(langs.size() + 1, 200);
	// Languages still to add.
	m_addLanguage->clear();
	m_addLanguage->addItem(tr("+ Language ..."));
	for (const auto& language : sourceLanguages()) {
		if (!m_files.count(language)) m_addLanguage->addItem(qs(languageDisplayName(language)), qs(language));
	}
	m_addLanguage->setEnabled(!m_set.name.empty());
	m_compile->setVisible(isCaptions());
	m_addCaption->setVisible(isCaptions());
	m_captionTools->setVisible(isCaptions());
	m_preview->setVisible(isCaptions());
	m_filling = false;
	applyFilter();
	if (m_table->rowCount() > 0 && langs.size() > 0) m_table->setCurrentCell(0, 1);
	updateEditor();
}

void TextsPanel::updateRowLook(int row) {
	const bool filling = m_filling;
	m_filling = true;   // a new background is an itemChanged too
	const QStringList langs = languages();
	for (int i = 0; i < langs.size(); ++i) {
		auto* cell = m_table->item(row, i + 1);
		if (!cell) continue;
		const bool missing = cell->text().isEmpty() && i > 0;   // English is the source; an empty English text is allowed
		cell->setBackground(missing ? QBrush(MISSING) : QBrush());
		if (missing) cell->setToolTip(tr("Missing translation"));
	}
	m_filling = filling;
}

void TextsPanel::applyFilter() {
	const QString words = m_search->text().trimmed();
	const int langs = int(m_files.size());
	for (int row = 0; row < m_table->rowCount(); ++row) {
		bool show = true;
		if (!words.isEmpty()) {
			show = false;
			for (int column = 0; column <= langs && !show; ++column) {
				if (auto* item = m_table->item(row, column); item && item->text().contains(words, Qt::CaseInsensitive)) show = true;
			}
		}
		if (show && m_onlyMissing->isChecked()) {
			bool missing = false;
			for (int column = 2; column <= langs; ++column) {
				if (auto* item = m_table->item(row, column); item && item->text().isEmpty()) missing = true;
			}
			show = missing;
		}
		m_table->setRowHidden(row, !show);
	}
}

QString TextsPanel::currentToken() const {
	const int row = m_table->currentRow();
	auto* item = row >= 0 ? m_table->item(row, 0) : nullptr;
	return item ? item->text() : QString();
}

QString TextsPanel::currentLanguage() const {
	const int column = m_table->currentColumn();
	const QStringList langs = languages();
	if (column >= 1 && column <= langs.size()) return langs[column - 1];
	return langs.isEmpty() ? QString() : langs.front();
}

void TextsPanel::updateEditor() {
	m_filling = true;
	const QString token = currentToken();
	const QString language = currentLanguage();
	m_edit->setEnabled(!token.isEmpty() && !language.isEmpty());
	m_editTitle->setText(token.isEmpty() ? tr("No text picked") : tr("%1 - %2").arg(token, qs(languageDisplayName(ss(language)))));
	m_edit->setPlainText(token.isEmpty() ? QString() : text(token, language));
	m_filling = false;
	updatePreview();
}

void TextsPanel::updatePreview() {
	if (!isCaptions()) return;
	const auto line = parseCaption(ss(m_edit->toPlainText()));
	QString html = captionHtml(line);
	QStringList notes;
	if (line.soundEffect) notes << tr("sound effect: only with full closed captions");
	if (line.length) notes << tr("stays %1 s").arg(*line.length);
	if (line.delay) notes << tr("waits %1 s").arg(*line.delay);
	for (const auto& tag : line.unknownTags) notes << tr("unknown tag <%1>").arg(qs(tag));
	if (!notes.isEmpty()) html += QStringLiteral("<div style='color:#9a9a9a; font-size:8pt'>%1</div>").arg(notes.join(QStringLiteral(" · ")).toHtmlEscaped());
	m_preview->setText(html.isEmpty() ? QStringLiteral("<span style='color:#777'>%1</span>").arg(tr("Caption preview")) : html);
}

QString TextsPanel::text(const QString& token, const QString& language) const {
	const auto it = m_files.find(ss(language.toLower()));
	return it == m_files.end() ? QString() : qs(it->second.get(ss(token)));
}

void TextsPanel::setText(const QString& token, const QString& language, const QString& value) {
	auto it = m_files.find(ss(language.toLower()));
	if (it == m_files.end() || token.isEmpty()) return;
	if (it->second.find(ss(token)) >= 0 && it->second.get(ss(token)) == ss(value)) return;
	it->second.set(ss(token), ss(value));
	setModified(true);
	// The table follows edits made below it (and the other way round).
	for (int row = 0; row < m_table->rowCount(); ++row) {
		if (m_table->item(row, 0)->text().compare(token, Qt::CaseInsensitive) != 0) continue;
		const int column = languages().indexOf(language.toLower()) + 1;
		if (auto* cell = m_table->item(row, column); cell && cell->text() != value) {
			m_filling = true;
			cell->setText(value);
			m_filling = false;
		}
		updateRowLook(row);
		if (row == m_table->currentRow() && column == m_table->currentColumn() && m_edit->toPlainText() != value) updateEditor();
		return;
	}
	rebuildTable();   // a new token
}

void TextsPanel::addLanguage(const QString& language) {
	const std::string name = toLower(ss(language));
	const auto* entry = m_editor->gameEntry();
	if (!entry || m_set.name.empty() || m_files.count(name)) return;
	LocalizationFile file;
	file.language = languageDisplayName(name);
	m_files[name] = file;
	m_set.languages[name] = localizationPath(entry->gameDir, m_set.name, name);
	setModified(true);
	rebuildTable();
	setStatus(tr("%1 added - the missing translations are marked.").arg(qs(file.language)));
}

void TextsPanel::insertTag(const QString& before, const QString& after) {
	auto cursor = m_edit->textCursor();
	const QString selected = cursor.selectedText();
	cursor.insertText(before + selected + after);
	m_edit->setFocus();
}

void TextsPanel::addToken() {
	if (m_set.name.empty()) return;
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("New token"), tr("Token (used as #Token):"), QLineEdit::Normal, QString(), &ok).trimmed().remove('#');
	if (!ok || name.isEmpty()) return;
	if (m_files["english"].find(ss(name)) >= 0) {
		setStatus(tr("%1 is already there.").arg(name), true);
		return;
	}
	m_files["english"].set(ss(name), {});
	setModified(true);
	rebuildTable();
	for (int row = 0; row < m_table->rowCount(); ++row) {
		if (m_table->item(row, 0)->text() == name) m_table->setCurrentCell(row, 1);
	}
	m_edit->setFocus();
}

void TextsPanel::renameToken() {
	const QString token = currentToken();
	if (token.isEmpty()) return;
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("Rename token"), tr("New name:"), QLineEdit::Normal, token, &ok).trimmed().remove('#');
	if (!ok || name.isEmpty() || name == token) return;
	for (auto& [language, file] : m_files) {
		if (file.find(ss(name)) >= 0 && !iequals(ss(name), ss(token))) {
			setStatus(tr("%1 is already there.").arg(name), true);
			return;
		}
	}
	for (auto& [language, file] : m_files) file.rename(ss(token), ss(name));
	setModified(true);
	rebuildTable();
}

void TextsPanel::deleteToken() {
	const QString token = currentToken();
	if (token.isEmpty()) return;
	for (auto& [language, file] : m_files) file.remove(ss(token));
	setModified(true);
	rebuildTable();
}

void TextsPanel::addCaptionForSound() {
	QStringList scripts;
	if (m_assets) {
		for (const auto& name : m_assets->soundNames()) {
			// Sound script entries ("NPC_Citizen.Hello"), not files ("vo/npc/...wav").
			if (!name.contains('/') && !name.endsWith(".wav", Qt::CaseInsensitive) && !name.endsWith(".mp3", Qt::CaseInsensitive) && !name.endsWith(".ogg", Qt::CaseInsensitive)) scripts << name;
		}
	}
	scripts.sort(Qt::CaseInsensitive);
	bool ok = false;
	const QString name = QInputDialog::getItem(this, tr("Caption for a sound"), tr("Sound script entry:"), scripts, 0, true, &ok).trimmed();
	if (!ok || name.isEmpty()) return;
	if (m_files["english"].find(ss(name)) < 0) {
		m_files["english"].set(ss(name), {});
		setModified(true);
		rebuildTable();
	}
	for (int row = 0; row < m_table->rowCount(); ++row) {
		if (m_table->item(row, 0)->text().compare(name, Qt::CaseInsensitive) == 0) m_table->setCurrentCell(row, 1);
	}
	m_edit->setFocus();
}

bool TextsPanel::save() {
	const auto* entry = m_editor->gameEntry();
	if (!entry || m_set.name.empty()) return false;
	QStringList written;
	for (const auto& [language, file] : m_files) {
		// Always the mod's own folder: never a file of the game or a VPK.
		const auto path = localizationPath(entry->gameDir, m_set.name, language);
		auto out = file;
		if (out.language.empty()) out.language = languageDisplayName(language);
		std::string error;
		if (!saveLocalizationFile(path, out, &error)) {
			setStatus(qs(error), true);
			return false;
		}
		m_set.languages[language] = path;
		written << QDir(fromPath(entry->gameDir)).relativeFilePath(fromPath(path));
	}
	setModified(false);
	// The set list knows the new files now.
	const std::string name = m_set.name;
	m_sets = findLocalizationSets(entry->gameDir);
	fillSetBox();
	{
		const QSignalBlocker block(m_setBox);
		m_setBox->setCurrentIndex(m_setBox->findData(qs(name)));
	}
	setStatus(tr("Saved: %1").arg(written.join(", ")));
	emit saved(qs(name));
	return true;
}

bool TextsPanel::compileCaptions() {
	if (!isCaptions() || !save()) return false;
	QStringList done;
	for (const auto& [language, path] : m_set.languages) {
		std::string error;
		int count = 0;
		if (!compileCaptionFile(path, &error, &count)) {
			setStatus(qs(error), true);
			return false;
		}
		done << tr("%1 (%2 captions)").arg(qs(languageDisplayName(language))).arg(count);
	}
	setStatus(tr("Compiled: %1 - the game reads the .dat files next to the .txt").arg(done.join(", ")));
	return true;
}

void TextsPanel::setModified(bool modified) {
	m_modified = modified;
	m_save->setEnabled(modified);
	m_save->setText(modified ? tr("Save *") : tr("Save"));
}

void TextsPanel::setStatus(const QString& text, bool error) {
	m_status->setText(text);
	m_status->setStyleSheet(error ? "color: #ff8a70;" : "color: #9a9a9a;");
}

} // namespace sme::app
