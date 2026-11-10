// material_field.cpp - Material field with picture, suggestions and picker (see material_field.h).

#include "material_field.h"

#include "asset_browser.h"
#include "editor.h"
#include "material_cache.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace sme::app {

namespace {

constexpr int PICTURE = 26;
constexpr int PICKER_THUMB = 96;

} // namespace

MaterialPickerDialog::MaterialPickerDialog(AssetBrowser* assets, const QString& current, const QString& title, QWidget* parent) : QDialog(parent) {
	setObjectName("materialPicker");
	setWindowTitle(title.isEmpty() ? tr("Choose a material") : title);
	auto* layout = new QVBoxLayout(this);
	m_search = new QLineEdit(this);
	m_search->setObjectName("materialPickerSearch");
	m_search->setPlaceholderText(tr("Search materials (words: asphalt road) ..."));
	m_search->setClearButtonEnabled(true);
	layout->addWidget(m_search);
	m_filter = new MaterialFilter(this);
	m_filter->setSourceModel(assets->materialModel());
	m_list = new QListView(this);
	m_list->setObjectName("materialPickerList");
	m_list->setModel(m_filter);
	m_list->setViewMode(QListView::IconMode);
	m_list->setIconSize(QSize(PICKER_THUMB, PICKER_THUMB));
	m_list->setGridSize(QSize(PICKER_THUMB + 18, PICKER_THUMB + 2 * m_list->fontMetrics().height() + 6));
	m_list->setResizeMode(QListView::Adjust);
	m_list->setMovement(QListView::Static);
	m_list->setUniformItemSizes(true);
	m_list->setWordWrap(true);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->setMinimumSize(560, 420);
	layout->addWidget(m_list, 1);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Use"));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_list, &QListView::doubleClicked, this, &QDialog::accept);
	const auto ok = [this, buttons] { buttons->button(QDialogButtonBox::Ok)->setEnabled(m_list->currentIndex().isValid()); };
	connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, ok);
	connect(m_search, &QLineEdit::textChanged, this, [this, ok](const QString& text) {
		m_filter->setText(text);
		if (m_filter->rowCount() > 0 && !m_list->currentIndex().isValid()) m_list->setCurrentIndex(m_filter->index(0, 0));
		ok();
	});
	// Start on the current material (shown and selected), else on an empty search.
	const QString name = normalizedMaterialName(current);
	if (!name.isEmpty()) {
		for (int row = 0; row < m_filter->rowCount(); ++row) {
			const QModelIndex index = m_filter->index(row, 0);
			if (index.data(Qt::UserRole).toString() == name) {
				m_list->setCurrentIndex(index);
				m_list->scrollTo(index, QAbstractItemView::PositionAtCenter);
				break;
			}
		}
	}
	ok();
	m_search->setFocus();
}

QString MaterialPickerDialog::material() const {
	const QModelIndex index = m_list->currentIndex();
	return index.isValid() ? index.data(Qt::UserRole).toString().toUpper() : QString();
}

void MaterialPickerDialog::setSearch(const QString& words) {
	m_search->setText(words);
}

MaterialField::MaterialField(const QString& name, Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	auto* row = new QHBoxLayout(this);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(4);
	m_picture = new QLabel(this);
	m_picture->setObjectName(name + "Picture");
	m_picture->setFixedSize(PICTURE, PICTURE);
	m_picture->setScaledContents(true);
	row->addWidget(m_picture);
	m_text = new QLineEdit(this);
	m_text->setObjectName(name);
	row->addWidget(m_text, 1);
	m_suggest = new QToolButton(this);
	m_suggest->setObjectName(name + "Suggest");
	m_suggest->setText(QStringLiteral("▾"));
	m_suggest->setToolTip(tr("Fitting materials of the game, and the one picked in the browser"));
	m_suggest->setPopupMode(QToolButton::InstantPopup);
	auto* menu = new QMenu(m_suggest);
	connect(menu, &QMenu::aboutToShow, this, [this, menu] {
		menu->clear();
		for (const auto& material : m_suggestions) {
			auto* action = menu->addAction(material.toUpper());
			connect(action, &QAction::triggered, this, [this, material] { choose(material.toUpper()); });
		}
		if (m_suggestions.isEmpty()) menu->addAction(tr("(no fitting material found by name)"))->setEnabled(false);
		menu->addSeparator();
		const QString current = m_editor->currentMaterial();
		auto* use = menu->addAction(tr("Picked in the browser: %1").arg(current));
		use->setEnabled(!current.isEmpty());
		connect(use, &QAction::triggered, this, [this, current] { choose(current.toUpper()); });
		auto* clear = menu->addAction(tr("Clear (%1)").arg(m_text->placeholderText().isEmpty() ? tr("none") : m_text->placeholderText()));
		connect(clear, &QAction::triggered, this, [this] { choose({}); });
	});
	m_suggest->setMenu(menu);
	row->addWidget(m_suggest);
	m_browse = new QToolButton(this);
	m_browse->setObjectName(name + "Browse");
	m_browse->setText(QStringLiteral("..."));
	m_browse->setToolTip(tr("Choose from all of the game's materials, with pictures"));
	m_browse->setEnabled(false);
	connect(m_browse, &QToolButton::clicked, this, &MaterialField::browse);
	row->addWidget(m_browse);
	connect(m_text, &QLineEdit::textChanged, this, [this](const QString& text) {
		updatePicture();
		emit textChanged(text);
	});
	connect(m_text, &QLineEdit::textEdited, this, &MaterialField::textEdited);
	updatePicture();
}

QString MaterialField::text() const {
	return m_text->text();
}

void MaterialField::setText(const QString& text) {
	m_text->setText(text);
}

void MaterialField::setPlaceholderText(const QString& text) {
	m_text->setPlaceholderText(text);
}

void MaterialField::setAssets(AssetBrowser* assets) {
	m_assets = assets;
	m_browse->setEnabled(assets != nullptr);
	if (assets) {
		connect(assets->materialThumbnails(), &MaterialCache::materialReady, this, [this](const QString& name) {
			if (name == normalizedMaterialName(m_text->text())) updatePicture();
		});
	}
	updatePicture();
}

void MaterialField::setSuggestions(const QStringList& materials) {
	m_suggestions = materials;
}

void MaterialField::browse() {
	if (!m_assets) return;
	const QString current = m_text->text().trimmed().isEmpty() ? m_text->placeholderText() : m_text->text().trimmed();
	MaterialPickerDialog picker(m_assets, current, tr("Choose a material"), this);
	if (picker.exec() == QDialog::Accepted && !picker.material().isEmpty()) choose(picker.material());
}

void MaterialField::choose(const QString& material) {
	setText(material);
	emit textEdited(material);
}

void MaterialField::updatePicture() {
	const QString name = normalizedMaterialName(m_text->text().trimmed());
	QPixmap picture(PICTURE, PICTURE);
	picture.fill(QColor(45, 47, 52));
	if (!name.isEmpty() && m_assets) {
		if (const auto info = m_assets->materialThumbnails()->get(name)) {
			const QImage& image = info->image.isNull() ? MaterialCache::checkerboard() : info->image;
			picture = QPixmap::fromImage(image.scaled(PICTURE * 2, PICTURE * 2, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		}
	}
	m_picture->setPixmap(picture);
	m_picture->setToolTip(name.isEmpty() ? tr("No material set") : name);
}

} // namespace sme::app
