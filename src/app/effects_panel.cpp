// effects_panel.cpp - Effects on a model's attachments and animations (see effects_panel.h).

#include "effects_panel.h"

#include "asset_browser.h"
#include "dialog_fit.h"
#include "editor.h"
#include "model_viewer.h"

#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/studio_anim.h>
#include <sme/text.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <fstream>
#include <map>
#include <sstream>

namespace sme::app {

namespace {

QString effectLine(const ModelEffect& e) {
	const QString where = e.attachment.empty() ? QString::fromUtf8(effectAttachName(e.attach).data(), int(effectAttachName(e.attach).size()))
	                                           : QString::fromStdString(e.attachment);
	if (e.when == ModelEffect::When::Always) return QObject::tr("%1 at %2 - always").arg(QString::fromStdString(e.particle), where);
	const QString until = e.endFrame >= 0 ? QObject::tr("frames %1-%2").arg(e.startFrame).arg(e.endFrame) : QObject::tr("from frame %1").arg(e.startFrame);
	return QObject::tr("%1 at %2 - during %3, %4").arg(QString::fromStdString(e.particle), where, QString::fromStdString(e.sequence), until);
}

} // namespace

std::vector<AttachmentPoint> attachmentPoints(const GameFileSystem& fs, std::string_view modelPath) {
	std::vector<AttachmentPoint> out;
	const auto mdl = fs.read(modelPath);
	if (!mdl) return out;
	const auto attachments = readAttachments(*mdl);
	const auto model = AnimatedModel::load(fs, modelPath);
	std::vector<BoneMatrix> rest;
	if (model) rest = model->boneToModel(-1, 0);   // an unknown sequence: the rest pose
	for (const auto& a : attachments) {
		AttachmentPoint p;
		p.name = a.name;
		p.position[0] = a.origin.x;
		p.position[1] = a.origin.y;
		p.position[2] = a.origin.z;
		if (model) {
			for (std::size_t b = 0; b < model->bones().size() && b < rest.size(); ++b) {
				if (!iequals(model->bones()[b].name, a.bone)) continue;
				const auto& m = rest[b].m;
				for (int r = 0; r < 3; ++r) p.position[r] = m[r][0] * a.origin.x + m[r][1] * a.origin.y + m[r][2] * a.origin.z + m[r][3];
				break;
			}
		}
		out.push_back(std::move(p));
	}
	return out;
}

// --- The assistant ---------------------------------------------------------------------------

EffectDialog::EffectDialog(AssetBrowser* assets, ModelViewer* viewer, const std::vector<AttachmentPoint>& attachments,
                           const std::vector<std::pair<std::string, int>>& sequences, ModelEffect effect, QWidget* parent)
	: QDialog(parent), m_viewer(viewer), m_attachments(attachments), m_sequences(sequences) {
	setObjectName("effectDialog");
	setWindowTitle(tr("Effect"));
	auto* form = new QFormLayout(this);

	m_particle = new QComboBox;
	m_particle->setObjectName("effectParticle");
	m_particle->setEditable(true);
	m_particle->setInsertPolicy(QComboBox::NoInsert);
	if (assets) {
		QStringList names;
		for (const auto& p : assets->particles()) names << QString::fromStdString(p.name);
		names.removeDuplicates();
		names.sort(Qt::CaseInsensitive);
		m_particle->addItems(names);
	}
	m_particle->setEditText(QString::fromStdString(effect.particle));
	m_particle->setToolTip(tr("A particle system of the game or the mod (make your own on the Particles page: New ... or Edit)"));
	form->addRow(tr("1. Effect"), m_particle);

	m_attachment = new QComboBox;
	m_attachment->setObjectName("effectAttachment");
	for (const auto& a : m_attachments) m_attachment->addItem(QString::fromStdString(a.name));
	if (!effect.attachment.empty()) {
		const int i = m_attachment->findText(QString::fromStdString(effect.attachment), Qt::MatchFixedString);
		if (i >= 0) m_attachment->setCurrentIndex(i);
	}
	m_attach = new QComboBox;
	m_attach->setObjectName("effectAttachType");
	m_attach->addItem(tr("Follows the attachment"), int(EffectAttach::FollowAttachment));
	m_attach->addItem(tr("Starts at the attachment, stays in the world"), int(EffectAttach::StartAtAttachment));
	m_attach->addItem(tr("Follows the model's origin"), int(EffectAttach::FollowOrigin));
	m_attach->addItem(tr("Starts at the origin"), int(EffectAttach::StartAtOrigin));
	m_attach->addItem(tr("Follows the root bone"), int(EffectAttach::FollowRootBone));
	m_attach->setCurrentIndex(std::max(0, m_attach->findData(int(effect.attach))));
	auto* where = new QWidget;
	auto* wv = new QVBoxLayout(where);
	wv->setContentsMargins(0, 0, 0, 0);
	wv->addWidget(m_attachment);
	wv->addWidget(m_attach);
	if (m_attachments.empty()) {
		auto* none = new QLabel(tr("The model has no attachments - the effect can sit at its origin or root bone."));
		none->setWordWrap(true);
		none->setProperty("role", "description");
		wv->addWidget(none);
	}
	form->addRow(tr("2. Where"), where);

	m_when = new QComboBox;
	m_when->setObjectName("effectWhen");
	m_when->addItem(tr("During an animation"));
	m_when->addItem(tr("Always (while the model is there)"));
	m_when->setCurrentIndex(effect.when == ModelEffect::When::Always ? 1 : 0);
	m_sequence = new QComboBox;
	m_sequence->setObjectName("effectSequence");
	for (const auto& [name, frames] : m_sequences) m_sequence->addItem(tr("%1  (%2 frames)").arg(QString::fromStdString(name)).arg(frames), QString::fromStdString(name));
	if (!effect.sequence.empty()) {
		for (int i = 0; i < m_sequence->count(); ++i) {
			if (m_sequence->itemData(i).toString().compare(QString::fromStdString(effect.sequence), Qt::CaseInsensitive) == 0) m_sequence->setCurrentIndex(i);
		}
	}
	m_start = new QSpinBox;
	m_start->setObjectName("effectStart");
	m_start->setRange(0, 100000);
	m_start->setValue(effect.startFrame);
	m_end = new QSpinBox;
	m_end->setObjectName("effectEnd");
	m_end->setRange(-1, 100000);
	m_end->setSpecialValueText(tr("until it ends"));
	m_end->setValue(effect.endFrame);
	m_instantly = new QCheckBox(tr("Remove at once at the stop (else it fades out)"));
	m_instantly->setChecked(effect.stopInstantly);
	auto* when = new QWidget;
	auto* whenLayout = new QFormLayout(when);
	whenLayout->setContentsMargins(0, 0, 0, 0);
	whenLayout->addRow(m_when);
	whenLayout->addRow(tr("Animation"), m_sequence);
	whenLayout->addRow(tr("Start at frame"), m_start);
	whenLayout->addRow(tr("Stop at frame"), m_end);
	whenLayout->addRow(m_instantly);
	form->addRow(tr("3. When"), when);

	m_problems = new QLabel;
	m_problems->setObjectName("effectProblems");
	m_problems->setWordWrap(true);
	m_problems->setStyleSheet(QStringLiteral("color: #e8a050;"));
	form->addRow(m_problems);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	m_ok = buttons->button(QDialogButtonBox::Ok);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	form->addRow(buttons);

	for (auto* combo : {m_attachment, m_attach, m_when, m_sequence}) connect(combo, &QComboBox::currentIndexChanged, this, &EffectDialog::updateState);
	connect(m_particle, &QComboBox::editTextChanged, this, &EffectDialog::updateState);
	connect(m_start, &QSpinBox::valueChanged, this, &EffectDialog::updateState);
	connect(m_end, &QSpinBox::valueChanged, this, &EffectDialog::updateState);
	updateState();
	fitDialogToScreen(this);
}

EffectDialog::~EffectDialog() {
	if (m_viewer) m_viewer->setMarkers({});
}

ModelEffect EffectDialog::effect() const {
	ModelEffect e;
	e.particle = m_particle->currentText().trimmed().toStdString();
	e.attach = EffectAttach(m_attach->currentData().toInt());
	e.attachment = (e.attach == EffectAttach::FollowAttachment || e.attach == EffectAttach::StartAtAttachment) ? m_attachment->currentText().toStdString() : std::string();
	e.when = m_when->currentIndex() == 1 ? ModelEffect::When::Always : ModelEffect::When::DuringAnimation;
	if (e.when == ModelEffect::When::DuringAnimation) {
		e.sequence = m_sequence->currentData().toString().toStdString();
		e.startFrame = m_start->value();
		e.endFrame = m_end->value();
		e.stopInstantly = m_instantly->isChecked();
	}
	return e;
}

std::vector<std::string> EffectDialog::problems() const {
	std::vector<std::string> attachments, sequences;
	for (const auto& a : m_attachments) attachments.push_back(a.name);
	int frames = 0;
	for (const auto& [name, count] : m_sequences) {
		sequences.push_back(name);
		if (iequals(name, m_sequence->currentData().toString().toStdString())) frames = count;
	}
	return effectProblems(effect(), attachments, sequences, frames);
}

void EffectDialog::updateState() {
	const bool during = m_when->currentIndex() == 0;
	for (QWidget* w : std::initializer_list<QWidget*>{m_sequence, m_start, m_end, m_instantly}) w->setEnabled(during);
	const auto attach = EffectAttach(m_attach->currentData().toInt());
	m_attachment->setEnabled(attach == EffectAttach::FollowAttachment || attach == EffectAttach::StartAtAttachment);
	const auto list = problems();
	QStringList lines;
	for (const auto& p : list) lines << QString::fromStdString(p);
	m_problems->setText(lines.join('\n'));
	m_problems->setVisible(!lines.isEmpty());
	m_ok->setEnabled(list.empty());
	if (m_viewer) {
		QList<ModelViewer::Marker> markers;
		for (const auto& a : m_attachments) {
			markers.push_back({QVector3D(a.position[0], a.position[1], a.position[2]), QString::fromStdString(a.name),
			                   m_attachment->isEnabled() && QString::fromStdString(a.name) == m_attachment->currentText()});
		}
		m_viewer->setMarkers(markers);
	}
}

// --- The list ------------------------------------------------------------------------------------

EffectsPanel::EffectsPanel(Editor* editor, AssetBrowser* assets, QWidget* parent) : QWidget(parent), m_editor(editor), m_assets(assets) {
	setObjectName("effectsPanel");
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	auto* title = new QLabel(tr("Effects (particles at attachments, always or during an animation)"));
	title->setProperty("role", "key");
	title->setWordWrap(true);
	v->addWidget(title);
	m_list = new QListWidget;
	m_list->setObjectName("effectsList");
	m_list->setMaximumHeight(90);
	v->addWidget(m_list);
	auto* row = new QHBoxLayout;
	m_add = new QPushButton(tr("Add ..."));
	m_add->setObjectName("effectsAdd");
	m_editButton = new QPushButton(tr("Edit ..."));
	m_remove = new QPushButton(tr("Remove"));
	m_compile = new QPushButton(tr("Compile into model ..."));
	m_compile->setObjectName("effectsCompile");
	m_compile->setToolTip(tr("Builds the model again with these effects into the mod (needs the game's studiomdl)"));
	auto* copy = new QPushButton(tr("Copy .qc lines"));
	copy->setToolTip(tr("The $sequence events and the $keyvalues block, for a .qc of your own"));
	row->addWidget(m_add);
	row->addWidget(m_editButton);
	row->addWidget(m_remove);
	row->addStretch(1);
	row->addWidget(copy);
	row->addWidget(m_compile);
	v->addLayout(row);
	m_note = new QLabel;
	m_note->setProperty("role", "description");
	m_note->setWordWrap(true);
	v->addWidget(m_note);

	connect(m_add, &QPushButton::clicked, this, [this] { edit(-1); });
	connect(m_editButton, &QPushButton::clicked, this, [this] { edit(m_list->currentRow()); });
	connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { edit(m_list->currentRow()); });
	connect(m_remove, &QPushButton::clicked, this, [this] {
		const int row = m_list->currentRow();
		if (row < 0 || row >= int(m_effects.size())) return;
		m_effects.erase(m_effects.begin() + row);
		save();
		refresh();
	});
	connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(qcText()); });
	connect(m_compile, &QPushButton::clicked, this, [this] {
		emit compileRequested(m_model, effectEvents(m_effects), QString::fromStdString(effectKeyValues(m_effects)));
	});
	connect(m_list, &QListWidget::currentRowChanged, this, [this] {
		const bool picked = m_list->currentRow() >= 0;
		m_editButton->setEnabled(picked);
		m_remove->setEnabled(picked);
	});
	refresh();
}

void EffectsPanel::setModel(const QString& modelPath, ModelViewer* viewer) {
	m_model = modelPath;
	m_viewer = viewer;
	m_effects.clear();
	m_attachments.clear();
	m_sequences.clear();
	if (const auto* game = m_editor->game(); game && !modelPath.isEmpty()) {
		m_attachments = attachmentPoints(game->files, modelPath.toStdString());
		if (const auto model = AnimatedModel::load(game->files, modelPath.toStdString())) {
			for (const auto& s : model->sequences()) m_sequences.emplace_back(s.name, s.frames);
		}
	}
	if (const auto* entry = m_editor->gameEntry(); entry && !modelPath.isEmpty()) {
		const auto file = modelEffectsFile(entry->gameDir, modelPath.toStdString());
		std::ifstream in(file, std::ios::binary);
		if (in) {
			std::ostringstream text;
			text << in.rdbuf();
			m_effects = readEffectsText(text.str());
		}
	}
	refresh();
}

void EffectsPanel::setEffects(std::vector<ModelEffect> effects) {
	m_effects = std::move(effects);
	save();
	refresh();
}

QString EffectsPanel::qcText() const {
	std::map<std::string, std::vector<SequenceEvent>> bySequence;
	for (const auto& e : effectEvents(m_effects)) bySequence[e.sequence].push_back(e);
	QString out;
	for (const auto& [sequence, events] : bySequence) {
		out += QStringLiteral("// in $sequence \"%1\" { ... }:\n").arg(QString::fromStdString(sequence));
		for (const auto& e : events) out += QStringLiteral("\t%1\n").arg(QString::fromStdString(qcEventLine(e)));
	}
	const auto kv = effectKeyValues(m_effects);
	if (!kv.empty()) out += QStringLiteral("// in $keyvalues { ... }:\n") + QString::fromStdString(kv);
	return out;
}

void EffectsPanel::refresh() {
	m_list->clear();
	for (const auto& e : m_effects) m_list->addItem(effectLine(e));
	const bool haveModel = !m_model.isEmpty();
	m_add->setEnabled(haveModel && m_editor->gameEntry());
	m_editButton->setEnabled(false);
	m_remove->setEnabled(false);
	m_compile->setEnabled(haveModel && !m_effects.empty());
	m_note->setText(m_effects.empty() ? tr("None yet. Add one: e.g. a glow sprite on the muzzle only while reloading.")
	                                  : tr("Kept in the mod until the model is compiled with them."));
}

void EffectsPanel::save() {
	const auto* entry = m_editor->gameEntry();
	if (!entry || m_model.isEmpty()) return;
	const auto file = modelEffectsFile(entry->gameDir, m_model.toStdString());
	if (file.empty()) return;
	std::error_code ec;
	if (m_effects.empty()) {
		std::filesystem::remove(file, ec);
		return;
	}
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	out << writeEffectsText(m_effects);
}

void EffectsPanel::edit(int index) {
	ModelEffect start;
	if (index >= 0 && index < int(m_effects.size())) start = m_effects[std::size_t(index)];
	else if (!m_attachments.empty()) start.attachment = m_attachments.front().name;
	EffectDialog dialog(m_assets, m_viewer, m_attachments, m_sequences, start, this);
	if (dialog.exec() != QDialog::Accepted) return;
	if (index >= 0 && index < int(m_effects.size())) m_effects[std::size_t(index)] = dialog.effect();
	else m_effects.push_back(dialog.effect());
	save();
	refresh();
}

} // namespace sme::app
