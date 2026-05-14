// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/headless.h>
#include <chiaki/audioreceiver.h>
#include <chiaki/packetstats.h>
#include <chiaki/time.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/base64.h>
#include <chiaki/log.h>
#include <chiaki/streamconnection.h>

#if CHIAKI_LIB_ENABLE_FFMPEG_DECODER
#include <chiaki/ffmpegdecoder.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/pixfmt.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifndef _WIN32
#include <unistd.h>
#endif

static ChiakiMutex g_runtime_lock;
static bool g_runtime_lock_init = false;
static ChiakiHeadlessSession *g_runtime_session = NULL;
static bool g_runtime_start_in_flight = false;
static uint64_t g_runtime_stop_generation = 0;
static ChiakiLog g_runtime_log;
static bool g_runtime_log_init = false;
static ChiakiHeadlessCallbacks g_runtime_callbacks = {0};
static bool g_runtime_callbacks_set = false;
static ChiakiHeadlessRuntimeAudioSinkConfig g_runtime_audio_sink_config = {0};
static bool g_runtime_audio_sink_config_set = false;
static ChiakiHeadlessStreamProfileOverrides g_runtime_stream_profile_overrides = {0};
static bool g_runtime_stream_profile_overrides_set = false;
static ChiakiHeadlessLaunchOverrides g_runtime_launch_overrides = {0};
static bool g_runtime_launch_overrides_set = false;
typedef ChiakiHeadlessRuntimePolicyOverrides HeadlessRuntimePolicyOverrides;

static HeadlessRuntimePolicyOverrides g_runtime_policy_overrides = {0};
static bool g_runtime_policy_overrides_set = false;
static uint64_t g_runtime_recovery_last_idr_request_us = 0;
static uint64_t g_runtime_recovery_degraded_streak = 0;
static ChiakiHeadlessRuntimeRecoveryAction g_runtime_recovery_last_recommended_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
static ChiakiHeadlessRuntimeRecoveryAction g_runtime_recovery_last_applied_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
static uint64_t g_runtime_recovery_attempt_count = 0;
static uint64_t g_runtime_recovery_success_count = 0;
static uint64_t g_runtime_recovery_failure_count = 0;
static ChiakiHeadlessRuntimeRecoveryResult g_runtime_last_recovery_result = {0};
static bool g_runtime_last_recovery_result_valid = false;
static ChiakiHeadlessRuntimeRecoveryConfig g_runtime_recovery_config = {0};
static bool g_runtime_recovery_config_init = false;
static const uint32_t kHeadlessExternalVideoAbiRevision = 1;
static uint64_t g_headless_ext_video_gate_logs = 0;
static uint64_t g_headless_ext_video_emit_logs = 0;
static const uint32_t kDrmFormatNv12 =
	((uint32_t)'N') |
	((uint32_t)'V' << 8) |
	((uint32_t)'1' << 16) |
	((uint32_t)'2' << 24);
static const uint32_t kDrmFormatR8 =
	((uint32_t)'R') |
	((uint32_t)'8' << 8) |
	((uint32_t)' ' << 16) |
	((uint32_t)' ' << 24);
static const uint32_t kDrmFormatGr88 =
	((uint32_t)'G') |
	((uint32_t)'R' << 8) |
	((uint32_t)'8' << 16) |
	((uint32_t)'8' << 24);

static ChiakiErrorCode headless_runtime_ensure_lock(void);
static void headless_runtime_recovery_status_reset_locked(void);

typedef struct headless_runtime_config_t
{
	ChiakiHeadlessCallbacks callbacks;
	bool callbacks_set;
	ChiakiHeadlessRuntimeAudioSinkConfig audio_sink_config;
	bool audio_sink_config_set;
	ChiakiHeadlessLaunchOverrides launch_overrides;
	bool launch_overrides_set;
	ChiakiHeadlessStreamProfileOverrides stream_profile_overrides;
	bool stream_profile_overrides_set;
	HeadlessRuntimePolicyOverrides policy_overrides;
	bool policy_overrides_set;
} HeadlessRuntimeConfig;

static void headless_runtime_recovery_status_reset_locked(void)
{
	g_runtime_recovery_last_idr_request_us = 0;
	g_runtime_recovery_degraded_streak = 0;
	g_runtime_recovery_last_recommended_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
	g_runtime_recovery_last_applied_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
	g_runtime_recovery_attempt_count = 0;
	g_runtime_recovery_success_count = 0;
	g_runtime_recovery_failure_count = 0;
	memset(&g_runtime_last_recovery_result, 0, sizeof(g_runtime_last_recovery_result));
	g_runtime_last_recovery_result_valid = false;
}

static void headless_runtime_recovery_config_init_locked(void)
{
	if(g_runtime_recovery_config_init)
		return;
	memset(&g_runtime_recovery_config, 0, sizeof(g_runtime_recovery_config));
	g_runtime_recovery_config.api_version = chiaki_headless_api_version();
	g_runtime_recovery_config.profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	g_runtime_recovery_config.tuning.api_version = chiaki_headless_api_version();
	g_runtime_recovery_config.tuning.degraded_streak_threshold = 3;
	g_runtime_recovery_config.tuning.idr_cooldown_sec = 2;
	g_runtime_recovery_config.tuning.stop_on_terminal = true;
	g_runtime_recovery_config_init = true;
}

static bool headless_is_valid_resolution_preset(ChiakiVideoResolutionPreset resolution)
{
	return resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_360p
		|| resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_540p
		|| resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_720p
		|| resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
}

static bool headless_is_valid_fps_preset(ChiakiVideoFPSPreset fps)
{
	return fps == CHIAKI_VIDEO_FPS_PRESET_30 || fps == CHIAKI_VIDEO_FPS_PRESET_60;
}

static bool headless_is_valid_codec(ChiakiCodec codec)
{
	return codec == CHIAKI_CODEC_H264
		|| codec == CHIAKI_CODEC_H265
		|| codec == CHIAKI_CODEC_H265_HDR;
}

static ChiakiErrorCode headless_validate_stream_profile_overrides(const ChiakiHeadlessStreamProfileOverrides *overrides)
{
	if(!overrides)
		return CHIAKI_ERR_SUCCESS;
	if(overrides->use_resolution && !headless_is_valid_resolution_preset(overrides->resolution))
		return CHIAKI_ERR_INVALID_DATA;
	if(overrides->use_fps && !headless_is_valid_fps_preset(overrides->fps))
		return CHIAKI_ERR_INVALID_DATA;
	if(overrides->use_bitrate && overrides->bitrate == 0)
		return CHIAKI_ERR_INVALID_DATA;
	if(overrides->use_codec && !headless_is_valid_codec(overrides->codec))
		return CHIAKI_ERR_INVALID_DATA;
	return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode headless_validate_policy_overrides(
	const HeadlessRuntimePolicyOverrides *policy_overrides)
{
	if(!policy_overrides)
		return CHIAKI_ERR_SUCCESS;
	if(policy_overrides->use_packet_loss_max)
	{
		if(policy_overrides->packet_loss_max < 0.0 || policy_overrides->packet_loss_max > 1.0)
			return CHIAKI_ERR_INVALID_DATA;
	}
	return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode headless_validate_launch_stream_fields(const ChiakiHeadlessCloudLaunchInfo *launch)
{
	if(!launch)
		return CHIAKI_ERR_INVALID_DATA;
	if(launch->resolution != 0 && !headless_is_valid_resolution_preset(launch->resolution))
		return CHIAKI_ERR_INVALID_DATA;
	if(launch->fps != 0 && !headless_is_valid_fps_preset(launch->fps))
		return CHIAKI_ERR_INVALID_DATA;
	if(!headless_is_valid_codec(launch->codec))
		return CHIAKI_ERR_INVALID_DATA;
	return CHIAKI_ERR_SUCCESS;
}

static void headless_apply_stream_profile_overrides(
	ChiakiHeadlessCloudLaunchInfo *launch,
	const ChiakiHeadlessStreamProfileOverrides *overrides,
	bool overrides_set)
{
	if(!launch || !overrides_set || !overrides)
		return;
	if(overrides->use_resolution)
		launch->resolution = overrides->resolution;
	if(overrides->use_fps)
		launch->fps = overrides->fps;
	if(overrides->use_bitrate)
		launch->bitrate = overrides->bitrate;
	if(overrides->use_codec)
		launch->codec = overrides->codec;
}

static void headless_apply_launch_overrides(
	ChiakiHeadlessCloudLaunchInfo *launch,
	const ChiakiHeadlessLaunchOverrides *overrides,
	bool overrides_set)
{
	if(!launch || !overrides_set || !overrides)
		return;
	if(overrides->use_ps5)
		launch->ps5 = overrides->ps5;
	if(overrides->use_enable_dualsense)
		launch->enable_dualsense = overrides->enable_dualsense;
	if(overrides->use_enable_keyboard)
		launch->enable_keyboard = overrides->enable_keyboard;
}

static void headless_apply_runtime_config_to_launch(
	ChiakiHeadlessCloudLaunchInfo *launch,
	const HeadlessRuntimeConfig *config)
{
	if(!launch || !config)
		return;
	headless_apply_launch_overrides(launch, &config->launch_overrides, config->launch_overrides_set);
	headless_apply_stream_profile_overrides(launch, &config->stream_profile_overrides, config->stream_profile_overrides_set);
}

static void headless_apply_runtime_policy_overrides_to_connect_info(
	ChiakiConnectInfo *connect_info,
	const HeadlessRuntimeConfig *config)
{
	if(!connect_info || !config || !config->policy_overrides_set)
		return;

	if(config->policy_overrides.use_video_profile_auto_downgrade)
		connect_info->video_profile_auto_downgrade = config->policy_overrides.video_profile_auto_downgrade;
	if(config->policy_overrides.use_enable_idr_on_fec_failure)
		connect_info->enable_idr_on_fec_failure = config->policy_overrides.enable_idr_on_fec_failure;
	if(config->policy_overrides.use_packet_loss_max)
		connect_info->packet_loss_max = config->policy_overrides.packet_loss_max;
}

static ChiakiErrorCode headless_runtime_snapshot_config(HeadlessRuntimeConfig *out_config)
{
	if(!out_config)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	out_config->callbacks = g_runtime_callbacks;
	out_config->callbacks_set = g_runtime_callbacks_set;
	out_config->audio_sink_config = g_runtime_audio_sink_config;
	out_config->audio_sink_config_set = g_runtime_audio_sink_config_set;
	out_config->launch_overrides = g_runtime_launch_overrides;
	out_config->launch_overrides_set = g_runtime_launch_overrides_set;
	out_config->stream_profile_overrides = g_runtime_stream_profile_overrides;
	out_config->stream_profile_overrides_set = g_runtime_stream_profile_overrides_set;
	out_config->policy_overrides = g_runtime_policy_overrides;
	out_config->policy_overrides_set = g_runtime_policy_overrides_set;
	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode headless_runtime_ensure_lock(void)
{
	if(g_runtime_lock_init)
		return CHIAKI_ERR_SUCCESS;
	ChiakiErrorCode err = chiaki_mutex_init(&g_runtime_lock, false);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	g_runtime_lock_init = true;
	return CHIAKI_ERR_SUCCESS;
}

static void headless_regist_key_zero_fill(uint8_t dst[CHIAKI_SESSION_AUTH_SIZE], const uint8_t *src, size_t src_size)
{
	memset(dst, 0, CHIAKI_SESSION_AUTH_SIZE);
	if(!src || src_size == 0)
		return;
	size_t n = src_size < CHIAKI_SESSION_AUTH_SIZE ? src_size : CHIAKI_SESSION_AUTH_SIZE;
	memcpy(dst, src, n);
}

static int headless_hex_nibble(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static void headless_noop_log_cb(ChiakiLogLevel level, const char *msg, void *user)
{
	(void)level;
	(void)msg;
	(void)user;
}

static ChiakiErrorCode headless_decode_hex(const char *hex, uint8_t *out, size_t out_size, size_t *decoded_size)
{
	if(!hex)
		return CHIAKI_ERR_INVALID_DATA;
	size_t len = strlen(hex);
	if((len % 2) != 0)
		return CHIAKI_ERR_INVALID_DATA;
	size_t bytes = len / 2;
	if(bytes > out_size)
		return CHIAKI_ERR_INVALID_DATA;
	memset(out, 0, out_size);
	for(size_t i = 0; i < bytes; i++)
	{
		int hi = headless_hex_nibble(hex[i * 2]);
		int lo = headless_hex_nibble(hex[i * 2 + 1]);
		if(hi < 0 || lo < 0)
			return CHIAKI_ERR_INVALID_DATA;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	if(decoded_size)
		*decoded_size = bytes;
	return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode headless_build_cloud_launch_from_strings(
	ChiakiHeadlessCloudLaunchInfo *out_launch,
	uint8_t *out_morning,
	size_t out_morning_size,
	uint8_t *out_regist_key,
	size_t out_regist_key_size,
	const char *host,
	uint16_t stream_port,
	const char *session_id,
	const char *launch_spec,
	const char *morning_b64,
	const char *regist_key_hex,
	bool ps5,
	bool enable_dualsense,
	bool enable_keyboard,
	ChiakiVideoResolutionPreset resolution,
	ChiakiVideoFPSPreset fps,
	unsigned int bitrate,
	ChiakiCodec codec)
{
	if(!out_launch || !out_morning || !out_regist_key || !morning_b64 || !regist_key_hex)
		return CHIAKI_ERR_INVALID_DATA;

	size_t morning_size = out_morning_size;
	ChiakiErrorCode err = chiaki_base64_decode(morning_b64, strlen(morning_b64), out_morning, &morning_size);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	if(morning_size != CHIAKI_HANDSHAKE_KEY_SIZE)
		return CHIAKI_ERR_INVALID_DATA;

	size_t regist_key_size = 0;
	err = headless_decode_hex(regist_key_hex, out_regist_key, out_regist_key_size, &regist_key_size);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	*out_launch = (ChiakiHeadlessCloudLaunchInfo){
		.host = host,
		.stream_port = stream_port,
		.session_id = session_id,
		.launch_spec = launch_spec,
		.morning = out_morning,
		.morning_size = morning_size,
		.regist_key = out_regist_key,
		.regist_key_size = regist_key_size,
		.ps5 = ps5,
		.enable_dualsense = enable_dualsense,
		.enable_keyboard = enable_keyboard,
		.resolution = resolution,
		.fps = fps,
		.bitrate = bitrate,
		.codec = codec,
	};
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_connect_info_init_cloud_direct(
	ChiakiConnectInfo *out_connect_info,
	const ChiakiHeadlessCloudLaunchInfo *launch_info)
{
	if(!out_connect_info || !launch_info || !launch_info->host || !launch_info->session_id || !launch_info->launch_spec || !launch_info->morning)
		return CHIAKI_ERR_INVALID_DATA;
	if(launch_info->morning_size != CHIAKI_HANDSHAKE_KEY_SIZE)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiConnectInfo connect = {0};
	connect.ps5 = launch_info->ps5;
	connect.host = launch_info->host;
	memcpy(connect.morning, launch_info->morning, CHIAKI_HANDSHAKE_KEY_SIZE);
	headless_regist_key_zero_fill((uint8_t *)connect.regist_key, launch_info->regist_key, launch_info->regist_key_size);

	ChiakiVideoResolutionPreset resolution = launch_info->resolution == 0 ? CHIAKI_VIDEO_RESOLUTION_PRESET_720p : launch_info->resolution;
	ChiakiVideoFPSPreset fps = launch_info->fps == 0 ? CHIAKI_VIDEO_FPS_PRESET_60 : launch_info->fps;
	chiaki_connect_video_profile_preset(&connect.video_profile, resolution, fps);
	if(launch_info->bitrate > 0)
		connect.video_profile.bitrate = launch_info->bitrate;
	connect.video_profile.codec = launch_info->codec;

	/* Allow runtime to adapt profile under sustained loss conditions. */
	connect.video_profile_auto_downgrade = true;
	connect.enable_keyboard = launch_info->enable_keyboard;
	connect.enable_dualsense = launch_info->enable_dualsense;
	connect.audio_video_disabled = CHIAKI_NONE_DISABLED;
	connect.auto_regist = false;
	/* Match GUI parity default (Settings::GetPacketLossReportedMax default).
	 * Too-large values can over-report loss into congestion control and
	 * drive unnecessary long-lived quality downgrades. */
	connect.packet_loss_max = 0.05;
	connect.enable_idr_on_fec_failure = true;

	connect.cloud_direct = true;
	connect.stream_port = launch_info->stream_port;
	connect.cloud_session_id = launch_info->session_id;
	connect.cloud_launch_spec_b64 = launch_info->launch_spec;

	*out_connect_info = connect;
	return CHIAKI_ERR_SUCCESS;
}

struct chiaki_headless_session_t
{
	ChiakiSession session;
	ChiakiLog *log;
	ChiakiHeadlessCallbacks callbacks;
	bool display_only_host_video_sink;
	ChiakiHeadlessRuntimeAudioSinkConfig runtime_audio_sink_config;
	bool runtime_audio_sink_enabled;
	bool runtime_audio_sink_suppress_legacy_callback;
	bool runtime_audio_sink_started;
	uint64_t runtime_audio_sink_submitted_frame_count;
	uint64_t runtime_audio_sink_submitted_byte_count;
	uint64_t runtime_audio_sink_dropped_frame_count;
	uint64_t runtime_audio_sink_underrun_count;
	uint64_t runtime_audio_sink_start_count;
	uint64_t runtime_audio_sink_stop_count;
	uint64_t runtime_audio_sink_last_submit_monotonic_us;
	uint64_t runtime_audio_sink_legacy_callback_frame_count;
	uint64_t runtime_audio_sink_suppressed_legacy_callback_frame_count;

#if CHIAKI_LIB_ENABLE_FFMPEG_DECODER
	ChiakiFfmpegDecoder video_decoder;
	bool video_decoder_init;
#endif

#if CHIAKI_LIB_ENABLE_OPUS
	ChiakiOpusDecoder audio_decoder;
	bool audio_decoder_init;
	uint32_t audio_channels;
	uint32_t audio_rate;
#endif

	ChiakiThread stats_thread;
	bool stats_thread_started;
	volatile bool stats_stop;

	ChiakiMutex cb_mutex;
	bool stopping;
	bool stopped;
	uint64_t video_frame_count;
	uint64_t audio_frame_count;
	bool has_video_frame_metadata;
	ChiakiHeadlessVideoFormat last_video_format;
	uint32_t last_video_width;
	uint32_t last_video_height;
	double last_video_pts_seconds;
	double last_video_duration_seconds;
	int32_t last_video_frames_lost;
	bool last_video_frame_recovered;
	uint64_t last_video_monotonic_us;
	uint64_t packets_received;
	uint64_t packets_lost;
	double measured_bitrate_kbps;
	uint64_t video_decode_lost_frames;
	uint64_t video_decode_recovered_frames;
	uint64_t video_decode_gap_event_count;
	uint8_t *last_video_planes[4];
	size_t last_video_plane_sizes[4];
	int32_t last_video_strides[4];
	uint8_t last_video_plane_count;
	uint64_t event_count;
	uint64_t ready_event_count;
	uint64_t stopped_event_count;
	uint64_t error_event_count;
	uint64_t last_event_monotonic_us;
	ChiakiHeadlessEventType last_event_type;
	bool runtime_controller_connection_refreshed_on_input;
};

struct chiaki_media_session_t
{
	ChiakiHeadlessCallbacks callbacks;
	bool callbacks_set;
	ChiakiHeadlessCallbacks internal_callbacks;
	ChiakiHeadlessSession *headless_session;
	bool started;
	ChiakiMutex stats_mutex;
	ChiakiMediaSessionStats stats;
	uint64_t last_video_monotonic_us;
	uint64_t last_audio_monotonic_us;
	bool have_video_monotonic;
	bool have_audio_monotonic;
	uint64_t stream_packets_received;
	uint64_t stream_packets_lost;
	double stream_measured_bitrate_kbps;
};

static bool chiaki_media_e2_enabled(void)
{
	const char *v = getenv("CHIAKI_MEDIA_E2_ENABLE");
	if(!v || !*v)
		return false;
	return strcmp(v, "1") == 0
		|| strcmp(v, "true") == 0
		|| strcmp(v, "TRUE") == 0
		|| strcmp(v, "yes") == 0
		|| strcmp(v, "YES") == 0;
}

static bool chiaki_env_gate_enabled(const char *name)
{
	if(!name || !*name)
		return false;
	const char *v = getenv(name);
	if(!v || !*v)
		return false;
	return strcmp(v, "1") == 0
		|| strcmp(v, "true") == 0
		|| strcmp(v, "TRUE") == 0
		|| strcmp(v, "yes") == 0
		|| strcmp(v, "YES") == 0;
}

static bool chiaki_media_e3_enabled(void)
{
	return chiaki_env_gate_enabled("DECKSTATION_MEDIA_E3_ENABLE");
}

static bool chiaki_media_e3_audio_enabled(void)
{
	return chiaki_env_gate_enabled("DECKSTATION_MEDIA_E3_AUDIO_ENABLE");
}

static bool chiaki_media_e3_sync_enabled(void)
{
	return chiaki_env_gate_enabled("DECKSTATION_MEDIA_E3_SYNC_ENABLE");
}

static void chiaki_media_stats_on_video_frame(const ChiakiHeadlessVideoFrame *frame, void *user)
{
	ChiakiMediaSession *session = user;
	if(!session)
		return;
	chiaki_mutex_lock(&session->stats_mutex);
	session->stats.video_frame_count++;
	if(frame)
	{
		if(frame->frames_lost > 0)
		{
			session->stats.video_decode_gap_event_count++;
			session->stats.video_decode_lost_frames += (uint64_t)frame->frames_lost;
		}
		if(frame->frame_recovered)
			session->stats.video_decode_recovered_frames++;
	}
	if(session->stats.e3_sync_effective && frame && frame->monotonic_time_us > 0)
	{
		session->last_video_monotonic_us = frame->monotonic_time_us;
		session->have_video_monotonic = true;
		if(session->have_audio_monotonic)
		{
			session->stats.e3_sync_observation_count++;
			session->stats.e3_last_av_delta_us = (int64_t)session->last_video_monotonic_us
				- (int64_t)session->last_audio_monotonic_us;
		}
	}
	chiaki_mutex_unlock(&session->stats_mutex);
	if(session->callbacks_set && session->callbacks.video_frame_cb)
		session->callbacks.video_frame_cb(frame, session->callbacks.user);
}

static void chiaki_media_stats_on_audio_frame(const ChiakiHeadlessAudioFrame *frame, void *user)
{
	ChiakiMediaSession *session = user;
	if(!session)
		return;
	chiaki_mutex_lock(&session->stats_mutex);
	session->stats.audio_frame_count++;
	if(session->stats.e3_audio_effective)
	{
		session->stats.e3_audio_hook_frame_count++;
		if(frame)
			session->stats.e3_audio_hook_sample_count += frame->frame_count;
	}
	if(session->stats.e3_sync_effective && frame && frame->monotonic_time_us > 0)
	{
		session->last_audio_monotonic_us = frame->monotonic_time_us;
		session->have_audio_monotonic = true;
		if(session->have_video_monotonic)
		{
			session->stats.e3_sync_observation_count++;
			session->stats.e3_last_av_delta_us = (int64_t)session->last_video_monotonic_us
				- (int64_t)session->last_audio_monotonic_us;
		}
	}
	chiaki_mutex_unlock(&session->stats_mutex);
	if(session->callbacks_set && session->callbacks.audio_frame_cb)
		session->callbacks.audio_frame_cb(frame, session->callbacks.user);
}

static void chiaki_media_stats_on_event(const ChiakiHeadlessEvent *event, void *user)
{
	ChiakiMediaSession *session = user;
	if(!session)
		return;
	chiaki_mutex_lock(&session->stats_mutex);
	session->stats.event_count++;
	session->stats.last_event_monotonic_us = chiaki_time_now_monotonic_us();
	if(event)
	{
		session->stats.last_event_type = event->type;
		if(event->type == CHIAKI_HEADLESS_EVENT_READY)
		{
			session->stats.ready_event_count++;
			session->stats.state = CHIAKI_MEDIA_SESSION_STATE_RUNNING;
		}
		else if(event->type == CHIAKI_HEADLESS_EVENT_ERROR)
		{
			session->stats.error_event_count++;
			session->stats.state = CHIAKI_MEDIA_SESSION_STATE_ERROR;
		}
		else if(event->type == CHIAKI_HEADLESS_EVENT_STOPPED)
		{
			session->stats.stopped_event_count++;
			session->stats.state = CHIAKI_MEDIA_SESSION_STATE_STOPPED;
		}
		else if(event->type == CHIAKI_HEADLESS_EVENT_STREAM_STATS)
		{
			session->stream_packets_received = event->stats.packets_received;
			session->stream_packets_lost = event->stats.packets_lost;
			session->stream_measured_bitrate_kbps = event->stats.measured_bitrate_kbps;
			session->stats.packets_received = session->stream_packets_received;
			session->stats.packets_lost = session->stream_packets_lost;
			session->stats.measured_bitrate_kbps = session->stream_measured_bitrate_kbps;
		}
		else if(event->type == CHIAKI_HEADLESS_EVENT_WARNING && event->warning.message)
		{
			const char *m = event->warning.message;
			if(strncmp(m, "[input.", 7) == 0)
			{
				session->stats.input_event_last_monotonic_us = session->stats.last_event_monotonic_us;
				if(strncmp(m, "[input.haptics] rumble ", 23) == 0)
				{
					session->stats.input_rumble_event_count++;
					session->stats.input_haptics_event_count++;
				}
				else if(strncmp(m, "[input.haptics] trigger_effects ", 32) == 0)
				{
					session->stats.input_trigger_effect_event_count++;
					session->stats.input_haptics_event_count++;
				}
				else if(strncmp(m, "[input.motion] reset_requested", 29) == 0)
				{
					session->stats.input_motion_reset_event_count++;
				}
				else if(strncmp(m, "[input.haptics] haptic_intensity=", 33) == 0)
				{
					session->stats.input_haptic_intensity_event_count++;
					session->stats.input_haptics_event_count++;
				}
				else if(strncmp(m, "[input.haptics] trigger_intensity=", 34) == 0)
				{
					session->stats.input_trigger_intensity_event_count++;
					session->stats.input_haptics_event_count++;
				}
				else if(strncmp(m, "[input.controller] player_index=", 32) == 0)
					session->stats.input_player_index_event_count++;
			}
		}
	}
	chiaki_mutex_unlock(&session->stats_mutex);
	if(session->callbacks_set && session->callbacks.event_cb)
		session->callbacks.event_cb(event, session->callbacks.user);
}

static void headless_emit_event(ChiakiHeadlessSession *s, const ChiakiHeadlessEvent *event)
{
	ChiakiHeadlessEventCallback event_cb = NULL;
	void *event_user = NULL;
	chiaki_mutex_lock(&s->cb_mutex);
	if(event)
	{
		s->event_count++;
		s->last_event_monotonic_us = chiaki_time_now_monotonic_us();
		s->last_event_type = event->type;
		if(event->type == CHIAKI_HEADLESS_EVENT_READY)
			s->ready_event_count++;
		else if(event->type == CHIAKI_HEADLESS_EVENT_ERROR)
			s->error_event_count++;
		else if(event->type == CHIAKI_HEADLESS_EVENT_STOPPED)
			s->stopped_event_count++;
	}
	bool drop = s->stopping || s->stopped;
	if(!drop)
	{
		event_cb = s->callbacks.event_cb;
		event_user = s->callbacks.user;
	}
	chiaki_mutex_unlock(&s->cb_mutex);
	if(event_cb)
		event_cb(event, event_user);
}

static void *headless_stats_thread(void *arg)
{
	ChiakiHeadlessSession *s = arg;
	while(!s->stats_stop)
	{
#ifdef _WIN32
		Sleep(1000);
#else
		usleep(1000 * 1000);
#endif
		if(s->stats_stop)
			break;
		uint64_t received = 0;
		uint64_t lost = 0;
		chiaki_packet_stats_get(&s->session.stream_connection.packet_stats, false, &received, &lost);
		chiaki_mutex_lock(&s->cb_mutex);
		s->packets_received = received;
		s->packets_lost = lost;
		s->measured_bitrate_kbps = s->session.stream_connection.measured_bitrate;
		chiaki_mutex_unlock(&s->cb_mutex);

		ChiakiHeadlessEvent ev = {0};
		ev.type = CHIAKI_HEADLESS_EVENT_STREAM_STATS;
		ev.stats.packets_received = received;
		ev.stats.packets_lost = lost;
		ev.stats.measured_bitrate_kbps = s->session.stream_connection.measured_bitrate;
		ev.stats.monotonic_time_us = chiaki_time_now_monotonic_us();
		headless_emit_event(s, &ev);
	}
	return NULL;
}

#if CHIAKI_LIB_ENABLE_FFMPEG_DECODER
static ChiakiHeadlessVideoFormat headless_map_pixfmt(enum AVPixelFormat fmt)
{
	switch(fmt)
	{
		case AV_PIX_FMT_YUV420P:
			return CHIAKI_HEADLESS_VIDEO_FORMAT_YUV420P;
		case AV_PIX_FMT_NV12:
			return CHIAKI_HEADLESS_VIDEO_FORMAT_NV12;
		case AV_PIX_FMT_P010LE:
			return CHIAKI_HEADLESS_VIDEO_FORMAT_P010LE;
		case AV_PIX_FMT_RGBA:
			return CHIAKI_HEADLESS_VIDEO_FORMAT_RGBA;
		case AV_PIX_FMT_DRM_PRIME:
			return CHIAKI_HEADLESS_VIDEO_FORMAT_DRM_PRIME;
		default:
			return CHIAKI_HEADLESS_VIDEO_FORMAT_UNKNOWN;
	}
}

static uint8_t headless_video_plane_count(ChiakiHeadlessVideoFormat format)
{
	switch(format)
	{
		case CHIAKI_HEADLESS_VIDEO_FORMAT_YUV420P:
			return 3;
		case CHIAKI_HEADLESS_VIDEO_FORMAT_NV12:
		case CHIAKI_HEADLESS_VIDEO_FORMAT_P010LE:
			return 2;
		case CHIAKI_HEADLESS_VIDEO_FORMAT_RGBA:
			return 1;
		case CHIAKI_HEADLESS_VIDEO_FORMAT_UNKNOWN:
		default:
			return 0;
	}
}

/*
 * Copying full decoded video planes every frame is expensive and is not
 * required for real-time playback. Keep it opt-in for diagnostics.
 */
static bool headless_copy_last_frame_enabled(void)
{
	static int cached = -1;
	if(cached >= 0)
		return cached != 0;
	const char *v = getenv("CHIAKI_HEADLESS_COPY_LAST_FRAME");
	cached = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
	return cached != 0;
}

static uint32_t headless_video_plane_height(
	ChiakiHeadlessVideoFormat format,
	uint32_t frame_height,
	uint8_t plane_index)
{
	switch(format)
	{
		case CHIAKI_HEADLESS_VIDEO_FORMAT_YUV420P:
			if(plane_index == 0)
				return frame_height;
			return (frame_height + 1u) / 2u;
		case CHIAKI_HEADLESS_VIDEO_FORMAT_NV12:
		case CHIAKI_HEADLESS_VIDEO_FORMAT_P010LE:
			return plane_index == 0 ? frame_height : ((frame_height + 1u) / 2u);
		case CHIAKI_HEADLESS_VIDEO_FORMAT_RGBA:
			return plane_index == 0 ? frame_height : 0;
		case CHIAKI_HEADLESS_VIDEO_FORMAT_UNKNOWN:
		default:
			return 0;
	}
}

static void headless_on_ffmpeg_frame(ChiakiFfmpegDecoder *decoder, void *user)
{
	ChiakiHeadlessSession *s = user;
	while(true)
	{
		int32_t frames_lost = 0;
		ChiakiFfmpegFrame frame = chiaki_ffmpeg_decoder_pull_frame(decoder, &frames_lost);
		if(!frame.frame)
			break;

		/* Always drain/free decoded frames even when host did not register a
		 * video callback, otherwise the decoder queue backs up and starts
		 * dropping frames with "internal buffer is full". */
		ChiakiHeadlessVideoFrame out = {0};
		out.format = headless_map_pixfmt(frame.frame->format);
		out.width = (uint32_t)frame.frame->width;
		out.height = (uint32_t)frame.frame->height;
		out.planes[0] = frame.frame->data[0];
		out.planes[1] = frame.frame->data[1];
		out.planes[2] = frame.frame->data[2];
		out.planes[3] = frame.frame->data[3];
		out.strides[0] = frame.frame->linesize[0];
		out.strides[1] = frame.frame->linesize[1];
		out.strides[2] = frame.frame->linesize[2];
		out.strides[3] = frame.frame->linesize[3];
		out.plane_count = headless_video_plane_count(out.format);
		out.pts_seconds = frame.pts;
		out.duration_seconds = frame.duration;
		out.frames_lost = frames_lost;
		out.frame_recovered = frame.recovered;
		out.monotonic_time_us = chiaki_time_now_monotonic_us();

		chiaki_mutex_lock(&s->cb_mutex);
		bool drop_callback = s->stopping || s->stopped;
		bool display_only_host_video_sink = s->display_only_host_video_sink;
		ChiakiHeadlessVideoFrameCallback video_frame_cb = drop_callback ? NULL : s->callbacks.video_frame_cb;
		ChiakiHeadlessExternalVideoFrameCallback external_video_frame_cb = drop_callback ? NULL : s->callbacks.external_video_frame_cb;
		void *video_frame_user = s->callbacks.user;
		s->video_frame_count++;
		s->has_video_frame_metadata = true;
		s->last_video_format = out.format;
		s->last_video_width = out.width;
		s->last_video_height = out.height;
		s->last_video_pts_seconds = out.pts_seconds;
		s->last_video_duration_seconds = out.duration_seconds;
		s->last_video_frames_lost = out.frames_lost;
		s->last_video_frame_recovered = out.frame_recovered;
		s->last_video_monotonic_us = out.monotonic_time_us;
		s->last_video_plane_count = out.plane_count;
		bool should_copy_last_frame =
			headless_copy_last_frame_enabled()
			/* Poll-based embedded hosts need a copied last-frame snapshot when
			 * no callback sink is registered; otherwise runtime poll reports
			 * metadata activity but cannot surface usable planes. */
			|| !video_frame_cb;
		for(uint8_t i = 0; i < 4; i++)
		{
			s->last_video_strides[i] = out.strides[i];
			if(!should_copy_last_frame)
			{
				s->last_video_plane_sizes[i] = 0;
				continue;
			}
			size_t required = 0;
			uint32_t plane_height = headless_video_plane_height(out.format, out.height, i);
			if(i < out.plane_count && out.planes[i] && out.strides[i] > 0 && plane_height > 0)
				required = (size_t)out.strides[i] * (size_t)plane_height;
			if(required == 0)
			{
				s->last_video_plane_sizes[i] = 0;
				continue;
			}
			if(s->last_video_plane_sizes[i] != required)
			{
				uint8_t *new_plane = realloc(s->last_video_planes[i], required);
				if(!new_plane)
				{
					s->last_video_plane_sizes[i] = 0;
					continue;
				}
				s->last_video_planes[i] = new_plane;
				s->last_video_plane_sizes[i] = required;
			}
			memcpy(s->last_video_planes[i], out.planes[i], required);
		}
		if(frames_lost > 0)
		{
			s->video_decode_gap_event_count++;
			s->video_decode_lost_frames += (uint64_t)frames_lost;
		}
		if(frame.recovered)
			s->video_decode_recovered_frames++;
		chiaki_mutex_unlock(&s->cb_mutex);
#if !defined(_WIN32)
		if(g_headless_ext_video_gate_logs < 8 || g_headless_ext_video_gate_logs % 600 == 0)
		{
			CHIAKI_LOGI(s->log,
				"[headless.ext] gate cb=%d displayOnly=%d format=%d w=%u h=%u",
				external_video_frame_cb ? 1 : 0,
				display_only_host_video_sink ? 1 : 0,
				(int)out.format,
				out.width,
				out.height);
		}
		g_headless_ext_video_gate_logs++;
		if(external_video_frame_cb && !display_only_host_video_sink)
		{
			AVFrame *drm_mapped = NULL;
			AVFrame *drm_frame = frame.frame;
			const AVDRMFrameDescriptor *desc = NULL;
			/* VAAPI decode outputs AV_PIX_FMT_VAAPI frames; map them to
			 * AV_PIX_FMT_DRM_PRIME so we can forward DMABUF planes to the
			 * embedded external callback path. */
			if(drm_frame->format == AV_PIX_FMT_VAAPI)
			{
				drm_mapped = av_frame_alloc();
				if(drm_mapped)
				{
					drm_mapped->format = AV_PIX_FMT_DRM_PRIME;
					int map_rc = av_hwframe_map(
						drm_mapped,
						drm_frame,
						AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT);
					if(map_rc == 0)
					{
						drm_frame = drm_mapped;
					}
					else
					{
						CHIAKI_LOGW(
							s->log,
							"[headless.ext] vaapi->drm map failed rc=%d",
							map_rc);
					}
				}
			}
			if(drm_frame->format == AV_PIX_FMT_DRM_PRIME)
				desc = (const AVDRMFrameDescriptor *)drm_frame->data[0];
			if(!desc)
			{
				if(drm_mapped)
					av_frame_free(&drm_mapped);
				goto skip_external_emit;
			}
			ChiakiHeadlessExternalVideoFrame ext = {0};
			ext.type = CHIAKI_HEADLESS_EXTERNAL_VIDEO_FRAME_TYPE_DMABUF_DRM_PRIME;
			ext.width = out.width;
			ext.height = out.height;
			ext.pts_seconds = out.pts_seconds;
			ext.duration_seconds = out.duration_seconds;
			ext.frames_lost = out.frames_lost;
			ext.frame_recovered = out.frame_recovered;
			ext.monotonic_time_us = out.monotonic_time_us;
			for(uint8_t i = 0; i < 4; i++)
				ext.planes[i].fd = -1;
			if(desc && desc->nb_layers > 0 && desc->nb_layers <= 4)
			{
				const AVDRMLayerDescriptor *layer = &desc->layers[0];
				/* Some VAAPI paths expose NV12 as two 1-plane layers (R8 + GR88)
				 * rather than one NV12 layer. Normalize to NV12 for Flutter
				 * media_plane import expectations. */
				bool normalized_nv12_from_split_layers = false;
				if(desc->nb_layers >= 2 &&
					desc->layers[0].nb_planes >= 1 &&
					desc->layers[1].nb_planes >= 1 &&
					desc->layers[0].format == kDrmFormatR8 &&
					desc->layers[1].format == kDrmFormatGr88)
				{
					ext.drm_format = kDrmFormatNv12;
					ext.plane_count = 2;
					for(uint8_t i = 0; i < 2; i++)
					{
						const AVDRMLayerDescriptor *src_layer = &desc->layers[i];
						const AVDRMPlaneDescriptor *plane = &src_layer->planes[0];
						if(plane->object_index >= desc->nb_objects)
							continue;
						const AVDRMObjectDescriptor *obj = &desc->objects[plane->object_index];
						ext.planes[i].fd = dup(obj->fd);
						ext.planes[i].offset = (uint32_t)plane->offset;
						ext.planes[i].pitch = plane->pitch;
						ext.planes[i].modifier = obj->format_modifier;
					}
					normalized_nv12_from_split_layers = true;
				}
				else
				{
					ext.drm_format = layer->format;
					ext.plane_count = (uint8_t)layer->nb_planes;
					if(ext.plane_count > 4)
						ext.plane_count = 4;
					for(uint8_t i = 0; i < ext.plane_count; i++)
					{
						const AVDRMPlaneDescriptor *plane = &layer->planes[i];
						if(plane->object_index >= desc->nb_objects)
							continue;
						const AVDRMObjectDescriptor *obj = &desc->objects[plane->object_index];
						ext.planes[i].fd = dup(obj->fd);
						ext.planes[i].offset = (uint32_t)plane->offset;
						ext.planes[i].pitch = plane->pitch;
						ext.planes[i].modifier = obj->format_modifier;
					}
				}
				if(g_headless_ext_video_emit_logs < 8 || g_headless_ext_video_emit_logs % 600 == 0)
				{
					CHIAKI_LOGI(s->log,
						"[headless.ext] emit drm=%u planes=%u fd0=%d pitch0=%u "
						"off0=%u mod0=%llu layers=%u normalized_nv12=%d",
						ext.drm_format,
						ext.plane_count,
						ext.planes[0].fd,
						ext.planes[0].pitch,
						ext.planes[0].offset,
						(unsigned long long)ext.planes[0].modifier,
						desc->nb_layers,
						normalized_nv12_from_split_layers ? 1 : 0);
				}
				g_headless_ext_video_emit_logs++;
				external_video_frame_cb(&ext, video_frame_user);
				for(uint8_t i = 0; i < ext.plane_count; i++)
				{
					if(ext.planes[i].fd >= 0)
						close(ext.planes[i].fd);
				}
			}
			if(drm_mapped)
				av_frame_free(&drm_mapped);
		}
skip_external_emit:
#endif
		if(video_frame_cb && !display_only_host_video_sink)
			video_frame_cb(&out, video_frame_user);

		av_frame_free(&frame.frame);
	}
}
#endif

#if CHIAKI_LIB_ENABLE_OPUS
static void headless_runtime_audio_sink_stop_locked(ChiakiHeadlessSession *s)
{
	if(!s || !s->runtime_audio_sink_enabled || !s->runtime_audio_sink_started)
		return;
	if(s->runtime_audio_sink_config.stop_cb)
		s->runtime_audio_sink_config.stop_cb(s->runtime_audio_sink_config.user);
	s->runtime_audio_sink_started = false;
	s->runtime_audio_sink_stop_count++;
}

static bool headless_runtime_audio_sink_start_if_needed_locked(ChiakiHeadlessSession *s)
{
	if(!s || !s->runtime_audio_sink_enabled)
		return false;
	if(s->runtime_audio_sink_started)
		return true;
	if(!s->runtime_audio_sink_config.submit_cb)
		return false;

	bool started = true;
	if(s->runtime_audio_sink_config.start_cb)
	{
		started = s->runtime_audio_sink_config.start_cb(
			s->audio_rate,
			s->audio_channels,
			CHIAKI_HEADLESS_AUDIO_FORMAT_S16,
			s->runtime_audio_sink_config.user);
	}
	if(!started)
		return false;
	s->runtime_audio_sink_started = true;
	s->runtime_audio_sink_start_count++;
	return true;
}

static void headless_on_audio_settings(uint32_t channels, uint32_t rate, void *user)
{
	ChiakiHeadlessSession *s = user;
	chiaki_mutex_lock(&s->cb_mutex);
	s->audio_channels = channels;
	s->audio_rate = rate;
	chiaki_mutex_unlock(&s->cb_mutex);
}

static void headless_on_audio_frame(int16_t *buf, size_t samples_count, void *user)
{
	ChiakiHeadlessSession *s = user;

	ChiakiHeadlessAudioFrame out = {0};
	out.format = CHIAKI_HEADLESS_AUDIO_FORMAT_S16;
	out.channels = s->audio_channels;
	out.sample_rate = s->audio_rate;
	out.frame_count = (uint32_t)samples_count;
	out.data = buf;
	size_t channels = s->audio_channels ? s->audio_channels : 1;
	out.data_size = samples_count * channels * sizeof(int16_t);
	out.monotonic_time_us = chiaki_time_now_monotonic_us();

	chiaki_mutex_lock(&s->cb_mutex);
	bool drop = s->stopping || s->stopped;
	ChiakiHeadlessAudioFrameCallback audio_frame_cb = drop ? NULL : s->callbacks.audio_frame_cb;
	void *audio_frame_user = s->callbacks.user;
	bool has_cb = audio_frame_cb != NULL;
	bool sink_enabled = s->runtime_audio_sink_enabled;
	bool suppress_legacy_cb = s->runtime_audio_sink_suppress_legacy_callback;
	bool sink_started = false;
	if(!drop && sink_enabled)
	{
		sink_started = headless_runtime_audio_sink_start_if_needed_locked(s);
		if(sink_started && s->runtime_audio_sink_config.submit_cb)
		{
			bool sink_submit_ok = s->runtime_audio_sink_config.submit_cb(
				out.data,
				out.data_size,
				out.frame_count,
				out.monotonic_time_us,
				s->runtime_audio_sink_config.user);
			if(sink_submit_ok)
			{
				s->runtime_audio_sink_submitted_frame_count += out.frame_count;
				s->runtime_audio_sink_submitted_byte_count += out.data_size;
				s->runtime_audio_sink_last_submit_monotonic_us = out.monotonic_time_us;
			}
			else
			{
				s->runtime_audio_sink_dropped_frame_count += out.frame_count;
			}
		}
		else
		{
			s->runtime_audio_sink_dropped_frame_count += out.frame_count;
		}
	}
	s->audio_frame_count++;
	if(!drop && has_cb)
	{
		if(sink_enabled && suppress_legacy_cb)
			s->runtime_audio_sink_suppressed_legacy_callback_frame_count += out.frame_count;
		else
			s->runtime_audio_sink_legacy_callback_frame_count += out.frame_count;
	}
	chiaki_mutex_unlock(&s->cb_mutex);
	if(!drop && has_cb && !(sink_enabled && suppress_legacy_cb))
		audio_frame_cb(&out, audio_frame_user);
}
#endif

static void headless_on_session_event(ChiakiEvent *event, void *user)
{
	ChiakiHeadlessSession *s = user;
	ChiakiHeadlessEvent out = {0};
	char message[256] = {0};

	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			out.type = CHIAKI_HEADLESS_EVENT_READY;
			headless_emit_event(s, &out);
			return;
		case CHIAKI_EVENT_QUIT:
			out.type = chiaki_quit_reason_is_error(event->quit.reason) ? CHIAKI_HEADLESS_EVENT_ERROR : CHIAKI_HEADLESS_EVENT_STOPPED;
			out.quit.reason = event->quit.reason;
			out.quit.reason_str = event->quit.reason_str;
			headless_emit_event(s, &out);
			return;
		case CHIAKI_EVENT_VIDEO_FEC_FAILURE:
			out.type = CHIAKI_HEADLESS_EVENT_WARNING;
			out.warning.message = "Video FEC failure";
			headless_emit_event(s, &out);
			return;
		case CHIAKI_EVENT_RUMBLE:
			out.type = CHIAKI_HEADLESS_EVENT_WARNING;
			snprintf(
				message,
				sizeof(message),
				"[input.haptics] rumble unknown=%u left=%u right=%u",
				(unsigned)event->rumble.unknown,
				(unsigned)event->rumble.left,
				(unsigned)event->rumble.right);
			out.warning.message = message;
			headless_emit_event(s, &out);
			return;
		case CHIAKI_EVENT_TRIGGER_EFFECTS:
			out.type = CHIAKI_HEADLESS_EVENT_WARNING;
			snprintf(
				message,
				sizeof(message),
				"[input.haptics] trigger_effects type_left=%u type_right=%u",
				(unsigned)event->trigger_effects.type_left,
				(unsigned)event->trigger_effects.type_right);
			out.warning.message = message;
			headless_emit_event(s, &out);
			return;
		case CHIAKI_EVENT_MOTION_RESET:
			out.type = CHIAKI_HEADLESS_EVENT_WARNING;
			out.warning.message = "[input.motion] reset_requested";
			headless_emit_event(s, &out);
			return;
		case CHIAKI_EVENT_HAPTIC_INTENSITY:
			out.type = CHIAKI_HEADLESS_EVENT_WARNING;
			snprintf(
				message,
				sizeof(message),
				"[input.haptics] haptic_intensity=%u",
				(unsigned)event->intensity);
			out.warning.message = message;
			headless_emit_event(s, &out);
			return;
		case CHIAKI_EVENT_TRIGGER_INTENSITY:
			out.type = CHIAKI_HEADLESS_EVENT_WARNING;
			snprintf(
				message,
				sizeof(message),
				"[input.haptics] trigger_intensity=%u",
				(unsigned)event->intensity);
			out.warning.message = message;
			headless_emit_event(s, &out);
			return;
		case CHIAKI_EVENT_PLAYER_INDEX:
			out.type = CHIAKI_HEADLESS_EVENT_WARNING;
			snprintf(
				message,
				sizeof(message),
				"[input.controller] player_index=%u",
				(unsigned)event->player_index);
			out.warning.message = message;
			headless_emit_event(s, &out);
			return;
		default:
			return;
	}
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_create(ChiakiHeadlessSession **out_session, const ChiakiHeadlessCreateInfo *create_info)
{
	if(!out_session || !create_info || !create_info->log)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessSession *s = calloc(1, sizeof(*s));
	if(!s)
		return CHIAKI_ERR_MEMORY;

	s->log = create_info->log;
	if(create_info->callbacks)
		memcpy(&s->callbacks, create_info->callbacks, sizeof(s->callbacks));
	if(create_info->runtime_audio_sink_config)
	{
		s->runtime_audio_sink_config = *create_info->runtime_audio_sink_config;
		s->runtime_audio_sink_enabled =
			s->runtime_audio_sink_config.enabled
			&& s->runtime_audio_sink_config.submit_cb != NULL;
		s->runtime_audio_sink_suppress_legacy_callback =
			s->runtime_audio_sink_enabled
			&& s->runtime_audio_sink_config.suppress_legacy_audio_callback;
	}
	s->display_only_host_video_sink = create_info->display_only_host_video_sink;

	ChiakiErrorCode err = chiaki_mutex_init(&s->cb_mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(s);
		return err;
	}

#if CHIAKI_LIB_ENABLE_FFMPEG_DECODER
	err = chiaki_ffmpeg_decoder_init(
		&s->video_decoder,
		s->log,
		create_info->connect_info.video_profile.codec,
		create_info->connect_info.video_profile.max_fps,
		create_info->ffmpeg_hw_decoder_name,
		NULL,
		headless_on_ffmpeg_frame,
		s);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		goto error_mutex;
	}
	s->video_decoder_init = true;
#endif

#if CHIAKI_LIB_ENABLE_OPUS
	chiaki_opus_decoder_init(&s->audio_decoder, s->log);
	chiaki_opus_decoder_set_cb(&s->audio_decoder, headless_on_audio_settings, headless_on_audio_frame, s);
	s->audio_decoder_init = true;
#endif

	err = chiaki_session_init(&s->session, (ChiakiConnectInfo *)&create_info->connect_info, s->log);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		goto error_video_audio;
	}

	chiaki_session_set_event_cb(&s->session, headless_on_session_event, s);

#if CHIAKI_LIB_ENABLE_FFMPEG_DECODER
	chiaki_session_set_video_sample_cb(&s->session, chiaki_ffmpeg_decoder_video_sample_cb, &s->video_decoder);
#endif

#if CHIAKI_LIB_ENABLE_OPUS
	ChiakiAudioSink audio_sink;
	chiaki_opus_decoder_get_sink(&s->audio_decoder, &audio_sink);
	chiaki_session_set_audio_sink(&s->session, &audio_sink);
#endif

	*out_session = s;
	return CHIAKI_ERR_SUCCESS;

error_video_audio:
#if CHIAKI_LIB_ENABLE_OPUS
	if(s->audio_decoder_init)
		chiaki_opus_decoder_fini(&s->audio_decoder);
#endif
#if CHIAKI_LIB_ENABLE_FFMPEG_DECODER
	if(s->video_decoder_init)
		chiaki_ffmpeg_decoder_fini(&s->video_decoder);
#endif
error_mutex:
	chiaki_mutex_fini(&s->cb_mutex);
	free(s);
	return err;
}

CHIAKI_EXPORT void chiaki_headless_session_destroy(ChiakiHeadlessSession *s)
{
	if(!s)
		return;

	s->stats_stop = true;
	if(s->stats_thread_started)
		chiaki_thread_join(&s->stats_thread, NULL);

	chiaki_session_fini(&s->session);

#if CHIAKI_LIB_ENABLE_OPUS
	chiaki_mutex_lock(&s->cb_mutex);
	headless_runtime_audio_sink_stop_locked(s);
	chiaki_mutex_unlock(&s->cb_mutex);
#endif

#if CHIAKI_LIB_ENABLE_OPUS
	if(s->audio_decoder_init)
		chiaki_opus_decoder_fini(&s->audio_decoder);
#endif

#if CHIAKI_LIB_ENABLE_FFMPEG_DECODER
	if(s->video_decoder_init)
		chiaki_ffmpeg_decoder_fini(&s->video_decoder);
#endif

	for(uint8_t i = 0; i < 4; i++)
		free(s->last_video_planes[i]);

	chiaki_mutex_fini(&s->cb_mutex);
	free(s);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_start(ChiakiHeadlessSession *s)
{
	if(!s)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessEvent ev = {0};
	ev.type = CHIAKI_HEADLESS_EVENT_CONNECTING;
	headless_emit_event(s, &ev);

	chiaki_mutex_lock(&s->cb_mutex);
	s->stopped = false;
	s->stopping = false;
	chiaki_mutex_unlock(&s->cb_mutex);

	ChiakiErrorCode err = chiaki_session_start(&s->session);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	s->stats_stop = false;
	if(!s->stats_thread_started)
	{
		err = chiaki_thread_create(&s->stats_thread, headless_stats_thread, s);
		if(err == CHIAKI_ERR_SUCCESS)
			s->stats_thread_started = true;
	}
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_stop(ChiakiHeadlessSession *s)
{
	if(!s)
		return CHIAKI_ERR_INVALID_DATA;
	chiaki_mutex_lock(&s->cb_mutex);
	s->stopping = true;
#if CHIAKI_LIB_ENABLE_OPUS
	headless_runtime_audio_sink_stop_locked(s);
#endif
	chiaki_mutex_unlock(&s->cb_mutex);

	ChiakiHeadlessEvent ev = {0};
	ev.type = CHIAKI_HEADLESS_EVENT_STOPPING;
	headless_emit_event(s, &ev);
	return chiaki_session_stop(&s->session);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_join(ChiakiHeadlessSession *s)
{
	if(!s)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiErrorCode err = chiaki_session_join(&s->session);
	s->stats_stop = true;
	if(s->stats_thread_started)
	{
		chiaki_thread_join(&s->stats_thread, NULL);
		s->stats_thread_started = false;
	}

	ChiakiHeadlessEvent ev = {0};
	ev.type = CHIAKI_HEADLESS_EVENT_STOPPED;
	headless_emit_event(s, &ev);
	chiaki_mutex_lock(&s->cb_mutex);
	s->stopped = true;
	chiaki_mutex_unlock(&s->cb_mutex);
	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_send_controller_state(ChiakiHeadlessSession *s, ChiakiControllerState *state)
{
	if(!s || !state)
		return CHIAKI_ERR_INVALID_DATA;

	bool non_idle = state->buttons
		|| state->l2_state
		|| state->r2_state
		|| state->left_x
		|| state->left_y
		|| state->right_x
		|| state->right_y;
	if(s->session.connect_info.cloud_direct
		&& non_idle
		&& !s->runtime_controller_connection_refreshed_on_input)
	{
		s->runtime_controller_connection_refreshed_on_input = true;
		CHIAKI_LOGI(s->log, "Headless runtime refreshing cloud controller connection on first input");
		ChiakiErrorCode conn_err = chiaki_stream_connection_send_controller_connection(&s->session.stream_connection);
		if(conn_err != CHIAKI_ERR_SUCCESS)
			CHIAKI_LOGW(s->log, "Headless runtime controller connection refresh failed: %d", conn_err);
	}

	return chiaki_session_set_controller_state(&s->session, state);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_request_idr(ChiakiHeadlessSession *s)
{
	return s ? chiaki_session_request_idr(&s->session) : CHIAKI_ERR_INVALID_DATA;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_toggle_microphone(ChiakiHeadlessSession *s, bool muted)
{
	return s ? chiaki_session_toggle_microphone(&s->session, muted) : CHIAKI_ERR_INVALID_DATA;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_go_home(ChiakiHeadlessSession *s)
{
	return s ? chiaki_session_go_home(&s->session) : CHIAKI_ERR_INVALID_DATA;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_set_login_pin(ChiakiHeadlessSession *s, const uint8_t *pin, size_t pin_size)
{
	return s ? chiaki_session_set_login_pin(&s->session, pin, pin_size) : CHIAKI_ERR_INVALID_DATA;
}

CHIAKI_EXPORT uint32_t chiaki_headless_api_version(void)
{
	return 40;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_probe(void)
{
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_probe_cloud_launch(
	const char *host,
	uint16_t stream_port,
	const char *session_id,
	const char *launch_spec,
	const uint8_t *morning,
	size_t morning_size,
	const uint8_t *regist_key,
	size_t regist_key_size,
	bool ps5,
	bool enable_dualsense,
	bool enable_keyboard,
	ChiakiVideoResolutionPreset resolution,
	ChiakiVideoFPSPreset fps,
	unsigned int bitrate,
	ChiakiCodec codec)
{
	ChiakiHeadlessCloudLaunchInfo launch = {
		.host = host,
		.stream_port = stream_port,
		.session_id = session_id,
		.launch_spec = launch_spec,
		.morning = morning,
		.morning_size = morning_size,
		.regist_key = regist_key,
		.regist_key_size = regist_key_size,
		.ps5 = ps5,
		.enable_dualsense = enable_dualsense,
		.enable_keyboard = enable_keyboard,
		.resolution = resolution,
		.fps = fps,
		.bitrate = bitrate,
		.codec = codec,
	};
	ChiakiConnectInfo connect_info = {0};
	return chiaki_headless_connect_info_init_cloud_direct(&connect_info, &launch);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_probe_cloud_launch_strings(
	const char *host,
	uint16_t stream_port,
	const char *session_id,
	const char *launch_spec,
	const char *morning_b64,
	const char *regist_key_hex,
	bool ps5,
	bool enable_dualsense,
	bool enable_keyboard,
	ChiakiVideoResolutionPreset resolution,
	ChiakiVideoFPSPreset fps,
	unsigned int bitrate,
	ChiakiCodec codec)
{
	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	uint8_t regist_key[CHIAKI_SESSION_AUTH_SIZE] = {0};
	ChiakiHeadlessCloudLaunchInfo launch = {0};
	ChiakiErrorCode err = headless_build_cloud_launch_from_strings(
		&launch,
		morning,
		sizeof(morning),
		regist_key,
		sizeof(regist_key),
		host,
		stream_port,
		session_id,
		launch_spec,
		morning_b64,
		regist_key_hex,
		ps5,
		enable_dualsense,
		enable_keyboard,
		resolution,
		fps,
		bitrate,
		codec);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiConnectInfo connect_info = {0};
	return chiaki_headless_connect_info_init_cloud_direct(&connect_info, &launch);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_probe_create_session_cloud_launch_strings(
	const char *host,
	uint16_t stream_port,
	const char *session_id,
	const char *launch_spec,
	const char *morning_b64,
	const char *regist_key_hex,
	bool ps5,
	bool enable_dualsense,
	bool enable_keyboard,
	ChiakiVideoResolutionPreset resolution,
	ChiakiVideoFPSPreset fps,
	unsigned int bitrate,
	ChiakiCodec codec,
	const char *ffmpeg_hw_decoder_name)
{
	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	uint8_t regist_key[CHIAKI_SESSION_AUTH_SIZE] = {0};
	ChiakiHeadlessCloudLaunchInfo launch = {0};
	ChiakiErrorCode err = headless_build_cloud_launch_from_strings(
		&launch,
		morning,
		sizeof(morning),
		regist_key,
		sizeof(regist_key),
		host,
		stream_port,
		session_id,
		launch_spec,
		morning_b64,
		regist_key_hex,
		ps5,
		enable_dualsense,
		enable_keyboard,
		resolution,
		fps,
		bitrate,
		codec);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiConnectInfo connect_info = {0};
	err = chiaki_headless_connect_info_init_cloud_direct(&connect_info, &launch);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	HeadlessRuntimeConfig runtime_config = {0};
	err = headless_runtime_snapshot_config(&runtime_config);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	headless_apply_runtime_policy_overrides_to_connect_info(&connect_info, &runtime_config);

	ChiakiLog log;
	chiaki_log_init(&log, 0, headless_noop_log_cb, NULL);

	ChiakiHeadlessCreateInfo create_info = {
		.connect_info = connect_info,
		.ffmpeg_hw_decoder_name = ffmpeg_hw_decoder_name,
		.callbacks = NULL,
		.log = &log,
	};

	ChiakiHeadlessSession *session = NULL;
	err = chiaki_headless_session_create(&session, &create_info);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	chiaki_headless_session_destroy(session);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_probe_start_stop_session_cloud_launch_strings(
	const char *host,
	uint16_t stream_port,
	const char *session_id,
	const char *launch_spec,
	const char *morning_b64,
	const char *regist_key_hex,
	bool ps5,
	bool enable_dualsense,
	bool enable_keyboard,
	ChiakiVideoResolutionPreset resolution,
	ChiakiVideoFPSPreset fps,
	unsigned int bitrate,
	ChiakiCodec codec,
	const char *ffmpeg_hw_decoder_name)
{
	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	uint8_t regist_key[CHIAKI_SESSION_AUTH_SIZE] = {0};
	ChiakiHeadlessCloudLaunchInfo launch = {0};
	ChiakiErrorCode err = headless_build_cloud_launch_from_strings(
		&launch,
		morning,
		sizeof(morning),
		regist_key,
		sizeof(regist_key),
		host,
		stream_port,
		session_id,
		launch_spec,
		morning_b64,
		regist_key_hex,
		ps5,
		enable_dualsense,
		enable_keyboard,
		resolution,
		fps,
		bitrate,
		codec);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiConnectInfo connect_info = {0};
	err = chiaki_headless_connect_info_init_cloud_direct(&connect_info, &launch);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiLog log;
	chiaki_log_init(&log, 0, headless_noop_log_cb, NULL);

	ChiakiHeadlessCreateInfo create_info = {
		.connect_info = connect_info,
		.ffmpeg_hw_decoder_name = ffmpeg_hw_decoder_name,
		.callbacks = NULL,
		.log = &log,
	};

	ChiakiHeadlessSession *session = NULL;
	err = chiaki_headless_session_create(&session, &create_info);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	err = chiaki_headless_session_start(session);
	if(err == CHIAKI_ERR_SUCCESS)
	{
		ChiakiErrorCode stop_err = chiaki_headless_session_stop(session);
		ChiakiErrorCode join_err = chiaki_headless_session_join(session);
		if(stop_err != CHIAKI_ERR_SUCCESS)
			err = stop_err;
		else if(join_err != CHIAKI_ERR_SUCCESS)
			err = join_err;
	}

	chiaki_headless_session_destroy(session);
	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_cloud_start(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	const char *ffmpeg_hw_decoder_name)
{
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	HeadlessRuntimeConfig runtime_config = {0};
	uint64_t stop_generation_at_reservation = 0;

	chiaki_mutex_lock(&g_runtime_lock);
	if(g_runtime_session || g_runtime_start_in_flight)
	{
		chiaki_mutex_unlock(&g_runtime_lock);
		if(g_runtime_log_init)
			CHIAKI_LOGW(&g_runtime_log, "Runtime start rejected: session is active or start is already in flight");
		return CHIAKI_ERR_MUTEX_LOCKED;
	}
	g_runtime_start_in_flight = true;
	stop_generation_at_reservation = g_runtime_stop_generation;
	runtime_config.callbacks = g_runtime_callbacks;
	runtime_config.callbacks_set = g_runtime_callbacks_set;
	runtime_config.audio_sink_config = g_runtime_audio_sink_config;
	runtime_config.audio_sink_config_set = g_runtime_audio_sink_config_set;
	runtime_config.launch_overrides = g_runtime_launch_overrides;
	runtime_config.launch_overrides_set = g_runtime_launch_overrides_set;
	runtime_config.stream_profile_overrides = g_runtime_stream_profile_overrides;
	runtime_config.stream_profile_overrides_set = g_runtime_stream_profile_overrides_set;
	runtime_config.policy_overrides = g_runtime_policy_overrides;
	runtime_config.policy_overrides_set = g_runtime_policy_overrides_set;
	chiaki_mutex_unlock(&g_runtime_lock);

	if(!launch_info || !launch_info->host || !launch_info->session_id || !launch_info->launch_spec || !launch_info->morning)
		return CHIAKI_ERR_INVALID_DATA;
	if(launch_info->morning_size != CHIAKI_HANDSHAKE_KEY_SIZE)
		return CHIAKI_ERR_INVALID_DATA;

	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	uint8_t regist_key[CHIAKI_SESSION_AUTH_SIZE] = {0};
	ChiakiHeadlessCloudLaunchInfo launch = *launch_info;
	memcpy(morning, launch_info->morning, CHIAKI_HANDSHAKE_KEY_SIZE);
	launch.morning = morning;
	headless_regist_key_zero_fill(regist_key, launch_info->regist_key, launch_info->regist_key_size);
	launch.regist_key = regist_key;
	launch.regist_key_size = CHIAKI_SESSION_AUTH_SIZE;

	headless_apply_runtime_config_to_launch(&launch, &runtime_config);
	err = headless_validate_launch_stream_fields(&launch);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_mutex_lock(&g_runtime_lock);
		g_runtime_start_in_flight = false;
		chiaki_mutex_unlock(&g_runtime_lock);
		return err;
	}

	ChiakiConnectInfo connect_info = {0};
	err = chiaki_headless_connect_info_init_cloud_direct(&connect_info, &launch);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_mutex_lock(&g_runtime_lock);
		g_runtime_start_in_flight = false;
		chiaki_mutex_unlock(&g_runtime_lock);
		return err;
	}
	headless_apply_runtime_policy_overrides_to_connect_info(&connect_info, &runtime_config);

	if(!g_runtime_log_init)
	{
		/* Keep runtime logging non-verbose by default for embedded hosts:
		 * verbose packet-level logs can flood stdout and destabilize host debug runs.
		 */
		chiaki_log_init(&g_runtime_log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE, NULL, NULL);
		g_runtime_log_init = true;
	}

	ChiakiHeadlessCreateInfo create_info = {
		.connect_info = connect_info,
		.ffmpeg_hw_decoder_name = ffmpeg_hw_decoder_name,
		.callbacks = runtime_config.callbacks_set ? &runtime_config.callbacks : NULL,
		.runtime_audio_sink_config =
			runtime_config.audio_sink_config_set ? &runtime_config.audio_sink_config : NULL,
		.display_only_host_video_sink =
			runtime_config.policy_overrides_set
			&& runtime_config.policy_overrides.use_display_only_host_video_sink
			&& runtime_config.policy_overrides.display_only_host_video_sink,
		.log = &g_runtime_log,
	};

	ChiakiHeadlessSession *session = NULL;
	err = chiaki_headless_session_create(&session, &create_info);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_mutex_lock(&g_runtime_lock);
		g_runtime_start_in_flight = false;
		chiaki_mutex_unlock(&g_runtime_lock);
		return err;
	}

	err = chiaki_headless_session_start(session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_mutex_lock(&g_runtime_lock);
		g_runtime_start_in_flight = false;
		chiaki_mutex_unlock(&g_runtime_lock);
		chiaki_headless_session_destroy(session);
		return err;
	}

	chiaki_mutex_lock(&g_runtime_lock);
	if(g_runtime_stop_generation != stop_generation_at_reservation)
	{
		g_runtime_start_in_flight = false;
		chiaki_mutex_unlock(&g_runtime_lock);
		if(g_runtime_log_init)
			CHIAKI_LOGW(&g_runtime_log, "Runtime start canceled: stop requested while start was in flight");
		chiaki_headless_session_stop(session);
		chiaki_headless_session_join(session);
		chiaki_headless_session_destroy(session);
		return CHIAKI_ERR_CANCELED;
	}
	g_runtime_session = session;
	g_runtime_start_in_flight = false;
	headless_runtime_recovery_status_reset_locked();
	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_cloud_start_strings(
	const char *host,
	uint16_t stream_port,
	const char *session_id,
	const char *launch_spec,
	const char *morning_b64,
	const char *regist_key_hex,
	bool ps5,
	bool enable_dualsense,
	bool enable_keyboard,
	ChiakiVideoResolutionPreset resolution,
	ChiakiVideoFPSPreset fps,
	unsigned int bitrate,
	ChiakiCodec codec,
	const char *ffmpeg_hw_decoder_name)
{
	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	uint8_t regist_key[CHIAKI_SESSION_AUTH_SIZE] = {0};
	ChiakiHeadlessCloudLaunchInfo launch = {0};
	ChiakiErrorCode err = headless_build_cloud_launch_from_strings(
		&launch,
		morning,
		sizeof(morning),
		regist_key,
		sizeof(regist_key),
		host,
		stream_port,
		session_id,
		launch_spec,
		morning_b64,
		regist_key_hex,
		ps5,
		enable_dualsense,
		enable_keyboard,
		resolution,
		fps,
		bitrate,
		codec);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	return chiaki_headless_runtime_cloud_start(&launch, ffmpeg_hw_decoder_name);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_cloud_stop(void)
{
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	g_runtime_stop_generation++;
	ChiakiHeadlessSession *session = g_runtime_session;
	g_runtime_session = NULL;
	headless_runtime_recovery_status_reset_locked();
	chiaki_mutex_unlock(&g_runtime_lock);

	if(!session)
		return CHIAKI_ERR_SUCCESS;

	ChiakiErrorCode stop_err = chiaki_headless_session_stop(session);
	ChiakiErrorCode join_err = chiaki_headless_session_join(session);
	chiaki_headless_session_destroy(session);
	if(stop_err != CHIAKI_ERR_SUCCESS)
		return stop_err;
	return join_err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_request_idr(void)
{
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	ChiakiHeadlessSession *session = g_runtime_session;
	chiaki_mutex_unlock(&g_runtime_lock);
	if(!session)
		return CHIAKI_ERR_INVALID_DATA;
	return chiaki_headless_session_request_idr(session);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_send_controller_state_compat(
	uint32_t buttons,
	uint8_t l2_state,
	uint8_t r2_state,
	int16_t left_x,
	int16_t left_y,
	int16_t right_x,
	int16_t right_y)
{
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	ChiakiHeadlessSession *session = g_runtime_session;
	chiaki_mutex_unlock(&g_runtime_lock);
	if(!session)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);
	state.buttons = buttons;
	state.l2_state = l2_state;
	state.r2_state = r2_state;
	state.left_x = left_x;
	state.left_y = left_y;
	state.right_x = right_x;
	state.right_y = right_y;
	return chiaki_headless_session_send_controller_state(session, &state);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_tap_button(uint32_t button_mask)
{
	ChiakiErrorCode err = chiaki_headless_runtime_send_controller_state_compat(
		button_mask,
		0,
		0,
		0,
		0,
		0,
		0);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_headless_runtime_send_controller_state_compat(0, 0, 0, 0, 0, 0, 0);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_callbacks(const ChiakiHeadlessCallbacks *callbacks)
{
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	if(callbacks && (g_runtime_session || g_runtime_start_in_flight))
	{
		chiaki_mutex_unlock(&g_runtime_lock);
		return CHIAKI_ERR_MUTEX_LOCKED;
	}

	if(callbacks)
	{
		memcpy(&g_runtime_callbacks, callbacks, sizeof(g_runtime_callbacks));
		g_runtime_callbacks_set = true;
	}
	else
	{
		memset(&g_runtime_callbacks, 0, sizeof(g_runtime_callbacks));
		g_runtime_callbacks_set = false;
		if(g_runtime_session)
		{
			chiaki_mutex_lock(&g_runtime_session->cb_mutex);
			memset(&g_runtime_session->callbacks, 0, sizeof(g_runtime_session->callbacks));
			chiaki_mutex_unlock(&g_runtime_session->cb_mutex);
		}
	}
	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_audio_sink_config_size(void)
{
	return sizeof(ChiakiHeadlessRuntimeAudioSinkConfig);
}

CHIAKI_EXPORT void chiaki_headless_runtime_audio_sink_config_init(
	ChiakiHeadlessRuntimeAudioSinkConfig *config)
{
	if(!config)
		return;
	memset(config, 0, sizeof(*config));
	config->api_version = chiaki_headless_api_version();
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_audio_sink_config(
	const ChiakiHeadlessRuntimeAudioSinkConfig *config)
{
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	if(g_runtime_session || g_runtime_start_in_flight)
	{
		chiaki_mutex_unlock(&g_runtime_lock);
		return CHIAKI_ERR_MUTEX_LOCKED;
	}

	if(config)
	{
		g_runtime_audio_sink_config = *config;
		g_runtime_audio_sink_config_set = true;
	}
	else
	{
		memset(&g_runtime_audio_sink_config, 0, sizeof(g_runtime_audio_sink_config));
		g_runtime_audio_sink_config.api_version = chiaki_headless_api_version();
		g_runtime_audio_sink_config_set = false;
	}
	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_audio_sink_config(
	ChiakiHeadlessRuntimeAudioSinkConfig *out_config)
{
	if(!out_config)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	*out_config = g_runtime_audio_sink_config;
	if(!g_runtime_audio_sink_config_set)
		chiaki_headless_runtime_audio_sink_config_init(out_config);
	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_effective_stream_profile(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	ChiakiHeadlessStreamProfile *out_profile)
{
	if(!launch_info || !out_profile)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessCloudLaunchInfo launch = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_effective_launch_info(launch_info, &launch);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	out_profile->resolution = launch.resolution == 0 ? CHIAKI_VIDEO_RESOLUTION_PRESET_720p : launch.resolution;
	out_profile->fps = launch.fps == 0 ? CHIAKI_VIDEO_FPS_PRESET_60 : launch.fps;
	out_profile->bitrate = launch.bitrate;
	out_profile->codec = launch.codec;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_effective_connect_info(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	ChiakiConnectInfo *out_connect_info)
{
	if(!launch_info || !out_connect_info)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessCloudLaunchInfo launch = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_effective_launch_info(launch_info, &launch);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	err = chiaki_headless_connect_info_init_cloud_direct(out_connect_info, &launch);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	HeadlessRuntimeConfig runtime_config = {0};
	err = headless_runtime_snapshot_config(&runtime_config);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	headless_apply_runtime_policy_overrides_to_connect_info(out_connect_info, &runtime_config);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_sanity_report(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	ChiakiHeadlessRuntimeSanityReport *out_report)
{
	if(!launch_info || !out_report)
		return CHIAKI_ERR_INVALID_DATA;

	HeadlessRuntimeConfig runtime_config = {0};
	ChiakiErrorCode err = headless_runtime_snapshot_config(&runtime_config);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiErrorCode lock_err = headless_runtime_ensure_lock();
	if(lock_err != CHIAKI_ERR_SUCCESS)
		return lock_err;

	bool runtime_session_active = false;
	chiaki_mutex_lock(&g_runtime_lock);
	runtime_session_active = g_runtime_session != NULL;
	chiaki_mutex_unlock(&g_runtime_lock);

	memset(out_report, 0, sizeof(*out_report));
	out_report->runtime_session_active = runtime_session_active;
	out_report->has_overrides = runtime_config.launch_overrides_set || runtime_config.stream_profile_overrides_set;

	ChiakiHeadlessCloudLaunchInfo effective = *launch_info;
	headless_apply_runtime_config_to_launch(&effective, &runtime_config);
	out_report->effective_launch_info = effective;

	out_report->validation_error = headless_validate_launch_stream_fields(&effective);
	if(out_report->validation_error == CHIAKI_ERR_SUCCESS)
	{
		out_report->effective_stream_profile.resolution =
			effective.resolution == 0 ? CHIAKI_VIDEO_RESOLUTION_PRESET_720p : effective.resolution;
		out_report->effective_stream_profile.fps =
			effective.fps == 0 ? CHIAKI_VIDEO_FPS_PRESET_60 : effective.fps;
		out_report->effective_stream_profile.bitrate = effective.bitrate;
		out_report->effective_stream_profile.codec = effective.codec;
	}
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_state_snapshot(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	ChiakiHeadlessRuntimeStateSnapshot *out_snapshot)
{
	if(!launch_info || !out_snapshot)
		return CHIAKI_ERR_INVALID_DATA;

	memset(out_snapshot, 0, sizeof(*out_snapshot));
	out_snapshot->api_version = chiaki_headless_api_version();

	ChiakiErrorCode err = chiaki_headless_runtime_get_overrides(&out_snapshot->overrides);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	err = chiaki_headless_runtime_get_sanity_report(launch_info, &out_snapshot->sanity_report);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_state_snapshot_size(void)
{
	return sizeof(ChiakiHeadlessRuntimeStateSnapshot);
}

CHIAKI_EXPORT void chiaki_headless_runtime_state_snapshot_init(
	ChiakiHeadlessRuntimeStateSnapshot *snapshot)
{
	if(!snapshot)
		return;
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->api_version = chiaki_headless_api_version();
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_state_snapshot_compat(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	void *out_snapshot_buf,
	size_t out_snapshot_size)
{
	if(!launch_info || !out_snapshot_buf || out_snapshot_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeStateSnapshot full = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_state_snapshot(launch_info, &full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	memset(out_snapshot_buf, 0, out_snapshot_size);
	size_t copy_size = out_snapshot_size < sizeof(full) ? out_snapshot_size : sizeof(full);
	memcpy(out_snapshot_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_capabilities_size(void)
{
	return sizeof(ChiakiHeadlessRuntimeCapabilities);
}

CHIAKI_EXPORT void chiaki_headless_runtime_capabilities_init(
	ChiakiHeadlessRuntimeCapabilities *capabilities)
{
	if(!capabilities)
		return;
	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->api_version = chiaki_headless_api_version();
	capabilities->min_state_snapshot_size = sizeof(ChiakiHeadlessRuntimeStateSnapshot);
	capabilities->min_runtime_diagnostics_snapshot_size = sizeof(ChiakiMediaSessionDiagnosticsSnapshot);
	capabilities->min_runtime_recovery_decision_size = sizeof(ChiakiHeadlessRuntimeRecoveryDecision);
	capabilities->min_runtime_recovery_result_size = sizeof(ChiakiHeadlessRuntimeRecoveryResult);
	capabilities->min_runtime_recovery_tuning_size = sizeof(ChiakiHeadlessRuntimeRecoveryTuning);
	capabilities->min_runtime_recovery_status_size = sizeof(ChiakiHeadlessRuntimeRecoveryStatus);
	capabilities->min_runtime_recovery_config_size = sizeof(ChiakiHeadlessRuntimeRecoveryConfig);
	capabilities->min_runtime_recovery_simulation_step_input_size =
		sizeof(ChiakiHeadlessRuntimeRecoverySimulationStepInput);
	capabilities->min_runtime_recovery_simulation_health_step_input_size =
		sizeof(ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput);
	capabilities->min_runtime_recovery_simulation_step_output_size =
		sizeof(ChiakiHeadlessRuntimeRecoverySimulationStepOutput);
	capabilities->min_runtime_recovery_simulation_report_size =
		sizeof(ChiakiHeadlessRuntimeRecoverySimulationReport);
	capabilities->min_runtime_recovery_auto_loop_timeline_step_size =
		sizeof(ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineStep);
	capabilities->min_runtime_recovery_auto_loop_timeline_summary_size =
		sizeof(ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineSummary);
	capabilities->min_runtime_host_status_size =
		sizeof(ChiakiHeadlessRuntimeHostStatus);
	capabilities->min_runtime_playback_readiness_status_size =
		sizeof(ChiakiHeadlessRuntimePlaybackReadinessStatus);
	capabilities->min_runtime_playback_continuity_status_size =
		sizeof(ChiakiHeadlessRuntimePlaybackContinuityStatus);
	capabilities->min_runtime_video_frame_metadata_size =
		sizeof(ChiakiHeadlessRuntimeVideoFrameMetadata);
	capabilities->min_runtime_video_frame_poll_size =
		sizeof(ChiakiHeadlessRuntimeVideoFramePoll);
	capabilities->min_runtime_audio_sink_config_size =
		sizeof(ChiakiHeadlessRuntimeAudioSinkConfig);
	capabilities->min_runtime_audio_sink_diagnostics_size =
		sizeof(ChiakiHeadlessRuntimeAudioSinkDiagnostics);
	capabilities->min_runtime_external_video_capabilities_size =
		sizeof(ChiakiHeadlessRuntimeExternalVideoCapabilities);
	capabilities->min_runtime_recovery_parity_fixture_expected_size =
		sizeof(ChiakiHeadlessRuntimeRecoveryParityFixtureExpected);
	capabilities->min_runtime_recovery_parity_fixture_result_size =
		sizeof(ChiakiHeadlessRuntimeRecoveryParityFixtureResult);
	capabilities->min_runtime_recovery_parity_smoke_result_size =
		sizeof(ChiakiHeadlessRuntimeRecoveryParitySmokeResult);
	capabilities->min_runtime_recovery_parity_smoke_runner_result_size =
		sizeof(ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult);
	capabilities->min_runtime_recovery_parity_baseline_scenario_count_size =
		sizeof(size_t);
	capabilities->min_runtime_recovery_parity_baseline_execution_detail_size =
		sizeof(ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail);
	capabilities->min_runtime_recovery_core_diagnostics_summary_size =
		sizeof(ChiakiHeadlessRuntimeRecoveryCoreDiagnosticsSummary);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_capabilities(
	ChiakiHeadlessRuntimeCapabilities *out_capabilities)
{
	if(!out_capabilities)
		return CHIAKI_ERR_INVALID_DATA;
	chiaki_headless_runtime_capabilities_init(out_capabilities);
	out_capabilities->supports_runtime_struct_start = true;
	out_capabilities->supports_runtime_cloud_start_strings = true;
	out_capabilities->supports_runtime_cloud_stop = true;
	out_capabilities->supports_runtime_overrides_bundle = true;
	out_capabilities->supports_runtime_overrides_patch = true;
	out_capabilities->supports_runtime_policy_overrides = true;
	out_capabilities->supports_runtime_sanity_report = true;
	out_capabilities->supports_runtime_state_snapshot = true;
	out_capabilities->supports_runtime_state_snapshot_compat = true;
	out_capabilities->supports_runtime_media_diagnostics_snapshot = true;
	out_capabilities->supports_runtime_media_diagnostics_snapshot_compat = true;
	out_capabilities->supports_runtime_media_diagnostics_snapshot_with_profile_key = true;
	out_capabilities->supports_runtime_media_diagnostics_snapshot_with_profile_key_compat = true;
	out_capabilities->supports_runtime_recovery_decision = true;
	out_capabilities->supports_runtime_recovery_decision_compat = true;
	out_capabilities->supports_runtime_recovery_decision_with_profile_key = true;
	out_capabilities->supports_runtime_recovery_decision_with_profile_key_compat = true;
	out_capabilities->supports_runtime_recovery_apply = true;
	out_capabilities->supports_runtime_recovery_apply_compat = true;
	out_capabilities->supports_runtime_recover_result = true;
	out_capabilities->supports_runtime_recover_result_compat = true;
	out_capabilities->supports_runtime_recover_result_with_profile_key = true;
	out_capabilities->supports_runtime_recover_result_with_profile_key_compat = true;
	out_capabilities->supports_runtime_recover_tuned = true;
	out_capabilities->supports_runtime_recover_tuned_compat = true;
	out_capabilities->supports_runtime_recovery_status = true;
	out_capabilities->supports_runtime_recovery_status_compat = true;
	out_capabilities->supports_runtime_recovery_status_reset = true;
	out_capabilities->supports_runtime_recovery_config_set_get = true;
	out_capabilities->supports_runtime_recovery_config_compat = true;
	out_capabilities->supports_runtime_recovery_profile_key_set_get = true;
	out_capabilities->supports_runtime_recover_auto = true;
	out_capabilities->supports_runtime_recover_auto_compat = true;
	out_capabilities->supports_runtime_recover_auto_with_status = true;
	out_capabilities->supports_runtime_recover_auto_with_status_compat = true;
	out_capabilities->supports_runtime_recovery_simulation = true;
	out_capabilities->supports_runtime_recovery_simulation_compat = true;
	out_capabilities->supports_runtime_recovery_simulation_sequence = true;
	out_capabilities->supports_runtime_recovery_simulation_report = true;
	out_capabilities->supports_runtime_recovery_simulation_sequence_compat = true;
	out_capabilities->supports_runtime_recovery_simulation_report_compat = true;
	out_capabilities->supports_runtime_recovery_simulation_sequence_with_report = true;
	out_capabilities->supports_runtime_recovery_simulation_sequence_with_report_compat = true;
	out_capabilities->supports_runtime_recovery_simulation_report_from_outputs = true;
	out_capabilities->supports_runtime_recovery_simulation_report_from_outputs_compat = true;
	out_capabilities->supports_runtime_recovery_simulation_sequence_with_profile_key = true;
	out_capabilities->supports_runtime_recovery_simulation_report_with_profile_key = true;
	out_capabilities->supports_runtime_recovery_simulation_sequence_with_profile_key_compat = true;
	out_capabilities->supports_runtime_recovery_simulation_report_with_profile_key_compat = true;
	out_capabilities->supports_runtime_recovery_simulation_sequence_with_profile_key_with_report = true;
	out_capabilities->supports_runtime_recovery_simulation_sequence_with_profile_key_with_report_compat = true;
	out_capabilities->supports_runtime_recovery_simulation_health_sequence_with_profile_key = true;
	out_capabilities->supports_runtime_recovery_simulation_health_report_with_profile_key = true;
	out_capabilities->supports_runtime_recovery_simulation_health_sequence_with_profile_key_compat = true;
	out_capabilities->supports_runtime_recovery_simulation_health_report_with_profile_key_compat = true;
	out_capabilities->supports_runtime_recovery_simulation_health_sequence_with_profile_key_with_report = true;
	out_capabilities->supports_runtime_recovery_simulation_health_sequence_with_profile_key_with_report_compat = true;
	out_capabilities->supports_runtime_recovery_auto_loop_timeline = true;
	out_capabilities->supports_runtime_recovery_auto_loop_timeline_compat = true;
	out_capabilities->supports_runtime_host_status = true;
	out_capabilities->supports_runtime_host_status_compat = true;
	out_capabilities->supports_runtime_playback_readiness_status = true;
	out_capabilities->supports_runtime_playback_readiness_status_compat = true;
	out_capabilities->supports_runtime_playback_continuity_status = true;
	out_capabilities->supports_runtime_playback_continuity_status_compat = true;
	out_capabilities->supports_runtime_video_frame_metadata = true;
	out_capabilities->supports_runtime_video_frame_metadata_compat = true;
	out_capabilities->supports_runtime_video_frame_poll = true;
	out_capabilities->supports_runtime_video_frame_poll_compat = true;
	out_capabilities->supports_runtime_audio_sink_config_set_get = true;
	out_capabilities->supports_runtime_audio_sink_diagnostics = true;
	out_capabilities->supports_runtime_audio_sink_diagnostics_compat = true;
	out_capabilities->supports_runtime_audio_sink_underrun_report = true;
	out_capabilities->supports_runtime_recovery_parity_fixture_eval = true;
	out_capabilities->supports_runtime_recovery_parity_fixture_eval_compat = true;
	out_capabilities->supports_runtime_recovery_parity_fixture_export = true;
	out_capabilities->supports_runtime_recovery_parity_fixture_export_compat = true;
	out_capabilities->supports_runtime_recovery_parity_smoke = true;
	out_capabilities->supports_runtime_recovery_parity_smoke_compat = true;
	out_capabilities->supports_runtime_recovery_parity_smoke_runner = true;
	out_capabilities->supports_runtime_recovery_parity_smoke_runner_compat = true;
	out_capabilities->supports_runtime_recovery_parity_baseline_smoke = true;
	out_capabilities->supports_runtime_recovery_parity_baseline_smoke_compat = true;
	out_capabilities->supports_runtime_recovery_parity_baseline_scenario_count = true;
	out_capabilities->supports_runtime_recovery_parity_baseline_scenario_count_compat = true;
	out_capabilities->supports_runtime_recovery_parity_baseline_scenario_label = true;
	out_capabilities->supports_runtime_recovery_parity_baseline_scenario_label_compat = true;
	out_capabilities->supports_runtime_recovery_parity_baseline_execution_details = true;
	out_capabilities->supports_runtime_recovery_parity_baseline_execution_details_compat = true;
	out_capabilities->supports_runtime_recovery_core_diagnostics = true;
	out_capabilities->supports_runtime_recovery_core_diagnostics_compat = true;
	out_capabilities->supports_runtime_display_only_host_video_sink_mode = true;
	out_capabilities->supports_runtime_external_video_capabilities = true;
	out_capabilities->supports_runtime_external_video_capabilities_compat = true;
#if !defined(_WIN32)
	out_capabilities->supports_runtime_external_video_dmabuf = true;
#else
	out_capabilities->supports_runtime_external_video_dmabuf = false;
#endif
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_simulation_report_from_outputs(
	const ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryStatus *final_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report)
{
	if(!step_outputs || step_count == 0 || !final_status || !out_report)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoverySimulationReport report = {0};
	report.api_version = chiaki_headless_api_version();
	report.step_count = step_count;
	report.first_idr_step_index = step_count;
	report.first_stop_step_index = step_count;
	report.first_none_step_index = step_count;
	report.final_status = *final_status;

	for(size_t i = 0; i < step_count; i++)
	{
		uint32_t health_state = step_outputs[i].result.decision.health_state;
		if(health_state == CHIAKI_MEDIA_HEALTH_READY)
			report.healthy_step_count++;
		else if(health_state == CHIAKI_MEDIA_HEALTH_DEGRADED)
			report.degraded_step_count++;
		else if(health_state == CHIAKI_MEDIA_HEALTH_TERMINAL)
			report.terminal_step_count++;
		switch(step_outputs[i].result.decision.action)
		{
			case CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR:
				report.idr_action_count++;
				if(report.first_idr_step_index == step_count)
					report.first_idr_step_index = i;
				break;
			case CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_STOP_RUNTIME:
				report.stop_action_count++;
				if(report.first_stop_step_index == step_count)
					report.first_stop_step_index = i;
				break;
			case CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE:
			default:
				report.none_action_count++;
				if(report.first_none_step_index == step_count)
					report.first_none_step_index = i;
				break;
		}
		if(health_state == CHIAKI_MEDIA_HEALTH_TERMINAL)
			report.saw_terminal_health = true;
	}

	*out_report = report;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_simulation_report_from_outputs_compat(
	const void *step_outputs_buf,
	size_t step_count,
	size_t step_output_stride,
	size_t step_output_size,
	const void *final_status_buf,
	size_t final_status_size,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!step_outputs_buf || step_count == 0 || step_output_stride == 0 || step_output_size == 0
		|| !final_status_buf || final_status_size == 0 || !out_report_buf || out_report_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryStatus final_status = {0};
	chiaki_headless_runtime_recovery_status_init(&final_status);
	size_t copy_size = final_status_size < sizeof(final_status) ? final_status_size : sizeof(final_status);
	memcpy(&final_status, final_status_buf, copy_size);

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *full_outputs =
		calloc(step_count, sizeof(*full_outputs));
	if(!full_outputs)
		return CHIAKI_ERR_MEMORY;
	for(size_t i = 0; i < step_count; i++)
	{
		const uint8_t *src = (const uint8_t *)step_outputs_buf + i * step_output_stride;
		size_t n = step_output_size < sizeof(full_outputs[i]) ? step_output_size : sizeof(full_outputs[i]);
		memcpy(&full_outputs[i], src, n);
	}

	ChiakiHeadlessRuntimeRecoverySimulationReport report = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_simulation_report_from_outputs(
		full_outputs,
		step_count,
		&final_status,
		&report);
	free(full_outputs);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	copy_size = out_report_size < sizeof(report) ? out_report_size : sizeof(report);
	memcpy(out_report_buf, &report, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_host_status_size(void)
{
	return sizeof(ChiakiHeadlessRuntimeHostStatus);
}

CHIAKI_EXPORT void chiaki_headless_runtime_host_status_init(
	ChiakiHeadlessRuntimeHostStatus *status)
{
	if(!status)
		return;
	memset(status, 0, sizeof(*status));
	status->api_version = chiaki_headless_api_version();
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_build_host_status(
	const ChiakiMediaSessionDiagnosticsSnapshot *snapshot,
	const ChiakiHeadlessRuntimeRecoveryResult *recovery_result,
	const ChiakiHeadlessRuntimeRecoveryStatus *recovery_status,
	ChiakiHeadlessRuntimeHostStatus *out_status)
{
	if(!snapshot || !recovery_result || !recovery_status || !out_status)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeHostStatus status = {0};
	status.api_version = chiaki_headless_api_version();
	status.health_state = snapshot->health.health_state;
	status.recommended_action = recovery_result->decision.action;
	status.applied_action = recovery_result->applied_action;
	status.recover_apply_succeeded = recovery_result->apply_succeeded;
	status.recover_action_transitioned = recovery_result->action_transitioned;
	status.recover_monotonic_us = recovery_result->recover_monotonic_us;
	status.runtime_session_active = recovery_status->runtime_session_active;
	status.degraded_streak = recovery_status->degraded_streak;
	status.last_idr_request_monotonic_us = recovery_status->last_idr_request_monotonic_us;
	status.recover_attempt_count = recovery_status->recover_attempt_count;
	status.recover_success_count = recovery_status->recover_success_count;
	status.recover_failure_count = recovery_status->recover_failure_count;
	status.event_count = snapshot->stats.event_count;
	status.error_event_count = snapshot->stats.error_event_count;
	status.ready_event_count = snapshot->stats.ready_event_count;
	status.packets_received = snapshot->stats.packets_received;
	status.packets_lost = snapshot->stats.packets_lost;
	status.measured_bitrate_kbps = snapshot->stats.measured_bitrate_kbps;
	status.video_decode_lost_frames = snapshot->stats.video_decode_lost_frames;
	status.video_decode_recovered_frames = snapshot->stats.video_decode_recovered_frames;
	status.video_decode_gap_event_count = snapshot->stats.video_decode_gap_event_count;

	*out_status = status;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_build_host_status_compat(
	const void *snapshot_buf,
	size_t snapshot_size,
	const void *recovery_result_buf,
	size_t recovery_result_size,
	const void *recovery_status_buf,
	size_t recovery_status_size,
	void *out_status_buf,
	size_t out_status_size)
{
	if(!snapshot_buf || snapshot_size == 0
		|| !recovery_result_buf || recovery_result_size == 0
		|| !recovery_status_buf || recovery_status_size == 0
		|| !out_status_buf || out_status_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
	chiaki_media_session_diagnostics_snapshot_init(&snapshot);
	size_t copy_size = snapshot_size < sizeof(snapshot) ? snapshot_size : sizeof(snapshot);
	memcpy(&snapshot, snapshot_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryResult recovery_result = {0};
	recovery_result.api_version = chiaki_headless_api_version();
	copy_size = recovery_result_size < sizeof(recovery_result) ? recovery_result_size : sizeof(recovery_result);
	memcpy(&recovery_result, recovery_result_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryStatus recovery_status = {0};
	chiaki_headless_runtime_recovery_status_init(&recovery_status);
	copy_size = recovery_status_size < sizeof(recovery_status) ? recovery_status_size : sizeof(recovery_status);
	memcpy(&recovery_status, recovery_status_buf, copy_size);

	ChiakiHeadlessRuntimeHostStatus out_status = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_build_host_status(
		&snapshot,
		&recovery_result,
		&recovery_status,
		&out_status);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	copy_size = out_status_size < sizeof(out_status) ? out_status_size : sizeof(out_status);
	memcpy(out_status_buf, &out_status, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_playback_readiness_status_size(void)
{
	return sizeof(ChiakiHeadlessRuntimePlaybackReadinessStatus);
}

CHIAKI_EXPORT void chiaki_headless_runtime_playback_readiness_status_init(
	ChiakiHeadlessRuntimePlaybackReadinessStatus *status)
{
	if(!status)
		return;
	memset(status, 0, sizeof(*status));
	status->api_version = chiaki_headless_api_version();
	chiaki_headless_runtime_host_status_init(&status->host_status);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_build_playback_readiness_status(
	const ChiakiMediaSessionDiagnosticsSnapshot *snapshot,
	const ChiakiHeadlessRuntimeRecoveryResult *recovery_result,
	const ChiakiHeadlessRuntimeRecoveryStatus *recovery_status,
	ChiakiHeadlessRuntimePlaybackReadinessStatus *out_status)
{
	if(!snapshot || !recovery_result || !recovery_status || !out_status)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimePlaybackReadinessStatus status = {0};
	status.api_version = chiaki_headless_api_version();

	ChiakiErrorCode err = chiaki_headless_runtime_build_host_status(
		snapshot,
		recovery_result,
		recovery_status,
		&status.host_status);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	status.recover_result_count = 1;
	status.recover_apply_success_count = recovery_result->apply_succeeded ? 1 : 0;
	status.recover_apply_failure_count = recovery_result->apply_succeeded ? 0 : 1;
	status.recover_action_transition_count = recovery_result->action_transitioned ? 1 : 0;
	status.recover_counter_observed_count =
		recovery_status->recover_success_count + recovery_status->recover_failure_count;
	status.recover_counters_consistent =
		recovery_status->recover_attempt_count == status.recover_counter_observed_count;

	*out_status = status;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_build_playback_readiness_status_compat(
	const void *snapshot_buf,
	size_t snapshot_size,
	const void *recovery_result_buf,
	size_t recovery_result_size,
	const void *recovery_status_buf,
	size_t recovery_status_size,
	void *out_status_buf,
	size_t out_status_size)
{
	if(!snapshot_buf || snapshot_size == 0
		|| !recovery_result_buf || recovery_result_size == 0
		|| !recovery_status_buf || recovery_status_size == 0
		|| !out_status_buf || out_status_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
	chiaki_media_session_diagnostics_snapshot_init(&snapshot);
	size_t copy_size = snapshot_size < sizeof(snapshot) ? snapshot_size : sizeof(snapshot);
	memcpy(&snapshot, snapshot_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryResult recovery_result = {0};
	recovery_result.api_version = chiaki_headless_api_version();
	copy_size = recovery_result_size < sizeof(recovery_result) ? recovery_result_size : sizeof(recovery_result);
	memcpy(&recovery_result, recovery_result_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryStatus recovery_status = {0};
	chiaki_headless_runtime_recovery_status_init(&recovery_status);
	copy_size = recovery_status_size < sizeof(recovery_status) ? recovery_status_size : sizeof(recovery_status);
	memcpy(&recovery_status, recovery_status_buf, copy_size);

	ChiakiHeadlessRuntimePlaybackReadinessStatus out_status = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_build_playback_readiness_status(
		&snapshot,
		&recovery_result,
		&recovery_status,
		&out_status);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	copy_size = out_status_size < sizeof(out_status) ? out_status_size : sizeof(out_status);
	memcpy(out_status_buf, &out_status, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_playback_continuity_status_size(void)
{
	return sizeof(ChiakiHeadlessRuntimePlaybackContinuityStatus);
}

CHIAKI_EXPORT void chiaki_headless_runtime_playback_continuity_status_init(
	ChiakiHeadlessRuntimePlaybackContinuityStatus *status)
{
	if(!status)
		return;
	memset(status, 0, sizeof(*status));
	status->api_version = chiaki_headless_api_version();
	chiaki_headless_runtime_host_status_init(&status->host_status);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_build_playback_continuity_status(
	const ChiakiMediaSessionDiagnosticsSnapshot *snapshot,
	const ChiakiHeadlessRuntimeRecoveryResult *recovery_result,
	const ChiakiHeadlessRuntimeRecoveryStatus *recovery_status,
	ChiakiHeadlessRuntimePlaybackContinuityStatus *out_status)
{
	if(!snapshot || !recovery_result || !recovery_status || !out_status)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimePlaybackContinuityStatus status = {0};
	status.api_version = chiaki_headless_api_version();

	ChiakiErrorCode err = chiaki_headless_runtime_build_host_status(
		snapshot,
		recovery_result,
		recovery_status,
		&status.host_status);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	status.video_frame_count = snapshot->stats.video_frame_count;
	status.audio_frame_count = snapshot->stats.audio_frame_count;
	status.frame_counter_observed_count = status.video_frame_count + status.audio_frame_count;
	status.decode_counter_observed_count =
		status.host_status.video_decode_lost_frames
		+ status.host_status.video_decode_recovered_frames;
	status.has_video_frame_activity = status.video_frame_count > 0;
	status.has_decode_gap_activity = status.host_status.video_decode_gap_event_count > 0;
	status.decode_gap_implies_decode_activity =
		status.host_status.video_decode_gap_event_count <= status.decode_counter_observed_count;

	*out_status = status;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_build_playback_continuity_status_compat(
	const void *snapshot_buf,
	size_t snapshot_size,
	const void *recovery_result_buf,
	size_t recovery_result_size,
	const void *recovery_status_buf,
	size_t recovery_status_size,
	void *out_status_buf,
	size_t out_status_size)
{
	if(!snapshot_buf || snapshot_size == 0
		|| !recovery_result_buf || recovery_result_size == 0
		|| !recovery_status_buf || recovery_status_size == 0
		|| !out_status_buf || out_status_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
	chiaki_media_session_diagnostics_snapshot_init(&snapshot);
	size_t copy_size = snapshot_size < sizeof(snapshot) ? snapshot_size : sizeof(snapshot);
	memcpy(&snapshot, snapshot_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryResult recovery_result = {0};
	recovery_result.api_version = chiaki_headless_api_version();
	copy_size = recovery_result_size < sizeof(recovery_result) ? recovery_result_size : sizeof(recovery_result);
	memcpy(&recovery_result, recovery_result_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryStatus recovery_status = {0};
	chiaki_headless_runtime_recovery_status_init(&recovery_status);
	copy_size = recovery_status_size < sizeof(recovery_status) ? recovery_status_size : sizeof(recovery_status);
	memcpy(&recovery_status, recovery_status_buf, copy_size);

	ChiakiHeadlessRuntimePlaybackContinuityStatus out_status = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_build_playback_continuity_status(
		&snapshot,
		&recovery_result,
		&recovery_status,
		&out_status);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	copy_size = out_status_size < sizeof(out_status) ? out_status_size : sizeof(out_status);
	memcpy(out_status_buf, &out_status, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_playback_continuity_status(
	uint32_t readiness_timeout_ms,
	ChiakiHeadlessRuntimePlaybackContinuityStatus *out_status)
{
	if(!out_status)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_media_diagnostics_snapshot(
		readiness_timeout_ms,
		&snapshot);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiHeadlessRuntimeRecoveryStatus recovery_status = {0};
	err = chiaki_headless_runtime_get_recovery_status(&recovery_status);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiHeadlessRuntimeRecoveryResult recovery_result = {0};
	recovery_result.api_version = chiaki_headless_api_version();
	recovery_result.decision.health_state = snapshot.health.health_state;
	recovery_result.decision.action = recovery_status.last_recommended_action;
	recovery_result.applied_action = recovery_status.last_applied_action;
	recovery_result.apply_error = CHIAKI_ERR_SUCCESS;
	recovery_result.apply_succeeded = true;
	recovery_result.action_transitioned =
		recovery_result.decision.action != recovery_result.applied_action;

	chiaki_mutex_lock(&g_runtime_lock);
	if(g_runtime_last_recovery_result_valid)
		recovery_result = g_runtime_last_recovery_result;
	chiaki_mutex_unlock(&g_runtime_lock);

	return chiaki_headless_runtime_build_playback_continuity_status(
		&snapshot,
		&recovery_result,
		&recovery_status,
		out_status);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_playback_continuity_status_compat(
	uint32_t readiness_timeout_ms,
	void *out_status_buf,
	size_t out_status_size)
{
	if(!out_status_buf || out_status_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimePlaybackContinuityStatus out_status = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_playback_continuity_status(
		readiness_timeout_ms,
		&out_status);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	size_t copy_size = out_status_size < sizeof(out_status) ? out_status_size : sizeof(out_status);
	memcpy(out_status_buf, &out_status, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_video_frame_metadata_size(void)
{
	return sizeof(ChiakiHeadlessRuntimeVideoFrameMetadata);
}

CHIAKI_EXPORT void chiaki_headless_runtime_video_frame_metadata_init(
	ChiakiHeadlessRuntimeVideoFrameMetadata *metadata)
{
	if(!metadata)
		return;
	memset(metadata, 0, sizeof(*metadata));
	metadata->api_version = chiaki_headless_api_version();
	metadata->format = CHIAKI_HEADLESS_VIDEO_FORMAT_UNKNOWN;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_video_frame_metadata(
	ChiakiHeadlessRuntimeVideoFrameMetadata *out_metadata)
{
	if(!out_metadata)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	ChiakiHeadlessSession *runtime_session = g_runtime_session;
	if(!runtime_session)
	{
		chiaki_mutex_unlock(&g_runtime_lock);
		return CHIAKI_ERR_INVALID_DATA;
	}

	ChiakiHeadlessRuntimeVideoFrameMetadata metadata = {0};
	metadata.api_version = chiaki_headless_api_version();

	chiaki_mutex_lock(&runtime_session->cb_mutex);
	metadata.runtime_session_active = !runtime_session->stopped;
	metadata.has_video_frame = runtime_session->has_video_frame_metadata;
	metadata.format = runtime_session->last_video_format;
	metadata.width = runtime_session->last_video_width;
	metadata.height = runtime_session->last_video_height;
	metadata.pts_seconds = runtime_session->last_video_pts_seconds;
	metadata.duration_seconds = runtime_session->last_video_duration_seconds;
	metadata.frames_lost = runtime_session->last_video_frames_lost;
	metadata.frame_recovered = runtime_session->last_video_frame_recovered;
	metadata.monotonic_time_us = runtime_session->last_video_monotonic_us;
	metadata.video_frame_count = runtime_session->video_frame_count;
	metadata.video_decode_lost_frames = runtime_session->video_decode_lost_frames;
	metadata.video_decode_recovered_frames = runtime_session->video_decode_recovered_frames;
	metadata.video_decode_gap_event_count = runtime_session->video_decode_gap_event_count;
	chiaki_mutex_unlock(&runtime_session->cb_mutex);

	chiaki_mutex_unlock(&g_runtime_lock);
	*out_metadata = metadata;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_video_frame_metadata_compat(
	void *out_metadata_buf,
	size_t out_metadata_size)
{
	if(!out_metadata_buf || out_metadata_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeVideoFrameMetadata out_metadata = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_video_frame_metadata(&out_metadata);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	size_t copy_size = out_metadata_size < sizeof(out_metadata) ? out_metadata_size : sizeof(out_metadata);
	memcpy(out_metadata_buf, &out_metadata, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_video_frame_poll_size(void)
{
	return sizeof(ChiakiHeadlessRuntimeVideoFramePoll);
}

CHIAKI_EXPORT void chiaki_headless_runtime_video_frame_poll_init(
	ChiakiHeadlessRuntimeVideoFramePoll *frame_poll)
{
	if(!frame_poll)
		return;
	memset(frame_poll, 0, sizeof(*frame_poll));
	frame_poll->api_version = chiaki_headless_api_version();
	chiaki_headless_runtime_video_frame_metadata_init(&frame_poll->metadata);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_poll_video_frame(
	ChiakiHeadlessRuntimeVideoFramePoll *inout_frame_poll)
{
	if(!inout_frame_poll)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	ChiakiHeadlessSession *runtime_session = g_runtime_session;
	if(!runtime_session)
	{
		chiaki_mutex_unlock(&g_runtime_lock);
		return CHIAKI_ERR_INVALID_DATA;
	}

	ChiakiHeadlessRuntimeVideoFramePoll out = {0};
	out.api_version = chiaki_headless_api_version();
	chiaki_headless_runtime_video_frame_metadata_init(&out.metadata);
	ChiakiErrorCode result = CHIAKI_ERR_SUCCESS;

	chiaki_mutex_lock(&runtime_session->cb_mutex);
	out.metadata.runtime_session_active = !runtime_session->stopped;
	out.metadata.has_video_frame = runtime_session->has_video_frame_metadata;
	out.metadata.format = runtime_session->last_video_format;
	out.metadata.width = runtime_session->last_video_width;
	out.metadata.height = runtime_session->last_video_height;
	out.metadata.pts_seconds = runtime_session->last_video_pts_seconds;
	out.metadata.duration_seconds = runtime_session->last_video_duration_seconds;
	out.metadata.frames_lost = runtime_session->last_video_frames_lost;
	out.metadata.frame_recovered = runtime_session->last_video_frame_recovered;
	out.metadata.monotonic_time_us = runtime_session->last_video_monotonic_us;
	out.metadata.video_frame_count = runtime_session->video_frame_count;
	out.metadata.video_decode_lost_frames = runtime_session->video_decode_lost_frames;
	out.metadata.video_decode_recovered_frames = runtime_session->video_decode_recovered_frames;
	out.metadata.video_decode_gap_event_count = runtime_session->video_decode_gap_event_count;
	out.plane_count = runtime_session->last_video_plane_count <= 4 ? runtime_session->last_video_plane_count : 4;
	for(uint8_t i = 0; i < 4; i++)
		out.strides[i] = runtime_session->last_video_strides[i];

	/* If frame metadata is not populated, avoid surfacing stale plane info. */
	if(!out.metadata.has_video_frame)
	{
		out.plane_count = 0;
		for(uint8_t i = 0; i < 4; i++)
		{
			out.plane_sizes[i] = 0;
			out.strides[i] = 0;
		}
	}

	/* First pass: publish required plane sizes and validate host buffers.
	 * Avoid partial copies when any plane is undersized. */
	for(uint8_t i = 0; i < out.plane_count && i < 4; i++)
	{
		size_t required = runtime_session->last_video_plane_sizes[i];
		out.plane_sizes[i] = required;
		if(required == 0)
			continue;
		if(!inout_frame_poll->planes[i] || inout_frame_poll->plane_sizes[i] < required)
			result = CHIAKI_ERR_BUF_TOO_SMALL;
	}

	if(result == CHIAKI_ERR_SUCCESS)
	{
		for(uint8_t i = 0; i < out.plane_count && i < 4; i++)
		{
			size_t required = out.plane_sizes[i];
			if(required == 0)
				continue;
			memcpy(inout_frame_poll->planes[i], runtime_session->last_video_planes[i], required);
		}
	}
	chiaki_mutex_unlock(&runtime_session->cb_mutex);
	chiaki_mutex_unlock(&g_runtime_lock);

	out.planes[0] = inout_frame_poll->planes[0];
	out.planes[1] = inout_frame_poll->planes[1];
	out.planes[2] = inout_frame_poll->planes[2];
	out.planes[3] = inout_frame_poll->planes[3];
	*inout_frame_poll = out;
	return result;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_poll_video_frame_compat(
	void *inout_frame_poll_buf,
	size_t inout_frame_poll_size)
{
	if(!inout_frame_poll_buf || inout_frame_poll_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	if(inout_frame_poll_size < sizeof(ChiakiHeadlessRuntimeVideoFramePoll))
		return CHIAKI_ERR_BUF_TOO_SMALL;
	return chiaki_headless_runtime_poll_video_frame(
		(ChiakiHeadlessRuntimeVideoFramePoll *)inout_frame_poll_buf);
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_audio_sink_diagnostics_size(void)
{
	return sizeof(ChiakiHeadlessRuntimeAudioSinkDiagnostics);
}

CHIAKI_EXPORT void chiaki_headless_runtime_audio_sink_diagnostics_init(
	ChiakiHeadlessRuntimeAudioSinkDiagnostics *diagnostics)
{
	if(!diagnostics)
		return;
	memset(diagnostics, 0, sizeof(*diagnostics));
	diagnostics->api_version = chiaki_headless_api_version();
	diagnostics->format = CHIAKI_HEADLESS_AUDIO_FORMAT_UNKNOWN;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_audio_sink_diagnostics(
	ChiakiHeadlessRuntimeAudioSinkDiagnostics *out_diagnostics)
{
	if(!out_diagnostics)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	ChiakiHeadlessSession *runtime_session = g_runtime_session;
	ChiakiHeadlessRuntimeAudioSinkConfig runtime_config = g_runtime_audio_sink_config;
	bool runtime_config_set = g_runtime_audio_sink_config_set;
	ChiakiHeadlessRuntimeAudioSinkDiagnostics diagnostics = {0};
	chiaki_headless_runtime_audio_sink_diagnostics_init(&diagnostics);
	diagnostics.sink_enabled = runtime_config_set && runtime_config.enabled;

	if(runtime_session)
	{
		chiaki_mutex_lock(&runtime_session->cb_mutex);
		diagnostics.runtime_session_active = !runtime_session->stopped;
		diagnostics.sink_enabled = runtime_session->runtime_audio_sink_enabled;
		diagnostics.sink_started = runtime_session->runtime_audio_sink_started;
		diagnostics.channels = runtime_session->audio_channels;
		diagnostics.sample_rate = runtime_session->audio_rate;
		diagnostics.format = CHIAKI_HEADLESS_AUDIO_FORMAT_S16;
		diagnostics.submitted_frame_count = runtime_session->runtime_audio_sink_submitted_frame_count;
		diagnostics.submitted_byte_count = runtime_session->runtime_audio_sink_submitted_byte_count;
		diagnostics.dropped_frame_count = runtime_session->runtime_audio_sink_dropped_frame_count;
		diagnostics.underrun_count = runtime_session->runtime_audio_sink_underrun_count;
		diagnostics.start_count = runtime_session->runtime_audio_sink_start_count;
		diagnostics.stop_count = runtime_session->runtime_audio_sink_stop_count;
		diagnostics.last_submit_monotonic_us = runtime_session->runtime_audio_sink_last_submit_monotonic_us;
		diagnostics.legacy_callback_frame_count =
			runtime_session->runtime_audio_sink_legacy_callback_frame_count;
		diagnostics.suppressed_legacy_callback_frame_count =
			runtime_session->runtime_audio_sink_suppressed_legacy_callback_frame_count;
		chiaki_mutex_unlock(&runtime_session->cb_mutex);
	}

	chiaki_mutex_unlock(&g_runtime_lock);
	*out_diagnostics = diagnostics;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_audio_sink_diagnostics_compat(
	void *out_diagnostics_buf,
	size_t out_diagnostics_size)
{
	if(!out_diagnostics_buf || out_diagnostics_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeAudioSinkDiagnostics diagnostics = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_audio_sink_diagnostics(&diagnostics);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	size_t copy_size = out_diagnostics_size < sizeof(diagnostics) ? out_diagnostics_size : sizeof(diagnostics);
	memcpy(out_diagnostics_buf, &diagnostics, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_audio_sink_report_underrun(
	uint64_t underrun_count)
{
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	ChiakiHeadlessSession *runtime_session = g_runtime_session;
	chiaki_mutex_unlock(&g_runtime_lock);
	if(!runtime_session)
		return CHIAKI_ERR_INVALID_DATA;

	chiaki_mutex_lock(&runtime_session->cb_mutex);
	runtime_session->runtime_audio_sink_underrun_count += underrun_count;
	chiaki_mutex_unlock(&runtime_session->cb_mutex);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_fixture_eval(
	const ChiakiHeadlessRuntimeRecoveryParityFixtureExpected *expected_markers,
	const ChiakiHeadlessRuntimeRecoverySimulationReport *actual_reports,
	size_t scenario_count,
	ChiakiHeadlessRuntimeRecoveryParityFixtureResult *out_result)
{
	if(!expected_markers || !actual_reports || scenario_count == 0 || !out_result)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryParityFixtureResult result = {0};
	result.api_version = chiaki_headless_api_version();
	result.scenario_count = scenario_count;

	for(size_t i = 0; i < scenario_count; i++)
	{
		bool pass = true;
		if(expected_markers[i].expected_idr_action_count != actual_reports[i].idr_action_count)
			pass = false;
		if(expected_markers[i].expected_stop_action_count != actual_reports[i].stop_action_count)
			pass = false;
		if(expected_markers[i].expected_none_action_count != actual_reports[i].none_action_count)
			pass = false;
		if(expected_markers[i].validate_first_none_step_index
			&& expected_markers[i].expected_first_none_step_index != actual_reports[i].first_none_step_index)
			pass = false;

		if(pass)
			result.pass_count++;
		else
			result.fail_count++;
	}

	*out_result = result;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_fixture_eval_compat(
	const void *expected_markers_buf,
	size_t scenario_count,
	size_t expected_marker_stride,
	size_t expected_marker_size,
	const void *actual_reports_buf,
	size_t actual_report_stride,
	size_t actual_report_size,
	void *out_result_buf,
	size_t out_result_size)
{
	if(!expected_markers_buf || scenario_count == 0 || expected_marker_stride == 0
		|| expected_marker_size == 0 || !actual_reports_buf || actual_report_stride == 0
		|| actual_report_size == 0 || !out_result_buf || out_result_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryParityFixtureExpected *expected_markers =
		calloc(scenario_count, sizeof(*expected_markers));
	ChiakiHeadlessRuntimeRecoverySimulationReport *actual_reports =
		calloc(scenario_count, sizeof(*actual_reports));
	if(!expected_markers || !actual_reports)
	{
		free(expected_markers);
		free(actual_reports);
		return CHIAKI_ERR_MEMORY;
	}

	for(size_t i = 0; i < scenario_count; i++)
	{
		const uint8_t *expected_src = (const uint8_t *)expected_markers_buf + i * expected_marker_stride;
		const uint8_t *report_src = (const uint8_t *)actual_reports_buf + i * actual_report_stride;
		size_t expected_n = expected_marker_size < sizeof(expected_markers[i])
			? expected_marker_size
			: sizeof(expected_markers[i]);
		size_t report_n = actual_report_size < sizeof(actual_reports[i])
			? actual_report_size
			: sizeof(actual_reports[i]);
		memcpy(&expected_markers[i], expected_src, expected_n);
		memcpy(&actual_reports[i], report_src, report_n);
	}

	ChiakiHeadlessRuntimeRecoveryParityFixtureResult result = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_fixture_eval(
		expected_markers,
		actual_reports,
		scenario_count,
		&result);
	free(expected_markers);
	free(actual_reports);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	size_t copy_size = out_result_size < sizeof(result) ? out_result_size : sizeof(result);
	memcpy(out_result_buf, &result, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_fixture_export(
	const char *scenario_label,
	const ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryStatus *final_status,
	bool validate_first_none_step_index,
	ChiakiHeadlessRuntimeRecoveryParityFixtureExpected *out_expected,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report)
{
	if(!step_outputs || step_count == 0 || !final_status || !out_expected || !out_report)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoverySimulationReport report = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_simulation_report_from_outputs(
		step_outputs,
		step_count,
		final_status,
		&report);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiHeadlessRuntimeRecoveryParityFixtureExpected expected = {0};
	expected.api_version = chiaki_headless_api_version();
	expected.scenario_label = scenario_label;
	expected.expected_idr_action_count = report.idr_action_count;
	expected.expected_stop_action_count = report.stop_action_count;
	expected.expected_none_action_count = report.none_action_count;
	expected.validate_first_none_step_index = validate_first_none_step_index;
	expected.expected_first_none_step_index = report.first_none_step_index;

	*out_expected = expected;
	*out_report = report;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_fixture_export_compat(
	const char *scenario_label,
	const void *step_outputs_buf,
	size_t step_count,
	size_t step_output_stride,
	size_t step_output_size,
	const void *final_status_buf,
	size_t final_status_size,
	bool validate_first_none_step_index,
	void *out_expected_buf,
	size_t out_expected_size,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!step_outputs_buf || step_count == 0 || step_output_stride == 0 || step_output_size == 0
		|| !final_status_buf || final_status_size == 0 || !out_expected_buf || out_expected_size == 0
		|| !out_report_buf || out_report_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryStatus final_status = {0};
	chiaki_headless_runtime_recovery_status_init(&final_status);
	size_t copy_size = final_status_size < sizeof(final_status) ? final_status_size : sizeof(final_status);
	memcpy(&final_status, final_status_buf, copy_size);

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *full_outputs =
		calloc(step_count, sizeof(*full_outputs));
	if(!full_outputs)
		return CHIAKI_ERR_MEMORY;
	for(size_t i = 0; i < step_count; i++)
	{
		const uint8_t *src = (const uint8_t *)step_outputs_buf + i * step_output_stride;
		size_t n = step_output_size < sizeof(full_outputs[i]) ? step_output_size : sizeof(full_outputs[i]);
		memcpy(&full_outputs[i], src, n);
	}

	ChiakiHeadlessRuntimeRecoveryParityFixtureExpected expected = {0};
	ChiakiHeadlessRuntimeRecoverySimulationReport report = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_fixture_export(
		scenario_label,
		full_outputs,
		step_count,
		&final_status,
		validate_first_none_step_index,
		&expected,
		&report);
	free(full_outputs);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	copy_size = out_expected_size < sizeof(expected) ? out_expected_size : sizeof(expected);
	memcpy(out_expected_buf, &expected, copy_size);
	copy_size = out_report_size < sizeof(report) ? out_report_size : sizeof(report);
	memcpy(out_report_buf, &report, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_smoke(
	const ChiakiHeadlessRuntimeRecoveryParityFixtureExpected *expected_markers,
	const ChiakiHeadlessRuntimeRecoverySimulationReport *actual_reports,
	size_t scenario_count,
	ChiakiHeadlessRuntimeRecoveryParitySmokeResult *out_result)
{
	if(!expected_markers || !actual_reports || scenario_count == 0 || !out_result)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryParitySmokeResult result = {0};
	result.api_version = chiaki_headless_api_version();
	result.scenario_count = scenario_count;
	result.first_failure_index = SIZE_MAX;

	for(size_t i = 0; i < scenario_count; i++)
	{
		bool pass = true;
		if(expected_markers[i].expected_idr_action_count != actual_reports[i].idr_action_count)
			pass = false;
		if(expected_markers[i].expected_stop_action_count != actual_reports[i].stop_action_count)
			pass = false;
		if(expected_markers[i].expected_none_action_count != actual_reports[i].none_action_count)
			pass = false;
		if(expected_markers[i].validate_first_none_step_index
			&& expected_markers[i].expected_first_none_step_index != actual_reports[i].first_none_step_index)
			pass = false;

		if(pass)
		{
			result.pass_count++;
		}
		else
		{
			result.fail_count++;
			if(result.first_failure_index == SIZE_MAX)
				result.first_failure_index = i;
		}
	}

	*out_result = result;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_smoke_compat(
	const void *expected_markers_buf,
	size_t scenario_count,
	size_t expected_marker_stride,
	size_t expected_marker_size,
	const void *actual_reports_buf,
	size_t actual_report_stride,
	size_t actual_report_size,
	void *out_result_buf,
	size_t out_result_size)
{
	if(!expected_markers_buf || scenario_count == 0 || expected_marker_stride == 0
		|| expected_marker_size == 0 || !actual_reports_buf || actual_report_stride == 0
		|| actual_report_size == 0 || !out_result_buf || out_result_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryParityFixtureExpected *expected_markers =
		calloc(scenario_count, sizeof(*expected_markers));
	ChiakiHeadlessRuntimeRecoverySimulationReport *actual_reports =
		calloc(scenario_count, sizeof(*actual_reports));
	if(!expected_markers || !actual_reports)
	{
		free(expected_markers);
		free(actual_reports);
		return CHIAKI_ERR_MEMORY;
	}

	for(size_t i = 0; i < scenario_count; i++)
	{
		const uint8_t *expected_src = (const uint8_t *)expected_markers_buf + i * expected_marker_stride;
		const uint8_t *report_src = (const uint8_t *)actual_reports_buf + i * actual_report_stride;
		size_t expected_n = expected_marker_size < sizeof(expected_markers[i])
			? expected_marker_size
			: sizeof(expected_markers[i]);
		size_t report_n = actual_report_size < sizeof(actual_reports[i])
			? actual_report_size
			: sizeof(actual_reports[i]);
		memcpy(&expected_markers[i], expected_src, expected_n);
		memcpy(&actual_reports[i], report_src, report_n);
	}

	ChiakiHeadlessRuntimeRecoveryParitySmokeResult result = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_smoke(
		expected_markers,
		actual_reports,
		scenario_count,
		&result);
	free(expected_markers);
	free(actual_reports);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	size_t copy_size = out_result_size < sizeof(result) ? out_result_size : sizeof(result);
	memcpy(out_result_buf, &result, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_smoke_runner(
	const ChiakiHeadlessRuntimeRecoveryParityFixtureExpected *expected_markers,
	const ChiakiHeadlessRuntimeRecoverySimulationReport *actual_reports,
	size_t scenario_count,
	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult *out_result)
{
	if(!expected_markers || !actual_reports || scenario_count == 0 || !out_result)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult result = {0};
	result.api_version = chiaki_headless_api_version();

	ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_fixture_eval(
		expected_markers,
		actual_reports,
		scenario_count,
		&result.fixture_result);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	err = chiaki_headless_runtime_recovery_parity_smoke(
		expected_markers,
		actual_reports,
		scenario_count,
		&result.smoke_result);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	*out_result = result;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_smoke_runner_compat(
	const void *expected_markers_buf,
	size_t scenario_count,
	size_t expected_marker_stride,
	size_t expected_marker_size,
	const void *actual_reports_buf,
	size_t actual_report_stride,
	size_t actual_report_size,
	void *out_result_buf,
	size_t out_result_size)
{
	if(!expected_markers_buf || scenario_count == 0 || expected_marker_stride == 0
		|| expected_marker_size == 0 || !actual_reports_buf || actual_report_stride == 0
		|| actual_report_size == 0 || !out_result_buf || out_result_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryParityFixtureExpected *expected_markers =
		calloc(scenario_count, sizeof(*expected_markers));
	ChiakiHeadlessRuntimeRecoverySimulationReport *actual_reports =
		calloc(scenario_count, sizeof(*actual_reports));
	if(!expected_markers || !actual_reports)
	{
		free(expected_markers);
		free(actual_reports);
		return CHIAKI_ERR_MEMORY;
	}

	for(size_t i = 0; i < scenario_count; i++)
	{
		const uint8_t *expected_src = (const uint8_t *)expected_markers_buf + i * expected_marker_stride;
		const uint8_t *report_src = (const uint8_t *)actual_reports_buf + i * actual_report_stride;
		size_t expected_n = expected_marker_size < sizeof(expected_markers[i])
			? expected_marker_size
			: sizeof(expected_markers[i]);
		size_t report_n = actual_report_size < sizeof(actual_reports[i])
			? actual_report_size
			: sizeof(actual_reports[i]);
		memcpy(&expected_markers[i], expected_src, expected_n);
		memcpy(&actual_reports[i], report_src, report_n);
	}

	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult result = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_smoke_runner(
		expected_markers,
		actual_reports,
		scenario_count,
		&result);
	free(expected_markers);
	free(actual_reports);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	size_t copy_size = out_result_size < sizeof(result) ? out_result_size : sizeof(result);
	memcpy(out_result_buf, &result, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

static const ChiakiHeadlessRuntimeRecoveryParityFixtureExpected g_runtime_recovery_baseline_expected[] = {
	{
		.api_version = 0,
		.scenario_label = "baseline_degraded_then_ready",
		.expected_idr_action_count = 1,
		.expected_stop_action_count = 0,
		.expected_none_action_count = 2,
		.validate_first_none_step_index = true,
		.expected_first_none_step_index = 1,
	},
	{
		.api_version = 0,
		.scenario_label = "baseline_steady_ready",
		.expected_idr_action_count = 0,
		.expected_stop_action_count = 0,
		.expected_none_action_count = 3,
		.validate_first_none_step_index = true,
		.expected_first_none_step_index = 0,
	},
	{
		.api_version = 0,
		.scenario_label = "baseline_terminal_stop",
		.expected_idr_action_count = 0,
		.expected_stop_action_count = 1,
		.expected_none_action_count = 1,
		.validate_first_none_step_index = true,
		.expected_first_none_step_index = 0,
	},
};

static const ChiakiHeadlessRuntimeRecoverySimulationReport g_runtime_recovery_baseline_reports[] = {
	{
		.api_version = 0,
		.step_count = 3,
		.idr_action_count = 1,
		.stop_action_count = 0,
		.none_action_count = 2,
		.first_none_step_index = 1,
	},
	{
		.api_version = 0,
		.step_count = 3,
		.idr_action_count = 0,
		.stop_action_count = 0,
		.none_action_count = 3,
		.first_none_step_index = 0,
	},
	{
		.api_version = 0,
		.step_count = 2,
		.idr_action_count = 0,
		.stop_action_count = 1,
		.none_action_count = 1,
		.first_none_step_index = 0,
	},
};

static size_t headless_runtime_recovery_parity_baseline_scenario_count(void)
{
	return sizeof(g_runtime_recovery_baseline_expected) / sizeof(g_runtime_recovery_baseline_expected[0]);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_scenario_count(
	size_t *out_scenario_count)
{
	if(!out_scenario_count)
		return CHIAKI_ERR_INVALID_DATA;
	*out_scenario_count = headless_runtime_recovery_parity_baseline_scenario_count();
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_scenario_count_compat(
	void *out_scenario_count_buf,
	size_t out_scenario_count_size)
{
	if(!out_scenario_count_buf || out_scenario_count_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	size_t scenario_count = headless_runtime_recovery_parity_baseline_scenario_count();
	size_t copy_size = out_scenario_count_size < sizeof(scenario_count) ? out_scenario_count_size : sizeof(scenario_count);
	memcpy(out_scenario_count_buf, &scenario_count, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_scenario_label(
	size_t scenario_index,
	const char **out_scenario_label)
{
	if(!out_scenario_label)
		return CHIAKI_ERR_INVALID_DATA;
	if(scenario_index >= headless_runtime_recovery_parity_baseline_scenario_count())
		return CHIAKI_ERR_INVALID_DATA;
	*out_scenario_label = g_runtime_recovery_baseline_expected[scenario_index].scenario_label;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_scenario_label_compat(
	size_t scenario_index,
	char *out_scenario_label,
	size_t out_scenario_label_size)
{
	if(!out_scenario_label || out_scenario_label_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	const char *label = NULL;
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_baseline_scenario_label(
		scenario_index,
		&label);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t need = strlen(label) + 1;
	if(out_scenario_label_size < need)
		return CHIAKI_ERR_BUF_TOO_SMALL;
	memcpy(out_scenario_label, label, need);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_execution_details(
	ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail *out_details,
	size_t detail_count,
	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult *out_aggregate_result)
{
	if(!out_details || !out_aggregate_result || detail_count == 0)
		return CHIAKI_ERR_INVALID_DATA;

	size_t scenario_count = headless_runtime_recovery_parity_baseline_scenario_count();
	if(detail_count < scenario_count)
		return CHIAKI_ERR_BUF_TOO_SMALL;

	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult aggregate = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_smoke_runner(
		g_runtime_recovery_baseline_expected,
		g_runtime_recovery_baseline_reports,
		scenario_count,
		&aggregate);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	for(size_t i = 0; i < scenario_count; i++)
	{
		const ChiakiHeadlessRuntimeRecoveryParityFixtureExpected *expected =
			&g_runtime_recovery_baseline_expected[i];
		const ChiakiHeadlessRuntimeRecoverySimulationReport *actual =
			&g_runtime_recovery_baseline_reports[i];
		ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail detail = {0};
		detail.api_version = chiaki_headless_api_version();
		detail.scenario_index = i;
		detail.pass = true;

		if(expected->scenario_label)
		{
			strncpy(
				detail.scenario_label,
				expected->scenario_label,
				sizeof(detail.scenario_label) - 1);
			detail.scenario_label[sizeof(detail.scenario_label) - 1] = '\0';
		}
		else
		{
			detail.scenario_label[0] = '\0';
		}

		detail.expected_idr_action_count = expected->expected_idr_action_count;
		detail.actual_idr_action_count = actual->idr_action_count;
		detail.mismatch_idr_action_count =
			expected->expected_idr_action_count != actual->idr_action_count;

		detail.expected_stop_action_count = expected->expected_stop_action_count;
		detail.actual_stop_action_count = actual->stop_action_count;
		detail.mismatch_stop_action_count =
			expected->expected_stop_action_count != actual->stop_action_count;

		detail.expected_none_action_count = expected->expected_none_action_count;
		detail.actual_none_action_count = actual->none_action_count;
		detail.mismatch_none_action_count =
			expected->expected_none_action_count != actual->none_action_count;

		detail.validate_first_none_step_index = expected->validate_first_none_step_index;
		detail.expected_first_none_step_index = expected->expected_first_none_step_index;
		detail.actual_first_none_step_index = actual->first_none_step_index;
		detail.mismatch_first_none_step_index = expected->validate_first_none_step_index
			&& expected->expected_first_none_step_index != actual->first_none_step_index;

		if(detail.mismatch_idr_action_count
			|| detail.mismatch_stop_action_count
			|| detail.mismatch_none_action_count
			|| detail.mismatch_first_none_step_index)
			detail.pass = false;

		out_details[i] = detail;
	}

	*out_aggregate_result = aggregate;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_execution_details_compat(
	void *out_details_buf,
	size_t detail_count,
	size_t detail_stride,
	size_t detail_size,
	void *out_aggregate_result_buf,
	size_t out_aggregate_result_size)
{
	if(!out_details_buf || detail_count == 0 || detail_stride == 0 || detail_size == 0
		|| !out_aggregate_result_buf || out_aggregate_result_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	size_t scenario_count = headless_runtime_recovery_parity_baseline_scenario_count();
	if(detail_count < scenario_count)
		return CHIAKI_ERR_BUF_TOO_SMALL;

	ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail *details =
		calloc(scenario_count, sizeof(*details));
	if(!details)
		return CHIAKI_ERR_MEMORY;

	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult aggregate = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_baseline_execution_details(
		details,
		scenario_count,
		&aggregate);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(details);
		return err;
	}

	for(size_t i = 0; i < scenario_count; i++)
	{
		uint8_t *dst = (uint8_t *)out_details_buf + i * detail_stride;
		size_t n = detail_size < sizeof(details[i]) ? detail_size : sizeof(details[i]);
		memcpy(dst, &details[i], n);
	}
	free(details);

	size_t copy_size = out_aggregate_result_size < sizeof(aggregate)
		? out_aggregate_result_size
		: sizeof(aggregate);
	memcpy(out_aggregate_result_buf, &aggregate, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_core_diagnostics(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryConfig *config,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail *out_baseline_details,
	size_t out_baseline_detail_count,
	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineStep *out_timeline_steps,
	ChiakiHeadlessRuntimeRecoveryCoreDiagnosticsSummary *out_summary)
{
	if(!step_inputs || step_count == 0 || !config || !initial_status || !out_baseline_details
		|| !out_timeline_steps || !out_summary)
		return CHIAKI_ERR_INVALID_DATA;

	size_t baseline_count = headless_runtime_recovery_parity_baseline_scenario_count();
	if(out_baseline_detail_count < baseline_count)
		return CHIAKI_ERR_BUF_TOO_SMALL;

	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult aggregate = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_baseline_execution_details(
		out_baseline_details,
		out_baseline_detail_count,
		&aggregate);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineSummary timeline_summary = {0};
	err = chiaki_headless_runtime_simulate_recovery_auto_loop_timeline(
		step_inputs,
		step_count,
		config,
		initial_status,
		out_timeline_steps,
		&timeline_summary);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	memset(out_summary, 0, sizeof(*out_summary));
	out_summary->api_version = chiaki_headless_api_version();
	out_summary->baseline_scenario_count = baseline_count;
	out_summary->baseline_aggregate_result = aggregate;
	out_summary->auto_loop_timeline_summary = timeline_summary;
	out_summary->e3_master_gate_enabled = chiaki_media_e3_enabled();
	out_summary->e3_audio_gate_enabled = chiaki_media_e3_audio_enabled();
	out_summary->e3_sync_gate_enabled = chiaki_media_e3_sync_enabled();
	out_summary->e3_audio_effective =
		out_summary->e3_master_gate_enabled && out_summary->e3_audio_gate_enabled;
	out_summary->e3_sync_effective =
		out_summary->e3_master_gate_enabled && out_summary->e3_sync_gate_enabled;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_core_diagnostics_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const void *config_buf,
	size_t config_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *out_baseline_details_buf,
	size_t out_baseline_detail_count,
	size_t out_baseline_detail_stride,
	size_t out_baseline_detail_size,
	void *out_timeline_steps_buf,
	size_t out_timeline_step_stride,
	size_t out_timeline_step_size,
	void *out_summary_buf,
	size_t out_summary_size)
{
	if(!step_inputs || step_count == 0 || !config_buf || config_size == 0 || !initial_status_buf
		|| initial_status_size == 0 || !out_baseline_details_buf || out_baseline_detail_count == 0
		|| out_baseline_detail_stride == 0 || out_baseline_detail_size == 0
		|| !out_timeline_steps_buf || out_timeline_step_stride == 0 || out_timeline_step_size == 0
		|| !out_summary_buf || out_summary_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	chiaki_headless_runtime_recovery_config_init(&config);
	size_t copy_size = config_size < sizeof(config) ? config_size : sizeof(config);
	memcpy(&config, config_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryStatus initial_status = {0};
	chiaki_headless_runtime_recovery_status_init(&initial_status);
	copy_size = initial_status_size < sizeof(initial_status) ? initial_status_size : sizeof(initial_status);
	memcpy(&initial_status, initial_status_buf, copy_size);

	size_t baseline_count = headless_runtime_recovery_parity_baseline_scenario_count();
	if(out_baseline_detail_count < baseline_count)
		return CHIAKI_ERR_BUF_TOO_SMALL;

	ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail *baseline_details =
		calloc(out_baseline_detail_count, sizeof(*baseline_details));
	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineStep *timeline_steps =
		calloc(step_count, sizeof(*timeline_steps));
	if(!baseline_details || !timeline_steps)
	{
		free(baseline_details);
		free(timeline_steps);
		return CHIAKI_ERR_MEMORY;
	}

	ChiakiHeadlessRuntimeRecoveryCoreDiagnosticsSummary summary = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_core_diagnostics(
		step_inputs,
		step_count,
		&config,
		&initial_status,
		baseline_details,
		out_baseline_detail_count,
		timeline_steps,
		&summary);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(baseline_details);
		free(timeline_steps);
		return err;
	}

	for(size_t i = 0; i < baseline_count; i++)
	{
		uint8_t *dst = (uint8_t *)out_baseline_details_buf + i * out_baseline_detail_stride;
		size_t n = out_baseline_detail_size < sizeof(baseline_details[i])
			? out_baseline_detail_size
			: sizeof(baseline_details[i]);
		memcpy(dst, &baseline_details[i], n);
	}
	for(size_t i = 0; i < step_count; i++)
	{
		uint8_t *dst = (uint8_t *)out_timeline_steps_buf + i * out_timeline_step_stride;
		size_t n = out_timeline_step_size < sizeof(timeline_steps[i])
			? out_timeline_step_size
			: sizeof(timeline_steps[i]);
		memcpy(dst, &timeline_steps[i], n);
	}
	copy_size = out_summary_size < sizeof(summary) ? out_summary_size : sizeof(summary);
	memcpy(out_summary_buf, &summary, copy_size);

	free(baseline_details);
	free(timeline_steps);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_smoke(
	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult *out_result)
{
	if(!out_result)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult result = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_smoke_runner(
		g_runtime_recovery_baseline_expected,
		g_runtime_recovery_baseline_reports,
		headless_runtime_recovery_parity_baseline_scenario_count(),
		&result);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	*out_result = result;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_smoke_compat(
	void *out_result_buf,
	size_t out_result_size)
{
	if(!out_result_buf || out_result_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult result = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_baseline_smoke(&result);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	size_t copy_size = out_result_size < sizeof(result) ? out_result_size : sizeof(result);
	memcpy(out_result_buf, &result, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_capabilities_compat(
	void *out_capabilities_buf,
	size_t out_capabilities_size)
{
	if(!out_capabilities_buf || out_capabilities_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeCapabilities full = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_capabilities(&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	memset(out_capabilities_buf, 0, out_capabilities_size);
	size_t copy_size = out_capabilities_size < sizeof(full) ? out_capabilities_size : sizeof(full);
	memcpy(out_capabilities_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_external_video_capabilities_size(void)
{
	return sizeof(ChiakiHeadlessRuntimeExternalVideoCapabilities);
}

CHIAKI_EXPORT void chiaki_headless_runtime_external_video_capabilities_init(
	ChiakiHeadlessRuntimeExternalVideoCapabilities *capabilities)
{
	if(!capabilities)
		return;
	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->api_version = chiaki_headless_api_version();
	capabilities->abi_revision = kHeadlessExternalVideoAbiRevision;
	capabilities->min_external_video_capabilities_size =
		sizeof(ChiakiHeadlessRuntimeExternalVideoCapabilities);
	capabilities->supports_runtime_external_video_capabilities = true;
	capabilities->supports_runtime_external_video_capabilities_compat = true;
#if !defined(_WIN32)
	capabilities->supports_runtime_external_video_dmabuf = true;
#else
	capabilities->supports_runtime_external_video_dmabuf = false;
#endif
	capabilities->dmabuf_max_planes = 4;
	capabilities->dmabuf_includes_fd = true;
	capabilities->dmabuf_includes_pitch = true;
	capabilities->dmabuf_includes_offset = true;
	capabilities->dmabuf_includes_modifier = true;
	capabilities->dmabuf_includes_drm_format = true;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_external_video_capabilities(
	ChiakiHeadlessRuntimeExternalVideoCapabilities *out_capabilities)
{
	if(!out_capabilities)
		return CHIAKI_ERR_INVALID_DATA;
	chiaki_headless_runtime_external_video_capabilities_init(out_capabilities);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_external_video_capabilities_compat(
	void *out_capabilities_buf,
	size_t out_capabilities_size)
{
	if(!out_capabilities_buf || out_capabilities_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeExternalVideoCapabilities full = {0};
	ChiakiErrorCode err =
		chiaki_headless_runtime_get_external_video_capabilities(&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	memset(out_capabilities_buf, 0, out_capabilities_size);
	size_t copy_size =
		out_capabilities_size < sizeof(full) ? out_capabilities_size : sizeof(full);
	memcpy(out_capabilities_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode headless_runtime_build_diagnostics_snapshot_locked(
	ChiakiHeadlessSession *runtime_session,
	const ChiakiMediaHealthPolicy *policy,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot)
{
	if(!runtime_session || !out_snapshot)
		return CHIAKI_ERR_INVALID_DATA;

	chiaki_media_session_diagnostics_snapshot_init(out_snapshot);

	chiaki_mutex_lock(&runtime_session->cb_mutex);
	out_snapshot->stats.started = !runtime_session->stopped;
	if(runtime_session->stopped)
		out_snapshot->stats.state = CHIAKI_MEDIA_SESSION_STATE_STOPPED;
	else if(runtime_session->stopping)
		out_snapshot->stats.state = CHIAKI_MEDIA_SESSION_STATE_STOPPING;
	else if(runtime_session->ready_event_count > 0)
		out_snapshot->stats.state = CHIAKI_MEDIA_SESSION_STATE_RUNNING;
	else
		out_snapshot->stats.state = CHIAKI_MEDIA_SESSION_STATE_STARTING;

	out_snapshot->stats.video_frame_count = runtime_session->video_frame_count;
	out_snapshot->stats.audio_frame_count = runtime_session->audio_frame_count;
	out_snapshot->stats.packets_received = runtime_session->packets_received;
	out_snapshot->stats.packets_lost = runtime_session->packets_lost;
	out_snapshot->stats.measured_bitrate_kbps = runtime_session->measured_bitrate_kbps;
	out_snapshot->stats.video_decode_lost_frames = runtime_session->video_decode_lost_frames;
	out_snapshot->stats.video_decode_recovered_frames = runtime_session->video_decode_recovered_frames;
	out_snapshot->stats.video_decode_gap_event_count = runtime_session->video_decode_gap_event_count;
	out_snapshot->stats.event_count = runtime_session->event_count;
	out_snapshot->stats.ready_event_count = runtime_session->ready_event_count;
	out_snapshot->stats.stopped_event_count = runtime_session->stopped_event_count;
	out_snapshot->stats.error_event_count = runtime_session->error_event_count;
	out_snapshot->stats.last_event_monotonic_us = runtime_session->last_event_monotonic_us;
	out_snapshot->stats.last_event_type = runtime_session->last_event_type;

	out_snapshot->readiness.started = out_snapshot->stats.started;
	out_snapshot->readiness.state = out_snapshot->stats.state;
	out_snapshot->readiness.seen_video_frame = runtime_session->video_frame_count > 0;
	out_snapshot->readiness.seen_ready_event = runtime_session->ready_event_count > 0;
	out_snapshot->readiness.seen_terminal_event =
		runtime_session->error_event_count > 0 || runtime_session->stopped_event_count > 0;
	out_snapshot->readiness.video_frame_count = runtime_session->video_frame_count;
	out_snapshot->readiness.event_count = runtime_session->event_count;
	out_snapshot->readiness.ready_event_count = runtime_session->ready_event_count;
	out_snapshot->readiness.stopped_event_count = runtime_session->stopped_event_count;
	out_snapshot->readiness.error_event_count = runtime_session->error_event_count;
	out_snapshot->readiness.last_event_monotonic_us = runtime_session->last_event_monotonic_us;
	out_snapshot->readiness.last_event_type = runtime_session->last_event_type;
	chiaki_mutex_unlock(&runtime_session->cb_mutex);

	return chiaki_media_health_evaluate_readiness(
		&out_snapshot->readiness,
		policy,
		&out_snapshot->health);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_media_diagnostics_snapshot_with_profile_key(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot)
{
	(void)readiness_timeout_ms;
	if(!profile_key || !out_snapshot)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	const char *canonical = NULL;
	err = chiaki_media_health_policy_profile_key_normalize(profile_key, &canonical);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	ChiakiMediaHealthPolicyProfile profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	err = chiaki_media_health_policy_profile_from_key(canonical, &profile);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	ChiakiMediaHealthPolicy policy = {0};
	err = chiaki_media_health_policy_from_profile(profile, &policy);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	ChiakiHeadlessSession *runtime_session = g_runtime_session;
	if(!runtime_session)
	{
		chiaki_mutex_unlock(&g_runtime_lock);
		return CHIAKI_ERR_INVALID_DATA;
	}
	err = headless_runtime_build_diagnostics_snapshot_locked(
		runtime_session,
		&policy,
		out_snapshot);
	chiaki_mutex_unlock(&g_runtime_lock);
	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_media_diagnostics_snapshot(
	uint32_t readiness_timeout_ms,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot)
{
	return chiaki_headless_runtime_get_media_diagnostics_snapshot_with_profile_key(
		readiness_timeout_ms,
		"default",
		out_snapshot);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_media_diagnostics_snapshot_compat(
	uint32_t readiness_timeout_ms,
	void *out_snapshot_buf,
	size_t out_snapshot_size)
{
	if(!out_snapshot_buf || out_snapshot_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaSessionDiagnosticsSnapshot full = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_media_diagnostics_snapshot(
		readiness_timeout_ms,
		&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_snapshot_size < sizeof(full) ? out_snapshot_size : sizeof(full);
	memcpy(out_snapshot_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_media_diagnostics_snapshot_with_profile_key_compat(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	void *out_snapshot_buf,
	size_t out_snapshot_size)
{
	if(!profile_key || !out_snapshot_buf || out_snapshot_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaSessionDiagnosticsSnapshot full = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_media_diagnostics_snapshot_with_profile_key(
		readiness_timeout_ms,
		profile_key,
		&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_snapshot_size < sizeof(full) ? out_snapshot_size : sizeof(full);
	memcpy(out_snapshot_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode headless_runtime_get_effective_policy_overrides(
	HeadlessRuntimePolicyOverrides *out_policy,
	bool *out_has_policy)
{
	if(!out_policy || !out_has_policy)
		return CHIAKI_ERR_INVALID_DATA;
	HeadlessRuntimeConfig config = {0};
	ChiakiErrorCode err = headless_runtime_snapshot_config(&config);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	*out_policy = config.policy_overrides;
	*out_has_policy = config.policy_overrides_set;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_decision_with_profile_key(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	ChiakiHeadlessRuntimeRecoveryDecision *out_decision)
{
	if(!profile_key || !out_decision)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_media_diagnostics_snapshot_with_profile_key(
		readiness_timeout_ms,
		profile_key,
		&snapshot);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	HeadlessRuntimePolicyOverrides policy = {0};
	bool has_policy = false;
	err = headless_runtime_get_effective_policy_overrides(&policy, &has_policy);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiHeadlessRuntimeRecoveryDecision decision = {0};
	decision.api_version = chiaki_headless_api_version();
	decision.health_state = snapshot.health.health_state;
	decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
	decision.runtime_session_active = snapshot.stats.started;
	decision.effective_enable_idr_on_fec_failure = true;
	if(has_policy && policy.use_enable_idr_on_fec_failure)
		decision.effective_enable_idr_on_fec_failure = policy.enable_idr_on_fec_failure;
	decision.event_count = snapshot.stats.event_count;
	decision.error_event_count = snapshot.stats.error_event_count;
	decision.ready_event_count = snapshot.stats.ready_event_count;

	if(!decision.runtime_session_active || snapshot.stats.state == CHIAKI_MEDIA_SESSION_STATE_STOPPED)
		decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
	else if(snapshot.health.health_state == CHIAKI_MEDIA_HEALTH_TERMINAL)
		decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_STOP_RUNTIME;
	else if(
		snapshot.health.health_state == CHIAKI_MEDIA_HEALTH_DEGRADED &&
		decision.effective_enable_idr_on_fec_failure &&
		snapshot.stats.error_event_count > 0)
		decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR;

	*out_decision = decision;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_decision(
	uint32_t readiness_timeout_ms,
	ChiakiHeadlessRuntimeRecoveryDecision *out_decision)
{
	return chiaki_headless_runtime_get_recovery_decision_with_profile_key(
		readiness_timeout_ms,
		"default",
		out_decision);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_decision_with_profile_key_compat(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	void *out_decision_buf,
	size_t out_decision_size)
{
	if(!profile_key || !out_decision_buf || out_decision_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiHeadlessRuntimeRecoveryDecision full = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_recovery_decision_with_profile_key(
		readiness_timeout_ms,
		profile_key,
		&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_decision_size < sizeof(full) ? out_decision_size : sizeof(full);
	memcpy(out_decision_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_decision_compat(
	uint32_t readiness_timeout_ms,
	void *out_decision_buf,
	size_t out_decision_size)
{
	if(!out_decision_buf || out_decision_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	return chiaki_headless_runtime_get_recovery_decision_with_profile_key_compat(
		readiness_timeout_ms,
		"default",
		out_decision_buf,
		out_decision_size);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_apply_recovery_decision(
	const ChiakiHeadlessRuntimeRecoveryDecision *decision,
	ChiakiHeadlessRuntimeRecoveryAction *out_applied_action)
{
	if(!decision)
		return CHIAKI_ERR_INVALID_DATA;

	if(out_applied_action)
		*out_applied_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;

	switch(decision->action)
	{
		case CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE:
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR:
		{
			ChiakiErrorCode err = chiaki_headless_runtime_request_idr();
			if(err != CHIAKI_ERR_SUCCESS)
				return err;
			if(out_applied_action)
				*out_applied_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR;
			return CHIAKI_ERR_SUCCESS;
		}
		case CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_STOP_RUNTIME:
		{
			ChiakiErrorCode err = chiaki_headless_runtime_cloud_stop();
			if(err != CHIAKI_ERR_SUCCESS)
				return err;
			if(out_applied_action)
				*out_applied_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_STOP_RUNTIME;
			return CHIAKI_ERR_SUCCESS;
		}
		default:
			return CHIAKI_ERR_INVALID_DATA;
	}
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_apply_recovery_decision_compat(
	const void *decision_buf,
	size_t decision_size,
	void *out_applied_action_buf,
	size_t out_applied_action_size)
{
	if(!decision_buf || decision_size == 0 || !out_applied_action_buf || out_applied_action_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiHeadlessRuntimeRecoveryDecision decision = {0};
	size_t copy_size = decision_size < sizeof(decision) ? decision_size : sizeof(decision);
	memcpy(&decision, decision_buf, copy_size);
	ChiakiHeadlessRuntimeRecoveryAction applied = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
	ChiakiErrorCode err = chiaki_headless_runtime_apply_recovery_decision(&decision, &applied);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	memset(out_applied_action_buf, 0, out_applied_action_size);
	copy_size = out_applied_action_size < sizeof(applied) ? out_applied_action_size : sizeof(applied);
	memcpy(out_applied_action_buf, &applied, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

static void headless_runtime_finalize_recovery_result(
	ChiakiHeadlessRuntimeRecoveryResult *result)
{
	if(!result)
		return;
	result->apply_succeeded = result->apply_error == CHIAKI_ERR_SUCCESS;
	result->action_transitioned = result->decision.action != result->applied_action;
	result->recover_monotonic_us = chiaki_time_now_monotonic_us();
}

static void headless_runtime_record_recover_result_locked(
	const ChiakiHeadlessRuntimeRecoveryResult *result)
{
	if(!result)
		return;
	g_runtime_recovery_last_recommended_action = result->decision.action;
	g_runtime_recovery_last_applied_action = result->applied_action;
	g_runtime_recovery_attempt_count++;
	g_runtime_last_recovery_result = *result;
	g_runtime_last_recovery_result_valid = true;
	if(result->apply_succeeded)
		g_runtime_recovery_success_count++;
	else
		g_runtime_recovery_failure_count++;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_with_profile_key(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	ChiakiHeadlessRuntimeRecoveryAction *out_applied_action)
{
	if(!profile_key)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiHeadlessRuntimeRecoveryDecision decision = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_recovery_decision_with_profile_key(
		readiness_timeout_ms,
		profile_key,
		&decision);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_headless_runtime_apply_recovery_decision(&decision, out_applied_action);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover(
	uint32_t readiness_timeout_ms,
	ChiakiHeadlessRuntimeRecoveryAction *out_applied_action)
{
	return chiaki_headless_runtime_recover_with_profile_key(
		readiness_timeout_ms,
		"default",
		out_applied_action);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recover_result_with_profile_key(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	ChiakiHeadlessRuntimeRecoveryResult *out_result)
{
	if(!profile_key || !out_result)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryResult result = {0};
	result.api_version = chiaki_headless_api_version();

	ChiakiErrorCode err = chiaki_headless_runtime_get_recovery_decision_with_profile_key(
		readiness_timeout_ms,
		profile_key,
		&result.decision);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	result.applied_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
	result.apply_error = chiaki_headless_runtime_apply_recovery_decision(
		&result.decision,
		&result.applied_action);
	headless_runtime_finalize_recovery_result(&result);
	chiaki_mutex_lock(&g_runtime_lock);
	headless_runtime_record_recover_result_locked(&result);
	chiaki_mutex_unlock(&g_runtime_lock);

	*out_result = result;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recover_result(
	uint32_t readiness_timeout_ms,
	ChiakiHeadlessRuntimeRecoveryResult *out_result)
{
	return chiaki_headless_runtime_get_recover_result_with_profile_key(
		readiness_timeout_ms,
		"default",
		out_result);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recover_result_with_profile_key_compat(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	void *out_result_buf,
	size_t out_result_size)
{
	if(!profile_key || !out_result_buf || out_result_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiHeadlessRuntimeRecoveryResult full = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_recover_result_with_profile_key(
		readiness_timeout_ms,
		profile_key,
		&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_result_size < sizeof(full) ? out_result_size : sizeof(full);
	memcpy(out_result_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recover_result_compat(
	uint32_t readiness_timeout_ms,
	void *out_result_buf,
	size_t out_result_size)
{
	if(!out_result_buf || out_result_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	return chiaki_headless_runtime_get_recover_result_with_profile_key_compat(
		readiness_timeout_ms,
		"default",
		out_result_buf,
		out_result_size);
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_recovery_tuning_size(void)
{
	return sizeof(ChiakiHeadlessRuntimeRecoveryTuning);
}

CHIAKI_EXPORT void chiaki_headless_runtime_recovery_tuning_init(
	ChiakiHeadlessRuntimeRecoveryTuning *tuning)
{
	if(!tuning)
		return;
	memset(tuning, 0, sizeof(*tuning));
	tuning->api_version = chiaki_headless_api_version();
	tuning->degraded_streak_threshold = 3;
	tuning->idr_cooldown_sec = 2;
	tuning->stop_on_terminal = true;
}

static void headless_runtime_apply_tuning_over_decision(
	const ChiakiHeadlessRuntimeRecoveryDecision *decision,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	ChiakiHeadlessRuntimeRecoveryAction *io_action,
	uint64_t now_us_override)
{
	if(!decision || !tuning || !io_action)
		return;

	ChiakiHeadlessRuntimeRecoveryAction action = *io_action;

	if(decision->health_state == CHIAKI_MEDIA_HEALTH_TERMINAL && !tuning->stop_on_terminal)
		action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;

	if(action == CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR)
	{
		uint64_t now_us = now_us_override > 0 ? now_us_override : chiaki_time_now_monotonic_us();
		chiaki_mutex_lock(&g_runtime_lock);
		if(decision->health_state == CHIAKI_MEDIA_HEALTH_DEGRADED)
			g_runtime_recovery_degraded_streak++;
		else
			g_runtime_recovery_degraded_streak = 0;

		bool streak_ok = g_runtime_recovery_degraded_streak >= tuning->degraded_streak_threshold;
		uint64_t cooldown_us = ((uint64_t)tuning->idr_cooldown_sec) * 1000000ULL;
		bool cooldown_ok =
			g_runtime_recovery_last_idr_request_us == 0 ||
			(now_us >= g_runtime_recovery_last_idr_request_us + cooldown_us);
		if(!(streak_ok && cooldown_ok))
			action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
		else
			g_runtime_recovery_last_idr_request_us = now_us;
		chiaki_mutex_unlock(&g_runtime_lock);
	}
	else
	{
		chiaki_mutex_lock(&g_runtime_lock);
		if(decision->health_state == CHIAKI_MEDIA_HEALTH_DEGRADED)
			g_runtime_recovery_degraded_streak++;
		else
			g_runtime_recovery_degraded_streak = 0;
		chiaki_mutex_unlock(&g_runtime_lock);
	}

	*io_action = action;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_with_profile_key_tuned(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	ChiakiHeadlessRuntimeRecoveryResult *out_result)
{
	if(!profile_key || !tuning || !out_result)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryResult result = {0};
	result.api_version = chiaki_headless_api_version();
	ChiakiErrorCode err = chiaki_headless_runtime_get_recovery_decision_with_profile_key(
		readiness_timeout_ms,
		profile_key,
		&result.decision);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiHeadlessRuntimeRecoveryAction tuned_action = result.decision.action;
	headless_runtime_apply_tuning_over_decision(&result.decision, tuning, &tuned_action, 0);
	result.decision.action = tuned_action;
	result.applied_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
	result.apply_error = CHIAKI_ERR_SUCCESS;
	if(tuned_action != CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE)
		result.apply_error = chiaki_headless_runtime_apply_recovery_decision(
			&result.decision,
			&result.applied_action);
	headless_runtime_finalize_recovery_result(&result);
	chiaki_mutex_lock(&g_runtime_lock);
	headless_runtime_record_recover_result_locked(&result);
	chiaki_mutex_unlock(&g_runtime_lock);

	*out_result = result;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_tuned(
	uint32_t readiness_timeout_ms,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	ChiakiHeadlessRuntimeRecoveryResult *out_result)
{
	return chiaki_headless_runtime_recover_with_profile_key_tuned(
		readiness_timeout_ms,
		"default",
		tuning,
		out_result);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_with_profile_key_tuned_compat(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	const void *tuning_buf,
	size_t tuning_size,
	void *out_result_buf,
	size_t out_result_size)
{
	if(!profile_key || !tuning_buf || tuning_size == 0 || !out_result_buf || out_result_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiHeadlessRuntimeRecoveryTuning tuning = {0};
	chiaki_headless_runtime_recovery_tuning_init(&tuning);
	size_t copy_size = tuning_size < sizeof(tuning) ? tuning_size : sizeof(tuning);
	memcpy(&tuning, tuning_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryResult full = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recover_with_profile_key_tuned(
		readiness_timeout_ms,
		profile_key,
		&tuning,
		&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	copy_size = out_result_size < sizeof(full) ? out_result_size : sizeof(full);
	memcpy(out_result_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_recovery_status_size(void)
{
	return sizeof(ChiakiHeadlessRuntimeRecoveryStatus);
}

CHIAKI_EXPORT void chiaki_headless_runtime_recovery_status_init(
	ChiakiHeadlessRuntimeRecoveryStatus *status)
{
	if(!status)
		return;
	memset(status, 0, sizeof(*status));
	status->api_version = chiaki_headless_api_version();
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_status(
	ChiakiHeadlessRuntimeRecoveryStatus *out_status)
{
	if(!out_status)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiHeadlessRuntimeRecoveryStatus status = {0};
	chiaki_headless_runtime_recovery_status_init(&status);
	chiaki_mutex_lock(&g_runtime_lock);
	status.degraded_streak = g_runtime_recovery_degraded_streak;
	status.last_idr_request_monotonic_us = g_runtime_recovery_last_idr_request_us;
	status.runtime_session_active = g_runtime_session != NULL;
	chiaki_mutex_unlock(&g_runtime_lock);
	*out_status = status;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_status_compat(
	void *out_status_buf,
	size_t out_status_size)
{
	if(!out_status_buf || out_status_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiHeadlessRuntimeRecoveryStatus full = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_recovery_status(&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_status_size < sizeof(full) ? out_status_size : sizeof(full);
	memcpy(out_status_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_reset_recovery_status(void)
{
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	chiaki_mutex_lock(&g_runtime_lock);
	headless_runtime_recovery_status_reset_locked();
	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_headless_runtime_recovery_config_size(void)
{
	return sizeof(ChiakiHeadlessRuntimeRecoveryConfig);
}

CHIAKI_EXPORT void chiaki_headless_runtime_recovery_config_init(
	ChiakiHeadlessRuntimeRecoveryConfig *config)
{
	if(!config)
		return;
	memset(config, 0, sizeof(*config));
	config->api_version = chiaki_headless_api_version();
	config->profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	chiaki_headless_runtime_recovery_tuning_init(&config->tuning);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_recovery_config(
	const ChiakiHeadlessRuntimeRecoveryConfig *config)
{
	if(!config)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	ChiakiMediaHealthPolicy policy = {0};
	err = chiaki_media_health_policy_from_profile(config->profile, &policy);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	g_runtime_recovery_config = *config;
	g_runtime_recovery_config.api_version = chiaki_headless_api_version();
	g_runtime_recovery_config.tuning.api_version = chiaki_headless_api_version();
	/* Keep host-visible semantics deterministic: when config/profile changes,
	 * clear stateful streak/cooldown carry-over from prior policy. */
	headless_runtime_recovery_status_reset_locked();
	g_runtime_recovery_config_init = true;
	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_config(
	ChiakiHeadlessRuntimeRecoveryConfig *out_config)
{
	if(!out_config)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	headless_runtime_recovery_config_init_locked();
	*out_config = g_runtime_recovery_config;
	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_recovery_config_compat(
	const void *config_buf,
	size_t config_size)
{
	if(!config_buf || config_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	chiaki_headless_runtime_recovery_config_init(&config);
	size_t copy_size = config_size < sizeof(config) ? config_size : sizeof(config);
	memcpy(&config, config_buf, copy_size);
	return chiaki_headless_runtime_set_recovery_config(&config);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_config_compat(
	void *out_config_buf,
	size_t out_config_size)
{
	if(!out_config_buf || out_config_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_recovery_config(&config);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_config_size < sizeof(config) ? out_config_size : sizeof(config);
	memcpy(out_config_buf, &config, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_recovery_profile_key(
	const char *profile_key)
{
	if(!profile_key)
		return CHIAKI_ERR_INVALID_DATA;
	const char *canonical = NULL;
	ChiakiErrorCode err = chiaki_media_health_policy_profile_key_normalize(profile_key, &canonical);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	ChiakiMediaHealthPolicyProfile profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	err = chiaki_media_health_policy_profile_from_key(canonical, &profile);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	err = chiaki_headless_runtime_get_recovery_config(&config);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	config.profile = (uint32_t)profile;
	return chiaki_headless_runtime_set_recovery_config(&config);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_profile_key(
	char *out_profile_key,
	size_t out_profile_key_size)
{
	if(!out_profile_key || out_profile_key_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_get_recovery_config(&config);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	const char *key = NULL;
	err = chiaki_media_health_policy_profile_key_from_profile(
		(ChiakiMediaHealthPolicyProfile)config.profile,
		&key);
	if(err != CHIAKI_ERR_SUCCESS || !key)
		return CHIAKI_ERR_INVALID_DATA;
	size_t need = strlen(key) + 1;
	if(out_profile_key_size < need)
		return CHIAKI_ERR_BUF_TOO_SMALL;
	memcpy(out_profile_key, key, need);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_auto(
	uint32_t readiness_timeout_ms,
	ChiakiHeadlessRuntimeRecoveryResult *out_result)
{
	if(!out_result)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	chiaki_mutex_lock(&g_runtime_lock);
	headless_runtime_recovery_config_init_locked();
	config = g_runtime_recovery_config;
	chiaki_mutex_unlock(&g_runtime_lock);

	const char *profile_key = "default";
	switch(config.profile)
	{
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE:
			profile_key = "aggressive";
			break;
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE:
			profile_key = "conservative";
			break;
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT:
		default:
			profile_key = "default";
			break;
	}

	return chiaki_headless_runtime_recover_with_profile_key_tuned(
		readiness_timeout_ms,
		profile_key,
		&config.tuning,
		out_result);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_auto_compat(
	uint32_t readiness_timeout_ms,
	void *out_result_buf,
	size_t out_result_size)
{
	if(!out_result_buf || out_result_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiHeadlessRuntimeRecoveryResult full = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recover_auto(
		readiness_timeout_ms,
		&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_result_size < sizeof(full) ? out_result_size : sizeof(full);
	memcpy(out_result_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_auto_with_status(
	uint32_t readiness_timeout_ms,
	ChiakiHeadlessRuntimeRecoveryResult *out_result,
	ChiakiHeadlessRuntimeRecoveryStatus *out_status)
{
	if(!out_result || !out_status)
		return CHIAKI_ERR_INVALID_DATA;

	/* Always return a status snapshot payload for host parity loops,
	 * even when recover path reports an error. */
	chiaki_headless_runtime_recovery_status_init(out_status);
	ChiakiErrorCode recover_err = chiaki_headless_runtime_recover_auto(
		readiness_timeout_ms,
		out_result);
	ChiakiErrorCode status_err = chiaki_headless_runtime_get_recovery_status(out_status);
	if(recover_err != CHIAKI_ERR_SUCCESS)
		return recover_err;
	return status_err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_auto_with_status_compat(
	uint32_t readiness_timeout_ms,
	void *out_result_buf,
	size_t out_result_size,
	void *out_status_buf,
	size_t out_status_size)
{
	if(!out_result_buf || out_result_size == 0 || !out_status_buf || out_status_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryResult result = {0};
	ChiakiHeadlessRuntimeRecoveryStatus status = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_recover_auto_with_status(
		readiness_timeout_ms,
		&result,
		&status);
	/* Preserve deterministic status/result payload semantics for compat callers,
	 * even when runtime recover returns a non-success code. */
	size_t copy_size = out_result_size < sizeof(result) ? out_result_size : sizeof(result);
	memcpy(out_result_buf, &result, copy_size);
	copy_size = out_status_size < sizeof(status) ? out_status_size : sizeof(status);
	memcpy(out_status_buf, &status, copy_size);
	return err;
}

static ChiakiErrorCode headless_runtime_resolve_policy_from_profile_key(
	const char *profile_key,
	ChiakiMediaHealthPolicy *out_policy)
{
	if(!profile_key || !out_policy)
		return CHIAKI_ERR_INVALID_DATA;
	const char *canonical = NULL;
	ChiakiErrorCode err = chiaki_media_health_policy_profile_key_normalize(profile_key, &canonical);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	ChiakiMediaHealthPolicyProfile profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	err = chiaki_media_health_policy_profile_from_key(canonical, &profile);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_media_health_policy_from_profile(profile, out_policy);
}

static ChiakiErrorCode headless_runtime_simulate_recovery_core(
	const ChiakiMediaSessionDiagnosticsSnapshot *snapshot,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *status_in,
	uint64_t now_us_override,
	ChiakiHeadlessRuntimeRecoveryResult *out_result,
	ChiakiHeadlessRuntimeRecoveryStatus *out_status)
{
	if(!snapshot || !profile_key || !tuning || !status_in || !out_result || !out_status)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiMediaHealthPolicy policy = {0};
	ChiakiErrorCode err = headless_runtime_resolve_policy_from_profile_key(profile_key, &policy);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiHeadlessRuntimeRecoveryResult result = {0};
	result.api_version = chiaki_headless_api_version();
	result.decision.api_version = chiaki_headless_api_version();
	result.decision.health_state = snapshot->health.health_state;
	result.decision.runtime_session_active = snapshot->stats.started;
	result.decision.effective_enable_idr_on_fec_failure = true;
	result.decision.event_count = snapshot->stats.event_count;
	result.decision.error_event_count = snapshot->stats.error_event_count;
	result.decision.ready_event_count = snapshot->stats.ready_event_count;
	result.decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;

	bool terminal = snapshot->health.health_state == CHIAKI_MEDIA_HEALTH_TERMINAL;
	bool degraded = snapshot->health.health_state == CHIAKI_MEDIA_HEALTH_DEGRADED;
	if(!result.decision.runtime_session_active || snapshot->stats.state == CHIAKI_MEDIA_SESSION_STATE_STOPPED)
		result.decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
	else if(terminal && (tuning->stop_on_terminal || policy.treat_stopped_as_terminal || policy.treat_error_event_as_terminal))
		result.decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_STOP_RUNTIME;
	else if(degraded && result.decision.effective_enable_idr_on_fec_failure && snapshot->stats.error_event_count > 0)
		result.decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR;

	ChiakiHeadlessRuntimeRecoveryStatus status = *status_in;
	status.api_version = chiaki_headless_api_version();
	status.runtime_session_active = result.decision.runtime_session_active;
	if(degraded)
		status.degraded_streak++;
	else
		status.degraded_streak = 0;

	ChiakiHeadlessRuntimeRecoveryAction action = result.decision.action;
	if(action == CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR)
	{
		uint64_t now_us = now_us_override > 0 ? now_us_override : chiaki_time_now_monotonic_us();
		bool streak_ok = status.degraded_streak >= tuning->degraded_streak_threshold;
		uint64_t cooldown_us = ((uint64_t)tuning->idr_cooldown_sec) * 1000000ULL;
		bool cooldown_ok = status.last_idr_request_monotonic_us == 0
			|| now_us >= status.last_idr_request_monotonic_us + cooldown_us;
		if(!(streak_ok && cooldown_ok))
			action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
		else
			status.last_idr_request_monotonic_us = now_us;
	}

	result.decision.action = action;
	result.applied_action = action;
	result.apply_error = CHIAKI_ERR_SUCCESS;
	*out_result = result;
	*out_status = status;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_with_profile_key(
	const ChiakiMediaSessionDiagnosticsSnapshot *snapshot,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *status_in,
	ChiakiHeadlessRuntimeRecoveryResult *out_result,
	ChiakiHeadlessRuntimeRecoveryStatus *out_status)
{
	return headless_runtime_simulate_recovery_core(
		snapshot,
		profile_key,
		tuning,
		status_in,
		0,
		out_result,
		out_status);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery(
	const ChiakiMediaSessionDiagnosticsSnapshot *snapshot,
	const ChiakiHeadlessRuntimeRecoveryConfig *config,
	const ChiakiHeadlessRuntimeRecoveryStatus *status_in,
	ChiakiHeadlessRuntimeRecoveryResult *out_result,
	ChiakiHeadlessRuntimeRecoveryStatus *out_status)
{
	if(!snapshot || !config || !status_in || !out_result || !out_status)
		return CHIAKI_ERR_INVALID_DATA;
	const char *profile_key = "default";
	switch((ChiakiMediaHealthPolicyProfile)config->profile)
	{
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE:
			profile_key = "aggressive";
			break;
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE:
			profile_key = "conservative";
			break;
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT:
		default:
			profile_key = "default";
			break;
	}
	return chiaki_headless_runtime_simulate_recovery_with_profile_key(
		snapshot,
		profile_key,
		&config->tuning,
		status_in,
		out_result,
		out_status);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_compat(
	const void *snapshot_buf,
	size_t snapshot_size,
	const void *config_buf,
	size_t config_size,
	const void *status_in_buf,
	size_t status_in_size,
	void *out_result_buf,
	size_t out_result_size,
	void *out_status_buf,
	size_t out_status_size)
{
	if(!snapshot_buf || snapshot_size == 0 || !config_buf || config_size == 0 || !status_in_buf
		|| status_in_size == 0 || !out_result_buf || out_result_size == 0 || !out_status_buf
		|| out_status_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
	chiaki_media_session_diagnostics_snapshot_init(&snapshot);
	size_t copy_size = snapshot_size < sizeof(snapshot) ? snapshot_size : sizeof(snapshot);
	memcpy(&snapshot, snapshot_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	chiaki_headless_runtime_recovery_config_init(&config);
	copy_size = config_size < sizeof(config) ? config_size : sizeof(config);
	memcpy(&config, config_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryStatus status_in = {0};
	chiaki_headless_runtime_recovery_status_init(&status_in);
	copy_size = status_in_size < sizeof(status_in) ? status_in_size : sizeof(status_in);
	memcpy(&status_in, status_in_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryResult result = {0};
	ChiakiHeadlessRuntimeRecoveryStatus status_out = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery(
		&snapshot,
		&config,
		&status_in,
		&result,
		&status_out);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	copy_size = out_result_size < sizeof(result) ? out_result_size : sizeof(result);
	memcpy(out_result_buf, &result, copy_size);
	copy_size = out_status_size < sizeof(status_out) ? out_status_size : sizeof(status_out);
	memcpy(out_status_buf, &status_out, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryConfig *config,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	ChiakiHeadlessRuntimeRecoveryStatus *out_final_status)
{
	if(!step_inputs || step_count == 0 || !config || !initial_status || !step_outputs || !out_final_status)
		return CHIAKI_ERR_INVALID_DATA;

	const char *profile_key = "default";
	switch((ChiakiMediaHealthPolicyProfile)config->profile)
	{
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE:
			profile_key = "aggressive";
			break;
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE:
			profile_key = "conservative";
			break;
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT:
		default:
			profile_key = "default";
			break;
	}

	ChiakiHeadlessRuntimeRecoveryStatus rolling = *initial_status;
	rolling.api_version = chiaki_headless_api_version();

	for(size_t i = 0; i < step_count; i++)
	{
		if(!step_inputs[i].snapshot || step_inputs[i].snapshot_size == 0)
			return CHIAKI_ERR_INVALID_DATA;
		ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
		chiaki_media_session_diagnostics_snapshot_init(&snapshot);
		size_t snapshot_copy = step_inputs[i].snapshot_size < sizeof(snapshot)
			? step_inputs[i].snapshot_size
			: sizeof(snapshot);
		memcpy(&snapshot, step_inputs[i].snapshot, snapshot_copy);

		ChiakiHeadlessRuntimeRecoveryResult step_result = {0};
		ChiakiHeadlessRuntimeRecoveryStatus step_status = {0};
		ChiakiErrorCode err = headless_runtime_simulate_recovery_core(
			&snapshot,
			profile_key,
			&config->tuning,
			&rolling,
			step_inputs[i].monotonic_now_us,
			&step_result,
			&step_status);
		if(err != CHIAKI_ERR_SUCCESS)
			return err;
		step_outputs[i].result = step_result;
		step_outputs[i].status = step_status;
		rolling = step_status;
	}

	*out_final_status = rolling;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const void *config_buf,
	size_t config_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *step_outputs_buf,
	size_t step_output_stride,
	size_t step_output_size,
	void *out_final_status_buf,
	size_t out_final_status_size)
{
	if(!step_inputs || step_count == 0 || !config_buf || config_size == 0 || !initial_status_buf
		|| initial_status_size == 0 || !step_outputs_buf || step_output_stride == 0
		|| step_output_size == 0 || !out_final_status_buf || out_final_status_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	chiaki_headless_runtime_recovery_config_init(&config);
	size_t copy_size = config_size < sizeof(config) ? config_size : sizeof(config);
	memcpy(&config, config_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryStatus initial_status = {0};
	chiaki_headless_runtime_recovery_status_init(&initial_status);
	copy_size = initial_status_size < sizeof(initial_status) ? initial_status_size : sizeof(initial_status);
	memcpy(&initial_status, initial_status_buf, copy_size);

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *full_outputs =
		calloc(step_count, sizeof(*full_outputs));
	if(!full_outputs)
		return CHIAKI_ERR_MEMORY;
	ChiakiHeadlessRuntimeRecoveryStatus final_status = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_sequence(
		step_inputs,
		step_count,
		&config,
		&initial_status,
		full_outputs,
		&final_status);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(full_outputs);
		return err;
	}

	for(size_t i = 0; i < step_count; i++)
	{
		uint8_t *dst = (uint8_t *)step_outputs_buf + i * step_output_stride;
		size_t n = step_output_size < sizeof(full_outputs[i]) ? step_output_size : sizeof(full_outputs[i]);
		memcpy(dst, &full_outputs[i], n);
	}
	copy_size = out_final_status_size < sizeof(final_status) ? out_final_status_size : sizeof(final_status);
	memcpy(out_final_status_buf, &final_status, copy_size);
	free(full_outputs);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_report(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryConfig *config,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report)
{
	if(!step_inputs || step_count == 0 || !config || !initial_status || !out_report)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *outputs =
		calloc(step_count, sizeof(*outputs));
	if(!outputs)
		return CHIAKI_ERR_MEMORY;

	ChiakiHeadlessRuntimeRecoveryStatus final_status = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_sequence(
		step_inputs,
		step_count,
		config,
		initial_status,
		outputs,
		&final_status);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(outputs);
		return err;
	}

	ChiakiErrorCode report_err = chiaki_headless_runtime_recovery_simulation_report_from_outputs(
		outputs,
		step_count,
		&final_status,
		out_report);
	free(outputs);
	return report_err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_report_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const void *config_buf,
	size_t config_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!step_inputs || step_count == 0 || !config_buf || config_size == 0 || !initial_status_buf
		|| initial_status_size == 0 || !out_report_buf || out_report_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	chiaki_headless_runtime_recovery_config_init(&config);
	size_t copy_size = config_size < sizeof(config) ? config_size : sizeof(config);
	memcpy(&config, config_buf, copy_size);
	ChiakiHeadlessRuntimeRecoveryStatus initial_status = {0};
	chiaki_headless_runtime_recovery_status_init(&initial_status);
	copy_size = initial_status_size < sizeof(initial_status) ? initial_status_size : sizeof(initial_status);
	memcpy(&initial_status, initial_status_buf, copy_size);

	ChiakiHeadlessRuntimeRecoverySimulationReport report = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_sequence_report(
		step_inputs,
		step_count,
		&config,
		&initial_status,
		&report);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	copy_size = out_report_size < sizeof(report) ? out_report_size : sizeof(report);
	memcpy(out_report_buf, &report, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_with_report(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryConfig *config,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	ChiakiHeadlessRuntimeRecoveryStatus *out_final_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report)
{
	if(!step_outputs || !out_final_status || !out_report)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_sequence(
		step_inputs,
		step_count,
		config,
		initial_status,
		step_outputs,
		out_final_status);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_headless_runtime_recovery_simulation_report_from_outputs(
		step_outputs,
		step_count,
		out_final_status,
		out_report);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_with_report_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const void *config_buf,
	size_t config_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *step_outputs_buf,
	size_t step_output_stride,
	size_t step_output_size,
	void *out_final_status_buf,
	size_t out_final_status_size,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!step_outputs_buf || !out_final_status_buf || !out_report_buf)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_sequence_compat(
		step_inputs,
		step_count,
		config_buf,
		config_size,
		initial_status_buf,
		initial_status_size,
		step_outputs_buf,
		step_output_stride,
		step_output_size,
		out_final_status_buf,
		out_final_status_size);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_headless_runtime_recovery_simulation_report_from_outputs_compat(
		step_outputs_buf,
		step_count,
		step_output_stride,
		step_output_size,
		out_final_status_buf,
		out_final_status_size,
		out_report_buf,
		out_report_size);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	ChiakiHeadlessRuntimeRecoveryStatus *out_final_status)
{
	if(!step_inputs || step_count == 0 || !profile_key || !tuning || !initial_status || !step_outputs || !out_final_status)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryStatus rolling = *initial_status;
	rolling.api_version = chiaki_headless_api_version();

	for(size_t i = 0; i < step_count; i++)
	{
		if(!step_inputs[i].snapshot || step_inputs[i].snapshot_size == 0)
			return CHIAKI_ERR_INVALID_DATA;
		ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
		chiaki_media_session_diagnostics_snapshot_init(&snapshot);
		size_t snapshot_copy = step_inputs[i].snapshot_size < sizeof(snapshot)
			? step_inputs[i].snapshot_size
			: sizeof(snapshot);
		memcpy(&snapshot, step_inputs[i].snapshot, snapshot_copy);

		ChiakiHeadlessRuntimeRecoveryResult step_result = {0};
		ChiakiHeadlessRuntimeRecoveryStatus step_status = {0};
		ChiakiErrorCode err = headless_runtime_simulate_recovery_core(
			&snapshot,
			profile_key,
			tuning,
			&rolling,
			step_inputs[i].monotonic_now_us,
			&step_result,
			&step_status);
		if(err != CHIAKI_ERR_SUCCESS)
			return err;
		step_outputs[i].result = step_result;
		step_outputs[i].status = step_status;
		rolling = step_status;
	}

	*out_final_status = rolling;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_report_with_profile_key(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report)
{
	if(!step_inputs || step_count == 0 || !profile_key || !tuning || !initial_status || !out_report)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *outputs =
		calloc(step_count, sizeof(*outputs));
	if(!outputs)
		return CHIAKI_ERR_MEMORY;

	ChiakiHeadlessRuntimeRecoveryStatus final_status = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key(
		step_inputs,
		step_count,
		profile_key,
		tuning,
		initial_status,
		outputs,
		&final_status);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(outputs);
		return err;
	}

	ChiakiErrorCode report_err = chiaki_headless_runtime_recovery_simulation_report_from_outputs(
		outputs,
		step_count,
		&final_status,
		out_report);
	free(outputs);
	return report_err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const void *tuning_buf,
	size_t tuning_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *step_outputs_buf,
	size_t step_output_stride,
	size_t step_output_size,
	void *out_final_status_buf,
	size_t out_final_status_size)
{
	if(!step_inputs || step_count == 0 || !profile_key || !tuning_buf || tuning_size == 0
		|| !initial_status_buf || initial_status_size == 0 || !step_outputs_buf
		|| step_output_stride == 0 || step_output_size == 0 || !out_final_status_buf
		|| out_final_status_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryTuning tuning = {0};
	chiaki_headless_runtime_recovery_tuning_init(&tuning);
	size_t copy_size = tuning_size < sizeof(tuning) ? tuning_size : sizeof(tuning);
	memcpy(&tuning, tuning_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryStatus initial_status = {0};
	chiaki_headless_runtime_recovery_status_init(&initial_status);
	copy_size = initial_status_size < sizeof(initial_status) ? initial_status_size : sizeof(initial_status);
	memcpy(&initial_status, initial_status_buf, copy_size);

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *full_outputs =
		calloc(step_count, sizeof(*full_outputs));
	if(!full_outputs)
		return CHIAKI_ERR_MEMORY;

	ChiakiHeadlessRuntimeRecoveryStatus final_status = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key(
		step_inputs,
		step_count,
		profile_key,
		&tuning,
		&initial_status,
		full_outputs,
		&final_status);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(full_outputs);
		return err;
	}

	for(size_t i = 0; i < step_count; i++)
	{
		uint8_t *dst = (uint8_t *)step_outputs_buf + i * step_output_stride;
		size_t n = step_output_size < sizeof(full_outputs[i]) ? step_output_size : sizeof(full_outputs[i]);
		memcpy(dst, &full_outputs[i], n);
	}
	copy_size = out_final_status_size < sizeof(final_status) ? out_final_status_size : sizeof(final_status);
	memcpy(out_final_status_buf, &final_status, copy_size);
	free(full_outputs);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_report_with_profile_key_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const void *tuning_buf,
	size_t tuning_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!step_inputs || step_count == 0 || !profile_key || !tuning_buf || tuning_size == 0
		|| !initial_status_buf || initial_status_size == 0 || !out_report_buf || out_report_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryTuning tuning = {0};
	chiaki_headless_runtime_recovery_tuning_init(&tuning);
	size_t copy_size = tuning_size < sizeof(tuning) ? tuning_size : sizeof(tuning);
	memcpy(&tuning, tuning_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryStatus initial_status = {0};
	chiaki_headless_runtime_recovery_status_init(&initial_status);
	copy_size = initial_status_size < sizeof(initial_status) ? initial_status_size : sizeof(initial_status);
	memcpy(&initial_status, initial_status_buf, copy_size);

	ChiakiHeadlessRuntimeRecoverySimulationReport report = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_sequence_report_with_profile_key(
		step_inputs,
		step_count,
		profile_key,
		&tuning,
		&initial_status,
		&report);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	copy_size = out_report_size < sizeof(report) ? out_report_size : sizeof(report);
	memcpy(out_report_buf, &report, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key_with_report(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	ChiakiHeadlessRuntimeRecoveryStatus *out_final_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report)
{
	if(!step_outputs || !out_final_status || !out_report)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key(
		step_inputs,
		step_count,
		profile_key,
		tuning,
		initial_status,
		step_outputs,
		out_final_status);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_headless_runtime_recovery_simulation_report_from_outputs(
		step_outputs,
		step_count,
		out_final_status,
		out_report);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key_with_report_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const void *tuning_buf,
	size_t tuning_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *step_outputs_buf,
	size_t step_output_stride,
	size_t step_output_size,
	void *out_final_status_buf,
	size_t out_final_status_size,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!step_outputs_buf || !out_final_status_buf || !out_report_buf)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key_compat(
		step_inputs,
		step_count,
		profile_key,
		tuning_buf,
		tuning_size,
		initial_status_buf,
		initial_status_size,
		step_outputs_buf,
		step_output_stride,
		step_output_size,
		out_final_status_buf,
		out_final_status_size);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_headless_runtime_recovery_simulation_report_from_outputs_compat(
		step_outputs_buf,
		step_count,
		step_output_stride,
		step_output_size,
		out_final_status_buf,
		out_final_status_size,
		out_report_buf,
		out_report_size);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key(
	const ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	ChiakiHeadlessRuntimeRecoveryStatus *out_final_status)
{
	if(!step_inputs || step_count == 0 || !profile_key || !tuning || !initial_status || !step_outputs || !out_final_status)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryStatus rolling = *initial_status;
	rolling.api_version = chiaki_headless_api_version();

	for(size_t i = 0; i < step_count; i++)
	{
		ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
		chiaki_media_session_diagnostics_snapshot_init(&snapshot);
		snapshot.stats.started = true;
		snapshot.stats.state = CHIAKI_MEDIA_SESSION_STATE_RUNNING;
		snapshot.stats.event_count = step_inputs[i].event_count;
		snapshot.stats.error_event_count = step_inputs[i].error_event_count;
		snapshot.stats.ready_event_count = step_inputs[i].ready_event_count;
		snapshot.health.health_state = step_inputs[i].health_state;

		ChiakiHeadlessRuntimeRecoveryResult step_result = {0};
		ChiakiHeadlessRuntimeRecoveryStatus step_status = {0};
		ChiakiErrorCode err = headless_runtime_simulate_recovery_core(
			&snapshot,
			profile_key,
			tuning,
			&rolling,
			step_inputs[i].monotonic_now_us,
			&step_result,
			&step_status);
		if(err != CHIAKI_ERR_SUCCESS)
			return err;
		step_outputs[i].result = step_result;
		step_outputs[i].status = step_status;
		rolling = step_status;
	}

	*out_final_status = rolling;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_health_sequence_report_with_profile_key(
	const ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report)
{
	if(!step_inputs || step_count == 0 || !profile_key || !tuning || !initial_status || !out_report)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *outputs =
		calloc(step_count, sizeof(*outputs));
	if(!outputs)
		return CHIAKI_ERR_MEMORY;

	ChiakiHeadlessRuntimeRecoveryStatus final_status = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key(
		step_inputs,
		step_count,
		profile_key,
		tuning,
		initial_status,
		outputs,
		&final_status);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(outputs);
		return err;
	}

	ChiakiErrorCode report_err = chiaki_headless_runtime_recovery_simulation_report_from_outputs(
		outputs,
		step_count,
		&final_status,
		out_report);
	free(outputs);
	return report_err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const void *tuning_buf,
	size_t tuning_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *step_outputs_buf,
	size_t step_output_stride,
	size_t step_output_size,
	void *out_final_status_buf,
	size_t out_final_status_size)
{
	if(!step_inputs || step_count == 0 || !profile_key || !tuning_buf || tuning_size == 0
		|| !initial_status_buf || initial_status_size == 0 || !step_outputs_buf
		|| step_output_stride == 0 || step_output_size == 0 || !out_final_status_buf
		|| out_final_status_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryTuning tuning = {0};
	chiaki_headless_runtime_recovery_tuning_init(&tuning);
	size_t copy_size = tuning_size < sizeof(tuning) ? tuning_size : sizeof(tuning);
	memcpy(&tuning, tuning_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryStatus initial_status = {0};
	chiaki_headless_runtime_recovery_status_init(&initial_status);
	copy_size = initial_status_size < sizeof(initial_status) ? initial_status_size : sizeof(initial_status);
	memcpy(&initial_status, initial_status_buf, copy_size);

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *full_outputs =
		calloc(step_count, sizeof(*full_outputs));
	if(!full_outputs)
		return CHIAKI_ERR_MEMORY;

	ChiakiHeadlessRuntimeRecoveryStatus final_status = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key(
		step_inputs,
		step_count,
		profile_key,
		&tuning,
		&initial_status,
		full_outputs,
		&final_status);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(full_outputs);
		return err;
	}

	for(size_t i = 0; i < step_count; i++)
	{
		uint8_t *dst = (uint8_t *)step_outputs_buf + i * step_output_stride;
		size_t n = step_output_size < sizeof(full_outputs[i]) ? step_output_size : sizeof(full_outputs[i]);
		memcpy(dst, &full_outputs[i], n);
	}
	copy_size = out_final_status_size < sizeof(final_status) ? out_final_status_size : sizeof(final_status);
	memcpy(out_final_status_buf, &final_status, copy_size);
	free(full_outputs);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_health_sequence_report_with_profile_key_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const void *tuning_buf,
	size_t tuning_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!step_inputs || step_count == 0 || !profile_key || !tuning_buf || tuning_size == 0
		|| !initial_status_buf || initial_status_size == 0 || !out_report_buf || out_report_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryTuning tuning = {0};
	chiaki_headless_runtime_recovery_tuning_init(&tuning);
	size_t copy_size = tuning_size < sizeof(tuning) ? tuning_size : sizeof(tuning);
	memcpy(&tuning, tuning_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryStatus initial_status = {0};
	chiaki_headless_runtime_recovery_status_init(&initial_status);
	copy_size = initial_status_size < sizeof(initial_status) ? initial_status_size : sizeof(initial_status);
	memcpy(&initial_status, initial_status_buf, copy_size);

	ChiakiHeadlessRuntimeRecoverySimulationReport report = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_health_sequence_report_with_profile_key(
		step_inputs,
		step_count,
		profile_key,
		&tuning,
		&initial_status,
		&report);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	copy_size = out_report_size < sizeof(report) ? out_report_size : sizeof(report);
	memcpy(out_report_buf, &report, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key_with_report(
	const ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	ChiakiHeadlessRuntimeRecoveryStatus *out_final_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report)
{
	if(!step_outputs || !out_final_status || !out_report)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key(
		step_inputs,
		step_count,
		profile_key,
		tuning,
		initial_status,
		step_outputs,
		out_final_status);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_headless_runtime_recovery_simulation_report_from_outputs(
		step_outputs,
		step_count,
		out_final_status,
		out_report);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key_with_report_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const void *tuning_buf,
	size_t tuning_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *step_outputs_buf,
	size_t step_output_stride,
	size_t step_output_size,
	void *out_final_status_buf,
	size_t out_final_status_size,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!step_outputs_buf || !out_final_status_buf || !out_report_buf)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key_compat(
		step_inputs,
		step_count,
		profile_key,
		tuning_buf,
		tuning_size,
		initial_status_buf,
		initial_status_size,
		step_outputs_buf,
		step_output_stride,
		step_output_size,
		out_final_status_buf,
		out_final_status_size);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_headless_runtime_recovery_simulation_report_from_outputs_compat(
		step_outputs_buf,
		step_count,
		step_output_stride,
		step_output_size,
		out_final_status_buf,
		out_final_status_size,
		out_report_buf,
		out_report_size);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_auto_loop_timeline(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryConfig *config,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineStep *timeline_steps,
	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineSummary *out_summary)
{
	if(!step_inputs || step_count == 0 || !config || !initial_status || !timeline_steps || !out_summary)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs =
		calloc(step_count, sizeof(*step_outputs));
	if(!step_outputs)
		return CHIAKI_ERR_MEMORY;

	ChiakiHeadlessRuntimeRecoveryStatus final_status = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_sequence(
		step_inputs,
		step_count,
		config,
		initial_status,
		step_outputs,
		&final_status);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(step_outputs);
		return err;
	}

	ChiakiHeadlessRuntimeRecoverySimulationReport report = {0};
	err = chiaki_headless_runtime_recovery_simulation_report_from_outputs(
		step_outputs,
		step_count,
		&final_status,
		&report);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(step_outputs);
		return err;
	}

	for(size_t i = 0; i < step_count; i++)
	{
		timeline_steps[i].api_version = chiaki_headless_api_version();
		timeline_steps[i].decision_action = step_outputs[i].result.decision.action;
		timeline_steps[i].applied_action = step_outputs[i].result.applied_action;
		timeline_steps[i].degraded_streak = step_outputs[i].status.degraded_streak;
		timeline_steps[i].last_idr_request_monotonic_us = step_outputs[i].status.last_idr_request_monotonic_us;
		timeline_steps[i].health_state = step_outputs[i].result.decision.health_state;
		timeline_steps[i].event_count = step_outputs[i].result.decision.event_count;
		timeline_steps[i].error_event_count = step_outputs[i].result.decision.error_event_count;
		timeline_steps[i].ready_event_count = step_outputs[i].result.decision.ready_event_count;
	}

	out_summary->api_version = chiaki_headless_api_version();
	out_summary->step_count = report.step_count;
	out_summary->idr_action_count = report.idr_action_count;
	out_summary->stop_action_count = report.stop_action_count;
	out_summary->none_action_count = report.none_action_count;
	out_summary->healthy_step_count = report.healthy_step_count;
	out_summary->degraded_step_count = report.degraded_step_count;
	out_summary->terminal_step_count = report.terminal_step_count;
	out_summary->first_idr_step_index = report.first_idr_step_index;
	out_summary->first_stop_step_index = report.first_stop_step_index;
	out_summary->first_none_step_index = report.first_none_step_index;
	out_summary->saw_terminal_health = report.saw_terminal_health;
	out_summary->final_status = report.final_status;

	free(step_outputs);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_auto_loop_timeline_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const void *config_buf,
	size_t config_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *timeline_steps_buf,
	size_t timeline_step_stride,
	size_t timeline_step_size,
	void *out_summary_buf,
	size_t out_summary_size)
{
	if(!step_inputs || step_count == 0 || !config_buf || config_size == 0
		|| !initial_status_buf || initial_status_size == 0 || !timeline_steps_buf
		|| timeline_step_stride == 0 || timeline_step_size == 0
		|| !out_summary_buf || out_summary_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	chiaki_headless_runtime_recovery_config_init(&config);
	size_t copy_size = config_size < sizeof(config) ? config_size : sizeof(config);
	memcpy(&config, config_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryStatus initial_status = {0};
	chiaki_headless_runtime_recovery_status_init(&initial_status);
	copy_size = initial_status_size < sizeof(initial_status) ? initial_status_size : sizeof(initial_status);
	memcpy(&initial_status, initial_status_buf, copy_size);

	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineStep *full_timeline_steps =
		calloc(step_count, sizeof(*full_timeline_steps));
	if(!full_timeline_steps)
		return CHIAKI_ERR_MEMORY;

	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineSummary summary = {0};
	ChiakiErrorCode err = chiaki_headless_runtime_simulate_recovery_auto_loop_timeline(
		step_inputs,
		step_count,
		&config,
		&initial_status,
		full_timeline_steps,
		&summary);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(full_timeline_steps);
		return err;
	}

	for(size_t i = 0; i < step_count; i++)
	{
		uint8_t *dst = (uint8_t *)timeline_steps_buf + i * timeline_step_stride;
		size_t n = timeline_step_size < sizeof(full_timeline_steps[i]) ? timeline_step_size : sizeof(full_timeline_steps[i]);
		memcpy(dst, &full_timeline_steps[i], n);
	}
	copy_size = out_summary_size < sizeof(summary) ? out_summary_size : sizeof(summary);
	memcpy(out_summary_buf, &summary, copy_size);
	free(full_timeline_steps);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_media_capabilities_size(void)
{
	return sizeof(ChiakiMediaCapabilities);
}

CHIAKI_EXPORT void chiaki_media_capabilities_init(ChiakiMediaCapabilities *capabilities)
{
	if(!capabilities)
		return;
	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->api_version = chiaki_headless_api_version();
	capabilities->media_capabilities_version = 16;
	capabilities->media_event_schema_version = 1;
	capabilities->min_session_stats_size = sizeof(ChiakiMediaSessionStats);
	capabilities->min_readiness_report_size = sizeof(ChiakiMediaReadinessReport);
	capabilities->min_health_report_size = sizeof(ChiakiMediaHealthReport);
	capabilities->min_health_policy_size = sizeof(ChiakiMediaHealthPolicy);
	capabilities->min_profile_info_size = sizeof(ChiakiMediaHealthPolicyProfileInfo);
	capabilities->min_profile_resolution_size = sizeof(ChiakiMediaHealthPolicyProfileResolution);
	capabilities->min_diagnostics_snapshot_size = sizeof(ChiakiMediaSessionDiagnosticsSnapshot);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_capabilities(ChiakiMediaCapabilities *out_capabilities)
{
	if(!out_capabilities)
		return CHIAKI_ERR_INVALID_DATA;
	chiaki_media_capabilities_init(out_capabilities);
	out_capabilities->supports_capabilities_query = true;
	out_capabilities->supports_session_create_destroy = true;
	out_capabilities->supports_capabilities_compat = true;
	out_capabilities->supports_session_stats = true;
	out_capabilities->supports_session_stats_compat = true;
	out_capabilities->supports_session_readiness = true;
	out_capabilities->supports_session_readiness_compat = true;
	out_capabilities->supports_session_health_report = true;
	out_capabilities->supports_session_health_report_compat = true;
	out_capabilities->supports_session_health_report_with_policy = true;
	out_capabilities->supports_session_health_report_with_policy_compat = true;
	out_capabilities->supports_health_policy_defaults_query = true;
	out_capabilities->supports_health_policy_defaults_compat = true;
	out_capabilities->supports_health_policy_profiles = true;
	out_capabilities->supports_health_policy_profiles_compat = true;
	out_capabilities->supports_session_health_report_with_profile = true;
	out_capabilities->supports_session_health_report_with_profile_compat = true;
	out_capabilities->supports_health_policy_profile_catalog = true;
	out_capabilities->supports_health_policy_profile_catalog_compat = true;
	out_capabilities->supports_health_policy_profile_metadata = true;
	out_capabilities->supports_health_policy_profile_lookup_by_key = true;
	out_capabilities->supports_health_policy_profile_lookup_by_key_compat = true;
	out_capabilities->supports_health_policy_profile_key_from_profile = true;
	out_capabilities->supports_health_policy_profile_key_normalization = true;
	out_capabilities->supports_health_policy_profile_key_normalization_compat = true;
	out_capabilities->supports_health_policy_profile_resolution = true;
	out_capabilities->supports_health_policy_profile_resolution_compat = true;
	out_capabilities->supports_session_health_report_with_profile_key = true;
	out_capabilities->supports_session_health_report_with_profile_key_compat = true;
	out_capabilities->supports_session_diagnostics_snapshot = true;
	out_capabilities->supports_session_diagnostics_snapshot_compat = true;
	out_capabilities->supports_session_diagnostics_snapshot_with_policy = true;
	out_capabilities->supports_session_diagnostics_snapshot_with_policy_compat = true;
	out_capabilities->supports_session_diagnostics_snapshot_with_profile = true;
	out_capabilities->supports_session_diagnostics_snapshot_with_profile_compat = true;
	out_capabilities->supports_session_diagnostics_snapshot_with_profile_key = true;
	out_capabilities->supports_session_diagnostics_snapshot_with_profile_key_compat = true;
	/*
	 * Capability contract should describe API availability, not current gate state.
	 * Runtime parity-safe gating still happens in chiaki_media_session_start_cloud_strings()
	 * which returns CHIAKI_ERR_UNINITIALIZED when E2 is disabled.
	 */
	out_capabilities->supports_start_cloud_strings = true;
	out_capabilities->supports_stop = true;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_capabilities_compat(
	void *out_capabilities_buf,
	size_t out_capabilities_size)
{
	if(!out_capabilities_buf || out_capabilities_size == 0)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiMediaCapabilities full = {0};
	ChiakiErrorCode err = chiaki_media_capabilities(&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	memset(out_capabilities_buf, 0, out_capabilities_size);
	size_t copy_size = out_capabilities_size < sizeof(full) ? out_capabilities_size : sizeof(full);
	memcpy(out_capabilities_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_create(
	ChiakiMediaSession **out_session,
	const ChiakiMediaCreateInfo *create_info)
{
	if(!out_session || !create_info)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiMediaSession *session = calloc(1, sizeof(*session));
	if(!session)
		return CHIAKI_ERR_MEMORY;
	chiaki_mutex_init(&session->stats_mutex, false);
	session->stats.state = CHIAKI_MEDIA_SESSION_STATE_CREATED;

	if(create_info->callbacks)
	{
		session->callbacks = *create_info->callbacks;
		session->callbacks_set = true;
	}
	session->internal_callbacks.video_frame_cb = chiaki_media_stats_on_video_frame;
	session->internal_callbacks.audio_frame_cb = chiaki_media_stats_on_audio_frame;
	session->internal_callbacks.event_cb = chiaki_media_stats_on_event;
	session->internal_callbacks.user = session;

	*out_session = session;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_media_session_destroy(ChiakiMediaSession *session)
{
	if(session && session->headless_session)
	{
		chiaki_headless_session_stop(session->headless_session);
		chiaki_headless_session_join(session->headless_session);
		chiaki_headless_session_destroy(session->headless_session);
		session->headless_session = NULL;
		session->started = false;
	}
	if(session)
		chiaki_mutex_fini(&session->stats_mutex);
	free(session);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_start_cloud_strings(
	ChiakiMediaSession *session,
	const char *host,
	uint16_t stream_port,
	const char *session_id,
	const char *launch_spec,
	const char *morning_b64,
	const char *regist_key_hex,
	bool ps5,
	bool enable_dualsense,
	bool enable_keyboard,
	ChiakiVideoResolutionPreset resolution,
	ChiakiVideoFPSPreset fps,
	unsigned int bitrate,
	ChiakiCodec codec,
	const char *ffmpeg_hw_decoder_name)
{
	if(!session)
		return CHIAKI_ERR_INVALID_DATA;
	if(!chiaki_media_e2_enabled())
		return CHIAKI_ERR_UNINITIALIZED;
	if(session->started || session->headless_session)
		return CHIAKI_ERR_MUTEX_LOCKED;
	chiaki_mutex_lock(&session->stats_mutex);
	session->stats.state = CHIAKI_MEDIA_SESSION_STATE_STARTING;
	chiaki_mutex_unlock(&session->stats_mutex);

	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	uint8_t regist_key[CHIAKI_SESSION_AUTH_SIZE] = {0};
	ChiakiHeadlessCloudLaunchInfo launch = {0};
	ChiakiErrorCode err = headless_build_cloud_launch_from_strings(
		&launch,
		morning,
		sizeof(morning),
		regist_key,
		sizeof(regist_key),
		host,
		stream_port,
		session_id,
		launch_spec,
		morning_b64,
		regist_key_hex,
		ps5,
		enable_dualsense,
		enable_keyboard,
		resolution,
		fps,
		bitrate,
		codec);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_mutex_lock(&session->stats_mutex);
		session->stats.state = CHIAKI_MEDIA_SESSION_STATE_ERROR;
		chiaki_mutex_unlock(&session->stats_mutex);
		return err;
	}

	ChiakiConnectInfo connect_info = {0};
	err = chiaki_headless_connect_info_init_cloud_direct(&connect_info, &launch);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_mutex_lock(&session->stats_mutex);
		session->stats.state = CHIAKI_MEDIA_SESSION_STATE_ERROR;
		chiaki_mutex_unlock(&session->stats_mutex);
		return err;
	}

	ChiakiLog *log = NULL;
	if(g_runtime_log_init)
		log = &g_runtime_log;
	else
	{
		chiaki_log_init(&g_runtime_log, CHIAKI_LOG_ALL, NULL, NULL);
		g_runtime_log_init = true;
		log = &g_runtime_log;
	}

	ChiakiHeadlessCreateInfo create_info = {
		.connect_info = connect_info,
		.ffmpeg_hw_decoder_name = ffmpeg_hw_decoder_name,
		.callbacks = &session->internal_callbacks,
		.log = log,
	};

	ChiakiHeadlessSession *headless_session = NULL;
	err = chiaki_headless_session_create(&headless_session, &create_info);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	err = chiaki_headless_session_start(headless_session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_headless_session_destroy(headless_session);
		chiaki_mutex_lock(&session->stats_mutex);
		session->stats.state = CHIAKI_MEDIA_SESSION_STATE_ERROR;
		chiaki_mutex_unlock(&session->stats_mutex);
		return err;
	}

	session->headless_session = headless_session;
	session->started = true;
	chiaki_mutex_lock(&session->stats_mutex);
	session->stats.started = true;
	session->stats.state = CHIAKI_MEDIA_SESSION_STATE_RUNNING;
	chiaki_mutex_unlock(&session->stats_mutex);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_stop(ChiakiMediaSession *session)
{
	if(!session)
		return CHIAKI_ERR_INVALID_DATA;
	if(!session->headless_session)
		return CHIAKI_ERR_SUCCESS;
	chiaki_mutex_lock(&session->stats_mutex);
	session->stats.state = CHIAKI_MEDIA_SESSION_STATE_STOPPING;
	chiaki_mutex_unlock(&session->stats_mutex);

	ChiakiErrorCode stop_err = chiaki_headless_session_stop(session->headless_session);
	ChiakiErrorCode join_err = chiaki_headless_session_join(session->headless_session);
	chiaki_headless_session_destroy(session->headless_session);
	session->headless_session = NULL;
	session->started = false;
	chiaki_mutex_lock(&session->stats_mutex);
	session->stats.started = false;
	session->stats.state = CHIAKI_MEDIA_SESSION_STATE_STOPPED;
	chiaki_mutex_unlock(&session->stats_mutex);
	if(stop_err != CHIAKI_ERR_SUCCESS)
		return stop_err;
	if(join_err != CHIAKI_ERR_SUCCESS)
		return join_err;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_stats(
	ChiakiMediaSession *session,
	ChiakiMediaSessionStats *out_stats)
{
	if(!session || !out_stats)
		return CHIAKI_ERR_INVALID_DATA;
	chiaki_mutex_lock(&session->stats_mutex);
	*out_stats = session->stats;
	chiaki_mutex_unlock(&session->stats_mutex);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_media_session_stats_size(void)
{
	return sizeof(ChiakiMediaSessionStats);
}

CHIAKI_EXPORT size_t chiaki_media_session_diagnostics_snapshot_size(void)
{
	return sizeof(ChiakiMediaSessionDiagnosticsSnapshot);
}

CHIAKI_EXPORT void chiaki_media_session_diagnostics_snapshot_init(
	ChiakiMediaSessionDiagnosticsSnapshot *snapshot)
{
	if(!snapshot)
		return;
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->api_version = chiaki_headless_api_version();
	chiaki_media_session_stats_init(&snapshot->stats);
	chiaki_media_readiness_report_init(&snapshot->readiness);
	chiaki_media_health_report_init(&snapshot->health);
}

CHIAKI_EXPORT void chiaki_media_session_stats_init(ChiakiMediaSessionStats *stats)
{
	if(!stats)
		return;
	memset(stats, 0, sizeof(*stats));
	stats->last_event_type = CHIAKI_HEADLESS_EVENT_CONNECTING;
	stats->state = CHIAKI_MEDIA_SESSION_STATE_CREATED;
	stats->e3_master_gate_enabled = chiaki_media_e3_enabled();
	stats->e3_audio_gate_enabled = chiaki_media_e3_audio_enabled();
	stats->e3_sync_gate_enabled = chiaki_media_e3_sync_enabled();
	stats->e3_audio_effective = stats->e3_master_gate_enabled && stats->e3_audio_gate_enabled;
	stats->e3_sync_effective = stats->e3_master_gate_enabled && stats->e3_sync_gate_enabled;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_stats_compat(
	ChiakiMediaSession *session,
	void *out_stats_buf,
	size_t out_stats_size)
{
	if(!session || !out_stats_buf || out_stats_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaSessionStats full = {0};
	ChiakiErrorCode err = chiaki_media_session_get_stats(session, &full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_stats_size < sizeof(full) ? out_stats_size : sizeof(full);
	memcpy(out_stats_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_wait_for_video_frame(
	ChiakiMediaSession *session,
	uint32_t timeout_ms,
	bool *out_received)
{
	if(!session || !out_received)
		return CHIAKI_ERR_INVALID_DATA;

	*out_received = false;
	uint64_t deadline_us = chiaki_time_now_monotonic_us() + ((uint64_t)timeout_ms * 1000);
	while(chiaki_time_now_monotonic_us() < deadline_us)
	{
		chiaki_mutex_lock(&session->stats_mutex);
		uint64_t frame_count = session->stats.video_frame_count;
		bool started = session->stats.started;
		chiaki_mutex_unlock(&session->stats_mutex);

		if(frame_count > 0)
		{
			*out_received = true;
			return CHIAKI_ERR_SUCCESS;
		}
		if(!started)
			return CHIAKI_ERR_SUCCESS;
#ifdef _WIN32
		Sleep(10);
#else
		usleep(1000 * 10);
#endif
	}
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_wait_for_event(
	ChiakiMediaSession *session,
	ChiakiHeadlessEventType event_type,
	uint32_t timeout_ms,
	bool *out_received)
{
	if(!session || !out_received)
		return CHIAKI_ERR_INVALID_DATA;
	*out_received = false;

	uint64_t deadline_us = chiaki_time_now_monotonic_us() + ((uint64_t)timeout_ms * 1000);
	while(chiaki_time_now_monotonic_us() < deadline_us)
	{
		chiaki_mutex_lock(&session->stats_mutex);
		ChiakiHeadlessEventType last_event = session->stats.last_event_type;
		bool started = session->stats.started;
		chiaki_mutex_unlock(&session->stats_mutex);

		if(last_event == event_type)
		{
			*out_received = true;
			return CHIAKI_ERR_SUCCESS;
		}
		if(!started)
			return CHIAKI_ERR_SUCCESS;
#ifdef _WIN32
		Sleep(10);
#else
		usleep(1000 * 10);
#endif
	}
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_wait_for_readiness(
	ChiakiMediaSession *session,
	uint32_t timeout_ms,
	ChiakiMediaReadinessReport *out_report)
{
	if(!session || !out_report)
		return CHIAKI_ERR_INVALID_DATA;

	memset(out_report, 0, sizeof(*out_report));
	uint64_t deadline_us = chiaki_time_now_monotonic_us() + ((uint64_t)timeout_ms * 1000);
	while(chiaki_time_now_monotonic_us() < deadline_us)
	{
		chiaki_mutex_lock(&session->stats_mutex);
		out_report->started = session->stats.started;
		out_report->state = session->stats.state;
		out_report->video_frame_count = session->stats.video_frame_count;
		out_report->event_count = session->stats.event_count;
		out_report->ready_event_count = session->stats.ready_event_count;
		out_report->stopped_event_count = session->stats.stopped_event_count;
		out_report->error_event_count = session->stats.error_event_count;
		out_report->last_event_monotonic_us = session->stats.last_event_monotonic_us;
		out_report->last_event_type = session->stats.last_event_type;
		chiaki_mutex_unlock(&session->stats_mutex);

		out_report->seen_video_frame = out_report->video_frame_count > 0;
		out_report->seen_ready_event = out_report->last_event_type == CHIAKI_HEADLESS_EVENT_READY;
		out_report->seen_terminal_event =
			out_report->last_event_type == CHIAKI_HEADLESS_EVENT_STOPPED
			|| out_report->last_event_type == CHIAKI_HEADLESS_EVENT_ERROR;

		if(out_report->seen_ready_event && out_report->seen_video_frame)
			return CHIAKI_ERR_SUCCESS;
		if(!out_report->started || out_report->seen_terminal_event)
			return CHIAKI_ERR_SUCCESS;
#ifdef _WIN32
		Sleep(10);
#else
		usleep(1000 * 10);
#endif
	}

	chiaki_mutex_lock(&session->stats_mutex);
	out_report->started = session->stats.started;
	out_report->state = session->stats.state;
	out_report->video_frame_count = session->stats.video_frame_count;
	out_report->event_count = session->stats.event_count;
	out_report->ready_event_count = session->stats.ready_event_count;
	out_report->stopped_event_count = session->stats.stopped_event_count;
	out_report->error_event_count = session->stats.error_event_count;
	out_report->last_event_monotonic_us = session->stats.last_event_monotonic_us;
	out_report->last_event_type = session->stats.last_event_type;
	chiaki_mutex_unlock(&session->stats_mutex);
	out_report->seen_video_frame = out_report->video_frame_count > 0;
	out_report->seen_ready_event = out_report->last_event_type == CHIAKI_HEADLESS_EVENT_READY;
	out_report->seen_terminal_event =
		out_report->last_event_type == CHIAKI_HEADLESS_EVENT_STOPPED
		|| out_report->last_event_type == CHIAKI_HEADLESS_EVENT_ERROR;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_media_readiness_report_size(void)
{
	return sizeof(ChiakiMediaReadinessReport);
}

CHIAKI_EXPORT void chiaki_media_readiness_report_init(ChiakiMediaReadinessReport *report)
{
	if(!report)
		return;
	memset(report, 0, sizeof(*report));
	report->last_event_type = CHIAKI_HEADLESS_EVENT_CONNECTING;
	report->state = CHIAKI_MEDIA_SESSION_STATE_CREATED;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_wait_for_readiness_compat(
	ChiakiMediaSession *session,
	uint32_t timeout_ms,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!session || !out_report_buf || out_report_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaReadinessReport full = {0};
	ChiakiErrorCode err = chiaki_media_session_wait_for_readiness(session, timeout_ms, &full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_report_size < sizeof(full) ? out_report_size : sizeof(full);
	memcpy(out_report_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_media_health_report_size(void)
{
	return sizeof(ChiakiMediaHealthReport);
}

CHIAKI_EXPORT void chiaki_media_health_report_init(ChiakiMediaHealthReport *report)
{
	if(!report)
		return;
	memset(report, 0, sizeof(*report));
	report->session_state = CHIAKI_MEDIA_SESSION_STATE_CREATED;
	report->last_event_type = CHIAKI_HEADLESS_EVENT_CONNECTING;
	report->health_state = CHIAKI_MEDIA_HEALTH_IDLE;
}

CHIAKI_EXPORT void chiaki_media_health_policy_init(ChiakiMediaHealthPolicy *policy)
{
	if(!policy)
		return;
	memset(policy, 0, sizeof(*policy));
	policy->terminal_error_event_count = 1;
	policy->degraded_no_video_min_event_count = 3;
	policy->treat_stopped_as_terminal = true;
	policy->treat_error_event_as_terminal = true;
}

CHIAKI_EXPORT size_t chiaki_media_health_policy_size(void)
{
	return sizeof(ChiakiMediaHealthPolicy);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_defaults(
	ChiakiMediaHealthPolicy *out_policy)
{
	if(!out_policy)
		return CHIAKI_ERR_INVALID_DATA;
	chiaki_media_health_policy_init(out_policy);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_from_profile(
	ChiakiMediaHealthPolicyProfile profile,
	ChiakiMediaHealthPolicy *out_policy)
{
	if(!out_policy)
		return CHIAKI_ERR_INVALID_DATA;
	chiaki_media_health_policy_init(out_policy);
	switch(profile)
	{
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT:
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE:
			out_policy->terminal_error_event_count = 1;
			out_policy->degraded_no_video_min_event_count = 1;
			out_policy->treat_stopped_as_terminal = true;
			out_policy->treat_error_event_as_terminal = true;
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE:
			out_policy->terminal_error_event_count = 3;
			out_policy->degraded_no_video_min_event_count = 6;
			out_policy->treat_stopped_as_terminal = false;
			out_policy->treat_error_event_as_terminal = false;
			return CHIAKI_ERR_SUCCESS;
		default:
			return CHIAKI_ERR_INVALID_DATA;
	}
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_defaults_compat(
	void *out_policy_buf,
	size_t out_policy_size)
{
	if(!out_policy_buf || out_policy_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaHealthPolicy full = {0};
	ChiakiErrorCode err = chiaki_media_health_policy_defaults(&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_policy_size < sizeof(full) ? out_policy_size : sizeof(full);
	memcpy(out_policy_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_from_profile_compat(
	ChiakiMediaHealthPolicyProfile profile,
	void *out_policy_buf,
	size_t out_policy_size)
{
	if(!out_policy_buf || out_policy_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaHealthPolicy full = {0};
	ChiakiErrorCode err = chiaki_media_health_policy_from_profile(profile, &full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_policy_size < sizeof(full) ? out_policy_size : sizeof(full);
	memcpy(out_policy_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_media_health_policy_profile_count(void)
{
	return 3;
}

CHIAKI_EXPORT size_t chiaki_media_health_policy_profile_info_size(void)
{
	return sizeof(ChiakiMediaHealthPolicyProfileInfo);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_info(
	size_t index,
	ChiakiMediaHealthPolicyProfileInfo *out_info)
{
	if(!out_info)
		return CHIAKI_ERR_INVALID_DATA;
	if(index >= chiaki_media_health_policy_profile_count())
		return CHIAKI_ERR_INVALID_DATA;
	memset(out_info, 0, sizeof(*out_info));
	out_info->profile = (ChiakiMediaHealthPolicyProfile)index;
	out_info->is_default_profile = index == CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	switch(out_info->profile)
	{
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT:
			out_info->profile_key = "default";
			out_info->display_label = "Default";
			break;
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE:
			out_info->profile_key = "aggressive";
			out_info->display_label = "Aggressive";
			break;
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE:
			out_info->profile_key = "conservative";
			out_info->display_label = "Conservative";
			break;
		default:
			return CHIAKI_ERR_INVALID_DATA;
	}
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_info_compat(
	size_t index,
	void *out_info_buf,
	size_t out_info_size)
{
	if(!out_info_buf || out_info_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaHealthPolicyProfileInfo full = {0};
	ChiakiErrorCode err = chiaki_media_health_policy_profile_info(index, &full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_info_size < sizeof(full) ? out_info_size : sizeof(full);
	memcpy(out_info_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_from_key(
	const char *profile_key,
	ChiakiMediaHealthPolicyProfile *out_profile)
{
	if(!profile_key || !out_profile)
		return CHIAKI_ERR_INVALID_DATA;
	if(strcmp(profile_key, "default") == 0)
	{
		*out_profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
		return CHIAKI_ERR_SUCCESS;
	}
	if(strcmp(profile_key, "aggressive") == 0)
	{
		*out_profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE;
		return CHIAKI_ERR_SUCCESS;
	}
	if(strcmp(profile_key, "conservative") == 0)
	{
		*out_profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE;
		return CHIAKI_ERR_SUCCESS;
	}
	return CHIAKI_ERR_INVALID_DATA;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_key_normalize(
	const char *input_profile_key,
	const char **out_canonical_profile_key)
{
	if(!input_profile_key || !out_canonical_profile_key)
		return CHIAKI_ERR_INVALID_DATA;
	char normalized[64] = {0};
	size_t n = 0;
	for(const char *p = input_profile_key; *p && n + 1 < sizeof(normalized); ++p)
	{
		unsigned char c = (unsigned char)*p;
		if(c == ' ' || c == '-' || c == '_')
			continue;
		normalized[n++] = (char)tolower(c);
	}
	normalized[n] = '\0';
	if(strcmp(normalized, "default") == 0)
	{
		*out_canonical_profile_key = "default";
		return CHIAKI_ERR_SUCCESS;
	}
	if(strcmp(normalized, "aggressive") == 0)
	{
		*out_canonical_profile_key = "aggressive";
		return CHIAKI_ERR_SUCCESS;
	}
	if(strcmp(normalized, "conservative") == 0)
	{
		*out_canonical_profile_key = "conservative";
		return CHIAKI_ERR_SUCCESS;
	}
	return CHIAKI_ERR_INVALID_DATA;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_from_key_compat(
	const char *profile_key,
	void *out_profile_buf,
	size_t out_profile_size)
{
	if(!profile_key || !out_profile_buf || out_profile_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaHealthPolicyProfile profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	ChiakiErrorCode err = chiaki_media_health_policy_profile_from_key(profile_key, &profile);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_profile_size < sizeof(profile) ? out_profile_size : sizeof(profile);
	memcpy(out_profile_buf, &profile, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_key_normalize_compat(
	const char *input_profile_key,
	void *out_profile_buf,
	size_t out_profile_size)
{
	if(!input_profile_key || !out_profile_buf || out_profile_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	const char *canonical = NULL;
	ChiakiErrorCode err = chiaki_media_health_policy_profile_key_normalize(input_profile_key, &canonical);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	ChiakiMediaHealthPolicyProfile profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	err = chiaki_media_health_policy_profile_from_key(canonical, &profile);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_profile_size < sizeof(profile) ? out_profile_size : sizeof(profile);
	memcpy(out_profile_buf, &profile, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT size_t chiaki_media_health_policy_profile_resolution_size(void)
{
	return sizeof(ChiakiMediaHealthPolicyProfileResolution);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_resolve(
	const char *input_profile_key,
	ChiakiMediaHealthPolicyProfileResolution *out_resolution)
{
	if(!input_profile_key || !out_resolution)
		return CHIAKI_ERR_INVALID_DATA;
	const char *canonical = NULL;
	ChiakiErrorCode err = chiaki_media_health_policy_profile_key_normalize(input_profile_key, &canonical);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	ChiakiMediaHealthPolicyProfile profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	err = chiaki_media_health_policy_profile_from_key(canonical, &profile);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	ChiakiMediaHealthPolicyProfileInfo info = {0};
	err = chiaki_media_health_policy_profile_info((size_t)profile, &info);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	memset(out_resolution, 0, sizeof(*out_resolution));
	out_resolution->profile = profile;
	out_resolution->canonical_profile_key = canonical;
	out_resolution->display_label = info.display_label;
	out_resolution->is_default_profile = info.is_default_profile;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_resolve_compat(
	const char *input_profile_key,
	void *out_resolution_buf,
	size_t out_resolution_size)
{
	if(!input_profile_key || !out_resolution_buf || out_resolution_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaHealthPolicyProfileResolution full = {0};
	ChiakiErrorCode err = chiaki_media_health_policy_profile_resolve(input_profile_key, &full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_resolution_size < sizeof(full) ? out_resolution_size : sizeof(full);
	memcpy(out_resolution_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_key_from_profile(
	ChiakiMediaHealthPolicyProfile profile,
	const char **out_profile_key)
{
	if(!out_profile_key)
		return CHIAKI_ERR_INVALID_DATA;
	switch(profile)
	{
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT:
			*out_profile_key = "default";
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE:
			*out_profile_key = "aggressive";
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE:
			*out_profile_key = "conservative";
			return CHIAKI_ERR_SUCCESS;
		default:
			return CHIAKI_ERR_INVALID_DATA;
	}
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_evaluate_readiness(
	const ChiakiMediaReadinessReport *readiness,
	const ChiakiMediaHealthPolicy *policy,
	ChiakiMediaHealthReport *out_report)
{
	if(!readiness || !out_report)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiMediaHealthPolicy local_policy = {0};
	chiaki_media_health_policy_init(&local_policy);
	const ChiakiMediaHealthPolicy *p = policy ? policy : &local_policy;

	chiaki_media_health_report_init(out_report);
	out_report->session_state = readiness->state;
	out_report->started = readiness->started;
	out_report->seen_video_frame = readiness->seen_video_frame;
	out_report->seen_ready_event = readiness->seen_ready_event;
	out_report->seen_terminal_event = readiness->seen_terminal_event;
	out_report->video_frame_count = readiness->video_frame_count;
	out_report->event_count = readiness->event_count;
	out_report->error_event_count = readiness->error_event_count;
	out_report->last_event_monotonic_us = readiness->last_event_monotonic_us;
	out_report->last_event_type = readiness->last_event_type;

	bool terminal_by_state = readiness->state == CHIAKI_MEDIA_SESSION_STATE_ERROR;
	bool terminal_by_event = readiness->seen_terminal_event
		&& ((p->treat_stopped_as_terminal && readiness->last_event_type == CHIAKI_HEADLESS_EVENT_STOPPED)
			|| (p->treat_error_event_as_terminal && readiness->last_event_type == CHIAKI_HEADLESS_EVENT_ERROR));
	bool terminal_by_error_count = readiness->error_event_count >= p->terminal_error_event_count
		&& p->terminal_error_event_count > 0;

	if(terminal_by_state || terminal_by_event || terminal_by_error_count)
	{
		out_report->health_state = CHIAKI_MEDIA_HEALTH_TERMINAL;
		return CHIAKI_ERR_SUCCESS;
	}
	if(readiness->seen_ready_event && readiness->seen_video_frame)
	{
		out_report->health_state = CHIAKI_MEDIA_HEALTH_READY;
		return CHIAKI_ERR_SUCCESS;
	}
	if(readiness->started
		|| readiness->state == CHIAKI_MEDIA_SESSION_STATE_STARTING
		|| readiness->state == CHIAKI_MEDIA_SESSION_STATE_RUNNING)
	{
		out_report->health_state = CHIAKI_MEDIA_HEALTH_STARTING;
		return CHIAKI_ERR_SUCCESS;
	}
	if(readiness->event_count >= p->degraded_no_video_min_event_count
		&& p->degraded_no_video_min_event_count > 0
		&& !readiness->seen_video_frame)
	{
		out_report->health_state = CHIAKI_MEDIA_HEALTH_DEGRADED;
		return CHIAKI_ERR_SUCCESS;
	}
	out_report->health_state = CHIAKI_MEDIA_HEALTH_IDLE;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report(
	ChiakiMediaSession *session,
	ChiakiMediaHealthReport *out_report)
{
	if(!session || !out_report)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiMediaReadinessReport readiness = {0};
	ChiakiErrorCode err = chiaki_media_session_wait_for_readiness(session, 0, &readiness);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	return chiaki_media_health_evaluate_readiness(&readiness, NULL, out_report);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_with_policy(
	ChiakiMediaSession *session,
	const ChiakiMediaHealthPolicy *policy,
	ChiakiMediaHealthReport *out_report)
{
	if(!session || !out_report)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiMediaReadinessReport readiness = {0};
	ChiakiErrorCode err = chiaki_media_session_wait_for_readiness(session, 0, &readiness);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	return chiaki_media_health_evaluate_readiness(&readiness, policy, out_report);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_compat(
	ChiakiMediaSession *session,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!session || !out_report_buf || out_report_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaHealthReport full = {0};
	ChiakiErrorCode err = chiaki_media_session_get_health_report(session, &full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_report_size < sizeof(full) ? out_report_size : sizeof(full);
	memcpy(out_report_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_with_policy_compat(
	ChiakiMediaSession *session,
	const ChiakiMediaHealthPolicy *policy,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!session || !out_report_buf || out_report_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaHealthReport full = {0};
	ChiakiErrorCode err = chiaki_media_session_get_health_report_with_policy(session, policy, &full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_report_size < sizeof(full) ? out_report_size : sizeof(full);
	memcpy(out_report_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_with_profile(
	ChiakiMediaSession *session,
	ChiakiMediaHealthPolicyProfile profile,
	ChiakiMediaHealthReport *out_report)
{
	if(!session || !out_report)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaHealthPolicy policy = {0};
	ChiakiErrorCode err = chiaki_media_health_policy_from_profile(profile, &policy);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_media_session_get_health_report_with_policy(session, &policy, out_report);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_with_profile_compat(
	ChiakiMediaSession *session,
	ChiakiMediaHealthPolicyProfile profile,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!session || !out_report_buf || out_report_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaHealthReport full = {0};
	ChiakiErrorCode err = chiaki_media_session_get_health_report_with_profile(session, profile, &full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_report_size < sizeof(full) ? out_report_size : sizeof(full);
	memcpy(out_report_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_with_profile_key(
	ChiakiMediaSession *session,
	const char *profile_key,
	ChiakiMediaHealthReport *out_report)
{
	if(!session || !profile_key || !out_report)
		return CHIAKI_ERR_INVALID_DATA;
	const char *canonical = NULL;
	ChiakiErrorCode err = chiaki_media_health_policy_profile_key_normalize(profile_key, &canonical);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	ChiakiMediaHealthPolicyProfile profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	err = chiaki_media_health_policy_profile_from_key(canonical, &profile);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_media_session_get_health_report_with_profile(session, profile, out_report);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_with_profile_key_compat(
	ChiakiMediaSession *session,
	const char *profile_key,
	void *out_report_buf,
	size_t out_report_size)
{
	if(!session || !profile_key || !out_report_buf || out_report_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaHealthReport full = {0};
	ChiakiErrorCode err = chiaki_media_session_get_health_report_with_profile_key(
		session,
		profile_key,
		&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_report_size < sizeof(full) ? out_report_size : sizeof(full);
	memcpy(out_report_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot)
{
	if(!session || !out_snapshot)
		return CHIAKI_ERR_INVALID_DATA;
	chiaki_media_session_diagnostics_snapshot_init(out_snapshot);

	ChiakiErrorCode err = chiaki_media_session_get_stats(session, &out_snapshot->stats);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	err = chiaki_media_session_wait_for_readiness(session, readiness_timeout_ms, &out_snapshot->readiness);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	err = chiaki_media_health_evaluate_readiness(&out_snapshot->readiness, NULL, &out_snapshot->health);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_compat(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	void *out_snapshot_buf,
	size_t out_snapshot_size)
{
	if(!session || !out_snapshot_buf || out_snapshot_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaSessionDiagnosticsSnapshot full = {0};
	ChiakiErrorCode err = chiaki_media_session_get_diagnostics_snapshot(
		session,
		readiness_timeout_ms,
		&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_snapshot_size < sizeof(full) ? out_snapshot_size : sizeof(full);
	memcpy(out_snapshot_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_with_policy(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	const ChiakiMediaHealthPolicy *policy,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot)
{
	if(!session || !out_snapshot)
		return CHIAKI_ERR_INVALID_DATA;
	chiaki_media_session_diagnostics_snapshot_init(out_snapshot);

	ChiakiErrorCode err = chiaki_media_session_get_stats(session, &out_snapshot->stats);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	err = chiaki_media_session_wait_for_readiness(session, readiness_timeout_ms, &out_snapshot->readiness);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	err = chiaki_media_health_evaluate_readiness(&out_snapshot->readiness, policy, &out_snapshot->health);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_with_policy_compat(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	const ChiakiMediaHealthPolicy *policy,
	void *out_snapshot_buf,
	size_t out_snapshot_size)
{
	if(!session || !out_snapshot_buf || out_snapshot_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaSessionDiagnosticsSnapshot full = {0};
	ChiakiErrorCode err = chiaki_media_session_get_diagnostics_snapshot_with_policy(
		session,
		readiness_timeout_ms,
		policy,
		&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_snapshot_size < sizeof(full) ? out_snapshot_size : sizeof(full);
	memcpy(out_snapshot_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_with_profile(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	ChiakiMediaHealthPolicyProfile profile,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot)
{
	if(!session || !out_snapshot)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaHealthPolicy policy = {0};
	ChiakiErrorCode err = chiaki_media_health_policy_from_profile(profile, &policy);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_media_session_get_diagnostics_snapshot_with_policy(
		session,
		readiness_timeout_ms,
		&policy,
		out_snapshot);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_with_profile_compat(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	ChiakiMediaHealthPolicyProfile profile,
	void *out_snapshot_buf,
	size_t out_snapshot_size)
{
	if(!session || !out_snapshot_buf || out_snapshot_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaSessionDiagnosticsSnapshot full = {0};
	ChiakiErrorCode err = chiaki_media_session_get_diagnostics_snapshot_with_profile(
		session,
		readiness_timeout_ms,
		profile,
		&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_snapshot_size < sizeof(full) ? out_snapshot_size : sizeof(full);
	memcpy(out_snapshot_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_with_profile_key(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot)
{
	if(!session || !profile_key || !out_snapshot)
		return CHIAKI_ERR_INVALID_DATA;
	const char *canonical = NULL;
	ChiakiErrorCode err = chiaki_media_health_policy_profile_key_normalize(profile_key, &canonical);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	ChiakiMediaHealthPolicyProfile profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	err = chiaki_media_health_policy_profile_from_key(canonical, &profile);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return chiaki_media_session_get_diagnostics_snapshot_with_profile(
		session,
		readiness_timeout_ms,
		profile,
		out_snapshot);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_with_profile_key_compat(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	void *out_snapshot_buf,
	size_t out_snapshot_size)
{
	if(!session || !profile_key || !out_snapshot_buf || out_snapshot_size == 0)
		return CHIAKI_ERR_INVALID_DATA;
	ChiakiMediaSessionDiagnosticsSnapshot full = {0};
	ChiakiErrorCode err = chiaki_media_session_get_diagnostics_snapshot_with_profile_key(
		session,
		readiness_timeout_ms,
		profile_key,
		&full);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	size_t copy_size = out_snapshot_size < sizeof(full) ? out_snapshot_size : sizeof(full);
	memcpy(out_snapshot_buf, &full, copy_size);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_effective_launch_info(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	ChiakiHeadlessCloudLaunchInfo *out_launch_info)
{
	if(!launch_info || !out_launch_info)
		return CHIAKI_ERR_INVALID_DATA;

	HeadlessRuntimeConfig runtime_config = {0};
	ChiakiErrorCode err = headless_runtime_snapshot_config(&runtime_config);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	*out_launch_info = *launch_info;
	headless_apply_runtime_config_to_launch(out_launch_info, &runtime_config);
	err = headless_validate_launch_stream_fields(out_launch_info);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_stream_profile_overrides(const ChiakiHeadlessStreamProfileOverrides *overrides)
{
	ChiakiErrorCode validate_err = headless_validate_stream_profile_overrides(overrides);
	if(validate_err != CHIAKI_ERR_SUCCESS)
		return validate_err;

	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	if(g_runtime_session || g_runtime_start_in_flight)
	{
		chiaki_mutex_unlock(&g_runtime_lock);
		return CHIAKI_ERR_MUTEX_LOCKED;
	}

	if(overrides)
	{
		memcpy(&g_runtime_stream_profile_overrides, overrides, sizeof(g_runtime_stream_profile_overrides));
		g_runtime_stream_profile_overrides_set = true;
	}
	else
	{
		memset(&g_runtime_stream_profile_overrides, 0, sizeof(g_runtime_stream_profile_overrides));
		g_runtime_stream_profile_overrides_set = false;
	}
	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_launch_overrides(const ChiakiHeadlessLaunchOverrides *overrides)
{
	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	if(g_runtime_session || g_runtime_start_in_flight)
	{
		chiaki_mutex_unlock(&g_runtime_lock);
		return CHIAKI_ERR_MUTEX_LOCKED;
	}

	if(overrides)
	{
		memcpy(&g_runtime_launch_overrides, overrides, sizeof(g_runtime_launch_overrides));
		g_runtime_launch_overrides_set = true;
	}
	else
	{
		memset(&g_runtime_launch_overrides, 0, sizeof(g_runtime_launch_overrides));
		g_runtime_launch_overrides_set = false;
	}
	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_overrides(const ChiakiHeadlessRuntimeOverrides *overrides)
{
	if(overrides)
	{
		ChiakiErrorCode validate_err = headless_validate_stream_profile_overrides(
			overrides->has_stream_profile_overrides ? &overrides->stream_profile_overrides : NULL);
		if(validate_err != CHIAKI_ERR_SUCCESS)
			return validate_err;
		validate_err = headless_validate_policy_overrides(
			overrides->has_policy_overrides ? &overrides->policy_overrides : NULL);
		if(validate_err != CHIAKI_ERR_SUCCESS)
			return validate_err;
	}

	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	if(g_runtime_session || g_runtime_start_in_flight)
	{
		chiaki_mutex_unlock(&g_runtime_lock);
		return CHIAKI_ERR_MUTEX_LOCKED;
	}

	if(overrides)
	{
		if(overrides->has_launch_overrides)
		{
			g_runtime_launch_overrides = overrides->launch_overrides;
			g_runtime_launch_overrides_set = true;
		}
		else
		{
			memset(&g_runtime_launch_overrides, 0, sizeof(g_runtime_launch_overrides));
			g_runtime_launch_overrides_set = false;
		}

			if(overrides->has_stream_profile_overrides)
			{
				g_runtime_stream_profile_overrides = overrides->stream_profile_overrides;
				g_runtime_stream_profile_overrides_set = true;
			}
		else
		{
				memset(&g_runtime_stream_profile_overrides, 0, sizeof(g_runtime_stream_profile_overrides));
				g_runtime_stream_profile_overrides_set = false;
			}

			if(overrides->has_policy_overrides)
			{
				g_runtime_policy_overrides = overrides->policy_overrides;
				g_runtime_policy_overrides_set = true;
			}
			else
			{
				memset(&g_runtime_policy_overrides, 0, sizeof(g_runtime_policy_overrides));
				g_runtime_policy_overrides_set = false;
			}
		}
		else
		{
			memset(&g_runtime_launch_overrides, 0, sizeof(g_runtime_launch_overrides));
			g_runtime_launch_overrides_set = false;
			memset(&g_runtime_stream_profile_overrides, 0, sizeof(g_runtime_stream_profile_overrides));
			g_runtime_stream_profile_overrides_set = false;
			memset(&g_runtime_policy_overrides, 0, sizeof(g_runtime_policy_overrides));
			g_runtime_policy_overrides_set = false;
		}
	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_patch_overrides(
	const ChiakiHeadlessRuntimeOverrides *overrides,
	bool clear_unspecified)
{
	if(!overrides)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiErrorCode validate_err = headless_validate_stream_profile_overrides(
		overrides->has_stream_profile_overrides ? &overrides->stream_profile_overrides : NULL);
	if(validate_err != CHIAKI_ERR_SUCCESS)
		return validate_err;
	validate_err = headless_validate_policy_overrides(
		overrides->has_policy_overrides ? &overrides->policy_overrides : NULL);
	if(validate_err != CHIAKI_ERR_SUCCESS)
		return validate_err;

	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	if(g_runtime_session || g_runtime_start_in_flight)
	{
		chiaki_mutex_unlock(&g_runtime_lock);
		return CHIAKI_ERR_MUTEX_LOCKED;
	}

	if(overrides->has_launch_overrides)
	{
		g_runtime_launch_overrides = overrides->launch_overrides;
		g_runtime_launch_overrides_set = true;
	}
	else if(clear_unspecified)
	{
		memset(&g_runtime_launch_overrides, 0, sizeof(g_runtime_launch_overrides));
		g_runtime_launch_overrides_set = false;
	}

	if(overrides->has_stream_profile_overrides)
	{
		g_runtime_stream_profile_overrides = overrides->stream_profile_overrides;
		g_runtime_stream_profile_overrides_set = true;
	}
	else if(clear_unspecified)
	{
		memset(&g_runtime_stream_profile_overrides, 0, sizeof(g_runtime_stream_profile_overrides));
		g_runtime_stream_profile_overrides_set = false;
	}

	if(overrides->has_policy_overrides)
	{
		g_runtime_policy_overrides = overrides->policy_overrides;
		g_runtime_policy_overrides_set = true;
	}
	else if(clear_unspecified)
	{
		memset(&g_runtime_policy_overrides, 0, sizeof(g_runtime_policy_overrides));
		g_runtime_policy_overrides_set = false;
	}

	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_overrides(ChiakiHeadlessRuntimeOverrides *out_overrides)
{
	if(!out_overrides)
		return CHIAKI_ERR_INVALID_DATA;

	ChiakiErrorCode err = headless_runtime_ensure_lock();
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	chiaki_mutex_lock(&g_runtime_lock);
	out_overrides->has_launch_overrides = g_runtime_launch_overrides_set;
	out_overrides->launch_overrides = g_runtime_launch_overrides;
	out_overrides->has_stream_profile_overrides = g_runtime_stream_profile_overrides_set;
	out_overrides->stream_profile_overrides = g_runtime_stream_profile_overrides;
	out_overrides->has_policy_overrides = g_runtime_policy_overrides_set;
	out_overrides->policy_overrides = g_runtime_policy_overrides;
	chiaki_mutex_unlock(&g_runtime_lock);
	return CHIAKI_ERR_SUCCESS;
}
