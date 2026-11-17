// material_review_dialog.cpp - Export of the mod's materials for review and loading the answer
// (see material_review_dialog.h).

#include "material_review_dialog.h"

#include "dialog_fit.h"
#include "editor.h"
#include "material_decision_store.h"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTextOption>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <sme/fidelity_effects.h>
#include <sme/filesystem.h>
#include <sme/shipping.h>
#include <sme/text.h>

namespace sme::app {

namespace {

constexpr int COLUMNS = 4;
constexpr int ROWS = 3;
constexpr int TILE = 384;          // picture
constexpr int PAD = 14;
constexpr int LABEL = 92;          // text under each picture
constexpr int HEADER = 44;

QImage toQImage(const ImageRgba& image) {
	if (image.width == 0 || image.height == 0 || image.pixels.size() < std::size_t(image.width) * image.height * 4) return {};
	return QImage(reinterpret_cast<const uchar*>(image.pixels.data()), image.width, image.height, image.width * 4, QImage::Format_RGBA8888).copy();
}

QString shortRole(const ReviewEntry& e) {
	const auto& a = e.analysis;
	QString role = QString::fromLatin1(decisionRoleKey(a.role));
	if (a.role == TextureRole::FacadeWindows && a.columns > 0) role += QStringLiteral(" %1x%2").arg(a.columns).arg(a.floorsPerTile());
	if (e.reliefPattern != "none" && !e.reliefPattern.empty()) {
		const QString pattern = QString::fromStdString(e.reliefPattern).section(':', 0, 0);
		role += QStringLiteral(" · ") + pattern;
	}
	return role;
}

/// Writes bytes to a file; false on failure.
bool writeBytes(const QString& path, const QByteArray& bytes) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

} // namespace

std::set<std::string> openMapMaterialNames(const Editor* editor) {
	std::set<std::string> out;
	const auto& doc = editor->doc();
	const auto add = [&out](const Solid& solid) {
		for (const auto& side : solid.sides) {
			if (!side.material.empty()) out.insert(toLower(side.material));
		}
	};
	for (const auto& solid : doc.world.solids) add(solid);
	for (const auto& entity : doc.entities) {
		for (const auto& solid : entity.solids) add(solid);
	}
	return out;
}

QImage drawReviewSheet(const std::vector<ReviewEntry>& entries, int sheet, int sheets) {
	const int width = PAD + COLUMNS * (TILE + PAD);
	const int height = HEADER + ROWS * (TILE + LABEL + PAD) + PAD;
	QImage image(width, height, QImage::Format_RGB32);
	image.fill(QColor(30, 32, 36));
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	QFont title = p.font();
	title.setPixelSize(22);
	title.setBold(true);
	p.setFont(title);
	p.setPen(QColor(235, 235, 235));
	const QString range = entries.empty() ? QString()
	                                       : QStringLiteral("#%1-%2").arg(entries.front().number).arg(entries.back().number);
	p.drawText(QRect(PAD, 8, width - 2 * PAD, HEADER - 12), Qt::AlignLeft | Qt::AlignVCenter,
	           QStringLiteral("Mapsmith materials for review - sheet %1 of %2 - %3").arg(sheet).arg(sheets).arg(range));
	QFont nameFont = p.font();
	nameFont.setPixelSize(17);
	nameFont.setBold(true);
	QFont infoFont = nameFont;
	infoFont.setPixelSize(15);
	infoFont.setBold(false);
	for (std::size_t i = 0; i < entries.size() && i < std::size_t(COLUMNS * ROWS); ++i) {
		const auto& e = entries[i];
		const int col = int(i) % COLUMNS, row = int(i) / COLUMNS;
		const int x = PAD + col * (TILE + PAD);
		const int y = HEADER + row * (TILE + LABEL + PAD);
		const QRect box(x, y, TILE, TILE);
		p.fillRect(box, QColor(55, 58, 64));
		const QImage picture = toQImage(e.base);
		if (!picture.isNull()) {
			// The whole texture, its aspect kept (a 2:1 trim strip stays a strip).
			const QSize fitted = picture.size().scaled(box.size(), Qt::KeepAspectRatio);
			const QRect target(box.x() + (box.width() - fitted.width()) / 2, box.y() + (box.height() - fitted.height()) / 2, fitted.width(), fitted.height());
			p.drawImage(target, picture);
		} else {
			p.setFont(infoFont);
			p.setPen(QColor(200, 120, 120));
			p.drawText(box, Qt::AlignCenter | Qt::TextWordWrap, e.error.empty() ? QStringLiteral("no texture") : QString::fromStdString(e.error));
		}
		const QImage bump = toQImage(e.bump);
		if (!bump.isNull()) {
			const int s = TILE / 3;
			const QRect small(box.right() - s - 6, box.bottom() - s - 6, s, s);
			p.fillRect(small.adjusted(-3, -3, 3, 3), QColor(240, 240, 240));
			p.drawImage(small, bump.scaled(small.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
		}
		// The label: number and name (wrapped at slashes when long), size and shader, flags and what Mapsmith thinks.
		const QRect label(x, y + TILE + 4, TILE, LABEL - 4);
		p.setFont(nameFont);
		p.setPen(QColor(255, 214, 120));
		QTextOption wrap(Qt::AlignLeft | Qt::AlignTop);
		wrap.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		const QString name = QStringLiteral("#%1 %2").arg(e.number).arg(QString::fromStdString(e.name)).replace('/', QStringLiteral("/​"));
		const QRectF nameRect = p.boundingRect(QRectF(label), name, wrap);
		p.drawText(QRectF(label), name, wrap);
		p.setFont(infoFont);
		p.setPen(QColor(215, 218, 224));
		const QString flags = QString::fromStdString(reviewFlagsText(e));
		const QString info = QStringLiteral("%1x%2 · %3%4\n%5").arg(e.width).arg(e.height).arg(QString::fromStdString(e.shader))
		                         .arg(flags.isEmpty() ? QString() : QStringLiteral(" · ") + flags)
		                         .arg(shortRole(e) + (e.decision.empty() ? QString() : QStringLiteral(" · decided")));
		const QRectF infoRect(label.x(), nameRect.bottom() + 2, label.width(), label.bottom() - nameRect.bottom() - 2);
		p.drawText(infoRect, info, wrap);
	}
	p.end();
	return image;
}

/// One picture as JPEG (PNG when this Qt has no JPEG writer); the name follows the format.
static QByteArray encodePicture(const QImage& image, QString& name, int quality) {
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	if (!image.save(&buffer, "JPG", quality)) {
		bytes.clear();
		buffer.seek(0);
		image.save(&buffer, "PNG");
		name.replace(QStringLiteral(".jpg"), QStringLiteral(".png"));
	}
	return bytes;
}

ReviewExportResult exportMaterialReview(const GameFileSystem& fs, const std::vector<std::string>& materials, const MaterialDecisions& decisions,
                                        const QString& folder, const QString& game, const std::function<bool(int, int)>& progress,
                                        const ReviewExportOptions& options) {
	ReviewExportResult result;
	result.folder = folder;
	result.materials = int(materials.size());
	const QDir dir(folder);
	if (!QDir().mkpath(folder) || (options.pictures && !QDir().mkpath(dir.filePath(QStringLiteral("pictures"))))) {
		result.error = QObject::tr("Could not make the folder %1").arg(QDir::toNativeSeparators(folder));
		return result;
	}
	const auto sheets = reviewSheets(materials.size(), COLUMNS * ROWS);
	result.sheets = int(sheets.size());
	std::vector<ReviewEntry> index;
	// Per sheet: its file and the pictures of its materials (they travel together in one part).
	std::vector<QStringList> groups;
	std::vector<std::vector<std::uint64_t>> groupSizes;
	int done = 0;
	const auto save = [&](const QImage& image, QString name, int quality, QStringList& files, std::vector<std::uint64_t>& sizes) -> QString {
		const QByteArray bytes = encodePicture(image, name, quality);
		if (bytes.isEmpty() || !writeBytes(dir.filePath(name), bytes)) return {};
		files << name;
		sizes.push_back(std::uint64_t(bytes.size()));
		return name;
	};
	for (std::size_t s = 0; s < sheets.size(); ++s) {
		std::vector<ReviewEntry> entries;
		QStringList files;
		std::vector<std::uint64_t> sizes;
		std::vector<ReviewPictures> pictures;
		for (const std::size_t i : sheets[s]) {
			if (progress && !progress(done, int(materials.size()))) {
				result.cancelled = true;
				return result;
			}
			ReviewEntry e = reviewEntry(fs, materials[i], &decisions, TILE);
			e.number = int(i) + 1;
			ReviewPictures picture;
			reviewRelief(fs, options.modDir, e, &decisions, options.mapMaterials.count(e.name) > 0, options.pictures ? &picture : nullptr);
			entries.push_back(std::move(e));
			pictures.push_back(std::move(picture));
			++done;
		}
		const QImage image = drawReviewSheet(entries, int(s) + 1, int(sheets.size()));
		if (save(image, QString::fromStdString(reviewSheetName(s)), 82, files, sizes).isEmpty()) {
			result.error = QObject::tr("Could not write %1").arg(QString::fromStdString(reviewSheetName(s)));
			return result;
		}
		for (std::size_t k = 0; k < entries.size() && options.pictures; ++k) {
			auto& e = entries[k];
			const auto& picture = pictures[k];
			const auto one = [&](const ImageRgba& rgba, const char* kind, QImage::Format format) {
				QImage q = toQImage(rgba);
				if (q.isNull()) return true;
				const QString name = save(q.convertToFormat(format), QString::fromStdString(reviewPictureName(e.number, kind)), 85, files, sizes);
				if (name.isEmpty()) return false;
				e.pictureFiles.push_back(name.toStdString());
				return true;
			};
			if (!one(picture.base, "base", QImage::Format_RGB888) || !one(picture.height, "height", QImage::Format_Grayscale8) ||
			    !one(picture.normal, "normal", QImage::Format_RGB888)) {
				result.error = QObject::tr("Could not write the pictures of %1").arg(QString::fromStdString(e.name));
				return result;
			}
		}
		groups.push_back(files);
		groupSizes.push_back(sizes);
		for (auto& e : entries) {
			e.base = {};
			e.bump = {};
			index.push_back(std::move(e));
		}
	}
	if (progress) progress(done, int(materials.size()));
	// The index and the README (every part carries them: each upload stands on its own).
	const std::string csv = reviewIndexCsv(index);
	const std::string json = reviewIndexJson(index);
	const qint64 fixedBytes = qint64(csv.size() + json.size()) + 8192;
	const std::uint64_t room = std::uint64_t(std::max<qint64>(options.partLimit - fixedBytes, 1024));
	// A sheet with its pictures is one unit; one too big for a part is split into its files.
	std::vector<QStringList> units;
	std::vector<std::uint64_t> unitSizes;
	for (std::size_t g = 0; g < groups.size(); ++g) {
		std::uint64_t total = 0;
		for (const auto size : groupSizes[g]) total += size;
		if (total <= room) {
			units.push_back(groups[g]);
			unitSizes.push_back(total);
		} else {
			for (int k = 0; k < groups[g].size(); ++k) {
				units.push_back({groups[g][k]});
				unitSizes.push_back(groupSizes[g][std::size_t(k)]);
			}
		}
	}
	const auto parts = reviewParts(unitSizes, room, 0);
	const std::string readme = reviewReadme(int(materials.size()), int(sheets.size()), int(parts.size()), game.toStdString(), options.pictures);
	if (!writeBytes(dir.filePath("index.csv"), QByteArray::fromStdString(csv)) || !writeBytes(dir.filePath("index.json"), QByteArray::fromStdString(json)) ||
	    !writeBytes(dir.filePath("README.txt"), QByteArray::fromStdString(readme))) {
		result.error = QObject::tr("Could not write the index in %1").arg(QDir::toNativeSeparators(folder));
		return result;
	}
	for (std::size_t part = 0; part < parts.size(); ++part) {
		std::vector<PackageEntry> files;
		for (const char* fixed : {"README.txt", "index.csv", "index.json"}) files.push_back({fixed, toPath(dir.filePath(fixed)), {}});
		for (const std::size_t i : parts[part]) {
			for (const QString& file : units[i]) files.push_back({file.toStdString(), toPath(dir.filePath(file)), {}});
		}
		const QString zip = dir.filePath(QStringLiteral("mapsmith_materials_part%1of%2.zip").arg(part + 1).arg(parts.size()));
		std::string error;
		if (!writeZip(toPath(zip), files, &error)) {
			result.error = QString::fromStdString(error);
			return result;
		}
		result.zips << zip;
	}
	return result;
}

// --- Export dialog ------------------------------------------------------------------------------

MaterialReviewDialog::MaterialReviewDialog(Editor* editor, QWidget* parent) : QDialog(parent), m_editor(editor) {
	setObjectName("materialReviewDialog");
	setWindowTitle(tr("Export materials for review"));
	auto* v = new QVBoxLayout(this);
	auto* intro = new QLabel(tr("Contact sheets of the mod's materials (12 per picture, with name, size, shader and flags), an index with what "
	                            "Mapsmith decided by itself, the pictures for the 3D check, and a README - packed in ZIP parts of at most 20 MB. Send "
	                            "the parts into the chat; paste the answer with Load material decisions."));
	intro->setWordWrap(true);
	v->addWidget(intro);
	auto* form = new QFormLayout;
	m_maps = new QRadioButton(tr("Materials of the mod's maps (and the open map)"));
	m_maps->setObjectName("reviewScopeMaps");
	m_folderScope = new QRadioButton(tr("Every brush material in a folder:"));
	m_folderScope->setObjectName("reviewScopeFolder");
	m_folderFilter = new QLineEdit;
	m_folderFilter->setObjectName("reviewFolder");
	m_folderFilter->setPlaceholderText(tr("brick/  or  building_template"));
	m_all = new QRadioButton(tr("Every brush material of the game and the mod (slow: thousands)"));
	m_all->setObjectName("reviewScopeAll");
	m_maps->setChecked(true);
	auto* folderRow = new QHBoxLayout;
	folderRow->addWidget(m_folderScope);
	folderRow->addWidget(m_folderFilter, 1);
	auto* scope = new QVBoxLayout;
	scope->addWidget(m_maps);
	scope->addLayout(folderRow);
	scope->addWidget(m_all);
	form->addRow(tr("Materials"), scope);
	m_tools = new QCheckBox(tr("tools/"));
	m_skybox = new QCheckBox(tr("skybox/"));
	m_decals = new QCheckBox(tr("decals and overlays"));
	auto* also = new QHBoxLayout;
	also->addWidget(m_tools);
	also->addWidget(m_skybox);
	also->addWidget(m_decals);
	also->addStretch(1);
	form->addRow(tr("Also"), also);
	m_reliefOnly = new QCheckBox(tr("Only materials that can get relief: walls, ground, roof, facades"));
	m_reliefOnly->setObjectName("reviewReliefOnly");
	m_reliefOnly->setToolTip(tr("The role from the decision file, else what Mapsmith reads from the picture. See-through materials and decals never get relief."));
	form->addRow(QString(), m_reliefOnly);
	m_pictures = new QCheckBox(tr("Pictures for the 3D check"));
	m_pictures->setObjectName("reviewPictures");
	m_pictures->setChecked(QSettings().value("review/pictures", true).toBool());
	m_pictures->setToolTip(tr("Each material also as pictures/NNN_base.jpg (its own resolution up to 1024), NNN_height.jpg (exactly the heights the "
	                          "game's relief uses) and NNN_normal.jpg (its normal map): enough to judge depth and bump, which the contact sheets cannot show."));
	form->addRow(tr("Pictures"), m_pictures);
	m_output = new QLineEdit;
	m_output->setObjectName("reviewOutput");
	const QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	m_output->setText(QDir(base.isEmpty() ? QDir::homePath() : base).filePath(QStringLiteral("Mapsmith review %1").arg(QDate::currentDate().toString(Qt::ISODate))));
	auto* browse = new QPushButton(tr("..."));
	connect(browse, &QPushButton::clicked, this, [this] {
		const QString dir = QFileDialog::getExistingDirectory(this, tr("Where to put the export"), m_output->text());
		if (!dir.isEmpty()) m_output->setText(dir);
	});
	auto* outRow = new QHBoxLayout;
	outRow->addWidget(m_output, 1);
	outRow->addWidget(browse);
	form->addRow(tr("Folder"), outRow);
	v->addLayout(form);
	m_count = new QLabel;
	m_count->setObjectName("reviewCount");
	v->addWidget(m_count);
	m_progress = new QProgressBar;
	m_progress->setVisible(false);
	v->addWidget(m_progress);
	m_status = new QLabel;
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	v->addWidget(m_status);
	auto* buttons = new QDialogButtonBox;
	m_start = buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
	m_start->setObjectName("reviewExport");
	m_openFolder = buttons->addButton(tr("Open folder"), QDialogButtonBox::ActionRole);
	m_openFolder->setEnabled(false);
	m_cancel = buttons->addButton(QDialogButtonBox::Close);
	v->addWidget(buttons);
	connect(m_start, &QPushButton::clicked, this, &MaterialReviewDialog::start);
	connect(m_openFolder, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_result.folder)); });
	connect(m_cancel, &QPushButton::clicked, this, [this] {
		if (running()) {
			m_stop->store(true);
			m_status->setText(tr("Stopping ..."));
		} else {
			reject();
		}
	});
	for (auto* radio : {m_maps, m_folderScope, m_all}) connect(radio, &QRadioButton::toggled, this, [this] { updateCount(); });
	for (auto* box : {m_tools, m_skybox, m_decals, m_reliefOnly}) connect(box, &QCheckBox::toggled, this, [this] { updateCount(); });
	connect(m_folderFilter, &QLineEdit::textChanged, this, [this] {
		if (!m_folderFilter->text().isEmpty()) m_folderScope->setChecked(true);
		updateCount();
	});
	m_tick.setInterval(150);
	connect(&m_tick, &QTimer::timeout, this, [this] {
		const int total = m_total->load(), done = m_done->load();
		m_progress->setMaximum(std::max(total, 1));
		m_progress->setValue(done);
		m_status->setText(total == 0 ? tr("Listing the materials ...") : tr("Reading and drawing %1 of %2 materials ...").arg(done).arg(total));
	});
	connect(&m_watcher, &QFutureWatcher<ReviewExportResult>::finished, this, [this] {
		m_tick.stop();
		m_busy = false;
		m_result = m_watcher.result();
		m_progress->setVisible(false);
		m_start->setEnabled(true);
		m_openFolder->setEnabled(!m_result.folder.isEmpty() && QFileInfo::exists(m_result.folder));
		if (m_result.cancelled) {
			m_status->setText(tr("Stopped."));
		} else if (!m_result.error.isEmpty()) {
			m_status->setText(tr("Export failed: %1").arg(m_result.error));
		} else {
			QStringList zips;
			for (const QString& zip : m_result.zips) zips << QFileInfo(zip).fileName();
			m_status->setText(tr("%1 materials on %2 sheets in %3: %4. Send these into the chat.")
			                      .arg(m_result.materials)
			                      .arg(m_result.sheets)
			                      .arg(QDir::toNativeSeparators(m_result.folder))
			                      .arg(zips.join(QStringLiteral(", "))));
		}
		emit exportFinished(m_result);
	});
	updateCount();
	fitDialogToScreen(this);
}

MaterialReviewDialog::~MaterialReviewDialog() {
	m_stop->store(true);
	m_watcher.waitForFinished();
}

ReviewScope MaterialReviewDialog::chosenScope() const {
	ReviewScope scope;
	scope.kind = m_all->isChecked() ? ReviewScope::Kind::AllWorld : m_folderScope->isChecked() ? ReviewScope::Kind::Folder : ReviewScope::Kind::MapMaterials;
	scope.folder = m_folderFilter->text().trimmed().toStdString();
	scope.tools = m_tools->isChecked();
	scope.skybox = m_skybox->isChecked();
	scope.decals = m_decals->isChecked();
	scope.reliefOnly = m_reliefOnly->isChecked();
	return scope;
}

std::set<std::string> MaterialReviewDialog::mapMaterials() const {
	const QString mapDir = m_editor->gameSettings().mapDir;
	auto maps = vmfMaterialNames(mapSourceFiles(mapDir.isEmpty() ? std::filesystem::path() : toPath(mapDir)));
	const auto open = openMapMaterialNames(m_editor);
	maps.insert(open.begin(), open.end());
	return maps;
}

std::vector<std::string> MaterialReviewDialog::chosenMaterials() const {
	const LoadedGame* game = m_editor->game();
	if (!game) return {};
	const ReviewScope scope = chosenScope();
	const auto decisions = m_editor->materialDecisions()->decisions();
	return reviewMaterialList(game->files, scope, scope.kind == ReviewScope::Kind::MapMaterials ? mapMaterials() : std::set<std::string>{}, &decisions);
}

void MaterialReviewDialog::updateCount() {
	if (m_all->isChecked()) {
		m_count->setText(tr("Every brush material: counted when the export starts."));
		m_start->setEnabled(!running());
		return;
	}
	const auto count = chosenMaterials().size();
	m_count->setText(tr("%n material(s), %1 sheet(s)", nullptr, int(count)).arg((count + 11) / 12));
	m_start->setEnabled(count > 0 && !running());
}

void MaterialReviewDialog::start() {
	const LoadedGame* game = m_editor->game();
	if (!game || running()) return;
	// The list is made in the background too: every brush material (and the relief filter, which
	// reads each picture) takes a while.
	const ReviewScope scope = chosenScope();
	ReviewExportOptions options;
	options.pictures = m_pictures->isChecked();
	options.mapMaterials = mapMaterials();
	if (const auto* entry = m_editor->gameEntry()) options.modDir = entry->gameDir;
	m_busy = true;
	m_stop->store(false);
	m_done->store(0);
	m_total->store(0);
	m_progress->setVisible(true);
	m_start->setEnabled(false);
	m_openFolder->setEnabled(false);
	m_tick.start();
	const GameFileSystem* fs = &game->files;
	const QString folder = m_output->text().trimmed();
	const QString title = QString::fromStdString(game->entry.title);
	QSettings().setValue("review/folder", folder);
	QSettings().setValue("review/pictures", options.pictures);
	m_watcher.setFuture(QtConcurrent::run([fs, scope, options, decisions = m_editor->materialDecisions()->decisions(), folder, title, done = m_done,
	                                       total = m_total, stop = m_stop] {
		const auto materials = reviewMaterialList(*fs, scope, scope.kind == ReviewScope::Kind::MapMaterials ? options.mapMaterials : std::set<std::string>{},
		                                          &decisions);
		if (materials.empty()) {
			ReviewExportResult empty;
			empty.error = QObject::tr("No materials to export.");
			return empty;
		}
		return exportMaterialReview(*fs, materials, decisions, folder, title, [done, total, stop](int d, int t) {
			done->store(d);
			total->store(t);
			return !stop->load();
		}, options);
	}));
}

// --- Load material decisions -------------------------------------------------------------------

MaterialDecisionsDialog::MaterialDecisionsDialog(Editor* editor, QWidget* parent) : QDialog(parent), m_editor(editor) {
	setObjectName("materialDecisionsDialog");
	setWindowTitle(tr("Load material decisions"));
	auto* v = new QVBoxLayout(this);
	auto* intro = new QLabel(tr("Paste the decision lines from the chat (or open a .txt). Each line: a material or wildcard, then keys like "
	                            "role=facade windows=2x1 parallax=off depth=0.006. Below: what changes in the mod's cfg/mapsmith_materials.txt."));
	intro->setWordWrap(true);
	v->addWidget(intro);
	m_text = new QPlainTextEdit;
	m_text->setObjectName("decisionsText");
	m_text->setPlaceholderText(QStringLiteral("brick/brickwall031d  role=wall parallax=on depth=0.008 pattern=brick\nbrick/*  parallax=off"));
	QFont mono = m_text->font();
	mono.setFamily(QStringLiteral("monospace"));
	mono.setStyleHint(QFont::TypeWriter);
	m_text->setFont(mono);
	v->addWidget(m_text, 2);
	auto* row = new QHBoxLayout;
	auto* open = new QPushButton(tr("Open file ..."));
	connect(open, &QPushButton::clicked, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Material decisions"), QString(), tr("Text (*.txt);;All files (*)"));
		if (path.isEmpty()) return;
		QFile file(path);
		if (file.open(QIODevice::ReadOnly)) m_text->setPlainText(QString::fromUtf8(file.readAll()));
	});
	row->addWidget(open);
	m_mode = new QComboBox;
	m_mode->setObjectName("decisionsMode");
	m_mode->addItem(tr("Merge into the mod's file (lines not mentioned stay)"));
	m_mode->addItem(tr("Replace the whole file"));
	row->addWidget(m_mode, 1);
	v->addLayout(row);
	m_problems = new QLabel;
	m_problems->setObjectName("decisionsProblems");
	m_problems->setWordWrap(true);
	m_problems->setStyleSheet("color: #e8a060;");
	v->addWidget(m_problems);
	m_preview = new QTreeWidget;
	m_preview->setObjectName("decisionsPreview");
	m_preview->setHeaderLabels({tr("Material"), tr("Before"), tr("After")});
	m_preview->setRootIsDecorated(false);
	m_preview->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	v->addWidget(m_preview, 3);
	auto* buttons = new QDialogButtonBox;
	m_apply = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
	m_apply->setObjectName("decisionsApply");
	buttons->addButton(QDialogButtonBox::Cancel);
	v->addWidget(buttons);
	connect(m_apply, &QPushButton::clicked, this, [this] {
		if (apply()) accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_text, &QPlainTextEdit::textChanged, this, &MaterialDecisionsDialog::updatePreview);
	connect(m_mode, &QComboBox::currentIndexChanged, this, &MaterialDecisionsDialog::updatePreview);
	resize(820, 620);
	updatePreview();
	fitDialogToScreen(this);
}

void MaterialDecisionsDialog::setText(const QString& text) {
	m_text->setPlainText(text);
}

void MaterialDecisionsDialog::setReplace(bool replace) {
	m_mode->setCurrentIndex(replace ? 1 : 0);
}

MaterialDecisions MaterialDecisionsDialog::result() const {
	const auto incoming = MaterialDecisions::parse(m_text->toPlainText().toStdString());
	if (m_mode->currentIndex() == 1) return incoming;
	MaterialDecisions merged = m_editor->materialDecisions()->decisions();
	merged.merge(incoming);
	return merged;
}

QStringList MaterialDecisionsDialog::previewLines() const {
	QStringList out;
	for (const auto& change : compareDecisions(m_editor->materialDecisions()->decisions(), result())) {
		out << QStringLiteral("%1: %2 -> %3")
		           .arg(QString::fromStdString(change.pattern), QString::fromStdString(change.before.empty() ? std::string("-") : change.before),
		                QString::fromStdString(change.after.empty() ? std::string("-") : change.after));
	}
	return out;
}

void MaterialDecisionsDialog::updatePreview() {
	const auto incoming = MaterialDecisions::parse(m_text->toPlainText().toStdString());
	QStringList problems;
	for (const auto& p : incoming.problems()) problems << tr("Line %1: %2").arg(p.line).arg(QString::fromStdString(p.message));
	m_problems->setText(problems.isEmpty() ? QString() : problems.join('\n') + tr("\n(These values are left out; the rest is used.)"));
	m_preview->clear();
	const auto changes = compareDecisions(m_editor->materialDecisions()->decisions(), result());
	for (const auto& change : changes) {
		auto* item = new QTreeWidgetItem(m_preview, {QString::fromStdString(change.pattern), QString::fromStdString(change.before),
		                                             QString::fromStdString(change.after)});
		if (change.after.empty()) item->setForeground(2, QColor(230, 120, 110));
	}
	m_apply->setEnabled(!changes.empty() && m_editor->gameEntry());
	m_apply->setText(changes.empty() ? tr("Apply") : tr("Apply %n change(s)", nullptr, int(changes.size())));
}

bool MaterialDecisionsDialog::apply() {
	const MaterialDecisions next = result();
	const bool ok = m_editor->materialDecisions()->change([&next](MaterialDecisions& d) { d = next; });
	if (ok) emit m_editor->message(tr("Material decisions: %1 lines in %2").arg(next.entryCount()).arg(QDir::toNativeSeparators(m_editor->materialDecisions()->filePath())));
	return ok;
}

void openMaterialDecisionsFile(Editor* editor, QWidget* parent) {
	const QString path = editor->materialDecisions()->filePath();
	if (path.isEmpty()) {
		QMessageBox::information(parent, QObject::tr("Material decisions"), QObject::tr("Choose a game first."));
		return;
	}
	if (!QFileInfo::exists(path)) {
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile file(path);
		if (file.open(QIODevice::WriteOnly)) {
			file.write("// Mapsmith: decisions about this mod's materials, one line each. Examples:\n"
			           "//   building_template/building_template007a  role=facade windows=2x1 floors=1\n"
			           "//   brick/brickwall031d  role=wall parallax=on depth=0.008 pattern=brick\n"
			           "//   brick/*  parallax=off\n"
			           "// Keys: role (facade wall trim roof shop ground interior unsuitable), windows=CxR, floors=N,\n"
			           "// windowrects=u0,v0,u1,v1;..., scale, parallax (auto on off), depth, pattern (brick tile plank stone none), note=\"...\"\n");
		}
	}
	QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

} // namespace sme::app
