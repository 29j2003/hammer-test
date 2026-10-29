// menu_editor.cpp - The mod's main menu editor (see menu_editor.h).

#include "menu_editor.h"

#include "lazy_refresh.h"
#include "editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <cstring>

#include <sme/game.h>
#include <sme/text.h>
#include <sme/texture.h>

namespace sme::app {

namespace {

QString qs(const std::string& text) {
	return QString::fromStdString(text);
}

QImage toQImage(const ImageRgba& image) {
	QImage out(image.width, image.height, QImage::Format_RGBA8888);
	for (int y = 0; y < image.height; ++y) {
		std::memcpy(out.scanLine(y), image.pixels.data() + static_cast<std::size_t>(y) * image.width * 4, static_cast<std::size_t>(image.width) * 4);
	}
	return out;
}

/// Small flat button for the cards (move, remove).
QToolButton* cardButton(const QString& text, const QString& tip, const QString& name) {
	auto* b = new QToolButton;
	b->setText(text);
	b->setToolTip(tip);
	b->setObjectName(name);
	b->setAutoRaise(true);
	return b;
}

/// A section header in small capitals with a line under it.
QLabel* sectionHeader(const QString& text) {
	auto* label = new QLabel(text.toUpper());
	label->setStyleSheet("color: #9a9a9a; font-size: 8pt; font-weight: 700; letter-spacing: 1px; padding: 10px 0 2px 0;");
	return label;
}

} // namespace

// --- Preview -------------------------------------------------------------------------------

MenuPreview::MenuPreview(QWidget* parent) : QWidget(parent) {
	setObjectName("menuPreview");
	setMinimumSize(320, 200);
}

void MenuPreview::setContent(const QString& title, const QString& title2, const QStringList& entries, const QImage& background) {
	m_title = title;
	m_title2 = title2;
	m_entries = entries;
	m_background = background;
	update();
}

void MenuPreview::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	// A 16:9 screen in the middle of the widget.
	QRectF screen = rect();
	const double aspect = 16.0 / 9.0;
	if (screen.width() / screen.height() > aspect) {
		screen.setWidth(screen.height() * aspect);
	} else {
		screen.setHeight(screen.width() / aspect);
	}
	screen.moveCenter(QRectF(rect()).center());
	p.fillRect(rect(), QColor(24, 24, 24));
	if (!m_background.isNull()) {
		p.drawImage(screen, m_background);
	} else {
		QLinearGradient sky(screen.topLeft(), screen.bottomLeft());
		sky.setColorAt(0, QColor(46, 52, 60));
		sky.setColorAt(1, QColor(14, 15, 18));
		p.fillRect(screen, sky);
	}
	// The game darkens the left side a little behind the menu; so does the mock.
	QLinearGradient shade(screen.topLeft(), screen.topRight());
	shade.setColorAt(0, QColor(0, 0, 0, 120));
	shade.setColorAt(0.55, QColor(0, 0, 0, 0));
	p.fillRect(screen, shade);

	const double unit = screen.height() / 480.0;   // the game lays out its menu for 640x480
	const double left = screen.left() + 64 * unit;
	double y = screen.top() + screen.height() * 0.40;
	QFont titleFont = font();
	titleFont.setPixelSize(std::max(10, int(30 * unit)));
	titleFont.setBold(true);
	p.setFont(titleFont);
	p.setPen(QColor(255, 255, 255));
	if (!m_title.isEmpty()) {
		p.drawText(QPointF(left, y), m_title);
		y += 30 * unit;
	}
	if (!m_title2.isEmpty()) {
		p.drawText(QPointF(left, y), m_title2);
		y += 30 * unit;
	}
	y += 14 * unit;
	QFont itemFont = font();
	itemFont.setPixelSize(std::max(8, int(14 * unit)));
	p.setFont(itemFont);
	for (const auto& entry : m_entries) {
		if (entry.isEmpty()) {
			y += 10 * unit;   // the game shows an empty label as a gap
			continue;
		}
		p.setPen(QColor(230, 230, 230));
		p.drawText(QPointF(left, y), entry);
		y += 20 * unit;
	}
	p.setPen(QPen(QColor(70, 70, 70), 1));
	p.drawRect(screen.adjusted(0.5, 0.5, -0.5, -0.5));
}

// --- Editor --------------------------------------------------------------------------------

MenuEditor::MenuEditor(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	setObjectName("menuEditor");
	auto* outer = new QHBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto* splitter = new QSplitter(Qt::Horizontal);
	outer->addWidget(splitter);

	// Left: the sections in a scroll area.
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* column = new QWidget;
	auto* v = new QVBoxLayout(column);
	v->setContentsMargins(12, 8, 12, 12);
	v->setSpacing(6);
	auto* intro = new QLabel(tr("What players see when your mod starts: its title, the chapters of New Game and the menu entries. "
	                            "Saving writes gameinfo.txt, cfg/chapterN.cfg, resource/<mod>_english.txt, scripts/ChapterBackgrounds.txt "
	                            "and resource/GameMenu.res in the mod (each replaced file is kept once as .bak)."));
	intro->setWordWrap(true);
	intro->setProperty("role", "description");
	v->addWidget(intro);

	buildInfoSection(v);

	v->addWidget(sectionHeader(tr("Chapters")));
	auto* chapterHolder = new QWidget;
	chapterHolder->setObjectName("menuChapters");
	m_chapterList = new QVBoxLayout(chapterHolder);
	m_chapterList->setContentsMargins(0, 0, 0, 0);
	m_chapterList->setSpacing(6);
	v->addWidget(chapterHolder);
	auto* addChapterButton = new QPushButton(tr("+ Chapter"));
	addChapterButton->setObjectName("menuAddChapter");
	connect(addChapterButton, &QPushButton::clicked, this, &MenuEditor::addChapter);
	v->addWidget(addChapterButton, 0, Qt::AlignLeft);

	v->addWidget(sectionHeader(tr("Main menu")));
	auto* itemHolder = new QWidget;
	itemHolder->setObjectName("menuItems");
	m_itemList = new QVBoxLayout(itemHolder);
	m_itemList->setContentsMargins(0, 0, 0, 0);
	m_itemList->setSpacing(4);
	v->addWidget(itemHolder);
	auto* addItem = new QPushButton(tr("+ Entry"));
	addItem->setObjectName("menuAddItem");
	connect(addItem, &QPushButton::clicked, this, [this] {
		m_menu.items.push_back({"New entry", "OpenOptionsDialog", false, false, false, {}});
		rebuildItems();
		updatePreview();
	});
	v->addWidget(addItem, 0, Qt::AlignLeft);
	v->addStretch(1);

	auto* saveRow = new QHBoxLayout;
	auto* save = new QPushButton(tr("Save to the mod"));
	save->setObjectName("menuSave");
	save->setProperty("accent", true);
	connect(save, &QPushButton::clicked, this, [this] { this->save(); });
	auto* revert = new QPushButton(tr("Reload"));
	revert->setToolTip(tr("Read the files again (drops what was not saved)"));
	connect(revert, &QPushButton::clicked, this, &MenuEditor::reload);
	m_status = new QLabel;
	m_status->setObjectName("menuStatus");
	m_status->setWordWrap(true);
	saveRow->addWidget(save);
	saveRow->addWidget(revert);
	saveRow->addWidget(m_status, 1);
	v->addLayout(saveRow);
	scroll->setWidget(column);
	splitter->addWidget(scroll);

	// Right: the preview.
	auto* right = new QWidget;
	auto* rv = new QVBoxLayout(right);
	rv->setContentsMargins(8, 8, 8, 8);
	rv->addWidget(sectionHeader(tr("Preview")));
	m_preview = new MenuPreview;
	rv->addWidget(m_preview, 1);
	m_inGame = new QCheckBox(tr("As shown during a game (Resume, Save ...)"));
	m_inGame->setObjectName("menuInGame");
	connect(m_inGame, &QCheckBox::toggled, this, &MenuEditor::updatePreview);
	rv->addWidget(m_inGame);
	auto* note = new QLabel(tr("An approximation: fonts and spacing come from the mod's scheme in the game."));
	note->setWordWrap(true);
	note->setProperty("role", "description");
	rv->addWidget(note);
	splitter->addWidget(right);
	splitter->setStretchFactor(0, 3);
	splitter->setStretchFactor(1, 2);

	// Read when the tab is seen (not at the start, while nobody looks at it).
	auto* lazy = new RefreshWhenShown(this, [this] { reload(); });
	connect(editor, &Editor::gameChanged, lazy, &RefreshWhenShown::request);
	lazy->request();
}

void MenuEditor::buildInfoSection(QVBoxLayout* layout) {
	layout->addWidget(sectionHeader(tr("Title and logo")));
	auto* box = new QFrame;
	box->setProperty("role", "card");
	auto* form = new QFormLayout(box);
	// A line edit bound to one gameinfo key; `addRow` false when the caller places it itself.
	const auto field = [&](const std::string& key, const QString& label, const QString& tip, bool addRow = true) {
		auto* edit = new QLineEdit;
		edit->setObjectName(QStringLiteral("menuInfo_%1").arg(qs(key)));
		edit->setToolTip(tip);
		connect(edit, &QLineEdit::textEdited, this, [this, key](const QString& text) {
			if (text.isEmpty()) {
				m_menu.info.erase(key);
			} else {
				m_menu.info[key] = text.toStdString();
			}
			updatePreview();
		});
		m_infoFields[key] = edit;
		if (addRow) form->addRow(label, edit);
		return edit;
	};
	field("game", tr("Name"), tr("The mod's name in Steam's library and the window title (gameinfo \"game\")"));
	field("title", tr("Menu title"), tr("First line of the title in the main menu (gameinfo \"title\")"));
	field("title2", tr("Second line"), tr("Optional second line under the title (gameinfo \"title2\")"));
	m_logo = new QCheckBox(tr("Show a logo image instead of the title text"));
	m_logo->setObjectName("menuLogo");
	m_logo->setToolTip(tr("gameinfo \"gamelogo 1\": the menu shows resource/GameLogo.res instead of the title"));
	connect(m_logo, &QCheckBox::toggled, this, [this](bool on) {
		if (m_loading) return;
		if (on) {
			m_menu.info["gamelogo"] = "1";
		} else {
			m_menu.info.erase("gamelogo");
		}
		updatePreview();
	});
	form->addRow(QString(), m_logo);
	auto* iconRow = new QHBoxLayout;
	auto* icon = field("icon", tr("Window icon"), tr("Path without extension, e.g. resource/icon (resource/icon.tga, 32x32; _big.tga 256x256)"), false);
	m_iconPreview = new QLabel;
	m_iconPreview->setFixedSize(32, 32);
	m_iconPreview->setObjectName("menuIconPreview");
	iconRow->addWidget(icon, 1);
	iconRow->addWidget(m_iconPreview);
	auto* iconHolder = new QWidget;
	iconHolder->setLayout(iconRow);
	iconRow->setContentsMargins(0, 0, 0, 0);
	form->addRow(tr("Window icon"), iconHolder);
	field("type", tr("Type"), tr("singleplayer_only or multiplayer_only (gameinfo \"type\")"));
	field("developer", tr("Developer"), tr("Shown in Steam (gameinfo \"developer\")"));
	field("developer_url", tr("Website"), tr("Developer website (gameinfo \"developer_url\")"));
	field("manual", tr("Manual link"), tr("Link to a manual or wiki page (gameinfo \"manual\")"));
	m_infoBox = box;
	layout->addWidget(box);
}

QStringList MenuEditor::mapNames() const {
	QStringList names;
	const auto* entry = m_editor->gameEntry();
	if (!entry) return names;
	const auto addFrom = [&](const QString& dir, const QString& pattern) {
		QDirIterator it(dir, {pattern}, QDir::Files);
		while (it.hasNext()) names << QFileInfo(it.next()).completeBaseName();
	};
	addFrom(fromPath(entry->gameDir / "maps"), "*.bsp");
	addFrom(m_editor->gameSettings().mapDir, "*.vmf");
	names.removeDuplicates();
	names.sort(Qt::CaseInsensitive);
	return names;
}

QString MenuEditor::displayLabel(const std::string& label) const {
	if (!label.starts_with('#')) return qs(label);
	if (auto own = m_menu.localization.get(label); !own.empty()) return qs(own);
	if (auto builtIn = builtInMenuLabel(label); !builtIn.empty()) return qs(builtIn);
	// The game's own menu texts (resource/gameui_english.txt), read once per game.
	if (!m_gameUiText) {
		m_gameUiText = Localization{};
		if (const auto* game = m_editor->game()) {
			if (const auto bytes = game->files.read("resource/gameui_english.txt")) m_gameUiText = parseLocalization(decodeTextFile(*bytes));
		}
	}
	if (auto text = m_gameUiText->get(label); !text.empty()) return qs(text);
	return qs(label);
}

void MenuEditor::reload() {
	const auto* entry = m_editor->gameEntry();
	m_loading = true;
	m_gameUiText.reset();
	m_menu = entry ? loadModMenu(entry->gameDir, m_editor->game() ? &m_editor->game()->files : nullptr) : ModMenu{};
	for (auto& [key, edit] : m_infoFields) {
		const auto it = m_menu.info.find(key);
		edit->setText(it == m_menu.info.end() ? QString() : qs(it->second));
	}
	m_logo->setChecked(m_menu.info.count("gamelogo") && m_menu.info.at("gamelogo") != "0");
	m_loading = false;
	rebuildChapters();
	rebuildItems();
	setEnabled(entry != nullptr);
	setStatus(entry ? (m_menu.menuFromMod ? tr("Menu of %1").arg(qs(entry->label)) : tr("The mod has no GameMenu.res yet: the game's menu is shown; saving gives the mod its own."))
	                : tr("No game chosen"));
	updatePreview();
}

void MenuEditor::refreshTitles() {
	const auto* entry = m_editor->gameEntry();
	if (!entry) return;
	const ModMenu fresh = loadModMenu(entry->gameDir);
	m_menu.localization = fresh.localization;
	const std::string folder = entry->gameDir.filename().string();
	for (auto& chapter : m_menu.chapters) chapter.title = m_menu.localization.get(chapterTitleToken(folder, chapter.number));
	rebuildChapters();
	updatePreview();
}

void MenuEditor::addChapter() {
	ChapterInfo chapter;
	chapter.number = static_cast<int>(m_menu.chapters.size()) + 1;
	chapter.title = tr("CHAPTER %1").arg(chapter.number).toStdString();
	m_menu.chapters.push_back(std::move(chapter));
	rebuildChapters();
	updatePreview();
}

void MenuEditor::rebuildChapters() {
	while (auto* item = m_chapterList->takeAt(0)) {
		delete item->widget();
		delete item;
	}
	for (int i = 0; i < static_cast<int>(m_menu.chapters.size()); ++i) {
		m_menu.chapters[static_cast<std::size_t>(i)].number = i + 1;
		m_chapterList->addWidget(makeChapterCard(i));
	}
	if (m_menu.chapters.empty()) {
		auto* none = new QLabel(tr("No chapters yet - New Game has nothing to offer. Add one and pick the map it starts."));
		none->setWordWrap(true);
		none->setProperty("role", "description");
		m_chapterList->addWidget(none);
	}
}

QWidget* MenuEditor::makeChapterCard(int index) {
	auto& chapter = m_menu.chapters[static_cast<std::size_t>(index)];
	auto* card = new QFrame;
	card->setObjectName("chapterCard");
	card->setProperty("role", "card");
	auto* v = new QVBoxLayout(card);
	v->setContentsMargins(10, 6, 6, 8);
	auto* head = new QHBoxLayout;
	auto* name = new QLabel(tr("Chapter %1").arg(index + 1));
	name->setStyleSheet("font-weight: 600;");
	head->addWidget(name, 1);
	auto* up = cardButton(QStringLiteral("↑"), tr("Earlier"), "chapterUp");
	auto* down = cardButton(QStringLiteral("↓"), tr("Later"), "chapterDown");
	auto* remove = cardButton(QStringLiteral("✕"), tr("Remove this chapter"), "chapterRemove");
	up->setEnabled(index > 0);
	down->setEnabled(index + 1 < static_cast<int>(m_menu.chapters.size()));
	head->addWidget(up);
	head->addWidget(down);
	head->addWidget(remove);
	v->addLayout(head);
	const auto move = [this, index](int by) {
		std::swap(m_menu.chapters[static_cast<std::size_t>(index)], m_menu.chapters[static_cast<std::size_t>(index + by)]);
		QMetaObject::invokeMethod(this, [this] { rebuildChapters(); updatePreview(); }, Qt::QueuedConnection);
	};
	connect(up, &QToolButton::clicked, this, [move] { move(-1); });
	connect(down, &QToolButton::clicked, this, [move] { move(1); });
	connect(remove, &QToolButton::clicked, this, [this, index] {
		m_menu.chapters.erase(m_menu.chapters.begin() + index);
		QMetaObject::invokeMethod(this, [this] { rebuildChapters(); updatePreview(); }, Qt::QueuedConnection);
	});

	auto* form = new QFormLayout;
	const QStringList maps = mapNames();
	const auto mapCombo = [&](const std::string& value, const QString& objectName, const QString& tip) {
		auto* combo = new QComboBox;
		combo->setObjectName(objectName);
		combo->setEditable(true);
		combo->addItems(maps);
		combo->setCurrentText(qs(value));
		combo->setToolTip(tip);
		combo->completer()->setFilterMode(Qt::MatchContains);
		return combo;
	};
	auto* title = new QLineEdit(qs(chapter.title));
	title->setObjectName("chapterTitle");
	title->setToolTip(tr("Shown in New Game (saved as %1 in the mod's localization file)").arg(qs(chapterTitleToken(
	                      m_editor->gameEntry() ? m_editor->gameEntry()->folder() : std::string("mod"), index + 1))));
	connect(title, &QLineEdit::textEdited, this, [this, index](const QString& text) {
		m_menu.chapters[static_cast<std::size_t>(index)].title = text.toStdString();
	});
	auto* map = mapCombo(chapter.map, "chapterMap", tr("The map this chapter starts (cfg/chapter%1.cfg)").arg(index + 1));
	connect(map, &QComboBox::currentTextChanged, this, [this, index](const QString& text) {
		m_menu.chapters[static_cast<std::size_t>(index)].map = text.trimmed().toStdString();
	});
	auto* background = mapCombo(chapter.background, "chapterBackground",
	                            tr("Map running behind the menu once the player reached this chapter (scripts/ChapterBackgrounds.txt). "
	                               "Its screenshot materials/console/<map>_widescreen.vtf shows in the preview."));
	connect(background, &QComboBox::currentTextChanged, this, [this, index](const QString& text) {
		m_menu.chapters[static_cast<std::size_t>(index)].background = text.trimmed().toStdString();
		updatePreview();
	});
	form->addRow(tr("Title"), title);
	form->addRow(tr("Map"), map);
	form->addRow(tr("Background"), background);
	v->addLayout(form);
	return card;
}

void MenuEditor::rebuildItems() {
	while (auto* item = m_itemList->takeAt(0)) {
		delete item->widget();
		delete item;
	}
	for (int i = 0; i < static_cast<int>(m_menu.items.size()); ++i) {
		m_itemList->addWidget(makeItemRow(i));
	}
}

QWidget* MenuEditor::makeItemRow(int index) {
	const auto& item = m_menu.items[static_cast<std::size_t>(index)];
	auto* row = new QFrame;
	row->setObjectName("menuItem");
	row->setProperty("role", "card");
	auto* v = new QVBoxLayout(row);
	v->setContentsMargins(8, 4, 4, 6);
	auto* top = new QHBoxLayout;
	auto* label = new QLineEdit(qs(item.label));
	label->setObjectName("itemLabel");
	label->setPlaceholderText(tr("Text (or #token)"));
	label->setToolTip(tr("The text of the entry. A #token is looked up in the localization files (shown: %1)").arg(displayLabel(item.label)));
	connect(label, &QLineEdit::textEdited, this, [this, index](const QString& text) {
		m_menu.items[static_cast<std::size_t>(index)].label = text.toStdString();
		updatePreview();
	});
	auto* command = new QComboBox;
	command->setObjectName("itemCommand");
	command->setEditable(true);
	for (const auto& c : commonMenuCommands()) command->addItem(qs(c.command), qs(c.description));
	command->setCurrentText(qs(item.command));
	command->setToolTip(tr("What the entry does; \"engine <console command>\" runs any command, e.g. engine map mymap"));
	connect(command, &QComboBox::currentTextChanged, this, [this, index](const QString& text) {
		m_menu.items[static_cast<std::size_t>(index)].command = text.toStdString();
	});
	top->addWidget(label, 3);
	top->addWidget(command, 2);
	auto* up = cardButton(QStringLiteral("↑"), tr("Higher"), "itemUp");
	auto* down = cardButton(QStringLiteral("↓"), tr("Lower"), "itemDown");
	auto* remove = cardButton(QStringLiteral("✕"), tr("Remove this entry"), "itemRemove");
	up->setEnabled(index > 0);
	down->setEnabled(index + 1 < static_cast<int>(m_menu.items.size()));
	top->addWidget(up);
	top->addWidget(down);
	top->addWidget(remove);
	v->addLayout(top);
	const auto move = [this, index](int by) {
		std::swap(m_menu.items[static_cast<std::size_t>(index)], m_menu.items[static_cast<std::size_t>(index + by)]);
		QMetaObject::invokeMethod(this, [this] { rebuildItems(); updatePreview(); }, Qt::QueuedConnection);
	};
	connect(up, &QToolButton::clicked, this, [move] { move(-1); });
	connect(down, &QToolButton::clicked, this, [move] { move(1); });
	connect(remove, &QToolButton::clicked, this, [this, index] {
		m_menu.items.erase(m_menu.items.begin() + index);
		QMetaObject::invokeMethod(this, [this] { rebuildItems(); updatePreview(); }, Qt::QueuedConnection);
	});
	auto* flags = new QHBoxLayout;
	const auto flag = [&](const QString& text, const QString& tip, bool GameMenuItem::*member) {
		auto* box = new QCheckBox(text);
		box->setToolTip(tip);
		box->setChecked(item.*member);
		connect(box, &QCheckBox::toggled, this, [this, index, member](bool on) {
			m_menu.items[static_cast<std::size_t>(index)].*member = on;
			updatePreview();
		});
		flags->addWidget(box);
	};
	flag(tr("Only in game"), tr("Shown only while a map is loaded (OnlyInGame)"), &GameMenuItem::onlyInGame);
	flag(tr("Not in multiplayer"), tr("Hidden in multiplayer (notmulti)"), &GameMenuItem::notMulti);
	flag(tr("Not in singleplayer"), tr("Hidden in singleplayer (notsingle)"), &GameMenuItem::notSingle);
	flags->addStretch(1);
	v->addLayout(flags);
	return row;
}

void MenuEditor::updatePreview() {
	if (m_loading) return;
	const auto info = [this](const char* key) {
		const auto it = m_menu.info.find(key);
		return it == m_menu.info.end() ? QString() : qs(it->second);
	};
	QString title = info("title"), title2 = info("title2");
	if (title.isEmpty() && title2.isEmpty()) title = info("game");
	if (info("gamelogo") == "1") {
		title = tr("[logo image]");
		title2.clear();
	}
	const bool inGame = m_inGame->isChecked();
	QStringList entries;
	for (const auto& item : m_menu.items) {
		if (item.onlyInGame && !inGame) continue;
		if (item.notSingle) continue;   // the mock shows singleplayer
		entries << displayLabel(item.label);
	}
	// Background: the first chapter's background map screenshot, if the game has it.
	QImage background;
	if (const auto* game = m_editor->game(); game && !m_menu.chapters.empty() && !m_menu.chapters.front().background.empty()) {
		const std::string bg = m_menu.chapters.front().background;
		for (const std::string& name : {"console/" + bg + "_widescreen", "console/" + bg}) {
			if (const auto image = loadTexture(game->files, name, 1024)) {
				background = toQImage(*image);
				break;
			}
		}
	}
	m_preview->setContent(title, title2, entries, background);

	// The window icon next to its field.
	QPixmap iconPicture;
	if (const auto* entry = m_editor->gameEntry(); entry && !info("icon").isEmpty()) {
		for (const char* extension : {".tga", ".png", "_big.tga"}) {
			const QImage image(fromPath(entry->gameDir) + "/" + info("icon") + extension);
			if (!image.isNull()) {
				iconPicture = QPixmap::fromImage(image.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
				break;
			}
		}
	}
	m_iconPreview->setPixmap(iconPicture);
	m_iconPreview->setToolTip(iconPicture.isNull() ? tr("No picture found for this path") : QString());
}

bool MenuEditor::save(QString* error) {
	const auto* entry = m_editor->gameEntry();
	if (!entry) {
		if (error) *error = tr("No game chosen");
		return false;
	}
	for (const auto& chapter : m_menu.chapters) {
		if (chapter.map.empty()) {
			const QString message = tr("Chapter %1 has no map yet.").arg(chapter.number);
			setStatus(message, true);
			if (error) *error = message;
			return false;
		}
	}
	const auto result = saveModMenu(entry->gameDir, m_menu);
	if (!result.error.empty()) {
		setStatus(qs(result.error), true);
		if (error) *error = qs(result.error);
		return false;
	}
	QStringList files;
	for (const auto& path : result.written) files << QDir(fromPath(entry->gameDir)).relativeFilePath(fromPath(path));
	setStatus(files.isEmpty() ? tr("Nothing changed.") : tr("Saved: %1").arg(files.join(", ")));
	m_menu.menuFromMod = true;
	emit saved(files);
	return true;
}

void MenuEditor::setStatus(const QString& text, bool error) {
	m_status->setText(text);
	m_status->setStyleSheet(error ? "color: #ff8a70;" : "color: #9a9a9a;");
}

} // namespace sme::app
