// anim_editor.cpp - The Animation editor window (see anim_editor.h).

#include "anim_editor.h"

#include "anim_edit_view.h"
#include "anim_timeline.h"
#include "animations_page.h"
#include "editor.h"
#include "import_dialog.h"
#include "model_cache.h"
#include "process_guard.h"
#include "ui_helpers.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <sme/animation_import.h>
#include <sme/assets.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace sme::app {

namespace {

/// The files studiomdl writes for a model (and the engine reads together).
const char* const MODEL_EXTENSIONS[] = {".mdl", ".vvd", ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".phy", ".ani"};

constexpr double RAD = 180.0 / M_PI;

QString stem(const QString& model) {
	return model.endsWith(".mdl", Qt::CaseInsensitive) ? model.chopped(4) : model;
}

QByteArray readFile(const QString& path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString& path, const QByteArray& data) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

std::vector<std::byte> toBytes(const QByteArray& data) {
	std::vector<std::byte> out(std::size_t(data.size()));
	if (!out.empty()) std::memcpy(out.data(), data.constData(), out.size());
	return out;
}

std::optional<int> checksumOf(const QString& path, std::optional<int> (*read)(std::span<const std::byte>)) {
	if (!QFileInfo::exists(path)) return std::nullopt;
	const auto bytes = toBytes(readFile(path));
	return read(bytes);
}

QDoubleSpinBox* valueBox(double range, int decimals, const QString& suffix) {
	auto* box = new QDoubleSpinBox;
	box->setRange(-range, range);
	box->setDecimals(decimals);
	box->setSuffix(suffix);
	box->setKeyboardTracking(false);
	box->setButtonSymbols(QAbstractSpinBox::NoButtons);
	box->setMinimumWidth(56);
	return box;
}

} // namespace

AnimationEditor::AnimationEditor(Editor* editor, MaterialCache* materials, AnimationsPage* animations, QWidget* parent)
    : QWidget(parent, Qt::Window), m_editor(editor), m_materials(materials), m_animations(animations) {
	setObjectName("animationEditor");
	setAttribute(Qt::WA_DeleteOnClose, false);
	setWindowTitle(tr("Animation editor"));
	buildUi();
	resize(1240, 740);
	connect(&m_loader, &QFutureWatcher<Loaded>::finished, this, &AnimationEditor::loadingDone);
	if (m_editor->models()) connect(m_editor->models(), &ModelCache::modelReady, this, [this](const QString& path) {
		if (path.compare(m_model, Qt::CaseInsensitive) == 0) geometryArrived();
	});
	connect(&m_decompile, &QFutureWatcher<std::optional<ModelExport>>::finished, this, [this] {
		const auto exported = m_decompile.result();
		if (!m_edit || !m_editor->game()) {
			emit replaceFinished(false);
			return;
		}
		if (!exported) {
			m_log->appendPlainText(tr("The model could not be taken apart."));
			emit replaceFinished(false);
			return;
		}
		// The rest runs here: write the sources, set the mod's files aside, start studiomdl.
		ModelExport out = *exported;
		const auto it = out.texts.find(out.qcFile);
		const auto replaced = it == out.texts.end() ? std::nullopt
		                                            : replaceSequenceInQc(it->second, m_replaceSequence.toStdString(), m_edit->clip().fps, m_edit->clip().loop);
		if (!replaced) {
			m_log->appendPlainText(tr("The model has no sequence \"%1\" of its own to replace.").arg(m_replaceSequence));
			emit replaceFinished(false);
			return;
		}
		it->second = replaced->qc;
		out.texts[replaced->smdFile] = m_edit->smd();
		for (const auto& note : out.notes) m_log->appendPlainText(tr("Note: %1").arg(qs(note)));
		const QString folder = QDir(sourceFolder()).filePath("compile");
		std::string error;
		if (!writeModelExport(m_editor->game()->files, out, toPath(folder), &error)) {
			m_log->appendPlainText(tr("Cannot write the sources: %1").arg(qs(error)));
			emit replaceFinished(false);
			return;
		}
		// The mod's own files of this model go aside; the first time with the model as it was.
		const QString mod = modDir();
		const QString original = QDir(sourceFolder()).filePath("original");
		m_firstReplace = !QFileInfo::exists(QDir(original).filePath("base.mdl"));
		if (m_firstReplace) {
			const auto& fs = m_editor->game()->files;
			const auto mdl = fs.read(m_model.toStdString());
			if (!mdl) {
				m_log->appendPlainText(tr("Cannot read %1").arg(m_model));
				emit replaceFinished(false);
				return;
			}
			writeFile(QDir(original).filePath("base.mdl"), QByteArray(reinterpret_cast<const char*>(mdl->data()), qsizetype(mdl->size())));
			if (const auto ani = fs.read(stem(m_model).toStdString() + ".ani")) {
				writeFile(QDir(original).filePath("base.ani"), QByteArray(reinterpret_cast<const char*>(ani->data()), qsizetype(ani->size())));
			}
			writeFile(QDir(original).filePath("README.txt"),
			          "Mapsmith: the model as it was before its first rebuild (base.mdl, base.ani) and the mod's own files of it then (mod/).\n"
			          "\"Restore the original model\" in the Animation editor puts them back.\n");
			m_movedTo = QDir(original).filePath("mod");
		} else {
			m_movedTo = QDir(sourceFolder()).filePath("backups/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
		}
		m_movedAside.clear();
		for (const char* ext : MODEL_EXTENSIONS) {
			const QString file = QDir(mod).filePath(stem(m_model) + ext);
			if (!QFileInfo::exists(file)) continue;
			const QString target = QDir(m_movedTo).filePath(QFileInfo(file).fileName());
			QDir().mkpath(m_movedTo);
			QFile::remove(target);
			if (QFile::rename(file, target)) m_movedAside << file;
		}
		// studiomdl writes models/<$modelname> into the mod (-game).
		auto* process = new QProcess(this);
		m_studiomdl = process;
		process->setProcessChannelMode(QProcess::MergedChannels);
		process->setWorkingDirectory(folder);
#ifdef Q_OS_WIN
		process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
#endif
		connect(process, &QProcess::readyRead, this, [this, process] { m_log->appendPlainText(QString::fromLocal8Bit(process->readAll()).remove('\r').trimmed()); });
		connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
			process->deleteLater();
			finishReplace(code, status != QProcess::NormalExit);
		});
		connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError e) {
			if (e != QProcess::FailedToStart) return;
			m_log->appendPlainText(tr("Could not start studiomdl: %1").arg(process->errorString()));
			process->deleteLater();
			finishReplace(-1, true);
		});
		m_log->appendPlainText(tr("Building %1 with studiomdl ...").arg(m_model));
		process->start(studiomdlProgram(), {"-game", mod, "-nop4", "-verbose", QDir(folder).filePath(qs(out.qcFile))});
	});
}

AnimationEditor::~AnimationEditor() {
	stopOwnProcesses(this);
	m_loader.waitForFinished();
	m_decompile.waitForFinished();
}

// --- Layout ----------------------------------------------------------------------------------

void AnimationEditor::buildUi() {
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);

	// Top: what is edited, undo, camera, gizmo, results.
	auto* top = new QHBoxLayout;
	m_title = new QLabel(tr("No model"));
	m_title->setObjectName("animEditorTitle");
	m_title->setProperty("role", "sectionTitle");
	top->addWidget(m_title, 1);
	m_undo = new QPushButton(tr("Undo"));
	m_undo->setObjectName("animUndo");
	m_redo = new QPushButton(tr("Redo"));
	m_redo->setObjectName("animRedo");
	top->addWidget(m_undo);
	top->addWidget(m_redo);
	m_camera = new QComboBox;
	m_camera->setObjectName("animCamera");
	m_camera->addItem(tr("Free view"));
	m_camera->addItem(tr("Player's view (weapons)"));
	m_camera->setToolTip(tr("Player's view: as a first-person weapon model is seen in the game (viewmodel_fov 54)"));
	top->addWidget(m_camera);
	m_rotate = new QToolButton;
	m_rotate->setObjectName("animGizmoRotate");
	m_rotate->setText(tr("Turn (E)"));
	m_rotate->setCheckable(true);
	m_rotate->setChecked(true);
	m_move = new QToolButton;
	m_move->setObjectName("animGizmoMove");
	m_move->setText(tr("Move (W)"));
	m_move->setCheckable(true);
	top->addWidget(m_rotate);
	top->addWidget(m_move);
	m_ghosts = new QCheckBox(tr("Ghosts"));
	m_ghosts->setObjectName("animGhosts");
	m_ghosts->setToolTip(tr("Show the skeleton a few frames before and after, faint (onion skin)"));
	top->addWidget(m_ghosts);
	auto* saveButton = new QPushButton(tr("Save"));
	saveButton->setObjectName("animSave");
	saveButton->setToolTip(tr("Keep the keys in a project file below the mod's modelsrc/ (Ctrl+S) to go on later"));
	top->addWidget(saveButton);
	auto* preview = new QPushButton(tr("Preview in Mapsmith"));
	preview->setObjectName("animPreview");
	preview->setToolTip(tr("Play it on the Animations page now (not built into the mod yet)"));
	top->addWidget(preview);
	m_compile = new QPushButton(tr("Build ..."));
	m_compile->setObjectName("animCompile");
	m_compile->setProperty("accent", true);
	auto* compileMenu = new QMenu(m_compile);
	compileMenu->addAction(tr("As a new animation (Mapsmith's copy of the model) ..."), this, [this] { previewInMapsmith(true); });
	compileMenu->addAction(tr("Replace the animation in the model (into the mod) ..."), this, [this] {
		if (!m_edit) return;
		// Which sequence: the tweaked one, or one to pick for a new animation.
		QDialog dialog(this);
		dialog.setWindowTitle(tr("Replace in the model"));
		auto* form = new QFormLayout(&dialog);
		auto* which = new QComboBox;
		for (const auto& seq : m_animated->sequences()) {
			if (seq.model == 0) which->addItem(qs(seq.name));
		}
		const QString wanted = qs(m_edit->clip().mode == AnimClip::Mode::Tweak ? m_edit->clip().baseSequence : m_edit->clip().name);
		which->setCurrentIndex(std::max(0, which->findText(wanted, Qt::MatchFixedString)));
		form->addRow(tr("Sequence"), which);
		form->addRow(noteLabel(tr("Mapsmith takes %1 apart, puts this animation in place of the sequence and builds the model into the mod at the same "
		                          "path - the game then uses it instead of its own. Facial flexes, LODs and IK rules are not carried over. The mod's "
		                          "earlier files are kept, \"Restore the original model\" puts them back.")
		                           .arg(m_model.section('/', -1))));
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		buttons->button(QDialogButtonBox::Ok)->setText(tr("Build into the mod"));
		form->addRow(buttons);
		connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		if (which->count() == 0) buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
		if (dialog.exec() == QDialog::Accepted) replaceInModel(which->currentText());
	});
	auto* restore = compileMenu->addAction(tr("Restore the original model"), this, [this] {
		QString error;
		if (!restoreOriginal(&error)) setMessage(error);
	});
	connect(compileMenu, &QMenu::aboutToShow, this, [this, restore] { restore->setEnabled(replaced()); });
	m_compile->setMenu(compileMenu);
	top->addWidget(m_compile);
	root->addLayout(top);

	// Middle: bones | 3D | values.
	auto* split = new QSplitter(Qt::Horizontal);
	split->setChildrenCollapsible(false);
	auto* left = new QWidget;
	auto* lv = new QVBoxLayout(left);
	lv->setContentsMargins(0, 0, 0, 0);
	m_boneSearch = new QLineEdit;
	m_boneSearch->setObjectName("animBoneSearch");
	m_boneSearch->setPlaceholderText(tr("Search bones"));
	m_boneSearch->setClearButtonEnabled(true);
	lv->addWidget(m_boneSearch);
	m_boneGroup = new QComboBox;
	m_boneGroup->setObjectName("animBoneGroup");
	lv->addWidget(m_boneGroup);
	m_bones = new QTreeWidget;
	m_bones->setObjectName("animBones");
	m_bones->setHeaderHidden(true);
	m_bones->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_bones->setUniformRowHeights(true);
	lv->addWidget(m_bones, 1);
	left->setMinimumWidth(180);
	split->addWidget(left);

	m_view = new AnimEditView(m_materials);
	m_view->setObjectName("animView");
	m_view->setMinimumSize(360, 300);
	split->addWidget(m_view);

	auto* right = new QWidget;
	auto* rv = new QVBoxLayout(right);
	rv->setContentsMargins(0, 0, 0, 0);
	auto* boneBox = new QGroupBox(tr("Bone"));
	auto* bf = new QFormLayout(boneBox);
	m_boneName = new QLabel(tr("Click a bone"));
	m_boneName->setObjectName("animBoneName");
	m_boneName->setWordWrap(true);
	bf->addRow(m_boneName);
	const char* rotNames[3] = {QT_TR_NOOP("Roll (X)"), QT_TR_NOOP("Pitch (Y)"), QT_TR_NOOP("Yaw (Z)")};
	const char* posNames[3] = {QT_TR_NOOP("Move X"), QT_TR_NOOP("Move Y"), QT_TR_NOOP("Move Z")};
	for (int i = 0; i < 3; ++i) {
		m_rot[i] = valueBox(360, 1, QStringLiteral("°"));
		m_rot[i]->setObjectName(QString("animRot%1").arg(i));
		bf->addRow(tr(rotNames[i]), m_rot[i]);
		connect(m_rot[i], &QDoubleSpinBox::valueChanged, this, &AnimationEditor::applyFields);
	}
	for (int i = 0; i < 3; ++i) {
		m_pos[i] = valueBox(10000, 2, QString());
		m_pos[i]->setObjectName(QString("animPos%1").arg(i));
		bf->addRow(tr(posNames[i]), m_pos[i]);
		connect(m_pos[i], &QDoubleSpinBox::valueChanged, this, &AnimationEditor::applyFields);
	}
	bf->addRow(noteLabel(tr("Changes on top of the original pose, at this frame.")));
	m_interp = new QComboBox;
	m_interp->setObjectName("animInterp");
	m_interp->addItem(tr("Smooth"), int(KeyInterp::Smooth));
	m_interp->addItem(tr("Linear"), int(KeyInterp::Linear));
	m_interp->setToolTip(tr("How the motion runs from this key to the next: smooth (eases in and out) or linear (even speed)"));
	bf->addRow(tr("To next key"), m_interp);
	auto* keyRow = new QHBoxLayout;
	auto* key = new QPushButton(tr("Key (K)"));
	key->setObjectName("animKey");
	key->setToolTip(tr("Set a key for the chosen bones at this frame"));
	auto* del = new QPushButton(tr("Delete key"));
	del->setObjectName("animDeleteKey");
	keyRow->addWidget(key);
	keyRow->addWidget(del);
	bf->addRow(keyRow);
	auto* resetRow = new QHBoxLayout;
	auto* reset = new QPushButton(tr("Original pose"));
	reset->setObjectName("animResetBone");
	reset->setToolTip(tr("Key the chosen bones back to the original pose at this frame"));
	auto* clear = new QPushButton(tr("Clear bone"));
	clear->setObjectName("animClearBone");
	clear->setToolTip(tr("Remove every key of the chosen bones"));
	resetRow->addWidget(reset);
	resetRow->addWidget(clear);
	bf->addRow(resetRow);
	auto* poseRow = new QHBoxLayout;
	auto* copy = new QPushButton(tr("Copy pose"));
	copy->setObjectName("animCopyPose");
	auto* paste = new QPushButton(tr("Paste"));
	paste->setObjectName("animPastePose");
	auto* mirror = new QPushButton(tr("Mirror"));
	mirror->setObjectName("animMirror");
	mirror->setToolTip(tr("Give the chosen bones their other side's pose, mirrored (all bones when none is chosen: the whole pose flipped)"));
	poseRow->addWidget(copy);
	poseRow->addWidget(paste);
	poseRow->addWidget(mirror);
	bf->addRow(poseRow);
	rv->addWidget(boneBox);

	auto* clipBox = new QGroupBox(tr("Animation"));
	auto* cf = new QFormLayout(clipBox);
	m_name = new QLineEdit;
	m_name->setObjectName("animName");
	cf->addRow(tr("Name"), m_name);
	m_activity = new QLineEdit;
	m_activity->setObjectName("animActivity");
	m_activity->setPlaceholderText(tr("e.g. ACT_VM_RELOAD"));
	cf->addRow(tr("Activity"), m_activity);
	m_length = new QSpinBox;
	m_length->setObjectName("animLength");
	m_length->setRange(1, ANIM_CLIP_MAX_FRAMES);
	m_length->setKeyboardTracking(false);
	m_length->setToolTip(tr("Length in frames: keys and the original animation stretch with it (fewer frames: faster)"));
	cf->addRow(tr("Frames"), m_length);
	m_fps = new QDoubleSpinBox;
	m_fps->setObjectName("animFps");
	m_fps->setRange(1, 1000);
	m_fps->setDecimals(1);
	m_fps->setKeyboardTracking(false);
	cf->addRow(tr("Frames per second"), m_fps);
	m_loop = new QCheckBox(tr("Loops"));
	m_loop->setObjectName("animLoop");
	cf->addRow(m_loop);
	m_modeNote = noteLabel(QString());
	m_modeNote->setWordWrap(true);
	cf->addRow(m_modeNote);
	rv->addWidget(clipBox);
	rv->addStretch(1);
	auto* rightScroll = new QScrollArea;
	rightScroll->setWidget(right);
	rightScroll->setWidgetResizable(true);
	rightScroll->setFrameShape(QFrame::NoFrame);
	rightScroll->setMinimumWidth(250);
	split->addWidget(rightScroll);
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 1);
	split->setStretchFactor(2, 0);
	split->setSizes({210, 760, 270});
	root->addWidget(split, 1);

	// Bottom: transport and timeline.
	auto* transport = new QHBoxLayout;
	m_play = new QToolButton;
	m_play->setObjectName("animPlay");
	m_play->setText(tr("Play"));
	m_play->setToolTip(tr("Play or pause (Space)"));
	transport->addWidget(m_play);
	m_frameBox = new QSpinBox;
	m_frameBox->setObjectName("animFrame");
	m_frameBox->setKeyboardTracking(false);
	transport->addWidget(new QLabel(tr("Frame")));
	transport->addWidget(m_frameBox);
	m_frameOf = new QLabel;
	transport->addWidget(m_frameOf);
	m_timeline = new AnimTimeline;
	transport->addWidget(m_timeline, 1);
	root->addLayout(transport);
	m_status = new QLabel;
	m_status->setObjectName("animStatus");
	m_status->setWordWrap(true);
	root->addWidget(m_status);
	m_log = new QPlainTextEdit;
	m_log->setObjectName("animLog");
	m_log->setReadOnly(true);
	m_log->setMaximumHeight(110);
	m_log->hide();
	root->addWidget(m_log);

	m_timer = new QTimer(this);
	m_timer->setInterval(16);
	connect(m_timer, &QTimer::timeout, this, &AnimationEditor::tick);

	// Wiring.
	connect(m_undo, &QPushButton::clicked, this, [this] {
		if (m_edit && m_edit->undo()) refresh();
	});
	connect(m_redo, &QPushButton::clicked, this, [this] {
		if (m_edit && m_edit->redo()) refresh();
	});
	connect(m_camera, &QComboBox::currentIndexChanged, this, [this](int index) {
		m_view->setCameraMode(index == 1 ? AnimEditView::CameraMode::Viewmodel : AnimEditView::CameraMode::Free);
	});
	connect(m_rotate, &QToolButton::clicked, this, [this] {
		m_view->setGizmo(AnimEditView::Gizmo::Rotate);
		m_rotate->setChecked(true);
		m_move->setChecked(false);
	});
	connect(m_move, &QToolButton::clicked, this, [this] {
		m_view->setGizmo(AnimEditView::Gizmo::Move);
		m_move->setChecked(true);
		m_rotate->setChecked(false);
	});
	connect(m_ghosts, &QCheckBox::toggled, this, [this] {
		m_shownRevision = -1;
		refresh();
	});
	connect(saveButton, &QPushButton::clicked, this, [this] {
		QString error;
		if (!save(&error)) setMessage(error);
	});
	connect(preview, &QPushButton::clicked, this, [this] { previewInMapsmith(false); });
	connect(m_boneSearch, &QLineEdit::textChanged, this, &AnimationEditor::filterBones);
	connect(m_boneGroup, &QComboBox::currentIndexChanged, this, &AnimationEditor::filterBones);
	connect(m_bones, &QTreeWidget::itemSelectionChanged, this, [this] {
		if (m_filling) return;
		std::vector<int> chosen;
		for (auto* item : m_bones->selectedItems()) chosen.push_back(item->data(0, Qt::UserRole).toInt());
		// The current item gets the gizmo: put it last.
		if (auto* current = m_bones->currentItem(); current && current->isSelected()) {
			const int active = current->data(0, Qt::UserRole).toInt();
			std::erase(chosen, active);
			chosen.push_back(active);
		}
		m_selection = chosen;
		m_shownRevision = -1;
		refresh();
	});
	connect(m_view, &AnimEditView::boneClicked, this, [this](int bone, bool add) {
		if (bone < 0) return;
		std::vector<int> chosen = add ? m_selection : std::vector<int>{};
		std::erase(chosen, bone);
		chosen.push_back(bone);
		selectBones(chosen);
	});
	connect(m_view, &AnimEditView::dragStarted, this, [this] {
		if (!m_edit) return;
		setPlaying(false);
		m_edit->beginChange();
	});
	connect(m_view, &AnimEditView::rotateDragged, this, [this](const AnimQuat& rotation) {
		if (!m_edit || m_selection.empty()) return;
		m_edit->revertChange();
		m_edit->rotateBone(m_selection.back(), m_frame, rotation);
		refresh();
	});
	connect(m_view, &AnimEditView::moveDragged, this, [this](const AnimVec& offset) {
		if (!m_edit || m_selection.empty()) return;
		m_edit->revertChange();
		m_edit->moveBone(m_selection.back(), m_frame, offset);
		refresh();
	});
	connect(m_view, &AnimEditView::dragFinished, this, [this] {
		if (!m_edit) return;
		m_edit->endChange();
		refresh();
	});
	connect(m_timeline, &AnimTimeline::frameChanged, this, [this](int f) { setFrame(f); });
	connect(m_timeline, &AnimTimeline::keyMoved, this, [this](int from, int to, bool all, bool copyKey) {
		if (!m_edit) return;
		m_edit->moveKeys(all ? std::vector<int>{} : m_selection, from, to, copyKey);
		setFrame(to);
		refresh();
	});
	connect(m_timeline, &AnimTimeline::keyMenuRequested, this, &AnimationEditor::showKeyMenu);
	connect(m_frameBox, &QSpinBox::valueChanged, this, [this](int f) {
		if (!m_filling) setFrame(f);
	});
	connect(m_play, &QToolButton::clicked, this, [this] { setPlaying(!m_playing); });
	connect(key, &QPushButton::clicked, this, &AnimationEditor::keySelection);
	connect(del, &QPushButton::clicked, this, &AnimationEditor::deleteKey);
	connect(reset, &QPushButton::clicked, this, [this] {
		if (!m_edit || m_selection.empty()) return;
		m_edit->resetBones(m_selection, m_frame);
		refresh();
	});
	connect(clear, &QPushButton::clicked, this, [this] {
		if (!m_edit || m_selection.empty()) return;
		m_edit->clearBones(m_selection);
		refresh();
	});
	connect(copy, &QPushButton::clicked, this, &AnimationEditor::copyPose);
	connect(paste, &QPushButton::clicked, this, &AnimationEditor::pastePose);
	connect(mirror, &QPushButton::clicked, this, &AnimationEditor::mirrorSelection);
	connect(m_interp, &QComboBox::activated, this, [this] {
		if (!m_edit) return;
		m_edit->setInterp(m_selection, m_frame, KeyInterp(m_interp->currentData().toInt()));
		refresh();
	});
	connect(m_name, &QLineEdit::editingFinished, this, [this] {
		if (!m_edit) return;
		m_edit->setName(m_name->text().toStdString());
		refresh();
	});
	connect(m_activity, &QLineEdit::editingFinished, this, [this] {
		if (!m_edit) return;
		m_edit->setActivity(m_activity->text().trimmed().toStdString());
		refresh();
	});
	connect(m_length, &QSpinBox::valueChanged, this, [this](int frames) {
		if (!m_edit || m_filling) return;
		const int old = m_edit->clip().frames;
		m_edit->setFrameCount(frames);
		// The playhead stays at the same point of the animation.
		m_frame = old > 1 ? int(std::lround(double(m_frame) * double(frames - 1) / double(old - 1))) : 0;
		refresh();
	});
	connect(m_fps, &QDoubleSpinBox::valueChanged, this, [this](double fps) {
		if (!m_edit || m_filling) return;
		m_edit->setFps(float(fps));
		refresh();
	});
	connect(m_loop, &QCheckBox::toggled, this, [this](bool on) {
		if (!m_edit || m_filling) return;
		m_edit->setLoop(on);
		refresh();
	});

	// Keys like Source Filmmaker's.
	const auto shortcut = [this](const QKeySequence& sequence, auto slot) {
		auto* s = new QShortcut(sequence, this);
		s->setContext(Qt::WindowShortcut);
		connect(s, &QShortcut::activated, this, slot);
	};
	shortcut(QKeySequence(Qt::Key_K), [this] { keySelection(); });
	shortcut(QKeySequence(Qt::Key_Delete), [this] { deleteKey(); });
	shortcut(QKeySequence(Qt::Key_Space), [this] { setPlaying(!m_playing); });
	shortcut(QKeySequence(Qt::Key_Left), [this] { setFrame(m_frame - 1); });
	shortcut(QKeySequence(Qt::Key_Right), [this] { setFrame(m_frame + 1); });
	shortcut(QKeySequence(Qt::Key_Home), [this] { setFrame(0); });
	shortcut(QKeySequence(Qt::Key_End), [this] { setFrame(m_edit ? m_edit->clip().frames - 1 : 0); });
	shortcut(QKeySequence::Undo, [this] {
		if (m_edit && m_edit->undo()) refresh();
	});
	shortcut(QKeySequence::Redo, [this] {
		if (m_edit && m_edit->redo()) refresh();
	});
	shortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), [this] {
		if (m_edit && m_edit->redo()) refresh();
	});
	shortcut(QKeySequence::Copy, [this] { copyPose(); });
	shortcut(QKeySequence::Paste, [this] { pastePose(); });
	shortcut(QKeySequence::Save, [this] {
		QString error;
		if (!save(&error)) setMessage(error);
	});
	shortcut(QKeySequence(Qt::Key_E), [this] { m_rotate->click(); });
	shortcut(QKeySequence(Qt::Key_W), [this] { m_move->click(); });
	setMessage(tr("Open an animation from the Animations page (Edit animation ...)"));
}

// --- Loading ---------------------------------------------------------------------------------

QString AnimationEditor::modDir() const {
	const auto* entry = m_editor->gameEntry();
	return entry ? fromPath(entry->gameDir) : QString();
}

QString AnimationEditor::sourceFolder() const {
	return fromPath(modelExportFolder(toPath(modDir()), m_model.toStdString()));
}

QString AnimationEditor::projectPath() const {
	if (!m_edit || modDir().isEmpty()) return {};
	return QDir(modDir()).filePath(qs(animClipPath(m_model.toStdString(), m_edit->clip().name)));
}

bool AnimationEditor::replaced() const {
	return !m_model.isEmpty() && QFileInfo::exists(QDir(sourceFolder()).filePath("original/base.mdl"));
}

void AnimationEditor::openModel(const QString& model, const QString& sequence, bool newAnimation) {
	setPlaying(false);
	m_model = qs(normalizePath(model.toStdString()));
	m_sequence = sequence;
	m_new = newAnimation;
	m_pendingClip.reset();
	// A project saved for this model and animation goes on where it was left.
	if (!newAnimation && !sequence.isEmpty() && m_editor->gameEntry()) {
		const QString path = QDir(modDir()).filePath(qs(animClipPath(m_model.toStdString(), sequence.toStdString() + "_edit")));
		if (QFileInfo::exists(path)) {
			if (auto clip = readAnimClip(readFile(path).toStdString())) m_pendingClip = std::move(*clip);
		}
	}
	startLoading();
}

bool AnimationEditor::openProject(const QString& path, QString* error) {
	std::string why;
	auto clip = readAnimClip(readFile(path).toStdString(), &why);
	if (!clip) {
		if (error) *error = tr("%1 cannot be read: %2").arg(QFileInfo(path).fileName(), qs(why));
		return false;
	}
	if (clip->model.empty()) {
		if (error) *error = tr("%1 names no model").arg(QFileInfo(path).fileName());
		return false;
	}
	setPlaying(false);
	m_model = qs(normalizePath(clip->model));
	m_sequence = qs(clip->baseSequence);
	m_new = clip->mode == AnimClip::Mode::New;
	m_pendingClip = std::move(*clip);
	startLoading();
	return true;
}

void AnimationEditor::startLoading() {
	m_edit.reset();
	m_animated.reset();
	m_selection.clear();
	m_frame = 0;
	m_shownRevision = -1;
	m_view->setSkeleton(nullptr, {});
	m_bones->clear();
	updateTitle();
	const auto* game = m_editor->game();
	if (!game || m_model.isEmpty()) {
		setMessage(tr("Open a game first"));
		return;
	}
	setMessage(tr("Loading %1 ...").arg(m_model));
	m_geometry = m_editor->models() ? m_editor->models()->get(m_model) : nullptr;
	m_view->setModel(m_geometry);
	if (!m_geometry) m_view->setMessage(tr("Loading %1 ...").arg(m_model.section('/', -1)));
	const GameFileSystem* fs = &game->files;
	const std::string path = m_model.toStdString();
	// After a rebuild the mod's model has the new animations; the tweak goes on from the model as
	// it was (kept in original/), so keys are not applied twice.
	const QString original = QDir(sourceFolder()).filePath("original/base.mdl");
	const QByteArray originalMdl = QFileInfo::exists(original) ? readFile(original) : QByteArray();
	const QByteArray originalAni = QFileInfo::exists(original) ? readFile(original.chopped(4) + ".ani") : QByteArray();
	const int generation = ++m_generation;
	m_loader.setProperty("generation", generation);
	m_loader.setFuture(QtConcurrent::run([fs, path, originalMdl, originalAni] {
		Loaded out;
		std::string error;
		if (originalMdl.isEmpty()) {
			out.model = AnimatedModel::load(*fs, path, &error);
		} else {
			std::vector<AnimatedModel::FileData> files;
			files.push_back({path, toBytes(originalMdl), toBytes(originalAni)});
			if (const auto info = readModelInfo(files.front().mdl)) {
				for (auto include : info->includeModels) {
					include = normalizePath(include);
					if (!startsWithI(include, "models/")) include = "models/" + include;
					if (auto mdl = fs->read(include)) {
						AnimatedModel::FileData data{include, std::move(*mdl), {}};
						if (auto ani = fs->read(include.substr(0, include.size() - 4) + ".ani")) data.ani = std::move(*ani);
						files.push_back(std::move(data));
					}
				}
			}
			out.model = AnimatedModel::fromFiles(std::move(files), &error);
		}
		if (!out.model) out.error = QString::fromStdString(error);
		return out;
	}));
}

void AnimationEditor::loadingDone() {
	if (m_loader.property("generation").toInt() != m_generation) return;
	Loaded result = m_loader.result();
	if (!result.model) {
		setMessage(tr("Cannot load the animations of %1: %2").arg(m_model, result.error));
		emit loaded(false);
		return;
	}
	if (result.model->bones().empty()) {
		setMessage(tr("%1 has no skeleton to animate").arg(m_model));
		emit loaded(false);
		return;
	}
	m_animated = std::move(result.model);
	beginEditing();
}

void AnimationEditor::geometryArrived() {
	m_geometry = m_editor->models()->get(m_model);
	m_view->setModel(m_geometry);
	if (!m_geometry) m_view->setMessage(tr("Cannot load: %1").arg(m_editor->models()->error(m_model)));
	m_shownRevision = -1;
	refresh();
}

void AnimationEditor::beginEditing() {
	AnimClip clip;
	if (m_pendingClip) {
		clip = std::move(*m_pendingClip);
		m_pendingClip.reset();
	} else {
		clip.model = m_model.toStdString();
		clip.baseSequence = m_sequence.toStdString();
		clip.mode = m_new ? AnimClip::Mode::New : AnimClip::Mode::Tweak;
		const int base = m_animated->findSequence(clip.baseSequence);
		if (base >= 0) {
			const auto& seq = m_animated->sequences()[std::size_t(base)];
			clip.fps = seq.fps > 0 ? seq.fps : 30;
			clip.loop = seq.looping;
			clip.activity = seq.activity;
			clip.frames = m_new ? 30 : std::max(1, seq.frames);
		} else {
			// No sequence given: a new animation from the model's idle (or its rest pose).
			const int idle = m_animated->idleSequence();
			clip.baseSequence = idle >= 0 ? m_animated->sequences()[std::size_t(idle)].name : std::string();
			clip.mode = AnimClip::Mode::New;
			clip.frames = 30;
			m_new = true;
		}
		clip.name = m_new ? "new_animation" : sequenceNameFromFile(m_sequence.toStdString() + "_edit");
		if (m_new) clip.activity.clear();
	}
	clip.model = m_model.toStdString();
	m_edit = std::make_unique<AnimEditor>(*m_animated, std::move(clip));
	m_savedRevision = m_edit->revision();
	fillBoneTree();
	m_frame = 0;
	m_shownRevision = -1;
	updateTitle();
	refresh();
	// The camera that fits: the player's view for a first-person weapon.
	const bool viewmodel = m_model.section('/', -1).startsWith("v_", Qt::CaseInsensitive) || m_model.contains("/c_arms", Qt::CaseInsensitive);
	m_camera->setCurrentIndex(viewmodel ? 1 : 0);
	setMessage(tr("Click a bone, turn it with the rings (or move it with W), K sets a key. Space plays."));
	emit loaded(true);
}

void AnimationEditor::fillBoneTree() {
	m_filling = true;
	m_bones->clear();
	const auto& bones = m_edit->bones();
	std::vector<QTreeWidgetItem*> items(bones.size(), nullptr);
	std::set<QString> groups;
	for (std::size_t i = 0; i < bones.size(); ++i) {
		auto* item = new QTreeWidgetItem;
		QString name = qs(bones[i].name);
		// "ValveBiped.Bip01_R_Hand" reads better as "R_Hand" (the full name is the tooltip).
		const QString shortName = name.section('.', -1).startsWith("Bip01_") ? name.section('.', -1).mid(6) : name.section('.', -1);
		item->setText(0, shortName.isEmpty() ? name : shortName);
		item->setToolTip(0, name);
		item->setData(0, Qt::UserRole, int(i));
		const QString group = qs(boneGroup(bones[i].name));
		item->setData(0, Qt::UserRole + 1, group);
		if (!group.isEmpty()) groups.insert(group);
		items[i] = item;
		const int parent = bones[i].parent;
		if (parent >= 0 && std::size_t(parent) < i) items[std::size_t(parent)]->addChild(item);
		else m_bones->addTopLevelItem(item);
	}
	m_bones->expandAll();
	{
		QSignalBlocker block(m_boneGroup);
		m_boneGroup->clear();
		m_boneGroup->addItem(tr("All bones"), QString());
		for (const auto& g : groups) m_boneGroup->addItem(tr(g.toUtf8().constData()), g);
	}
	m_filling = false;
	filterBones();
}

void AnimationEditor::filterBones() {
	const QString search = m_boneSearch->text().trimmed();
	const QString group = m_boneGroup->currentData().toString();
	// An item shows when it matches, or when one of its children does (the way to it).
	std::function<bool(QTreeWidgetItem*)> apply = [&](QTreeWidgetItem* item) {
		bool childShown = false;
		for (int i = 0; i < item->childCount(); ++i) childShown |= apply(item->child(i));
		const bool match = (search.isEmpty() || item->toolTip(0).contains(search, Qt::CaseInsensitive)) &&
		                   (group.isEmpty() || item->data(0, Qt::UserRole + 1).toString() == group);
		item->setHidden(!match && !childShown);
		return match || childShown;
	};
	for (int i = 0; i < m_bones->topLevelItemCount(); ++i) apply(m_bones->topLevelItem(i));
}

// --- Editing ---------------------------------------------------------------------------------

void AnimationEditor::setFrame(int frame) {
	if (!m_edit) return;
	frame = std::clamp(frame, 0, m_edit->clip().frames - 1);
	if (frame == m_frame && !m_playing) return;
	m_frame = frame;
	m_playFrame = float(frame);
	m_shownRevision = -1;
	refresh();
}

void AnimationEditor::selectBones(const std::vector<int>& bones) {
	m_selection = bones;
	m_filling = true;
	m_bones->clearSelection();
	std::function<void(QTreeWidgetItem*)> mark = [&](QTreeWidgetItem* item) {
		const int bone = item->data(0, Qt::UserRole).toInt();
		if (std::find(bones.begin(), bones.end(), bone) != bones.end()) {
			item->setSelected(true);
			if (!bones.empty() && bone == bones.back()) {
				m_bones->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
				m_bones->scrollToItem(item);
			}
		}
		for (int i = 0; i < item->childCount(); ++i) mark(item->child(i));
	};
	for (int i = 0; i < m_bones->topLevelItemCount(); ++i) mark(m_bones->topLevelItem(i));
	m_filling = false;
	m_shownRevision = -1;
	refresh();
}

void AnimationEditor::keySelection() {
	if (!m_edit) return;
	if (m_selection.empty()) {
		setMessage(tr("Choose a bone first (click it in the view or the list)"));
		return;
	}
	m_edit->keyBones(m_selection, m_frame);
	refresh();
}

void AnimationEditor::deleteKey() {
	if (!m_edit) return;
	if (!m_edit->deleteKeys(m_selection, m_frame)) setMessage(tr("No key at frame %1 for the chosen bones").arg(m_frame));
	refresh();
}

void AnimationEditor::copyPose() {
	if (!m_edit) return;
	m_clipboard = m_edit->copyPose(m_selection, m_frame);
	setMessage(tr("Pose of %n bone(s) copied", nullptr, int(m_clipboard.bones.size())));
}

void AnimationEditor::pastePose() {
	if (!m_edit || m_clipboard.bones.empty()) return;
	m_edit->pastePose(m_clipboard, m_frame);
	refresh();
}

void AnimationEditor::mirrorSelection() {
	if (!m_edit) return;
	const int changed = m_edit->mirrorPose(m_selection, m_frame);
	setMessage(changed > 0 ? tr("Mirrored %n bone(s)", nullptr, changed) : tr("Nothing to mirror: the pose is the same on both sides"));
	refresh();
}

void AnimationEditor::setPlaying(bool on) {
	if (on && !m_edit) return;
	m_playing = on;
	if (m_play) m_play->setText(on ? tr("Pause") : tr("Play"));
	if (on) {
		m_playFrame = float(m_frame);
		m_lastTick = QDateTime::currentMSecsSinceEpoch();
		m_timer->start();
	} else if (m_timer) {
		m_timer->stop();
		m_shownRevision = -1;
		refresh();
	}
}

void AnimationEditor::tick() {
	if (!m_edit || !m_playing) return;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const float dt = float(now - m_lastTick) / 1000.f;
	m_lastTick = now;
	const float last = float(std::max(1, m_edit->clip().frames - 1));
	m_playFrame += dt * m_edit->clip().fps;
	if (m_playFrame > last) m_playFrame = std::fmod(m_playFrame, last);   // the preview repeats, looping or not
	m_frame = std::clamp(int(m_playFrame), 0, m_edit->clip().frames - 1);
	m_shownRevision = -1;
	refresh();
}

void AnimationEditor::refresh() {
	if (!m_edit) {
		m_undo->setEnabled(false);
		m_redo->setEnabled(false);
		return;
	}
	const bool changed = m_edit->revision() != m_shownRevision;
	m_shownRevision = m_edit->revision();
	if (!changed) return;
	const float shownFrame = m_playing ? m_playFrame : float(m_frame);
	// The posed model and its skeleton.
	if (m_geometry && !m_geometry->empty()) {
		std::vector<ModelVertex> posed;
		skinVertices(*m_geometry, m_edit->skinning(shownFrame), posed);
		m_view->setPose(std::move(posed));
	}
	m_view->setSkeleton(&m_edit->bones(), m_edit->boneToModel(shownFrame));
	m_view->setSelection(m_selection, m_selection.empty() ? -1 : m_selection.back());
	std::vector<bool> keyed(m_edit->bones().size(), false);
	for (std::size_t b = 0; b < keyed.size(); ++b) keyed[b] = m_edit->hasKey(int(b), m_frame);
	m_view->setKeyed(std::move(keyed));
	std::vector<std::vector<BoneMatrix>> ghosts;
	if (m_ghosts->isChecked() && !m_playing) {
		for (const int d : {-4, 4}) {
			const int f = m_frame + d;
			if (f >= 0 && f < m_edit->clip().frames) ghosts.push_back(m_edit->boneToModel(float(f)));
		}
	}
	m_view->setGhosts(std::move(ghosts));
	// Timeline.
	m_timeline->setFrameCount(m_edit->clip().frames);
	m_timeline->setFrame(m_frame);
	std::map<int, KeyInterp> chosen;
	for (const int b : m_selection) {
		if (b < 0 || b >= int(m_edit->bones().size())) continue;
		const auto it = m_edit->clip().tracks.find(m_edit->bones()[std::size_t(b)].name);
		if (it == m_edit->clip().tracks.end()) continue;
		for (const auto& k : it->second) chosen[k.frame] = k.interp;
	}
	m_timeline->setKeys(std::move(chosen), m_edit->keyFrames());
	refreshFields();
	m_undo->setEnabled(m_edit->canUndo());
	m_redo->setEnabled(m_edit->canRedo());
	updateTitle();
}

void AnimationEditor::refreshFields() {
	m_filling = true;
	const auto& clip = m_edit->clip();
	m_frameBox->setRange(0, clip.frames - 1);
	m_frameBox->setValue(m_frame);
	m_frameOf->setText(tr("of %1").arg(clip.frames - 1));
	if (!m_name->hasFocus()) m_name->setText(qs(clip.name));
	if (!m_activity->hasFocus()) m_activity->setText(qs(clip.activity));
	m_length->setValue(clip.frames);
	m_fps->setValue(clip.fps);
	m_loop->setChecked(clip.loop);
	const int active = m_selection.empty() ? -1 : m_selection.back();
	const bool has = active >= 0 && active < int(m_edit->bones().size());
	for (auto* box : m_rot) box->setEnabled(has);
	for (auto* box : m_pos) box->setEnabled(has);
	m_interp->setEnabled(has && m_edit->hasKey(active, m_frame));
	if (has) {
		const auto [rot, pos] = m_edit->offset(active, float(m_frame));
		const AnimVec angles = quaternionAngles(rot);
		const float values[3] = {angles.x, angles.y, angles.z};
		const float moves[3] = {pos.x, pos.y, pos.z};
		for (int i = 0; i < 3; ++i) {
			if (!m_rot[i]->hasFocus()) m_rot[i]->setValue(double(values[i]) * RAD);
			if (!m_pos[i]->hasFocus()) m_pos[i]->setValue(double(moves[i]));
		}
		QString name = qs(m_edit->bones()[std::size_t(active)].name);
		if (m_selection.size() > 1) name += tr(" (+%n more)", nullptr, int(m_selection.size()) - 1);
		name += m_edit->hasKey(active, m_frame) ? tr(" - key at this frame") : QString();
		m_boneName->setText(name);
		const auto& keys = m_edit->clip().tracks;
		const auto it = keys.find(m_edit->bones()[std::size_t(active)].name);
		if (it != keys.end()) {
			for (const auto& k : it->second) {
				if (k.frame == m_frame) m_interp->setCurrentIndex(m_interp->findData(int(k.interp)));
			}
		}
	} else {
		m_boneName->setText(tr("Click a bone in the view or the list"));
		for (int i = 0; i < 3; ++i) {
			m_rot[i]->setValue(0);
			m_pos[i]->setValue(0);
		}
	}
	m_modeNote->setText(clip.mode == AnimClip::Mode::Tweak
	                        ? tr("Changes on top of \"%1\": bones without keys move as in the original.").arg(qs(clip.baseSequence))
	                        : (clip.baseSequence.empty() ? tr("A new animation, starting from the rest pose.")
	                                                     : tr("A new animation, starting from the first pose of \"%1\".").arg(qs(clip.baseSequence))));
	m_filling = false;
}

void AnimationEditor::applyFields() {
	if (m_filling || !m_edit || m_selection.empty()) return;
	const int active = m_selection.back();
	const AnimVec angles{float(m_rot[0]->value() / RAD), float(m_rot[1]->value() / RAD), float(m_rot[2]->value() / RAD)};
	const AnimVec pos{float(m_pos[0]->value()), float(m_pos[1]->value()), float(m_pos[2]->value())};
	m_edit->setKey(active, m_frame, angleQuaternion(angles), pos);
	refresh();
}

void AnimationEditor::showKeyMenu(int frame, bool allBones, const QPoint& at) {
	if (!m_edit) return;
	setFrame(frame);
	const std::vector<int> bones = allBones ? std::vector<int>{} : m_selection;
	QMenu menu(this);
	menu.addAction(tr("Delete key"), this, [this, bones, frame] {
		m_edit->deleteKeys(bones, frame);
		refresh();
	});
	menu.addAction(tr("Smooth to the next key"), this, [this, bones, frame] {
		m_edit->setInterp(bones, frame, KeyInterp::Smooth);
		refresh();
	});
	menu.addAction(tr("Linear to the next key"), this, [this, bones, frame] {
		m_edit->setInterp(bones, frame, KeyInterp::Linear);
		refresh();
	});
	menu.exec(at);
}

void AnimationEditor::setMessage(const QString& text) {
	m_status->setText(text);
}

QString AnimationEditor::titleText() const {
	if (!m_edit) return m_model.isEmpty() ? tr("No model") : tr("%1 - loading").arg(m_model.section('/', -1));
	const auto& clip = m_edit->clip();
	const QString what = clip.mode == AnimClip::Mode::Tweak ? tr("improving \"%1\"").arg(qs(clip.baseSequence)) : tr("new animation");
	return tr("%1 - %2 as \"%3\"%4").arg(m_model.section('/', -1), what, qs(clip.name), m_edit->revision() != m_savedRevision ? QStringLiteral(" *") : QString());
}

void AnimationEditor::updateTitle() {
	const QString title = titleText();
	m_title->setText(title);
	setWindowTitle(tr("Animation editor - %1").arg(title));
}

// --- Results ---------------------------------------------------------------------------------

bool AnimationEditor::save(QString* error) {
	if (!m_edit) return false;
	const QString path = projectPath();
	if (path.isEmpty() || !writeFile(path, QByteArray::fromStdString(writeAnimClip(m_edit->clip())))) {
		if (error) *error = tr("Cannot save the project (%1)").arg(path.isEmpty() ? tr("no mod folder") : path);
		return false;
	}
	m_savedRevision = m_edit->revision();
	updateTitle();
	setMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path)));
	return true;
}

bool AnimationEditor::previewInMapsmith(bool build) {
	if (!m_edit) return false;
	if (!m_animations) {
		setMessage(tr("The Animations page is not there"));
		return false;
	}
	// The .smd next to the project file.
	const QString smdPath = projectPath().chopped(QStringLiteral(".smanim").size()) + ".smd";
	if (!writeFile(smdPath, QByteArray::fromStdString(m_edit->smd()))) {
		setMessage(tr("Cannot write %1").arg(smdPath));
		return false;
	}
	save();
	ImportedAnimation options;
	options.name = m_edit->clip().name;
	options.fps = m_edit->clip().fps;
	options.loop = m_edit->clip().loop;
	options.activity = m_edit->clip().activity;
	const auto bring = [this, smdPath, options, build] {
		QString why;
		if (!m_animations->importAnimation(smdPath, options, &why)) {
			setMessage(why);
			return;
		}
		if (build) m_animations->compileImports();
		setMessage(build ? tr("\"%1\" is being built into Mapsmith's copy of the model - see the Animations page").arg(qs(options.name))
		                 : tr("\"%1\" plays on the Animations page").arg(qs(options.name)));
	};
	if (m_animations->currentModel().compare(m_model, Qt::CaseInsensitive) == 0 && m_animations->animated()) {
		bring();
	} else {
		auto connection = std::make_shared<QMetaObject::Connection>();
		*connection = connect(m_animations, &AnimationsPage::modelLoaded, this, [this, connection, bring] {
			disconnect(*connection);
			if (m_animations && m_animations->animated()) bring();
		});
		m_animations->selectModel(m_model);
	}
	return true;
}

QString AnimationEditor::studiomdlProgram() const {
	return m_studiomdlOverride.isEmpty() ? ImportDialog::studiomdlPath(m_editor) : m_studiomdlOverride;
}

QString AnimationEditor::log() const {
	return m_log->toPlainText();
}

bool AnimationEditor::replaceInModel(const QString& sequence) {
	if (!m_edit || !m_editor->game() || modDir().isEmpty()) return false;
	if ((m_studiomdl && m_studiomdl->state() != QProcess::NotRunning) || m_decompile.isRunning()) return false;
	m_log->clear();
	m_log->show();
	const QString program = studiomdlProgram();
	if (program.isEmpty() || !QFileInfo::exists(program)) {
		m_log->appendPlainText(tr("studiomdl was not found (%1). It comes with the game's SDK tools, next to vbsp.exe - check the game's tools in Compile settings.")
		                           .arg(program.isEmpty() ? tr("no SDK tools set") : program));
		return false;
	}
	m_replaceSequence = sequence.isEmpty() ? qs(m_edit->clip().baseSequence) : sequence;
	if (m_replaceSequence.isEmpty()) {
		m_log->appendPlainText(tr("Choose the sequence to replace"));
		return false;
	}
	if (const int s = m_animated->findSequence(m_replaceSequence.toStdString()); s >= 0 && m_animated->sequences()[std::size_t(s)].blends > 1) {
		m_log->appendPlainText(tr("Note: \"%1\" blends several poses (aiming); it becomes this one animation.").arg(m_replaceSequence));
	}
	setPlaying(false);
	save();
	m_log->appendPlainText(tr("Taking %1 apart ...").arg(m_model));
	const GameFileSystem* fs = &m_editor->game()->files;
	const std::string path = m_model.toStdString();
	ModelExportOptions options;
	options.materials = false;
	options.pictures = false;
	m_decompile.setFuture(QtConcurrent::run([fs, path, options]() -> std::optional<ModelExport> { return decompileModel(*fs, path, options); }));
	return true;
}

void AnimationEditor::finishReplace(int code, bool crashed) {
	const QString mod = modDir();
	const QString base = QDir(mod).filePath(stem(m_model));
	ModelChecksums sums;
	sums.mdl = checksumOf(base + ".mdl", mdlChecksum);
	sums.vvd = checksumOf(base + ".vvd", vvdChecksum);
	sums.dx90 = checksumOf(base + ".dx90.vtx", vtxChecksum);
	sums.dx80 = checksumOf(base + ".dx80.vtx", vtxChecksum);
	sums.sw = checksumOf(base + ".sw.vtx", vtxChecksum);
	sums.phy = checksumOf(base + ".phy", phyChecksum);
	const bool ok = !crashed && code == 0 && sums.consistent();
	auto& files = m_editor->game()->files;
	if (ok) {
		for (const char* ext : MODEL_EXTENSIONS) {
			if (QFileInfo::exists(base + ext)) files.noteWrittenFile(toPath(base + ext));
		}
		if (m_editor->models()) m_editor->models()->forget(m_model);
		m_log->appendPlainText(tr("Built %1 into the mod - the game plays your \"%2\" now.").arg(m_model, m_replaceSequence));
		setMessage(tr("Built into the mod. \"Restore the original model\" (Build menu) puts the old one back."));
	} else {
		if (code == 0 && !crashed) m_log->appendPlainText(tr("The built files do not belong together (their checksums differ) - the game would refuse the model."));
		else m_log->appendPlainText(tr("studiomdl failed (exit code %1) - the lines above say why.").arg(code));
		// Nothing half-built stays: the new files go, the mod's own come back.
		for (const char* ext : MODEL_EXTENSIONS) QFile::remove(base + ext);
		for (const QString& file : std::as_const(m_movedAside)) QFile::rename(QDir(m_movedTo).filePath(QFileInfo(file).fileName()), file);
		if (m_firstReplace) QDir(QDir(sourceFolder()).filePath("original")).removeRecursively();
		setMessage(tr("Building failed - the mod's model is as it was. See the lines below."));
	}
	m_movedAside.clear();
	emit replaceFinished(ok);
}

bool AnimationEditor::restoreOriginal(QString* error) {
	const QString original = QDir(sourceFolder()).filePath("original");
	if (!replaced()) {
		if (error) *error = tr("The mod has no model rebuilt by Mapsmith for %1").arg(m_model);
		return false;
	}
	const QString mod = modDir();
	const QString base = QDir(mod).filePath(stem(m_model));
	const QString aside = QDir(sourceFolder()).filePath("backups/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + "-rebuilt");
	auto& files = m_editor->game()->files;
	for (const char* ext : MODEL_EXTENSIONS) {
		const QString file = base + ext;
		if (!QFileInfo::exists(file)) continue;
		QDir().mkpath(aside);
		QFile::remove(QDir(aside).filePath(QFileInfo(file).fileName()));
		if (QFile::rename(file, QDir(aside).filePath(QFileInfo(file).fileName()))) files.noteRemovedFile(toPath(file));
	}
	// The mod's own files from before (if it had any).
	const QDir kept(QDir(original).filePath("mod"));
	for (const QString& name : kept.entryList(QDir::Files)) {
		const QString target = QDir(QFileInfo(base).absolutePath()).filePath(name);
		QDir().mkpath(QFileInfo(target).absolutePath());
		if (QFile::rename(kept.filePath(name), target)) files.noteWrittenFile(toPath(target));
	}
	QDir(original).removeRecursively();
	if (m_editor->models()) m_editor->models()->forget(m_model);
	setMessage(tr("%1 is the original again (the rebuilt one is kept in %2)").arg(m_model, QDir::toNativeSeparators(aside)));
	// The base of the tweak is the same model, now from the game again.
	return true;
}

void AnimationEditor::closeEvent(QCloseEvent* event) {
	if (m_edit && m_edit->revision() != m_savedRevision) {
		const auto answer = QMessageBox::question(this, tr("Animation editor"), tr("Keep the changes to \"%1\" in its project file?").arg(qs(m_edit->clip().name)),
		                                          QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
		if (answer == QMessageBox::Cancel) {
			event->ignore();
			return;
		}
		if (answer == QMessageBox::Save) save();
	}
	setPlaying(false);
	event->accept();
}

} // namespace sme::app
