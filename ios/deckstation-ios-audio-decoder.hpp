// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef DECKSTATION_IOS_AUDIO_DECODER_HPP
#define DECKSTATION_IOS_AUDIO_DECODER_HPP

#include "deckstation-ios-runtime.h"

#include <atomic>
#include <cstdint>

#include <chiaki/audio.h>
#include <chiaki/log.h>
#include <chiaki/opusdecoder.h>

class DeckStationIOSAudioDecoder
{
public:
	DeckStationIOSAudioDecoder(ChiakiLog *log,
		DeckStationIOSAudioCallback callback, void *callback_user);
	~DeckStationIOSAudioDecoder();

	void GetSink(ChiakiAudioSink *sink);
	uint64_t DecodedFrames() const { return decoded_frames_.load(); }
	uint64_t DecodedSamples() const { return decoded_samples_.load(); }

private:
	static void SettingsCallback(uint32_t channels, uint32_t rate, void *user);
	static void FrameCallback(int16_t *samples, size_t frame_count, void *user);
	void Configure(uint32_t channels, uint32_t rate);
	void Deliver(int16_t *samples, size_t frame_count);

	ChiakiOpusDecoder decoder_{};
	DeckStationIOSAudioCallback callback_;
	void *callback_user_;
	uint32_t channels_ = 0;
	uint32_t sample_rate_ = 0;
	std::atomic<uint64_t> decoded_frames_{0};
	std::atomic<uint64_t> decoded_samples_{0};
};

#endif
