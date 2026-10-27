// sound_player.h - Plays decoded sounds (sme::decodeAudio) through the default audio output.
//
// The sound is converted to the output's own format (rate, channels, sample type) and played
// with QAudioSink, so playing needs no media backend plugin (FFmpeg) - only an audio device.
// Without Qt Multimedia in the build, available() is false.

#pragma once

#include <QObject>
#include <QString>

#include <memory>

#include <sme/audio.h>

namespace sme::app {

class SoundPlayer : public QObject {
	Q_OBJECT

public:
	explicit SoundPlayer(QObject* parent = nullptr);
	~SoundPlayer() override;

	[[nodiscard]] static bool available();
	/// Plays from `fromSeconds`; a looping sound (cue point) repeats while loop() is on.
	void play(std::shared_ptr<const DecodedAudio> audio, double fromSeconds = 0);
	void stop();
	[[nodiscard]] bool playing() const;
	/// Seconds into the sound that are being heard now.
	[[nodiscard]] double position() const;
	void setLoop(bool on);
	[[nodiscard]] bool loop() const { return m_loop; }
	void setVolume(float volume);   ///< 0..1 slider position (heard in decibels, see gain())
	/// Loudness the way ears hear it: the slider spans -50 dB .. 0 dB, so the lower half is really
	/// quiet (a linear volume stays loud until the very bottom). 0 is silent.
	[[nodiscard]] static float gain(float position);

signals:
	void stateChanged(bool playing);
	void positionChanged(double seconds);
	void error(const QString& text);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
	bool m_loop = true;
	float m_volume = 0.8f;
};

} // namespace sme::app
