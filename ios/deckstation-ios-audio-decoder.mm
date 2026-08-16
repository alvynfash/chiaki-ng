// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "deckstation-ios-audio-decoder.hpp"

DeckStationIOSAudioDecoder::DeckStationIOSAudioDecoder(
	ChiakiLog *log, DeckStationIOSAudioCallback callback, void *callback_user)
	: callback_(callback), callback_user_(callback_user)
{
	chiaki_opus_decoder_init(&decoder_, log);
	chiaki_opus_decoder_set_cb(&decoder_, SettingsCallback, FrameCallback, this);
}

DeckStationIOSAudioDecoder::~DeckStationIOSAudioDecoder()
{
	chiaki_opus_decoder_fini(&decoder_);
}

void DeckStationIOSAudioDecoder::GetSink(ChiakiAudioSink *sink)
{
	chiaki_opus_decoder_get_sink(&decoder_, sink);
}

void DeckStationIOSAudioDecoder::SettingsCallback(
	uint32_t channels, uint32_t rate, void *user)
{
	if(user)
		static_cast<DeckStationIOSAudioDecoder *>(user)->Configure(channels, rate);
}

void DeckStationIOSAudioDecoder::FrameCallback(
	int16_t *samples, size_t frame_count, void *user)
{
	if(user && samples && frame_count)
		static_cast<DeckStationIOSAudioDecoder *>(user)->Deliver(
			samples, frame_count);
}

void DeckStationIOSAudioDecoder::Configure(uint32_t channels, uint32_t rate)
{
	channels_ = channels;
	sample_rate_ = rate;
}

void DeckStationIOSAudioDecoder::Deliver(
	int16_t *samples, size_t frame_count)
{
	if(!callback_ || channels_ == 0 || sample_rate_ == 0)
		return;
	const size_t sample_count = frame_count * channels_;
	decoded_frames_++;
	decoded_samples_ += sample_count;
	callback_(callback_user_, samples, static_cast<uint32_t>(sample_count),
		channels_, sample_rate_);
}
