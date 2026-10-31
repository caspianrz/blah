#include "internal/blah_internal.h"

#include <blah_audio.h>
#include <blah_time.h>

#define STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"

#define CUTE_SOUND_FORCE_SDL

#define CUTE_SOUND_IMPLEMENTATION
#define CUTE_SOUND_SCALAR_MODE
#include "third_party/cute_sound.h"

namespace Blah {
	namespace Internal {
		bool audio_init(unsigned play_frequency_in_Hz, int buffered_samples) {
			// SDL3 manages audio buffering.
			(void) buffered_samples;

			cs_error_t err = cs_init(play_frequency_in_Hz, nullptr);
			if (err != CUTE_SOUND_ERROR_NONE) {
				Log::error(cs_error_as_string(err));
				return false;
			}

			// cute_sound v3.00 uses SDL3's audio callback.
			// No separate mix thread is required.
			return true;
		}

		void audio_shutdown() {
			cs_shutdown();
		}

		void audio_update() {
			cs_update(Time::delta);
		}
	} // namespace Internal

	Audio::Audio(void *audio) {
		m_ptr = audio;
	}

	Audio::~Audio() {
		if (m_ptr)
			cs_free_audio_source(static_cast<cs_audio_source_t *>(m_ptr));
	}

	u64 Audio::get_sample_count() {
		if (!m_ptr)
			return 0;

		return static_cast<u64>(
			cs_get_sample_count(static_cast<cs_audio_source_t *>(m_ptr)));
	}

	AudioRef Audio::create(const FilePath &path) {
		FileStream fs(path, FileMode::OpenRead);
		if (!fs.is_readable())
			return AudioRef();

		if (path.ends_with(".ogg"))
			return create_from_ogg(fs);
		else if (path.ends_with(".wav"))
			return create_from_wav(fs);

		Log::error("Unable to load unrecognized audio file type.");
		return AudioRef();
	}

	AudioRef Audio::create_from_wav(Stream &stream) {
		if (!stream.is_readable()) {
			Log::error("Unable to load audio as the Stream was not readable");
			return AudioRef();
		}

		Vector<u8> buffer;
		buffer.resize((int) stream.length());
		stream.read(buffer.data(), stream.length());

		cs_error_t err;
		void *audio = cs_read_mem_wav(buffer.data(), stream.length(), &err);

		if (!audio) {
			Log::error(cs_error_as_string(err));
			return AudioRef();
		}

		return AudioRef(new Audio(audio));
	}

	AudioRef Audio::create_from_ogg(Stream &stream) {
		if (!stream.is_readable()) {
			Log::error("Unable to load audio as the Stream was not readable");
			return AudioRef();
		}

		Vector<u8> buffer;
		buffer.resize((int) stream.length());
		stream.read(buffer.data(), stream.length());

		cs_error_t err;
		void *audio = cs_read_mem_ogg(buffer.data(), stream.length(), &err);

		if (!audio) {
			Log::error(cs_error_as_string(err));
			return AudioRef();
		}

		return AudioRef(new Audio(audio));
	}

	void set_global_pan(float pan) {
		cs_set_global_pan(pan);
	}

	void set_global_volume(float volume) {
		cs_set_global_volume(volume);
	}

	void set_global_pause(bool true_for_paused) {
		cs_set_global_pause(true_for_paused);
	}

	void set_sound_volume(float volume) {
		cs_set_playing_sounds_volume(volume);
	}

	void set_music_volume(float volume) {
		cs_music_set_volume(volume);
	}

	namespace Music {
		void play(AudioRef audio, float fade_in_time) {
			cs_music_play(
				static_cast<cs_audio_source_t *>(audio->get_backend_handle()),
				fade_in_time);
		}

		void stop(float fade_out_time) {
			cs_music_stop(fade_out_time);
		}

		void set_volume(float volume) {
			cs_music_set_volume(volume);
		}

		void set_loop(bool true_to_loop) {
			cs_music_set_loop(true_to_loop);
		}

		void pause() {
			cs_music_pause();
		}

		void resume() {
			cs_music_resume();
		}

		void switch_to(AudioRef audio, float fade_out_time,
					   float fade_in_time) {
			cs_music_switch_to(
				static_cast<cs_audio_source_t *>(audio->get_backend_handle()),
				fade_out_time, fade_in_time);
		}

		void crossfade(AudioRef audio, float cross_fade_time) {
			cs_music_crossfade(
				static_cast<cs_audio_source_t *>(audio->get_backend_handle()),
				cross_fade_time);
		}

		double get_time() {
			return cs_music_get_time();
		}

		void set_time(double time_in_seconds) {
			cs_error_t err = cs_music_set_time(time_in_seconds);

			if (err != CUTE_SOUND_ERROR_NONE)
				Log::error(cs_error_as_string(err));
		}
	} // namespace Music

	Sound Sound::play(AudioRef audio, SoundParams params) {
		cs_sound_params_t csparams = cs_sound_params_default();

		csparams.paused = params.paused;
		csparams.looped = params.looped;
		csparams.volume = params.volume;
		csparams.pan	= params.pan;

		cs_playing_sound_t cssound = cs_play_sound(
			static_cast<cs_audio_source_t *>(audio->get_backend_handle()),
			csparams);

		Sound sound;
		sound.id = cssound.id;
		return sound;
	}

	bool Sound::is_active() {
		cs_playing_sound_t cssound = {id};
		return cs_sound_is_active(cssound);
	}

	bool Sound::is_paused() {
		cs_playing_sound_t cssound = {id};
		return cs_sound_get_is_paused(cssound);
	}

	bool Sound::is_looped() {
		cs_playing_sound_t cssound = {id};
		return cs_sound_get_is_looped(cssound);
	}

	float Sound::get_volume() {
		cs_playing_sound_t cssound = {id};
		return cs_sound_get_volume(cssound);
	}

	double Sound::get_time() {
		cs_playing_sound_t cssound = {id};
		return cs_sound_get_time(cssound);
	}

	void Sound::set_is_paused(bool true_for_paused) {
		cs_playing_sound_t cssound = {id};
		cs_sound_set_is_paused(cssound, true_for_paused);
	}

	void Sound::set_is_looped(bool true_for_looped) {
		cs_playing_sound_t cssound = {id};
		cs_sound_set_is_looped(cssound, true_for_looped);
	}

	void Sound::set_volume(float volume) {
		cs_playing_sound_t cssound = {id};
		cs_sound_set_volume(cssound, volume);
	}

	void Sound::set_time(double time_in_seconds) {
		cs_playing_sound_t cssound = {id};

		cs_error_t err = cs_sound_set_time(cssound, time_in_seconds);

		if (err != CUTE_SOUND_ERROR_NONE)
			Log::error(cs_error_as_string(err));
	}
} // namespace Blah

#undef STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"
