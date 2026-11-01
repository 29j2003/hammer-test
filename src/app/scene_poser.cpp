// scene_poser.cpp - Model poses for scene ghosts (see scene_poser.h).

#include "scene_poser.h"

#include "editor.h"
#include "model_cache.h"

#include <QMetaObject>
#include <QPointer>

#include <cmath>

#include <sme/game.h>

namespace sme::app {

ScenePoser::ScenePoser(Editor* editor) : QObject(editor), m_editor(editor) {
	m_pool.setMaxThreadCount(1);
	// Reading uses the game's files: wait for it before the game goes away.
	connect(editor, &Editor::gameAboutToChange, this, &ScenePoser::stop);
}

ScenePoser::~ScenePoser() {
	m_pool.waitForDone();
}

void ScenePoser::stop() {
	++m_generation;
	m_pool.waitForDone();
	m_entries.clear();
}

const AnimatedModel* ScenePoser::animated(const QString& model) {
	if (model.isEmpty()) {
		return nullptr;
	}
	const QString key = model.toLower();
	auto& entry = m_entries[key];
	if (entry.model || entry.failed) {
		return entry.model.get();
	}
	if (entry.loading) {
		return nullptr;
	}
	const auto* game = m_editor->game();
	if (!game) {
		return nullptr;
	}
	entry.loading = true;
	const GameFileSystem* fs = &game->files;
	const std::string path = model.toStdString();
	const int generation = m_generation;
	QPointer<ScenePoser> self(this);
	m_pool.start([self, fs, path, key, generation] {
		auto loaded = AnimatedModel::load(*fs, path);
		std::shared_ptr<const AnimatedModel> shared = loaded ? std::make_shared<const AnimatedModel>(std::move(*loaded)) : nullptr;
		if (!self) return;
		QMetaObject::invokeMethod(self.data(), [self, key, generation, shared] {
			if (!self || generation != self->m_generation) return;
			auto& done = self->m_entries[key];
			done.loading = false;
			done.model = shared;
			done.failed = !shared;
			emit self->ready(key);
		}, Qt::QueuedConnection);
	});
	return nullptr;
}

bool ScenePoser::failed(const QString& model) const {
	const auto it = m_entries.find(model.toLower());
	return it != m_entries.end() && it->failed;
}

QStringList ScenePoser::sequences(const QString& model) {
	QStringList names;
	if (const auto* animatedModel = animated(model)) {
		for (const auto& sequence : animatedModel->sequences()) {
			names << QString::fromStdString(sequence.name);
		}
	}
	return names;
}

QString ScenePoser::activitySequence(const QString& model, const char* activity) {
	if (const auto* animatedModel = animated(model)) {
		for (const auto& sequence : animatedModel->sequences()) {
			if (!sequence.layer && QString::fromStdString(sequence.activity).compare(QLatin1String(activity), Qt::CaseInsensitive) == 0) {
				return QString::fromStdString(sequence.name);
			}
		}
	}
	return {};
}

std::optional<double> ScenePoser::sequenceSeconds(const QString& model, const QString& sequence) {
	const auto* animatedModel = animated(model);
	if (!animatedModel) {
		return std::nullopt;
	}
	const int index = animatedModel->findSequence(sequence.toStdString());
	if (index < 0) {
		return std::nullopt;
	}
	return double(animatedModel->sequences()[static_cast<std::size_t>(index)].duration());
}

bool ScenePoser::pose(const QString& model, const QString& sequence, double seconds, std::vector<ModelVertex>& out) {
	const auto* animatedModel = animated(model);
	const auto geometry = m_editor->models() ? m_editor->models()->get(model) : nullptr;
	if (!animatedModel || !geometry || animatedModel->bones().empty() || geometry->weights.size() != geometry->vertices.size()) {
		return false;
	}
	int index = sequence.isEmpty() ? -1 : animatedModel->findSequence(sequence.toStdString());
	if (index < 0) {
		index = animatedModel->idleSequence();
	}
	float cycle = 0;
	if (index >= 0) {
		const auto& info = animatedModel->sequences()[static_cast<std::size_t>(index)];
		const double length = info.duration();
		if (length > 0) {
			const double pass = std::max(0.0, seconds) / length;
			cycle = float(info.looping || sequence.isEmpty() ? pass - std::floor(pass) : std::min(1.0, pass));
		}
	}
	skinVertices(*geometry, animatedModel->skinning(index, cycle), out);
	return !out.empty();
}

} // namespace sme::app
