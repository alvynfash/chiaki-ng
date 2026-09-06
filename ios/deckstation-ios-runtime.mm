// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "deckstation-ios-runtime.h"
#include "deckstation-ios-audio-decoder.hpp"
#include "deckstation-ios-video-decoder.hpp"

#import <Foundation/Foundation.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include <chiaki/base64.h>
#include <chiaki/controller.h>
#include <chiaki/headless.h>
#include <chiaki/packetstats.h>
#include <chiaki/session.h>
#include <chiaki/time.h>

namespace
{
struct RuntimeCallbacks
{
	void *user = nullptr;
	DeckStationIOSVideoCallback video = nullptr;
	DeckStationIOSAudioCallback audio = nullptr;
	DeckStationIOSEventCallback event = nullptr;
	DeckStationIOSStatsCallback stats = nullptr;
};

struct RuntimeSession
{
	ChiakiSession session{};
	ChiakiLog log{};
	std::unique_ptr<DeckStationIOSVideoDecoder> video_decoder;
	std::unique_ptr<DeckStationIOSAudioDecoder> audio_decoder;
	RuntimeCallbacks callbacks;
	std::string host;
	std::string session_id;
	std::string launch_spec;
	bool session_initialized = false;
	bool started = false;
	std::atomic<bool> local_stop_requested{false};
	std::atomic<uint64_t> video_samples{0};
	std::atomic<uint64_t> audio_frames{0};
	std::atomic<uint64_t> audio_samples{0};
	std::atomic<uint64_t> video_decode_lost_frames{0};
	std::atomic<uint64_t> video_decode_recovered_frames{0};
	std::atomic<uint64_t> video_decode_gap_events{0};
	uint64_t last_stats_emit_monotonic_us = 0;
};

std::mutex callbacks_mutex;
RuntimeCallbacks callbacks;
std::mutex lifecycle_mutex;
std::mutex session_mutex;
RuntimeSession *active_session = nullptr;
std::mutex variant_mutex;
std::string controller_variant = "ds4:0";

void emit(RuntimeSession *runtime, const char *type, const char *detail,
	int64_t value0 = 0, int64_t value1 = 0)
{
	if(runtime && runtime->callbacks.event)
		runtime->callbacks.event(runtime->callbacks.user, type, detail,
			value0, value1);
}

void emit_stream_stats(RuntimeSession *runtime)
{
	if(!runtime || !runtime->callbacks.stats || !runtime->session_initialized)
		return;
	const uint64_t now = chiaki_time_now_monotonic_us();
	if(runtime->last_stats_emit_monotonic_us != 0
		&& now - runtime->last_stats_emit_monotonic_us < 1000000)
		return;
	runtime->last_stats_emit_monotonic_us = now;

	ChiakiStreamConnection *connection = &runtime->session.stream_connection;
	uint64_t packets_received = 0;
	uint64_t packets_lost = 0;
	uint64_t video_stream_bytes = 0;
	uint64_t video_stream_frames = 0;
	chiaki_packet_stats_get(&connection->packet_stats, false,
		&packets_received, &packets_lost);
	if(connection->video_receiver)
	{
		ChiakiStreamStats *stream_stats =
			&connection->video_receiver->frame_processor.stream_stats;
		video_stream_bytes = stream_stats->total_bytes;
		video_stream_frames = stream_stats->total_frames;
	}

	const double values[] = {
		connection->measured_bitrate,
		connection->last_connection_quality_rtt_ms,
		static_cast<double>(connection->last_connection_quality_target_bitrate),
		static_cast<double>(connection->last_connection_quality_upstream_bitrate),
		connection->last_connection_quality_upstream_loss,
		static_cast<double>(connection->last_connection_quality_loss_raw),
		static_cast<double>(packets_received),
		static_cast<double>(packets_lost),
		static_cast<double>(video_stream_bytes),
		static_cast<double>(video_stream_frames),
		static_cast<double>(runtime->video_decoder
			? runtime->video_decoder->RenderedFrames() : 0),
		static_cast<double>(runtime->audio_frames.load()),
		static_cast<double>(runtime->video_decode_lost_frames.load()),
		static_cast<double>(runtime->video_decode_recovered_frames.load()),
		static_cast<double>(runtime->video_decode_gap_events.load()),
	};
	runtime->callbacks.stats(runtime->callbacks.user, values,
		static_cast<int32_t>(sizeof(values) / sizeof(values[0])));
}

void log_callback(ChiakiLogLevel level, const char *message, void *user)
{
	(void)user;
	if(message)
		NSLog(@"[DeckStationChiakiIOS][%c] %s",
			chiaki_log_level_char(level), message);
}

void session_event(ChiakiEvent *event, void *user)
{
	auto *runtime = static_cast<RuntimeSession *>(user);
	if(!event || !runtime)
		return;
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			emit(runtime, "ready", "Chiaki session connected");
			break;
		case CHIAKI_EVENT_VIDEO_FEC_FAILURE:
			emit(runtime, "warning", "Video FEC failure",
				(int64_t)event->video_fec_failure.frame_index,
				event->video_fec_failure.idr_request_sent ? 1 : 0);
			break;
		case CHIAKI_EVENT_QUIT:
			if(!runtime->local_stop_requested.load())
				emit(runtime, "terminal",
					event->quit.reason_str
						? event->quit.reason_str : "Chiaki session stopped",
					(int64_t)event->quit.reason, 0);
			break;
		default:
			break;
	}
}

bool video_sample(uint8_t *data, size_t size, int frames_lost,
	bool frame_recovered, void *user)
{
	auto *runtime = static_cast<RuntimeSession *>(user);
	if(!runtime || !runtime->video_decoder)
		return false;
	runtime->video_samples++;
	if(frames_lost > 0)
	{
		runtime->video_decode_lost_frames += static_cast<uint64_t>(frames_lost);
		runtime->video_decode_gap_events++;
	}
	if(frame_recovered)
		runtime->video_decode_recovered_frames++;
	const bool accepted = runtime->video_decoder->Submit(
		data, size, frames_lost, frame_recovered);
	emit_stream_stats(runtime);
	return accepted;
}

void audio_proxy(void *user, const int16_t *samples, uint32_t sample_count,
	uint32_t channels, uint32_t sample_rate)
{
	auto *runtime = static_cast<RuntimeSession *>(user);
	if(!runtime || runtime->local_stop_requested.load())
		return;
	runtime->audio_frames++;
	runtime->audio_samples += sample_count;
	if(runtime->callbacks.audio)
		runtime->callbacks.audio(runtime->callbacks.user, samples, sample_count,
			channels, sample_rate);
}

void video_proxy(void *user, void *pixel_buffer, int32_t width, int32_t height,
	int64_t monotonic_time_us)
{
	auto *runtime = static_cast<RuntimeSession *>(user);
	if(!runtime || runtime->local_stop_requested.load())
	{
		if(pixel_buffer)
			CFRelease(pixel_buffer);
		return;
	}
	if(runtime->callbacks.video)
		runtime->callbacks.video(runtime->callbacks.user, pixel_buffer,
			width, height, monotonic_time_us);
	else if(pixel_buffer)
		CFRelease(pixel_buffer);
}

int hex_nibble(char value)
{
	if(value >= '0' && value <= '9') return value - '0';
	if(value >= 'a' && value <= 'f') return value - 'a' + 10;
	if(value >= 'A' && value <= 'F') return value - 'A' + 10;
	return -1;
}

bool decode_hex(const std::string &value, uint8_t *output, size_t output_size)
{
	if(value.size() > output_size * 2 || value.size() % 2 != 0)
		return false;
	std::memset(output, 0, output_size);
	for(size_t index = 0; index < value.size() / 2; index++)
	{
		const int high = hex_nibble(value[index * 2]);
		const int low = hex_nibble(value[index * 2 + 1]);
		if(high < 0 || low < 0)
			return false;
		output[index] = static_cast<uint8_t>((high << 4) | low);
	}
	return true;
}

NSString *string_value(NSDictionary *json, NSString *key)
{
	id value = json[key];
	return [value isKindOfClass:[NSString class]] ? value : @"";
}

NSInteger integer_value(NSDictionary *json, NSString *key, NSInteger fallback)
{
	id value = json[key];
	return [value respondsToSelector:@selector(integerValue)]
		? [value integerValue] : fallback;
}

bool bool_value(NSDictionary *json, NSString *key, bool fallback)
{
	id value = json[key];
	return [value respondsToSelector:@selector(boolValue)]
		? [value boolValue] : fallback;
}

void free_session(RuntimeSession *runtime)
{
	if(!runtime)
		return;
	if(runtime->started)
	{
		runtime->local_stop_requested = true;
		chiaki_session_stop(&runtime->session);
		chiaki_session_join(&runtime->session);
		runtime->started = false;
	}
	if(runtime->session_initialized)
	{
		chiaki_session_fini(&runtime->session);
		runtime->session_initialized = false;
	}
	// VideoToolbox must drain before the callback context is destroyed.
	runtime->video_decoder.reset();
	runtime->audio_decoder.reset();
	delete runtime;
}
}

extern "C" void deckstation_ios_runtime_set_callbacks(void *user,
	DeckStationIOSVideoCallback video_callback,
	DeckStationIOSAudioCallback audio_callback,
	DeckStationIOSEventCallback event_callback,
	DeckStationIOSStatsCallback stats_callback)
{
	std::lock_guard<std::mutex> lock(callbacks_mutex);
	callbacks = {user, video_callback, audio_callback, event_callback,
		stats_callback};
}

extern "C" int32_t deckstation_ios_runtime_start_json(const char *json_value)
{
	if(!json_value)
		return CHIAKI_ERR_INVALID_DATA;
	std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex);
	@autoreleasepool
	{
		NSData *data = [NSData dataWithBytes:json_value length:std::strlen(json_value)];
		NSError *json_error = nil;
		id object = [NSJSONSerialization JSONObjectWithData:data options:0 error:&json_error];
		if(json_error || ![object isKindOfClass:[NSDictionary class]])
			return CHIAKI_ERR_INVALID_DATA;
		NSDictionary *json = object;

		std::unique_ptr<RuntimeSession> runtime(new RuntimeSession());
		{
			std::lock_guard<std::mutex> lock(callbacks_mutex);
			runtime->callbacks = callbacks;
		}
		runtime->host = [string_value(json, @"host") UTF8String];
		runtime->session_id = [string_value(json, @"sessionId") UTF8String];
		runtime->launch_spec = [string_value(json, @"launchSpec") UTF8String];
		const std::string morning_b64 = [string_value(json, @"morningB64") UTF8String];
		const std::string regist_key_hex = [string_value(json, @"registKeyHex") UTF8String];
		if(runtime->host.empty() || runtime->session_id.empty()
			|| runtime->launch_spec.empty() || morning_b64.empty())
			return CHIAKI_ERR_INVALID_DATA;

		uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
		size_t morning_size = sizeof(morning);
		uint8_t regist_key[CHIAKI_SESSION_AUTH_SIZE] = {0};
		ChiakiErrorCode error = chiaki_base64_decode(morning_b64.c_str(),
			morning_b64.size(), morning, &morning_size);
		if(error == CHIAKI_ERR_SUCCESS && morning_size != sizeof(morning))
			error = CHIAKI_ERR_INVALID_DATA;
		if(error == CHIAKI_ERR_SUCCESS && !regist_key_hex.empty()
			&& !decode_hex(regist_key_hex, regist_key, sizeof(regist_key)))
			error = CHIAKI_ERR_INVALID_DATA;
		if(error != CHIAKI_ERR_SUCCESS)
			return error;

		chiaki_log_init(&runtime->log, CHIAKI_LOG_WARNING | CHIAKI_LOG_ERROR,
			log_callback, nullptr);
		ChiakiHeadlessCloudLaunchInfo launch{
			.host = runtime->host.c_str(),
			.stream_port = static_cast<uint16_t>(integer_value(json, @"streamPort", 0)),
			.session_id = runtime->session_id.c_str(),
			.launch_spec = runtime->launch_spec.c_str(),
			.morning = morning,
			.morning_size = sizeof(morning),
			.regist_key = regist_key,
			.regist_key_size = sizeof(regist_key),
			.ps5 = bool_value(json, @"ps5", true),
			.enable_dualsense = bool_value(json, @"enableDualsense", true),
			.enable_keyboard = bool_value(json, @"enableKeyboard", false),
			.takion_protocol_version = static_cast<uint8_t>(
				integer_value(json, @"takionProtocolVersion", 12)),
			.psn_wrapper_type = static_cast<uint8_t>(
				integer_value(json, @"psnWrapperType", 1)),
			.resolution = static_cast<ChiakiVideoResolutionPreset>(
				integer_value(json, @"resolutionPreset", 4)),
			.fps = static_cast<ChiakiVideoFPSPreset>(
				integer_value(json, @"fpsPreset", 2)),
			.bitrate = static_cast<unsigned int>(
				integer_value(json, @"bitrate", 15000)),
			.codec = static_cast<ChiakiCodec>(integer_value(json, @"codec", 1)),
		};
		ChiakiConnectInfo connect_info{};
		// Keep cloud launch construction byte-for-byte aligned with the desktop
		// headless runtime. VideoToolbox remains an iOS-only consumer after the
		// shared transport configuration has been built.
		error = chiaki_headless_runtime_build_cloud_connect_info(
			&connect_info, &launch);
		if(error != CHIAKI_ERR_SUCCESS)
			return error;

		runtime->video_decoder = std::make_unique<DeckStationIOSVideoDecoder>(
			&runtime->log, connect_info.video_profile.codec, video_proxy,
			runtime.get());
		runtime->audio_decoder = std::make_unique<DeckStationIOSAudioDecoder>(
			&runtime->log, audio_proxy, runtime.get());
		error = chiaki_session_init(&runtime->session, &connect_info, &runtime->log);
		if(error == CHIAKI_ERR_SUCCESS)
			runtime->session_initialized = true;
		if(error == CHIAKI_ERR_SUCCESS)
		{
			chiaki_session_set_event_cb(&runtime->session, session_event, runtime.get());
			chiaki_session_set_video_sample_cb(&runtime->session, video_sample, runtime.get());
			ChiakiAudioSink audio_sink{};
			runtime->audio_decoder->GetSink(&audio_sink);
			chiaki_session_set_audio_sink(&runtime->session, &audio_sink);
			error = chiaki_session_start(&runtime->session);
			if(error == CHIAKI_ERR_SUCCESS)
				runtime->started = true;
		}
		if(error != CHIAKI_ERR_SUCCESS)
		{
			free_session(runtime.release());
			return error;
		}

		RuntimeSession *installed = runtime.release();
		RuntimeSession *previous = nullptr;
		{
			std::lock_guard<std::mutex> lock(session_mutex);
			previous = active_session;
			active_session = installed;
		}
		free_session(previous);
		emit(installed, "starting", "iOS Chiaki session starting");
		return CHIAKI_ERR_SUCCESS;
	}
}

extern "C" int32_t deckstation_ios_runtime_stop(void)
{
	std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex);
	RuntimeSession *runtime = nullptr;
	{
		std::lock_guard<std::mutex> lock(session_mutex);
		runtime = active_session;
		active_session = nullptr;
	}
	free_session(runtime);
	return CHIAKI_ERR_SUCCESS;
}

extern "C" int32_t deckstation_ios_runtime_stats(
	uint64_t *values, int32_t value_count)
{
	if(!values || value_count <= 0)
		return CHIAKI_ERR_INVALID_DATA;
	for(int32_t index = 0; index < value_count; index++)
		values[index] = 0;
	std::lock_guard<std::mutex> lock(session_mutex);
	if(!active_session)
		return CHIAKI_ERR_SUCCESS;
	RuntimeSession *runtime = active_session;
	if(value_count > 0) values[0] = runtime->video_samples.load();
	if(value_count > 1 && runtime->video_decoder)
		values[1] = runtime->video_decoder->RenderedFrames();
	if(value_count > 2) values[2] = runtime->audio_frames.load();
	if(value_count > 3) values[3] = runtime->audio_samples.load();
	if(value_count > 4) values[4] = runtime->started ? 1 : 0;
	if(value_count > 5 && runtime->video_decoder)
		values[5] = runtime->video_decoder->RejectedFrames();
	ChiakiStreamConnection *connection = &runtime->session.stream_connection;
	uint64_t packets_received = 0;
	uint64_t packets_lost = 0;
	chiaki_packet_stats_get(&connection->packet_stats, false,
		&packets_received, &packets_lost);
	if(value_count > 6) values[6] = packets_received;
	if(value_count > 7) values[7] = packets_lost;
	if(connection->video_receiver)
	{
		ChiakiStreamStats *stream_stats =
			&connection->video_receiver->frame_processor.stream_stats;
		if(value_count > 8) values[8] = stream_stats->total_bytes;
		if(value_count > 9) values[9] = stream_stats->total_frames;
		if(value_count > 10) values[10] = 1;
	}
	chiaki_mutex_lock(&connection->state_mutex);
	if(value_count > 11) values[11] = static_cast<uint64_t>(connection->state);
	if(value_count > 12) values[12] = connection->state_finished ? 1 : 0;
	if(value_count > 13) values[13] = connection->state_failed ? 1 : 0;
	if(value_count > 14) values[14] = connection->remote_disconnected ? 1 : 0;
	if(value_count > 15) values[15] = connection->last_big_client_version;
	chiaki_mutex_unlock(&connection->state_mutex);
	return CHIAKI_ERR_SUCCESS;
}

extern "C" int32_t deckstation_ios_runtime_send_controller_state(
	int32_t buttons, int32_t l2_state, int32_t r2_state,
	int32_t left_x, int32_t left_y, int32_t right_x, int32_t right_y,
	int32_t touch0_id, int32_t touch0_x, int32_t touch0_y,
	int32_t touch1_id, int32_t touch1_x, int32_t touch1_y)
{
	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);
	state.buttons = static_cast<uint32_t>(buttons);
	state.l2_state = static_cast<uint8_t>(l2_state);
	state.r2_state = static_cast<uint8_t>(r2_state);
	state.left_x = static_cast<int16_t>(left_x);
	state.left_y = static_cast<int16_t>(left_y);
	state.right_x = static_cast<int16_t>(right_x);
	state.right_y = static_cast<int16_t>(right_y);
	state.touches[0].id = static_cast<int8_t>(touch0_id);
	state.touches[0].x = static_cast<uint16_t>(touch0_x);
	state.touches[0].y = static_cast<uint16_t>(touch0_y);
	state.touches[1].id = static_cast<int8_t>(touch1_id);
	state.touches[1].x = static_cast<uint16_t>(touch1_x);
	state.touches[1].y = static_cast<uint16_t>(touch1_y);

	std::lock_guard<std::mutex> lock(session_mutex);
	return active_session && active_session->session_initialized
		? chiaki_session_set_controller_state(&active_session->session, &state)
		: CHIAKI_ERR_UNINITIALIZED;
}

extern "C" const char *deckstation_ios_runtime_set_cloud_controller_variant(
	const char *value)
{
	std::lock_guard<std::mutex> lock(variant_mutex);
	if(value && *value)
		controller_variant = value;
	setenv("CHIAKI_CLOUD_CONTROLLER_VARIANT", controller_variant.c_str(), 1);
	return controller_variant.c_str();
}
