// sound_player.h - Plays a sound file of the game (WAV, MP3, OGG) straight from the game's
// folders or VPKs. Uses Qt Multimedia when the editor was built with it, otherwise playing
// is unavailable and available() returns false.

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

namespace sme::app {

class SoundPlayer : public QObject {
	Q_OBJECT

public:
	explicit SoundPlayer(QObject* parent = nullptr);
	~SoundPlayer() override;

	[[nodiscard]] static bool available();
	/// Starts playing the file contents (the name only tells the format).
	void play(const QString& name, QByteArray data);
	void stop();
	[[nodiscard]] bool playing() const;

signals:
	void stateChanged(bool playing);
	void error(const QString& text);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace sme::app
