// test_sound_edit.cpp - Sound editing (trim, cut, fades, gain, normalize, reverse, resampling,
// mono, WAV with loop) and the categories of the Sounds page.

#include <doctest/doctest.h>

#include <sme/audio.h>
#include <sme/sound_categories.h>
#include <sme/sound_edit.h>

#include <cmath>
#include <numbers>

using namespace sme;

namespace {

/// Mono (or stereo) sine with a value per frame that is easy to follow.
DecodedAudio tone(int frames, int rate = 22050, int channels = 1, double frequency = 440, double amplitude = 10000) {
	DecodedAudio audio;
	audio.sampleRate = rate;
	audio.channels = channels;
	audio.format = "PCM 16 bit";
	for (int f = 0; f < frames; ++f) {
		const auto v = static_cast<std::int16_t>(std::lround(std::sin(2 * std::numbers::pi * frequency * f / rate) * amplitude));
		for (int c = 0; c < channels; ++c) audio.samples.push_back(c == 0 ? v : std::int16_t(-v));
	}
	return audio;
}

/// Frames counting up: frame i has the value i (mono).
DecodedAudio ramp(int frames) {
	DecodedAudio audio;
	audio.sampleRate = 22050;
	audio.channels = 1;
	for (int i = 0; i < frames; ++i) audio.samples.push_back(std::int16_t(i));
	return audio;
}

double rms(const DecodedAudio& audio, std::size_t from, std::size_t to) {
	double sum = 0;
	for (std::size_t i = from; i < to; ++i) sum += double(audio.samples[i]) * audio.samples[i];
	return std::sqrt(sum / double(std::max<std::size_t>(1, to - from)));
}

} // namespace

TEST_CASE("sound editing: trim, cut, silence and the loop point") {
	auto audio = ramp(100);
	audio.loopStart = 40;
	const auto kept = trimmed(audio, {30, 60});
	REQUIRE(kept.frames() == 30);
	CHECK(kept.samples.front() == 30);
	CHECK(kept.samples.back() == 59);
	REQUIRE(kept.loopStart);
	CHECK(*kept.loopStart == 10);
	CHECK_FALSE(trimmed(audio, {50, 60}).loopStart);   // the loop point fell away
	CHECK(trimmed(audio, {}).frames() == 100);           // empty range: everything

	const auto shorter = cut(audio, {10, 20});
	REQUIRE(shorter.frames() == 90);
	CHECK(shorter.samples[9] == 9);
	CHECK(shorter.samples[10] == 20);
	REQUIRE(shorter.loopStart);
	CHECK(*shorter.loopStart == 30);
	CHECK(cut(audio, {}).frames() == 100);   // nothing selected, nothing cut
	CHECK(*cut(audio, {35, 45}).loopStart == 35);

	auto quiet = ramp(10);
	silence(quiet, {2, 5});
	CHECK(quiet.samples[1] == 1);
	CHECK(quiet.samples[2] == 0);
	CHECK(quiet.samples[4] == 0);
	CHECK(quiet.samples[5] == 5);
	// Out of range clamps instead of crashing.
	silence(quiet, {8, 500});
	CHECK(quiet.samples[9] == 0);
}

TEST_CASE("sound editing: fades, gain, normalize, reverse") {
	auto audio = tone(2000);
	const double before = rms(audio, 0, 2000);
	fadeIn(audio, {0, 1000});
	CHECK(audio.samples[0] == 0);
	CHECK(rms(audio, 0, 200) < rms(audio, 800, 1000) * 0.4);
	CHECK(rms(audio, 1000, 2000) == doctest::Approx(before).epsilon(0.05));   // after the fade untouched
	fadeOut(audio, {1500, 2000});
	CHECK(audio.samples[1999] == 0);

	auto loud = tone(1000);
	applyGain(loud, 6.0206);   // twice as loud
	CHECK(peakLevel(loud) == doctest::Approx(20000 / 32767.0).epsilon(0.01));
	applyGain(loud, 20);       // clips at full scale instead of wrapping around
	CHECK(peakLevel(loud) == doctest::Approx(1.0));
	for (const auto s : loud.samples) CHECK(std::abs(int(s)) <= 32768);

	auto soft = tone(1000, 22050, 1, 440, 1000);
	normalize(soft, -1);
	CHECK(peakLevel(soft) == doctest::Approx(std::pow(10.0, -1 / 20.0)).epsilon(0.01));
	auto silent = ramp(10);
	silence(silent, {});
	normalize(silent);   // stays silent, no division by zero
	CHECK(peakLevel(silent) == 0);

	auto backwards = ramp(10);
	reverse(backwards);
	CHECK(backwards.samples.front() == 9);
	CHECK(backwards.samples.back() == 0);
	auto part = ramp(10);
	reverse(part, {2, 5});
	CHECK(part.samples[2] == 4);
	CHECK(part.samples[4] == 2);
	CHECK(part.samples[5] == 5);
}

TEST_CASE("sound editing: resampling, speed and mono keep the sound") {
	const auto audio = tone(22050, 22050, 2, 440);   // one second, stereo
	const auto up = resampled(audio, 44100);
	CHECK(up.sampleRate == 44100);
	CHECK(up.channels == 2);
	CHECK(up.seconds() == doctest::Approx(1.0).epsilon(0.01));
	// Same tone: zero crossings per second stay ~880 (440 Hz).
	const auto crossings = [](const DecodedAudio& a) {
		int count = 0;
		const auto ch = std::size_t(a.channels);
		for (std::size_t f = 1; f < a.frames(); ++f) {
			if ((a.samples[(f - 1) * ch] < 0) != (a.samples[f * ch] < 0)) ++count;
		}
		return count / a.seconds();
	};
	CHECK(crossings(up) == doctest::Approx(880).epsilon(0.02));
	CHECK(rms(up, 2000, 80000) == doctest::Approx(rms(audio, 1000, 40000)).epsilon(0.05));
	const auto down = resampled(audio, 11025);
	CHECK(down.frames() == 11025);
	CHECK(crossings(down) == doctest::Approx(880).epsilon(0.02));
	// A tone above the new Nyquist frequency is filtered out instead of folding back.
	const auto high = resampled(tone(22050, 44100, 1, 15000), 22050);
	CHECK(rms(high, 100, high.frames() - 100) < 1000);

	const auto faster = speedChanged(audio, 2.0);
	CHECK(faster.sampleRate == 22050);
	CHECK(faster.seconds() == doctest::Approx(0.5).epsilon(0.01));
	CHECK(crossings(faster) == doctest::Approx(1760).epsilon(0.02));

	auto looping = ramp(1000);
	looping.loopStart = 500;
	CHECK(*resampled(looping, 44100).loopStart == 1000);

	const auto mono = toMono(tone(100, 22050, 2));
	CHECK(mono.channels == 1);
	CHECK(mono.frames() == 100);
	CHECK(peakLevel(mono) < 0.01);   // the two channels are opposite: they cancel
	CHECK(isSourceSampleRate(44100));
	CHECK_FALSE(isSourceSampleRate(48000));
}

TEST_CASE("sound editing: WAV files decode again, with their loop") {
	auto audio = tone(3000, 44100, 2);
	audio.loopStart = 1200;
	const auto file = encodeWav(audio);
	std::string error;
	const auto back = decodeAudio(file, "edited.wav", &error);
	REQUIRE_MESSAGE(back, error);
	CHECK(back->sampleRate == 44100);
	CHECK(back->channels == 2);
	CHECK(back->format == "PCM 16 bit");
	CHECK(back->samples == audio.samples);
	REQUIRE(back->loopStart);
	CHECK(*back->loopStart == 1200);
	// RIFF size matches the file; without a loop there is no cue chunk.
	const auto riff = std::uint32_t(file[4]) | std::uint32_t(file[5]) << 8 | std::uint32_t(file[6]) << 16 | std::uint32_t(file[7]) << 24;
	CHECK(riff == file.size() - 8);
	audio.loopStart.reset();
	const auto plain = decodeAudio(encodeWav(audio));
	REQUIRE(plain);
	CHECK_FALSE(plain->loopStart);
	// Odd sizes stay word aligned.
	auto odd = ramp(3);
	CHECK(encodeWav(odd).size() % 2 == 0);
}

TEST_CASE("sound categories: files by folder, scripts by name or file") {
	CHECK(categorizeSoundFile("weapons/pistol/pistol_fire2.wav") == SoundCategory::Weapons);
	CHECK(categorizeSoundFile("Weapons\\SMG1\\smg1_fire1.wav") == SoundCategory::Weapons);
	CHECK(categorizeSoundFile("npc/zombie/moan_loop1.wav") == SoundCategory::Npcs);
	CHECK(categorizeSoundFile("vo/npc/male01/hi01.wav") == SoundCategory::Voice);
	CHECK(categorizeSoundFile("ambient/wind1.wav") == SoundCategory::Ambient);
	CHECK(categorizeSoundFile("physics/metal/metal_box_impact_hard1.wav") == SoundCategory::Physics);
	CHECK(categorizeSoundFile("player/footsteps/concrete1.wav") == SoundCategory::Player);
	CHECK(categorizeSoundFile("vehicles/airboat/fan_blade_fullthrottle_loop1.wav") == SoundCategory::Vehicles);
	CHECK(categorizeSoundFile("doors/door_metal_thin_open1.wav") == SoundCategory::Doors);
	CHECK(categorizeSoundFile("buttons/button14.wav") == SoundCategory::Doors);
	CHECK(categorizeSoundFile("music/hl2_song12_long.mp3") == SoundCategory::Music);
	CHECK(categorizeSoundFile("ui/buttonclick.wav") == SoundCategory::Interface);
	CHECK(categorizeSoundFile(")weapons/shotgun/shotgun_fire6.wav") == SoundCategory::Weapons);   // script characters
	// Without a known folder the words decide.
	CHECK(categorizeSoundFile("mymod/big_door_open.wav") == SoundCategory::Doors);
	CHECK(categorizeSoundFile("mymod/song_intro.ogg") == SoundCategory::Music);
	CHECK(categorizeSoundFile("beep.wav") == SoundCategory::Other);

	CHECK(categorizeSoundScript("Weapon_Pistol.Single", "weapons/pistol/pistol_fire2.wav") == SoundCategory::Weapons);
	CHECK(categorizeSoundScript("Wood.ImpactHard", "physics/wood/wood_box_impact_hard1.wav") == SoundCategory::Physics);
	CHECK(categorizeSoundScript("Concrete.StepLeft", "player/footsteps/concrete1.wav") == SoundCategory::Player);
	CHECK(categorizeSoundScript("Metal_Box.Break", "") == SoundCategory::Physics);
	CHECK(categorizeSoundScript("NPC_Citizen.Hello", "vo/npc/male01/hi01.wav") == SoundCategory::Voice);   // spoken
	CHECK(categorizeSoundScript("NPC_Zombie.Moan", "npc/zombie/moan1.wav") == SoundCategory::Npcs);
	CHECK(categorizeSoundScript("NPC_Zombie.Moan", "") == SoundCategory::Npcs);
	CHECK(categorizeSoundScript("Doors.Move3", "") == SoundCategory::Doors);
	CHECK(categorizeSoundScript("Demo.Wind", "ambient/wind1.wav") == SoundCategory::Ambient);
	CHECK(categorizeSoundScript("Airboat.FanLoop", "") == SoundCategory::Vehicles);
	CHECK(categorizeSoundScript("Something.Else", "") == SoundCategory::Other);

	CHECK(soundFolder("weapons/pistol/fire.wav") == "weapons/pistol");
	CHECK(soundFolder("Beep.wav").empty());
	CHECK(soundCategoryName(SoundCategory::Npcs) == "NPCs and creatures");
	CHECK(SOUND_CATEGORIES.back() == SoundCategory::Other);
}
