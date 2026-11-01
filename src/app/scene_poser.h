// scene_poser.h - Poses of NPC models for the scene ghosts (Choreography panel, views): the
// model's skeleton and sequences (AnimatedModel, read in the background once per model) and
// the model's vertices moved into the pose of a sequence at a time.

#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QThreadPool>

#include <memory>
#include <optional>
#include <vector>

#include <sme/studio_anim.h>

namespace sme::app {

class Editor;

class ScenePoser : public QObject {
	Q_OBJECT

public:
	explicit ScenePoser(Editor* editor);
	~ScenePoser() override;

	/// The model's skeleton and sequences if read; otherwise starts reading (ready follows) and
	/// returns nullptr. nullptr also for models that cannot be read (failed()).
	[[nodiscard]] const AnimatedModel* animated(const QString& model);
	[[nodiscard]] bool failed(const QString& model) const;
	/// Names of the model's sequences (empty while reading).
	[[nodiscard]] QStringList sequences(const QString& model);
	/// The first sequence playing `activity` ("ACT_WALK"), "" if none or not read yet.
	[[nodiscard]] QString activitySequence(const QString& model, const char* activity);
	/// Seconds one pass of a sequence takes (nullopt: model not read yet or no such sequence).
	[[nodiscard]] std::optional<double> sequenceSeconds(const QString& model, const QString& sequence);
	/// The model's vertices in the pose of `sequence` `seconds` after it started (looping
	/// sequences wrap, others hold their last frame); an empty sequence name gives the idle.
	/// False while the model or its animation is not read yet.
	bool pose(const QString& model, const QString& sequence, double seconds, std::vector<ModelVertex>& out);

signals:
	/// A model's animations were read (or failed).
	void ready(const QString& model);

private:
	struct Entry {
		std::shared_ptr<const AnimatedModel> model;
		bool loading = false;
		bool failed = false;
	};
	void stop();

	Editor* m_editor;
	QHash<QString, Entry> m_entries;
	QThreadPool m_pool;
	int m_generation = 0;
};

} // namespace sme::app
