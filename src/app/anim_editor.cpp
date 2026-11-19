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

#include <QButtonGroup>
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
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtConcurrent>

#include <sme/animation_import.h>
#include <sme/assets.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>

#ifdef Q_OS_WIN
#include <qt_windows.h>
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
		// The new frames are already in the export (ModelExportOptions::replacements): the .qc keeps the
		// sequence's activity and its events (sounds, effects), written by decompileModel.
		ModelExport out = *exported;
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

	// Top row: what is edited, the mode, undo, save, build.
	auto* top = new QHBoxLayout;
	m_title = new QLabel(tr("No model"));
	m_title->setObjectName("animEditorTitle");
	m_title->setProperty("role", "sectionTitle");
	top->addWidget(m_title, 1);
	// Two modes: one change for every animation (the pose), or keys in one animation.
	m_modePose = new QToolButton;
	m_modePose->setObjectName("animModePose");
	m_modePose->setText(tr("Pose (all animations)"));
	m_modePose->setCheckable(true);
	m_modePose->setToolTip(tr("Change how the hands and arms hold the weapon in every animation at once: drag a grip, bend the fingers"));
	m_modeAnimate = new QToolButton;
	m_modeAnimate->setObjectName("animModeAnimate");
	m_modeAnimate->setText(tr("Animate (one animation)"));
	m_modeAnimate->setCheckable(true);
	m_modeAnimate->setChecked(true);
	m_modeAnimate->setToolTip(tr("Improve one animation or make a new one with keys, like in Source Filmmaker"));
	auto* modes = new QButtonGroup(this);
	modes->setExclusive(true);
	modes->addButton(m_modePose);
	modes->addButton(m_modeAnimate);
	top->addWidget(m_modePose);
	top->addWidget(m_modeAnimate);
	top->addSpacing(12);
	m_undo = new QPushButton(tr("Undo"));
	m_undo->setObjectName("animUndo");
	m_redo = new QPushButton(tr("Redo"));
	m_redo->setObjectName("animRedo");
	top->addWidget(m_undo);
	top->addWidget(m_redo);
	auto* saveButton = new QPushButton(tr("Save"));
	saveButton->setObjectName("animSave");
	saveButton->setToolTip(tr("Keep your work in a project file below the mod's modelsrc/ (Ctrl+S) to go on later"));
	top->addWidget(saveButton);
	m_compile = new QPushButton(tr("Build ..."));
	m_compile->setObjectName("animCompile");
	m_compile->setProperty("accent", true);

	// Animate: keys.
	m_animateTools = new QWidget;
	auto* animRow = new QHBoxLayout(m_animateTools);
	animRow->setContentsMargins(0, 0, 0, 0);
	auto* key = new QPushButton(tr("Set key (K)"));
	key->setObjectName("animKey");
	key->setToolTip(tr("Key the chosen bones (or the placed hand/foot) at the playhead"));
	animRow->addWidget(key);
	m_autoKeyBox = new QCheckBox(tr("Auto key"));
	m_autoKeyBox->setObjectName("animAutoKey");
	m_autoKeyBox->setChecked(true);
	m_autoKeyBox->setToolTip(tr("On: every move sets a key at the playhead. Off: a move only changes a key that is there - Set key first"));
	animRow->addWidget(m_autoKeyBox);
	animRow->addWidget(new QLabel(tr("To next key")));
	m_interpBar = new QComboBox;
	m_interpBar->setObjectName("animInterpBar");
	m_interpBar->setToolTip(tr("How the motion runs from the key at the playhead to the next key"));
	animRow->addWidget(m_interpBar);
	auto* animMenuButton = new QPushButton(tr("Animation ▾"));
	animMenuButton->setObjectName("animMenu");
	auto* animMenu = new QMenu(animMenuButton);
	animMenu->addAction(tr("Duplicate animation ..."), this, [this] {
		if (!m_edit) return;
		bool ok = false;
		const QString name = QInputDialog::getText(this, tr("Duplicate animation"), tr("Name of the copy"), QLineEdit::Normal,
		                                           qs(m_edit->clip().name) + "_copy", &ok);
		if (ok && !name.trimmed().isEmpty()) duplicateAnimation(name.trimmed());
	});
	animMenu->addAction(tr("Start from another animation ..."), this, [this] { showStartFromDialog(false); });
	animMenu->addAction(tr("New from scratch ..."), this, [this] { showStartFromDialog(true); });
	animMenu->addSeparator();
	animMenu->addAction(tr("Change only from the playhead on"), this, [this] { editFromPlayhead(); });
	animMenu->addAction(tr("Cut everything before the playhead"), this, [this] { cutBeforePlayhead(); });
	animMenu->addAction(tr("Cut everything after the playhead"), this, [this] { cutAfterPlayhead(); });
	animMenuButton->setMenu(animMenu);
	animRow->addWidget(animMenuButton);
	auto* preview = new QPushButton(tr("Preview in Mapsmith"));
	preview->setObjectName("animPreview");
	preview->setToolTip(tr("Play it on the Animations page now (not built into the mod yet)"));
	animRow->addWidget(preview);

	// Pose: which animation it is checked on.
	m_poseTools = new QWidget;
	auto* poseToolsRow = new QHBoxLayout(m_poseTools);
	poseToolsRow->setContentsMargins(0, 0, 0, 0);
	poseToolsRow->addWidget(new QLabel(tr("Check on")));
	m_poseSequence = new QComboBox;
	m_poseSequence->setObjectName("animPoseSequence");
	m_poseSequence->setToolTip(tr("The pose is the same in every animation - look at it on reload, fire, idle ... (Space plays)"));
	m_poseSequence->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	poseToolsRow->addWidget(m_poseSequence);
	auto* resetPose = new QPushButton(tr("Reset pose"));
	resetPose->setObjectName("animPoseReset");
	resetPose->setToolTip(tr("Back to the model's own pose (the hands and fingers as they were)"));
	poseToolsRow->addWidget(resetPose);
	connect(resetPose, &QPushButton::clicked, this, [this] {
		if (!m_edit || m_edit->keyCount() == 0) return;
		m_edit->clearBones({});
		m_shownRevision = -1;
		refresh();
	});
	m_poseTools->hide();

	// Second row: how it is seen, the mode's tools.
	auto* viewRow = new QHBoxLayout;
	viewRow->addWidget(new QLabel(tr("View")));
	m_camera = new QComboBox;
	m_camera->setObjectName("animCamera");
	m_camera->addItem(tr("3D (turn around it)"));
	m_camera->addItem(tr("Player's view"));
	m_camera->addItem(tr("Front"));
	m_camera->addItem(tr("Side"));
	m_camera->addItem(tr("Top"));
	m_camera->setToolTip(tr("3D: drag to turn around the model, right-drag to move, wheel to zoom, F frames the chosen bone.\n"
	                        "Player's view: as a first-person weapon is seen in the game (viewmodel_fov 54)"));
	viewRow->addWidget(m_camera);
	m_rotate = new QToolButton;
	m_rotate->setObjectName("animGizmoRotate");
	m_rotate->setText(tr("Turn (E)"));
	m_rotate->setCheckable(true);
	m_rotate->setChecked(true);
	m_rotate->setToolTip(tr("Click a bone, then drag its rings to turn it"));
	m_move = new QToolButton;
	m_move->setObjectName("animGizmoMove");
	m_move->setText(tr("Move (W)"));
	m_move->setCheckable(true);
	m_move->setToolTip(tr("Click a bone, then drag its arrows to move it"));
	viewRow->addWidget(m_rotate);
	viewRow->addWidget(m_move);
	// What else the view shows, in one menu.
	auto* show = new QToolButton;
	show->setObjectName("animShow");
	show->setText(tr("Show ▾"));
	show->setPopupMode(QToolButton::InstantPopup);
	auto* showMenu = new QMenu(show);
	auto* showBox = new QWidget;
	auto* showLayout = new QVBoxLayout(showBox);
	showLayout->setContentsMargins(8, 6, 8, 6);
	m_split = new QCheckBox(tr("Player's view beside it"));
	m_split->setObjectName("animSplit");
	m_split->setToolTip(tr("Show the player's view next to the 3D view (for weapons)"));
	m_pathBox = new QCheckBox(tr("Motion path"));
	m_pathBox->setObjectName("animPath");
	m_pathBox->setChecked(true);
	m_pathBox->setToolTip(tr("Show where the chosen bone goes over the whole animation (orange dots: keys)"));
	m_ghosts = new QCheckBox(tr("Ghosts"));
	m_ghosts->setObjectName("animGhosts");
	m_ghosts->setToolTip(tr("Show the skeleton a few frames before and after, faint (onion skin)"));
	for (auto* box : {m_split, m_pathBox, m_ghosts}) showLayout->addWidget(box);
	auto* showAction = new QWidgetAction(showMenu);
	showAction->setDefaultWidget(showBox);
	showMenu->addAction(showAction);
	show->setMenu(showMenu);
	viewRow->addWidget(show);
	viewRow->addSpacing(16);
	viewRow->addWidget(m_animateTools);
	viewRow->addWidget(m_poseTools);
	viewRow->addStretch(1);
	auto* compileMenu = new QMenu(m_compile);
	auto* buildPoseAction = compileMenu->addAction(tr("Build the pose into the model (all animations)"), this, [this] { buildPose(); });
	buildPoseAction->setObjectName("animBuildPose");
	auto* asNewAction = compileMenu->addAction(tr("As a new animation (Mapsmith's copy of the model) ..."), this, [this] { previewInMapsmith(true); });
	auto* replaceAction = compileMenu->addAction(tr("Replace the animation in the model (into the mod) ..."), this, [this] {
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
	connect(compileMenu, &QMenu::aboutToShow, this, [this, restore, buildPoseAction, asNewAction, replaceAction] {
		restore->setEnabled(replaced());
		buildPoseAction->setVisible(m_pose);
		asNewAction->setVisible(!m_pose);
		replaceAction->setVisible(!m_pose);
	});
	m_compile->setMenu(compileMenu);
	top->addWidget(m_compile);
	root->addLayout(top);
	root->addLayout(viewRow);
	// How it works, in three steps.
	m_howTo = noteLabel(QString());
	m_howTo->setObjectName("animHowTo");
	m_howTo->setWordWrap(true);
	root->addWidget(m_howTo);

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
	m_bonePanel = left;
	left->hide();   // under "More": clicking the model chooses bones
	split->addWidget(left);

	auto* views = new QWidget;
	auto* viewsLayout = new QHBoxLayout(views);
	viewsLayout->setContentsMargins(0, 0, 0, 0);
	viewsLayout->setSpacing(4);
	m_view = new AnimEditView(m_materials);
	m_view->setObjectName("animView");
	m_view->setMinimumSize(300, 260);
	viewsLayout->addWidget(m_view, 3);
	// The player's view of a weapon beside it (shows the same pose, its own camera).
	m_playerView = new AnimEditView(m_materials);
	m_playerView->setObjectName("animPlayerView");
	m_playerView->setCameraMode(AnimEditView::CameraMode::Viewmodel);
	m_playerView->setMinimumSize(200, 160);
	m_playerView->hide();
	viewsLayout->addWidget(m_playerView, 2);
	split->addWidget(views);

	auto* right = new QWidget;
	auto* rv = new QVBoxLayout(right);
	rv->setContentsMargins(0, 0, 0, 0);
	m_boneName = new QLabel(tr("Click a bone"));
	m_boneName->setObjectName("animBoneName");
	m_boneName->setWordWrap(true);
	rv->addWidget(m_boneName);
	// The rest of the bone's tools under "More".
	auto* boneBox = new QGroupBox(tr("Bone values"));
	auto* bf = new QFormLayout(boneBox);
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
	m_interp->setToolTip(tr("How the motion runs from this key to the next: smooth (eases in and out), linear (even speed), step "
	                        "(holds, then cuts), ease in (starts slow), ease out (ends slow)"));
	for (auto* box : {m_interp, m_interpBar}) {
		box->addItem(tr("Smooth"), int(KeyInterp::Smooth));
		box->addItem(tr("Linear"), int(KeyInterp::Linear));
		box->addItem(tr("Step"), int(KeyInterp::Step));
		box->addItem(tr("Ease in"), int(KeyInterp::EaseIn));
		box->addItem(tr("Ease out"), int(KeyInterp::EaseOut));
	}
	bf->addRow(tr("To next key"), m_interp);
	auto* keyRow = new QHBoxLayout;
	auto* del = new QPushButton(tr("Delete key"));
	del->setObjectName("animDeleteKey");
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

	// Hands: curl and spread the fingers (per finger under More).
	auto* handsBox = new QGroupBox(tr("Fingers"));
	handsBox->setObjectName("animHands");
	auto* hv = new QVBoxLayout(handsBox);
	m_handsContent = new QWidget;
	auto* hl = new QVBoxLayout(m_handsContent);
	hl->setContentsMargins(0, 0, 0, 0);
	hv->addWidget(m_handsContent);
	m_handsBox = handsBox;
	handsBox->hide();
	rv->addWidget(handsBox);

	auto* clipBox = new QGroupBox(tr("Animation"));
	m_clipBox = clipBox;
	auto* cf = new QFormLayout(clipBox);
	m_clipForm = cf;
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
	m_editFromBox = new QSpinBox;
	m_editFromBox->setObjectName("animEditFrom");
	m_editFromBox->setKeyboardTracking(false);
	m_editFromBox->setToolTip(tr("Frames before this one stay exactly as the original; changes start here"));
	cf->addRow(tr("Change from frame"), m_editFromBox);
	m_modeNote = noteLabel(QString());
	m_modeNote->setWordWrap(true);
	cf->addRow(m_modeNote);
	rv->addWidget(clipBox);
	m_moreButton = new QToolButton;
	m_moreButton->setObjectName("animMore");
	m_moreButton->setText(tr("More ▸"));
	m_moreButton->setCheckable(true);
	m_moreButton->setToolTip(tr("The bone list, exact values of the chosen bone, reset, copy and paste a pose, mirror, each finger on its own"));
	rv->addWidget(m_moreButton);
	m_moreBox = boneBox;
	boneBox->hide();
	rv->addWidget(boneBox);
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
	transport->addStretch(1);
	root->addLayout(transport);
	// The timeline over the whole width: easier to hit a frame and drag keys.
	m_timeline = new AnimTimeline;
	root->addWidget(m_timeline);
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
		if (index == 2) m_view->setPreset(AnimEditView::Preset::Front);
		if (index == 3) m_view->setPreset(AnimEditView::Preset::Side);
		if (index == 4) m_view->setPreset(AnimEditView::Preset::Top);
	});
	connect(m_split, &QCheckBox::toggled, this, &AnimationEditor::setSplitView);
	connect(m_autoKeyBox, &QCheckBox::toggled, this, [this](bool on) {
		setMessage(on ? tr("Auto key: every move sets a key at the playhead") : tr("Auto key off: Set key (K) first, then moves change that key"));
	});
	connect(m_interpBar, &QComboBox::activated, this, [this] { setInterpolation(KeyInterp(m_interpBar->currentData().toInt())); });
	connect(m_pathBox, &QCheckBox::toggled, this, [this] {
		m_pathRevision = -1;
		m_shownRevision = -1;
		refresh();
	});
	connect(m_moreButton, &QToolButton::toggled, this, [this](bool on) {
		m_moreBox->setVisible(on);
		m_bonePanel->setVisible(on);
		for (QWidget* w : {static_cast<QWidget*>(m_activity), static_cast<QWidget*>(m_fps), static_cast<QWidget*>(m_editFromBox)}) m_clipForm->setRowVisible(w, on);
		for (auto& hand : m_handSliders) {
			if (hand.fingerBox) hand.fingerBox->setVisible(on);
		}
		m_moreButton->setText(on ? tr("More ▾") : tr("More ▸"));
	});
	// The rarer animation settings wait under More too.
	for (QWidget* w : {static_cast<QWidget*>(m_activity), static_cast<QWidget*>(m_fps), static_cast<QWidget*>(m_editFromBox)}) m_clipForm->setRowVisible(w, false);
	connect(m_modePose, &QToolButton::clicked, this, [this] { setPoseMode(true); });
	connect(m_modeAnimate, &QToolButton::clicked, this, [this] { setPoseMode(false); });
	connect(m_poseSequence, &QComboBox::activated, this, [this] { setPoseSequence(m_poseSequence->currentText()); });
	connect(m_editFromBox, &QSpinBox::valueChanged, this, [this](int frame) {
		if (!m_edit || m_filling) return;
		m_edit->setEditFrom(frame);
		refresh();
	});
	connect(m_view, &AnimEditView::gripDragged, this, [this](int bone, const AnimVec& target) {
		if (!m_edit) return;
		const int chain = m_edit->chainOf(bone);
		if (chain < 0) return;
		m_edit->revertChange();
		m_edit->placeEnd(chain, m_frame, target);
		refresh();
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
		m_dragBone = m_selection.empty() ? -1 : m_selection.back();
		m_dragHadKey = m_dragBone >= 0 && m_edit->hasKey(m_dragBone, m_frame);
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
	connect(m_view, &AnimEditView::dragFinished, this, &AnimationEditor::finishDrag);
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
	connect(m_interp, &QComboBox::activated, this, [this] { setInterpolation(KeyInterp(m_interp->currentData().toInt())); });
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
	shortcut(QKeySequence(Qt::Key_F), [this] { m_view->focusBone(m_selection.empty() ? -1 : m_selection.back()); });
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
	m_openPose = false;
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
	m_parked.reset();
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
	m_playerView->setModel(m_geometry);
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
	m_playerView->setModel(m_geometry);
	if (!m_geometry) m_view->setMessage(tr("Cannot load: %1").arg(m_editor->models()->error(m_model)));
	m_shownRevision = -1;
	refresh();
}

AnimClip AnimationEditor::defaultClip() const {
	AnimClip clip;
	clip.model = m_model.toStdString();
	clip.baseSequence = m_sequence.toStdString();
	clip.mode = m_new ? AnimClip::Mode::New : AnimClip::Mode::Tweak;
	bool fresh = m_new;
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
		fresh = true;
	}
	clip.name = fresh ? "new_animation" : sequenceNameFromFile(m_sequence.toStdString() + "_edit");
	if (fresh) clip.activity.clear();
	return clip;
}

std::optional<AnimClip> AnimationEditor::savedClip(const std::string& name) const {
	if (modDir().isEmpty() || m_model.isEmpty() || name.empty()) return std::nullopt;
	const QString path = QDir(modDir()).filePath(qs(animClipPath(m_model.toStdString(), name)));
	if (!QFileInfo::exists(path)) return std::nullopt;
	return readAnimClip(readFile(path).toStdString());
}

AnimClip AnimationEditor::poseClip() const {
	if (auto saved = savedClip(POSE_LAYER_NAME); saved && saved->mode == AnimClip::Mode::Pose) {
		saved->model = m_model.toStdString();
		return *saved;
	}
	AnimClip clip;
	clip.model = m_model.toStdString();
	clip.name = POSE_LAYER_NAME;
	clip.mode = AnimClip::Mode::Pose;
	// Shown on the opened animation, else the idle, else the first one.
	int shown = m_sequence.isEmpty() ? -1 : m_animated->findSequence(m_sequence.toStdString());
	if (shown < 0) shown = m_animated->idleSequence();
	if (shown < 0 && !m_animated->sequences().empty()) shown = 0;
	if (shown >= 0) clip.baseSequence = m_animated->sequences()[std::size_t(shown)].name;
	return clip;
}

void AnimationEditor::beginEditing() {
	AnimClip clip;
	if (m_pendingClip) {
		clip = std::move(*m_pendingClip);
		m_pendingClip.reset();
	} else if (m_openPose) {
		clip = poseClip();
	} else {
		clip = defaultClip();
	}
	m_openPose = false;
	m_new = clip.mode == AnimClip::Mode::New;
	m_pose = clip.mode == AnimClip::Mode::Pose;
	m_parked.reset();
	clip.model = m_model.toStdString();
	m_edit = std::make_unique<AnimEditor>(*m_animated, std::move(clip));
	if (m_pose) m_edit->setPreviewSequence(m_edit->clip().baseSequence);
	m_savedRevision = m_edit->revision();
	fillBoneTree();
	fillHands();
	fillPoseSequences();
	applyMode();
	m_frame = 0;
	m_shownRevision = -1;
	updateTitle();
	refresh();
	// Work in 3D; a first-person weapon also gets the player's view beside it.
	const bool viewmodel = m_model.section('/', -1).startsWith("v_", Qt::CaseInsensitive) || m_model.contains("/c_arms", Qt::CaseInsensitive);
	m_camera->setCurrentIndex(0);
	m_split->setChecked(viewmodel);
	m_view->frameModel();
	setMessage(m_edit->ikChains().empty() ? tr("Click a bone, turn it with the rings (or move it with W), K sets a key. Space plays.")
	                                      : tr("Drag an orange grip to place a hand or foot - the arm or leg follows. Space plays."));
	emit loaded(true);
}

// --- Modes: Pose (all animations) and Animate (one animation) ----------------------------------

void AnimationEditor::openPose(const QString& model) {
	openModel(model, QString(), false);
	m_openPose = true;   // the loading has only started: beginEditing() takes it
}

void AnimationEditor::setPoseMode(bool on) {
	if (!m_animated || !m_edit) {
		m_openPose = on;
		applyMode();
		return;
	}
	if (on == m_pose) {
		applyMode();
		return;
	}
	setPlaying(false);
	std::unique_ptr<AnimEditor> next;
	int nextSaved = 0;
	if (m_parked && (m_parked->clip().mode == AnimClip::Mode::Pose) == on) {
		next = std::move(m_parked);
		nextSaved = m_parkedSavedRevision;
	} else {
		AnimClip clip = on ? poseClip() : defaultClip();
		clip.model = m_model.toStdString();
		next = std::make_unique<AnimEditor>(*m_animated, std::move(clip));
		if (on) next->setPreviewSequence(next->clip().baseSequence);
		nextSaved = next->revision();
	}
	m_parked = std::move(m_edit);
	m_parkedSavedRevision = m_savedRevision;
	m_edit = std::move(next);
	m_savedRevision = nextSaved;
	m_pose = on;
	m_new = m_edit->clip().mode == AnimClip::Mode::New;
	m_selection.clear();
	m_frame = 0;
	m_shownRevision = -1;
	m_pathRevision = -1;
	fillHands();
	fillPoseSequences();
	applyMode();
	refresh();
	setMessage(on ? tr("Pose: drag a grip or bend the fingers - it changes every animation alike. Check on shows it on another one.")
	              : tr("Animate: keys in \"%1\". Your pose is kept - switch back to Pose to go on with it.").arg(qs(m_edit->clip().name)));
}

void AnimationEditor::applyMode() {
	{
		QSignalBlocker a(m_modePose), b(m_modeAnimate);
		m_modePose->setChecked(m_pose);
		m_modeAnimate->setChecked(!m_pose);
	}
	m_animateTools->setVisible(!m_pose);
	m_poseTools->setVisible(m_pose);
	m_clipBox->setVisible(!m_pose);
	m_handsBox->setVisible(m_edit && !m_edit->hands().empty());
	m_howTo->setText(m_pose ? tr("1. Drag an orange grip: the hand goes there, the arm follows (or click a bone and turn it).   "
	                             "2. Bend the fingers with Curl and Spread.   "
	                             "3. Check it on other animations, then Build ... - every animation of the model gets it.")
	                        : tr("1. Go to a frame and drag an orange grip (or click a bone and turn its rings).   "
	                             "2. Every move sets a key there (Auto key) - or press K.   "
	                             "3. Go to another frame, move it again; Space plays. Right-click a key: Smooth, Linear, Step, Ease in/out."));
	updateTitle();
}

void AnimationEditor::fillPoseSequences() {
	QSignalBlocker block(m_poseSequence);
	m_poseSequence->clear();
	if (!m_animated) return;
	for (const auto& seq : m_animated->sequences()) {
		if (seq.model == 0 && !seq.delta) m_poseSequence->addItem(qs(seq.name));
	}
	if (m_edit) m_poseSequence->setCurrentIndex(std::max(0, m_poseSequence->findText(qs(m_edit->clip().baseSequence), Qt::MatchFixedString)));
}

void AnimationEditor::setPoseSequence(const QString& sequence) {
	if (!m_edit || !m_pose) return;
	const bool wasSaved = m_edit->revision() == m_savedRevision;
	m_edit->setPreviewSequence(sequence.toStdString());
	if (wasSaved) m_savedRevision = m_edit->revision();   // only what is shown changed
	{
		QSignalBlocker block(m_poseSequence);
		m_poseSequence->setCurrentIndex(std::max(0, m_poseSequence->findText(qs(m_edit->clip().baseSequence), Qt::MatchFixedString)));
	}
	m_frame = 0;
	m_playFrame = 0;
	m_shownRevision = -1;
	m_pathRevision = -1;
	refresh();
}

void AnimationEditor::fillHands() {
	m_handSliders.clear();
	auto* layout = static_cast<QVBoxLayout*>(m_handsContent->layout());
	while (auto* item = layout->takeAt(0)) {
		if (auto* w = item->widget()) w->deleteLater();
		delete item;
	}
	if (!m_edit) return;
	const auto& hands = m_edit->hands();
	const bool more = m_moreButton->isChecked();
	for (std::size_t h = 0; h < hands.size(); ++h) {
		auto* box = new QWidget;
		auto* form = new QFormLayout(box);
		form->setContentsMargins(0, 0, 0, 4);
		auto* title = new QLabel(QStringLiteral("<b>%1</b>").arg(tr(hands[h].label.c_str())));
		form->addRow(title);
		HandSliders sliders;
		const auto slider = [&](int low, int high, const QString& name, const QString& tip) {
			auto* s = new QSlider(Qt::Horizontal);
			s->setRange(low, high);
			s->setObjectName(name);
			s->setToolTip(tip);
			return s;
		};
		sliders.curl = slider(-25, 100, QString("animCurl%1").arg(h), tr("Open hand (left) to fist (right)"));
		sliders.spread = slider(-100, 100, QString("animSpread%1").arg(h), tr("Fingers together (left) or fanned out (right)"));
		form->addRow(tr("Curl"), sliders.curl);
		form->addRow(tr("Spread"), sliders.spread);
		sliders.fingerBox = new QWidget;
		auto* fingerForm = new QFormLayout(sliders.fingerBox);
		fingerForm->setContentsMargins(0, 0, 0, 0);
		for (std::size_t f = 0; f < hands[h].fingers.size(); ++f) {
			auto* s = slider(-100, 100, QString("animFinger%1_%2").arg(h).arg(f), tr("This finger bent more (right) or less (left) than the others"));
			fingerForm->addRow(tr(hands[h].fingers[f].label.c_str()), s);
			sliders.fingers.push_back(s);
		}
		sliders.fingerBox->setVisible(more);
		form->addRow(sliders.fingerBox);
		layout->addWidget(box);
		const int hand = int(h);
		const auto apply = [this, hand] {
			if (m_filling || !m_edit || hand >= int(m_handSliders.size())) return;
			const HandSliders& now = m_handSliders[std::size_t(hand)];
			HandShape shape;
			shape.curl = float(now.curl->value()) / 100.f;
			shape.spread = float(now.spread->value()) / 100.f;
			const auto& rig = m_edit->hands()[std::size_t(hand)];
			for (std::size_t f = 0; f < now.fingers.size() && f < rig.fingers.size(); ++f) {
				if (now.fingers[f]->value() != 0) shape.fingers[m_edit->bones()[std::size_t(rig.fingers[f].segments.front())].name] = float(now.fingers[f]->value()) / 100.f;
			}
			setPlaying(false);
			if (m_sliderDrag) m_edit->revertChange();
			m_edit->setHandShape(hand, shape, m_frame);
			m_shownRevision = -1;
			refresh();
		};
		std::vector<QSlider*> all{sliders.curl, sliders.spread};
		all.insert(all.end(), sliders.fingers.begin(), sliders.fingers.end());
		for (auto* s : all) {
			connect(s, &QSlider::valueChanged, this, apply);
			connect(s, &QSlider::sliderPressed, this, [this] {
				if (!m_edit) return;
				m_sliderDrag = true;
				m_edit->beginChange();
			});
			connect(s, &QSlider::sliderReleased, this, [this] {
				if (!m_edit || !m_sliderDrag) return;
				m_sliderDrag = false;
				m_edit->endChange();
				m_shownRevision = -1;
				refresh();
			});
		}
		m_handSliders.push_back(std::move(sliders));
	}
	refreshHands();
}

void AnimationEditor::refreshHands() {
	if (!m_edit || m_sliderDrag) return;
	const bool filling = m_filling;
	m_filling = true;
	for (std::size_t h = 0; h < m_handSliders.size() && h < m_edit->hands().size(); ++h) {
		const HandShape shape = m_edit->handShape(int(h));
		auto& sliders = m_handSliders[h];
		sliders.curl->setValue(int(std::lround(shape.curl * 100.f)));
		sliders.spread->setValue(int(std::lround(shape.spread * 100.f)));
		const auto& rig = m_edit->hands()[h];
		for (std::size_t f = 0; f < sliders.fingers.size() && f < rig.fingers.size(); ++f) {
			float extra = 0;
			const std::string& name = m_edit->bones()[std::size_t(rig.fingers[f].segments.front())].name;
			for (const auto& [bone, value] : shape.fingers) {
				if (iequals(bone, name)) extra = value;
			}
			sliders.fingers[f]->setValue(int(std::lround(extra * 100.f)));
		}
	}
	m_filling = filling;
}

void AnimationEditor::setHandShape(int hand, float curl, float spread) {
	if (!m_edit || hand < 0 || hand >= int(m_edit->hands().size())) return;
	HandShape shape = m_edit->handShape(hand);
	shape.curl = curl;
	shape.spread = spread;
	m_edit->setHandShape(hand, shape, m_frame);
	m_shownRevision = -1;
	refresh();
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
	const bool split = m_playerView->isVisible();
	if (m_geometry && !m_geometry->empty()) {
		std::vector<ModelVertex> posed;
		skinVertices(*m_geometry, m_edit->skinning(shownFrame), posed);
		if (split) m_playerView->setPose(posed);
		m_view->setPose(std::move(posed));
	}
	auto placement = m_edit->boneToModel(shownFrame);
	if (split) m_playerView->setSkeleton(&m_edit->bones(), placement);
	m_view->setSkeleton(&m_edit->bones(), std::move(placement));
	m_view->setSelection(m_selection, m_selection.empty() ? -1 : m_selection.back());
	// Grips on the IK chains' ends.
	std::vector<AnimEditView::Grip> grips;
	for (const auto& chain : m_edit->ikChains()) grips.push_back({chain.end, qs(chain.label)});
	m_view->setGrips(std::move(grips));
	// The motion path of the chosen bone (recomputed only when the clip or the bone changes).
	const int pathBone = m_selection.empty() ? -1 : m_selection.back();
	if (m_pathBox->isChecked() && pathBone >= 0 && !m_playing) {
		if (m_pathRevision != m_edit->revision() || m_pathBone != pathBone) {
			m_pathPoints = m_edit->motionPath(pathBone);
			m_pathRevision = m_edit->revision();
			m_pathBone = pathBone;
		}
		const int frames = m_edit->clip().frames;
		const int count = int(m_pathPoints.size());
		std::vector<bool> keyedPoints(m_pathPoints.size(), false);
		const auto indexOf = [&](int frame) { return frames > 1 && count > 1 ? int(std::lround(double(frame) * double(count - 1) / double(frames - 1))) : 0; };
		for (const int f : m_edit->keyFrames({pathBone})) {
			const int i = indexOf(f);
			if (i >= 0 && i < count) keyedPoints[std::size_t(i)] = true;
		}
		m_view->setMotionPath(m_pathPoints, std::move(keyedPoints), indexOf(m_frame));
	} else {
		m_view->setMotionPath({}, {}, -1);
	}
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
	m_timeline->setEditFrom(m_edit->clip().editFrom);
	refreshFields();
	refreshHands();
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
	m_editFromBox->setRange(0, clip.frames - 1);
	m_editFromBox->setValue(clip.editFrom);
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
				if (k.frame != m_frame) continue;
				m_interp->setCurrentIndex(m_interp->findData(int(k.interp)));
				m_interpBar->setCurrentIndex(m_interpBar->findData(int(k.interp)));
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
	menu.addSeparator();
	const std::pair<KeyInterp, QString> kinds[] = {{KeyInterp::Smooth, tr("Smooth to the next key")},
	                                               {KeyInterp::Linear, tr("Linear to the next key")},
	                                               {KeyInterp::Step, tr("Step: hold, then cut to the next key")},
	                                               {KeyInterp::EaseIn, tr("Ease in: start slow")},
	                                               {KeyInterp::EaseOut, tr("Ease out: end slow")}};
	for (const auto& [kind, text] : kinds) {
		menu.addAction(text, this, [this, bones, frame, kind] {
			m_edit->setInterp(bones, frame, kind);
			refresh();
		});
	}
	menu.exec(at);
}

// --- Simple keys, copies, views ----------------------------------------------------------------

void AnimationEditor::setInterpolation(KeyInterp interp) {
	if (!m_edit) return;
	m_edit->setInterp(m_selection, m_frame, interp);
	if (m_edit->keyFrames(m_selection).empty() ||
	    std::none_of(m_selection.begin(), m_selection.end(), [&](int b) { return m_edit->hasKey(b, m_frame); })) {
		setMessage(tr("No key at frame %1 for the chosen bones - set a key first").arg(m_frame));
	}
	refresh();
}

void AnimationEditor::setAutoKey(bool on) {
	m_autoKeyBox->setChecked(on);
}

bool AnimationEditor::autoKey() const {
	return m_autoKeyBox->isChecked();
}

void AnimationEditor::finishDrag() {
	if (!m_edit) return;
	// Without Auto key a move only changes a key that was there: otherwise it is taken back.
	if (!m_pose && !autoKey() && m_dragBone >= 0 && !m_dragHadKey) {
		m_edit->revertChange();
		setMessage(tr("Auto key is off: Set key (K) at this frame first, then move the bone"));
	}
	m_edit->endChange();
	m_dragBone = -1;
	refresh();
}

void AnimationEditor::setSplitView(bool on) {
	if (m_split->isChecked() != on) m_split->setChecked(on);
	m_playerView->setVisible(on);
	if (on && m_geometry) m_playerView->setModel(m_geometry);
	m_shownRevision = -1;
	refresh();
}

void AnimationEditor::startClip(AnimClip clip) {
	if (!m_animated) return;
	setPlaying(false);
	clip.model = m_model.toStdString();
	const bool hasKeys = !clip.tracks.empty();
	m_pendingClip = std::move(clip);
	beginEditing();
	if (hasKeys) m_savedRevision = -1;   // a copy with keys is not saved yet (an empty start is nothing to lose)
	updateTitle();
}

void AnimationEditor::duplicateAnimation(const QString& name) {
	if (!m_edit) return;
	AnimClip copy = m_edit->clip();
	copy.name = sequenceNameFromFile(name.toStdString());
	if (copy.name.empty()) copy.name = m_edit->clip().name + "_copy";
	startClip(std::move(copy));
	setMessage(tr("A copy: \"%1\". The original stays as it was; Save keeps the copy in its own file.").arg(qs(m_edit->clip().name)));
}

void AnimationEditor::startFrom(const QString& sequence, bool copyMotion, const QString& name) {
	if (!m_animated) return;
	AnimClip clip;
	clip.baseSequence = sequence.toStdString();
	const int base = sequence.isEmpty() ? -1 : m_animated->findSequence(clip.baseSequence);
	if (base < 0) clip.baseSequence.clear();
	clip.mode = copyMotion && base >= 0 ? AnimClip::Mode::Tweak : AnimClip::Mode::New;
	if (base >= 0) {
		const auto& seq = m_animated->sequences()[std::size_t(base)];
		clip.fps = seq.fps > 0 ? seq.fps : 30;
		clip.frames = clip.mode == AnimClip::Mode::Tweak ? std::max(1, seq.frames) : 30;
		clip.loop = seq.looping;
		if (clip.mode == AnimClip::Mode::Tweak) clip.activity = seq.activity;
	}
	clip.name = sequenceNameFromFile(name.toStdString());
	if (clip.name.empty()) clip.name = "new_animation";
	m_new = clip.mode == AnimClip::Mode::New;
	startClip(std::move(clip));
}

void AnimationEditor::showStartFromDialog(bool fromScratch) {
	if (!m_animated) return;
	QDialog dialog(this);
	dialog.setWindowTitle(fromScratch ? tr("New animation from scratch") : tr("Start from another animation"));
	auto* form = new QFormLayout(&dialog);
	auto* which = new QComboBox;
	which->setObjectName("animStartSequence");
	if (fromScratch) {
		which->addItem(tr("Rest pose"), QString());
		const int idle = m_animated->idleSequence();
		if (idle >= 0) which->addItem(tr("Idle pose (%1)").arg(qs(m_animated->sequences()[std::size_t(idle)].name)), qs(m_animated->sequences()[std::size_t(idle)].name));
		which->setCurrentIndex(which->count() - 1);
	} else {
		for (const auto& seq : m_animated->sequences()) which->addItem(qs(seq.name), qs(seq.name));
		which->setCurrentIndex(std::max(0, which->findData(m_sequence)));
	}
	form->addRow(fromScratch ? tr("Start pose") : tr("Animation"), which);
	auto* copyMotion = new QCheckBox(tr("Copy its motion and change it (off: start from its first pose)"));
	copyMotion->setChecked(true);
	if (!fromScratch) form->addRow(copyMotion);
	auto* name = new QLineEdit(fromScratch ? QStringLiteral("new_animation") : QString());
	const auto suggest = [which, name, fromScratch] {
		if (!fromScratch) name->setText(which->currentData().toString() + "_new");
	};
	suggest();
	connect(which, &QComboBox::currentIndexChanged, &dialog, suggest);
	form->addRow(tr("Name"), name);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) return;
	startFrom(which->currentData().toString(), !fromScratch && copyMotion->isChecked(), name->text());
}

void AnimationEditor::editFromPlayhead() {
	if (!m_edit) return;
	m_edit->setEditFrom(m_frame);
	setMessage(m_frame > 0 ? tr("Frames before %1 stay exactly as the original; changes start here").arg(m_frame) : tr("Every frame may change"));
	refresh();
}

void AnimationEditor::cutBeforePlayhead() {
	if (!m_edit || !m_edit->trim(m_frame, m_edit->clip().frames - 1)) return;
	m_frame = 0;
	m_shownRevision = -1;
	refresh();
}

void AnimationEditor::cutAfterPlayhead() {
	if (!m_edit || !m_edit->trim(0, m_frame)) return;
	m_shownRevision = -1;
	refresh();
}

void AnimationEditor::setMessage(const QString& text) {
	m_status->setText(text);
}

QString AnimationEditor::titleText() const {
	if (!m_edit) return m_model.isEmpty() ? tr("No model") : tr("%1 - loading").arg(m_model.section('/', -1));
	const auto& clip = m_edit->clip();
	const QString unsaved = m_edit->revision() != m_savedRevision ? QStringLiteral(" *") : QString();
	if (clip.mode == AnimClip::Mode::Pose) return tr("%1 - pose for all animations%2").arg(m_model.section('/', -1), unsaved);
	const QString what = clip.mode == AnimClip::Mode::Tweak ? tr("improving \"%1\"").arg(qs(clip.baseSequence)) : tr("new animation");
	return tr("%1 - %2 as \"%3\"%4").arg(m_model.section('/', -1), what, qs(clip.name), unsaved);
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

std::map<std::string, std::string> AnimationEditor::rebuildRecord() const {
	std::map<std::string, std::string> record;
	if (m_model.isEmpty()) return record;
	const QByteArray text = readFile(QDir(sourceFolder()).filePath("rebuild.txt"));
	if (text.size() > 64 * 1024) return record;
	for (const QByteArray& line : text.split('\n')) {
		const QList<QByteArray> parts = line.trimmed().split('\t');
		if (parts.size() != 2 || record.size() >= 512) continue;
		const std::string sequence = toLower(std::string(trim(parts[0].toStdString())));
		const std::string name = sequenceNameFromFile(parts[1].trimmed().toStdString());
		if (!sequence.empty() && !name.empty() && sequence.find_first_of("/\\") == std::string::npos) record[sequence] = name;
	}
	return record;
}

void AnimationEditor::writeRebuildRecord(const std::map<std::string, std::string>& record) const {
	const QString path = QDir(sourceFolder()).filePath("rebuild.txt");
	if (record.empty()) {
		QFile::remove(path);
		return;
	}
	QByteArray text = "# Mapsmith: what the Animation editor built into the mod's model (sequence, project); #pose = the pose\n";
	for (const auto& [sequence, name] : record) text += QByteArray::fromStdString(sequence + "\t" + name + "\n");
	writeFile(path, text);
}

bool AnimationEditor::replaceInModel(const QString& sequence) {
	if (!m_edit || !m_animated) return false;
	m_replaceSequence = sequence.isEmpty() ? qs(m_edit->clip().baseSequence) : sequence;
	if (m_replaceSequence.isEmpty()) {
		m_log->show();
		m_log->appendPlainText(tr("Choose the sequence to replace"));
		return false;
	}
	const int replacedIndex = m_animated->findSequence(m_replaceSequence.toStdString());
	if (replacedIndex < 0 || m_animated->sequences()[std::size_t(replacedIndex)].model != 0) {
		m_log->show();
		m_log->appendPlainText(tr("The model has no sequence \"%1\" of its own to replace.").arg(m_replaceSequence));
		return false;
	}
	if (m_animated->sequences()[std::size_t(replacedIndex)].blends > 1) {
		m_log->appendPlainText(tr("Note: \"%1\" blends several poses (aiming); it becomes this one animation.").arg(m_replaceSequence));
	}
	// A pose built into the model before stays on this animation too.
	auto frames = m_edit->bake();
	auto record = rebuildRecord();
	if (record.count("#pose")) {
		if (auto pose = savedClip(POSE_LAYER_NAME); pose && pose->mode == AnimClip::Mode::Pose) applyPoseLayer(*pose, m_edit->bones(), frames);
	}
	SequenceReplacement replacement;
	replacement.smd = poseFramesSmd(m_edit->bones(), frames);
	replacement.fps = m_edit->clip().fps;
	replacement.loop = m_edit->clip().loop ? 1 : 0;
	std::map<std::string, SequenceReplacement> replacements;
	const std::string key = m_replaceSequence.toLower().toStdString();
	replacements[key] = std::move(replacement);
	record[key] = m_edit->clip().name;
	m_pendingRecord = std::move(record);
	return startRebuild(std::move(replacements));
}

bool AnimationEditor::buildPose() {
	if (!m_edit || !m_animated || !m_pose) return false;
	save();
	// What was built before (improved animations) stays, with the pose on top.
	auto record = rebuildRecord();
	std::map<std::string, AnimClip> built;
	for (const auto& [sequence, name] : record) {
		if (sequence == "#pose") continue;
		if (auto clip = savedClip(name); clip && clip->mode != AnimClip::Mode::Pose) built[sequence] = std::move(*clip);
	}
	auto replacements = poseLayerReplacements(*m_animated, m_edit->clip(), built);
	if (replacements.empty()) {
		setMessage(tr("The pose changes nothing yet - drag a grip or bend the fingers first"));
		return false;
	}
	if (m_edit->keyCount() > 0) record["#pose"] = POSE_LAYER_NAME;
	else record.erase("#pose");
	m_replaceSequence.clear();
	m_pendingRecord = std::move(record);
	return startRebuild(std::move(replacements));
}

bool AnimationEditor::startRebuild(std::map<std::string, SequenceReplacement> replacements) {
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
	setPlaying(false);
	save();
	m_log->appendPlainText(tr("Taking %1 apart ...").arg(m_model));
	const GameFileSystem* fs = &m_editor->game()->files;
	const std::string path = m_model.toStdString();
	ModelExportOptions options;
	options.materials = false;
	options.pictures = false;
	options.replacements = std::move(replacements);
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
		writeRebuildRecord(m_pendingRecord);
		if (m_replaceSequence.isEmpty()) m_log->appendPlainText(tr("Built %1 into the mod - every animation has your pose now.").arg(m_model));
		else m_log->appendPlainText(tr("Built %1 into the mod - the game plays your \"%2\" now.").arg(m_model, m_replaceSequence));
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
	m_pendingRecord.clear();
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
	writeRebuildRecord({});
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
