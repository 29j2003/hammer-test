// sound_player.cpp - Sound preview playback (see sound_player.h).

#include "sound_player.h"

#ifdef SME_HAVE_MULTIMEDIA
#include <QAudioOutput>
#include <QBuffer>
#include <QMediaPlayer>
#include <QUrl>
#endif

namespace sme::app {

#ifdef SME_HAVE_MULTIMEDIA

struct SoundPlayer::Impl {
	QMediaPlayer player;
	QAudioOutput output;
	std::unique_ptr<QBuffer> buffer;
	QByteArray data;
};

// The media backend is created on the first play, not at startup: some systems have a
// broken or missing backend, and the editor must start anyway.
SoundPlayer::SoundPlayer(QObject* parent) : QObject(parent) {}

SoundPlayer::~SoundPlayer() {
	if (m_impl) {
		m_impl->player.stop();
	}
}

bool SoundPlayer::available() {
	return true;
}

void SoundPlayer::play(const QString& name, QByteArray data) {
	if (!m_impl) {
		m_impl = std::make_unique<Impl>();
		m_impl->player.setAudioOutput(&m_impl->output);
		m_impl->output.setVolume(0.8f);
		connect(&m_impl->player, &QMediaPlayer::playbackStateChanged, this,
		        [this](QMediaPlayer::PlaybackState state) { emit stateChanged(state == QMediaPlayer::PlayingState); });
		connect(&m_impl->player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& text) { emit error(text); });
	}
	m_impl->player.stop();
	m_impl->player.setSourceDevice(nullptr);
	m_impl->data = std::move(data);
	m_impl->buffer = std::make_unique<QBuffer>(&m_impl->data);
	m_impl->buffer->open(QIODevice::ReadOnly);
	// The URL only tells the decoder the format.
	m_impl->player.setSourceDevice(m_impl->buffer.get(), QUrl(QStringLiteral("sound:///") + name));
	m_impl->player.play();
}

void SoundPlayer::stop() {
	if (m_impl) {
		m_impl->player.stop();
	}
}

bool SoundPlayer::playing() const {
	return m_impl && m_impl->player.playbackState() == QMediaPlayer::PlayingState;
}

#else

struct SoundPlayer::Impl {};

SoundPlayer::SoundPlayer(QObject* parent) : QObject(parent) {}
SoundPlayer::~SoundPlayer() = default;

bool SoundPlayer::available() {
	return false;
}

void SoundPlayer::play(const QString&, QByteArray) {
	emit error(tr("This build cannot play sounds (built without Qt Multimedia)."));
}

void SoundPlayer::stop() {}

bool SoundPlayer::playing() const {
	return false;
}

#endif

} // namespace sme::app
