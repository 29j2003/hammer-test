// sound_player.cpp - Sound preview playback (see sound_player.h).

#include "sound_player.h"

#ifdef SME_HAVE_MULTIMEDIA
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#endif

namespace sme::app {

#ifdef SME_HAVE_MULTIMEDIA

namespace {

/// The converted sound as a readable device; jumps back to the loop point at the end.
class LoopingSource : public QIODevice {
public:
	QByteArray data;
	qint64 loopFrom = -1;     ///< Byte offset to continue from at the end (-1: stop)
	qint64 pos = 0;

	qint64 readData(char* out, qint64 maxSize) override {
		qint64 written = 0;
		while (written < maxSize) {
			if (pos >= data.size()) {
				if (loopFrom < 0 || loopFrom >= data.size()) break;
				pos = loopFrom;
			}
			const qint64 n = std::min(maxSize - written, qint64(data.size()) - pos);
			std::memcpy(out + written, data.constData() + pos, static_cast<std::size_t>(n));
			pos += n;
			written += n;
		}
		return written;
	}
	qint64 writeData(const char*, qint64) override { return -1; }
	qint64 bytesAvailable() const override { return loopFrom >= 0 ? (1 << 20) : std::max<qint64>(0, data.size() - pos) + QIODevice::bytesAvailable(); }
	bool isSequential() const override { return true; }
};

/// Linear resampling and channel mapping into the output format.
QByteArray convert(const DecodedAudio& audio, const QAudioFormat& format, std::size_t fromFrame) {
	const int outChannels = std::max(1, format.channelCount());
	const int inChannels = std::max(1, audio.channels);
	const std::size_t inFrames = audio.frames();
	const double step = double(audio.sampleRate) / std::max(1, format.sampleRate());
	const std::size_t outFrames = inFrames > fromFrame ? static_cast<std::size_t>((inFrames - fromFrame) / step) : 0;
	const bool asFloat = format.sampleFormat() == QAudioFormat::Float;
	QByteArray out(static_cast<qsizetype>(outFrames * outChannels * (asFloat ? 4 : 2)), Qt::Uninitialized);
	auto* f = reinterpret_cast<float*>(out.data());
	auto* s = reinterpret_cast<std::int16_t*>(out.data());
	for (std::size_t i = 0; i < outFrames; ++i) {
		const double at = fromFrame + i * step;
		const auto a = std::min(inFrames - 1, static_cast<std::size_t>(at));
		const auto b = std::min(inFrames - 1, a + 1);
		const double t = at - double(a);
		for (int c = 0; c < outChannels; ++c) {
			const int src = std::min(c, inChannels - 1);   // mono goes to every speaker
			const double v = audio.samples[a * inChannels + src] * (1 - t) + audio.samples[b * inChannels + src] * t;
			if (asFloat) {
				f[i * outChannels + c] = float(v / 32768.0);
			} else {
				s[i * outChannels + c] = static_cast<std::int16_t>(std::clamp(v, -32768.0, 32767.0));
			}
		}
	}
	return out;
}

} // namespace

struct SoundPlayer::Impl {
	std::unique_ptr<QAudioSink> sink;
	LoopingSource source;
	QAudioFormat format;
	QTimer clock;
	double startSeconds = 0;     ///< Where in the sound the data begins
	double loopSeconds = -1;
	double length = 0;
	int bytesPerFrame = 2;
};

SoundPlayer::SoundPlayer(QObject* parent) : QObject(parent) {}

SoundPlayer::~SoundPlayer() {
	stop();
}

bool SoundPlayer::available() {
	return true;
}

void SoundPlayer::play(std::shared_ptr<const DecodedAudio> audio, double fromSeconds) {
	stop();
	if (!audio || audio->frames() == 0) {
		return;
	}
	// The audio device is opened on the first play, not at startup: some systems have none.
	const QAudioDevice device = QMediaDevices::defaultAudioOutput();
	if (device.isNull()) {
		emit error(tr("No audio output device found"));
		return;
	}
	if (!m_impl) {
		m_impl = std::make_unique<Impl>();
		m_impl->clock.setInterval(30);
		connect(&m_impl->clock, &QTimer::timeout, this, [this] { emit positionChanged(position()); });
	}
	QAudioFormat format = device.preferredFormat();
	if (format.sampleFormat() != QAudioFormat::Float) {
		format.setSampleFormat(QAudioFormat::Int16);
	}
	if (!device.isFormatSupported(format)) {
		format = device.preferredFormat();
	}
	if (format.sampleFormat() != QAudioFormat::Float && format.sampleFormat() != QAudioFormat::Int16) {
		emit error(tr("The audio device uses a sample format the preview cannot write"));
		return;
	}
	const std::size_t fromFrame = std::min(audio->frames() - 1, static_cast<std::size_t>(std::max(0.0, fromSeconds) * audio->sampleRate));
	auto& impl = *m_impl;
	impl.format = format;
	impl.bytesPerFrame = format.channelCount() * (format.sampleFormat() == QAudioFormat::Float ? 4 : 2);
	impl.source.close();
	impl.source.data = convert(*audio, format, fromFrame);
	impl.source.pos = 0;
	impl.startSeconds = double(fromFrame) / audio->sampleRate;
	impl.length = audio->seconds();
	impl.loopSeconds = audio->loopStart ? double(*audio->loopStart) / audio->sampleRate : -1;
	impl.source.loopFrom = -1;
	if (m_loop && impl.loopSeconds >= 0) {
		// The loop point counted from where playing started (before it: from the beginning).
		const double into = std::max(0.0, impl.loopSeconds - impl.startSeconds);
		impl.source.loopFrom = static_cast<qint64>(into * format.sampleRate()) * impl.bytesPerFrame;
	}
	impl.source.open(QIODevice::ReadOnly);
	impl.sink = std::make_unique<QAudioSink>(device, format);
	impl.sink->setVolume(gain(m_volume));
	connect(impl.sink.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
		if (state == QAudio::IdleState || state == QAudio::StoppedState) {
			if (state == QAudio::IdleState) {
				stop();   // played to the end
			}
		}
	});
	impl.sink->start(&impl.source);
	if (impl.sink->error() != QAudio::NoError) {
		emit error(tr("The audio device could not be opened"));
		impl.sink.reset();
		return;
	}
	impl.clock.start();
	emit stateChanged(true);
}

void SoundPlayer::stop() {
	if (!m_impl || !m_impl->sink) {
		return;
	}
	auto sink = std::move(m_impl->sink);
	sink->disconnect(this);
	sink->stop();
	m_impl->clock.stop();
	emit stateChanged(false);
	emit positionChanged(-1);
}

bool SoundPlayer::playing() const {
	return m_impl && m_impl->sink;
}

double SoundPlayer::position() const {
	if (!playing()) {
		return -1;
	}
	const auto& impl = *m_impl;
	// Bytes handed to the device minus what still waits in its buffer.
	const qint64 heard = std::max<qint64>(0, impl.sink->processedUSecs());
	double seconds = impl.startSeconds + heard / 1e6;
	if (impl.source.loopFrom >= 0 && seconds > impl.length) {
		const double loopLength = std::max(0.01, impl.length - std::max(impl.loopSeconds, impl.startSeconds));
		seconds = std::max(impl.loopSeconds, impl.startSeconds) + std::fmod(seconds - impl.length, loopLength);
	}
	return std::min(seconds, impl.length);
}

void SoundPlayer::setLoop(bool on) {
	m_loop = on;
	if (m_impl && !on) {
		m_impl->source.loopFrom = -1;   // ends after this round
	}
}

void SoundPlayer::setVolume(float volume) {
	m_volume = std::clamp(volume, 0.f, 1.f);
	if (m_impl && m_impl->sink) {
		m_impl->sink->setVolume(gain(m_volume));
	}
}

#else

struct SoundPlayer::Impl {};

SoundPlayer::SoundPlayer(QObject* parent) : QObject(parent) {}
SoundPlayer::~SoundPlayer() = default;

bool SoundPlayer::available() {
	return false;
}

void SoundPlayer::play(std::shared_ptr<const DecodedAudio>, double) {
	emit error(tr("This build cannot play sounds (built without Qt Multimedia)."));
}

void SoundPlayer::stop() {}

bool SoundPlayer::playing() const {
	return false;
}

double SoundPlayer::position() const {
	return -1;
}

void SoundPlayer::setLoop(bool on) {
	m_loop = on;
}

void SoundPlayer::setVolume(float volume) {
	m_volume = volume;
}

#endif

float SoundPlayer::gain(float position) {
	constexpr float RANGE_DB = 50;
	if (position <= 0) return 0;
	return std::pow(10.f, (std::min(position, 1.f) - 1) * RANGE_DB / 20);
}

} // namespace sme::app
