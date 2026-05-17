// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_HEADLESS_H
#define CHIAKI_HEADLESS_H

#include <chiaki/common.h>
#include <chiaki/controller.h>
#include <chiaki/session.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_headless_session_t ChiakiHeadlessSession;

typedef enum chiaki_headless_video_format_t
{
	CHIAKI_HEADLESS_VIDEO_FORMAT_UNKNOWN = 0,
	CHIAKI_HEADLESS_VIDEO_FORMAT_YUV420P,
	CHIAKI_HEADLESS_VIDEO_FORMAT_NV12,
	CHIAKI_HEADLESS_VIDEO_FORMAT_P010LE,
	CHIAKI_HEADLESS_VIDEO_FORMAT_RGBA,
	CHIAKI_HEADLESS_VIDEO_FORMAT_DRM_PRIME,
} ChiakiHeadlessVideoFormat;

typedef enum chiaki_headless_audio_format_t
{
	CHIAKI_HEADLESS_AUDIO_FORMAT_UNKNOWN = 0,
	CHIAKI_HEADLESS_AUDIO_FORMAT_S16,
	CHIAKI_HEADLESS_AUDIO_FORMAT_F32,
} ChiakiHeadlessAudioFormat;

typedef struct chiaki_headless_video_frame_t
{
	ChiakiHeadlessVideoFormat format;
	uint32_t width;
	uint32_t height;
	const uint8_t *planes[4];
	int32_t strides[4];
	uint8_t plane_count;
	double pts_seconds;
	double duration_seconds;
	int32_t frames_lost;
	bool frame_recovered;
	uint64_t monotonic_time_us;
} ChiakiHeadlessVideoFrame;

typedef struct chiaki_headless_audio_frame_t
{
	ChiakiHeadlessAudioFormat format;
	uint32_t channels;
	uint32_t sample_rate;
	uint32_t frame_count;
	const void *data;
	size_t data_size;
	uint64_t monotonic_time_us;
} ChiakiHeadlessAudioFrame;

typedef enum chiaki_headless_external_video_frame_type_t
{
	CHIAKI_HEADLESS_EXTERNAL_VIDEO_FRAME_TYPE_NONE = 0,
	CHIAKI_HEADLESS_EXTERNAL_VIDEO_FRAME_TYPE_DMABUF_DRM_PRIME,
} ChiakiHeadlessExternalVideoFrameType;

typedef struct chiaki_headless_dmabuf_plane_t
{
	/* Ephemeral per-callback FD; host should dup() if it needs to retain it
	 * after callback return. */
	int32_t fd;
	uint32_t offset;
	uint32_t pitch;
	uint64_t modifier;
} ChiakiHeadlessDmabufPlane;

typedef struct chiaki_headless_external_video_frame_t
{
	ChiakiHeadlessExternalVideoFrameType type;
	uint32_t width;
	uint32_t height;
	uint32_t drm_format;
	uint8_t plane_count;
	ChiakiHeadlessDmabufPlane planes[4];
	double pts_seconds;
	double duration_seconds;
	int32_t frames_lost;
	bool frame_recovered;
	uint64_t monotonic_time_us;
} ChiakiHeadlessExternalVideoFrame;

typedef struct chiaki_headless_stats_t
{
	double measured_bitrate_kbps;
	double rtt_ms;
	double packet_loss_percent;
	uint32_t video_width;
	uint32_t video_height;
	uint64_t packets_received;
	uint64_t packets_lost;
	uint64_t video_frame_count;
	uint64_t audio_frame_count;
	uint64_t video_decode_lost_frames;
	uint64_t video_decode_recovered_frames;
	uint64_t video_decode_gap_event_count;
	uint64_t monotonic_time_us;
} ChiakiHeadlessStats;

typedef enum chiaki_headless_event_type_t
{
	CHIAKI_HEADLESS_EVENT_CONNECTING = 0,
	CHIAKI_HEADLESS_EVENT_READY,
	CHIAKI_HEADLESS_EVENT_STOPPING,
	CHIAKI_HEADLESS_EVENT_STOPPED,
	CHIAKI_HEADLESS_EVENT_STREAM_STATS,
	CHIAKI_HEADLESS_EVENT_WARNING,
	CHIAKI_HEADLESS_EVENT_ERROR,
} ChiakiHeadlessEventType;

typedef struct chiaki_headless_event_t
{
	ChiakiHeadlessEventType type;
	union
	{
		ChiakiHeadlessStats stats;
		struct
		{
			ChiakiQuitReason reason;
			const char *reason_str;
		} quit;
		struct
		{
			const char *message;
		} warning;
	};
} ChiakiHeadlessEvent;

typedef void (*ChiakiHeadlessVideoFrameCallback)(const ChiakiHeadlessVideoFrame *frame, void *user);
typedef void (*ChiakiHeadlessExternalVideoFrameCallback)(const ChiakiHeadlessExternalVideoFrame *frame, void *user);
typedef void (*ChiakiHeadlessAudioFrameCallback)(const ChiakiHeadlessAudioFrame *frame, void *user);
typedef void (*ChiakiHeadlessEventCallback)(const ChiakiHeadlessEvent *event, void *user);
typedef bool (*ChiakiHeadlessRuntimeAudioSinkStartCallback)(
	uint32_t sample_rate,
	uint32_t channels,
	ChiakiHeadlessAudioFormat format,
	void *user);
typedef bool (*ChiakiHeadlessRuntimeAudioSinkSubmitCallback)(
	const void *data,
	size_t data_size,
	uint32_t frame_count,
	uint64_t monotonic_time_us,
	void *user);
typedef void (*ChiakiHeadlessRuntimeAudioSinkStopCallback)(void *user);

typedef struct chiaki_headless_callbacks_t
{
	ChiakiHeadlessVideoFrameCallback video_frame_cb;
	ChiakiHeadlessExternalVideoFrameCallback external_video_frame_cb;
	ChiakiHeadlessAudioFrameCallback audio_frame_cb;
	ChiakiHeadlessEventCallback event_cb;
	void *user;
} ChiakiHeadlessCallbacks;

typedef struct chiaki_headless_runtime_audio_sink_config_t
{
	uint32_t api_version;
	bool enabled;
	/* When true, runtime sink consumes decoded audio and legacy audio_frame_cb
	 * is suppressed to avoid duplicate host-side transport/copies. */
	bool suppress_legacy_audio_callback;
	ChiakiHeadlessRuntimeAudioSinkStartCallback start_cb;
	ChiakiHeadlessRuntimeAudioSinkSubmitCallback submit_cb;
	ChiakiHeadlessRuntimeAudioSinkStopCallback stop_cb;
	void *user;
} ChiakiHeadlessRuntimeAudioSinkConfig;

typedef struct chiaki_headless_create_info_t
{
	ChiakiConnectInfo connect_info;
	const char *ffmpeg_hw_decoder_name;
	const ChiakiHeadlessCallbacks *callbacks;
	const ChiakiHeadlessRuntimeAudioSinkConfig *runtime_audio_sink_config;
	bool display_only_host_video_sink;
	ChiakiLog *log;
} ChiakiHeadlessCreateInfo;

typedef struct chiaki_headless_cloud_launch_info_t
{
	/**
	 * Cloud server host/IP (from Gaikai allocate serverInfo.serverIp).
	 */
	const char *host;
	/**
	 * Cloud Takion UDP port (from Gaikai allocate serverInfo.serverPort).
	 */
	uint16_t stream_port;
	/**
	 * Gaikai session id (from allocate sessionId).
	 */
	const char *session_id;
	/**
	 * launchSpecification string as received from allocate.
	 */
	const char *launch_spec;
	/**
	 * 16-byte morning/handshake key.
	 */
	const uint8_t *morning;
	size_t morning_size;
	/**
	 * 16-byte regist key. If null/size 0, zero-key is used.
	 */
	const uint8_t *regist_key;
	size_t regist_key_size;
	/**
	 * Rendering/input mode defaults.
	 */
	bool ps5;
	bool enable_dualsense;
	bool enable_keyboard;
	ChiakiVideoResolutionPreset resolution;
	ChiakiVideoFPSPreset fps;
	unsigned int bitrate;
	ChiakiCodec codec;
} ChiakiHeadlessCloudLaunchInfo;

typedef struct chiaki_headless_stream_profile_overrides_t
{
	bool use_resolution;
	ChiakiVideoResolutionPreset resolution;
	bool use_fps;
	ChiakiVideoFPSPreset fps;
	bool use_bitrate;
	unsigned int bitrate;
	bool use_codec;
	ChiakiCodec codec;
} ChiakiHeadlessStreamProfileOverrides;

typedef struct chiaki_headless_stream_profile_t
{
	ChiakiVideoResolutionPreset resolution;
	ChiakiVideoFPSPreset fps;
	unsigned int bitrate;
	ChiakiCodec codec;
} ChiakiHeadlessStreamProfile;

typedef struct chiaki_headless_launch_overrides_t
{
	bool use_ps5;
	bool ps5;
	bool use_enable_dualsense;
	bool enable_dualsense;
	bool use_enable_keyboard;
	bool enable_keyboard;
} ChiakiHeadlessLaunchOverrides;

typedef struct chiaki_headless_runtime_policy_overrides_t
{
	bool use_video_profile_auto_downgrade;
	bool video_profile_auto_downgrade;
	bool use_enable_idr_on_fec_failure;
	bool enable_idr_on_fec_failure;
	bool use_packet_loss_max;
	double packet_loss_max;
	bool use_display_only_host_video_sink;
	bool display_only_host_video_sink;
} ChiakiHeadlessRuntimePolicyOverrides;

typedef struct chiaki_headless_runtime_overrides_t
{
	bool has_launch_overrides;
	ChiakiHeadlessLaunchOverrides launch_overrides;
	bool has_stream_profile_overrides;
	ChiakiHeadlessStreamProfileOverrides stream_profile_overrides;
	bool has_policy_overrides;
	ChiakiHeadlessRuntimePolicyOverrides policy_overrides;
} ChiakiHeadlessRuntimeOverrides;

typedef struct chiaki_headless_runtime_sanity_report_t
{
	ChiakiErrorCode validation_error;
	ChiakiHeadlessCloudLaunchInfo effective_launch_info;
	ChiakiHeadlessStreamProfile effective_stream_profile;
	bool runtime_session_active;
	bool has_overrides;
} ChiakiHeadlessRuntimeSanityReport;

typedef struct chiaki_headless_runtime_state_snapshot_t
{
	uint32_t api_version;
	ChiakiHeadlessRuntimeOverrides overrides;
	ChiakiHeadlessRuntimeSanityReport sanity_report;
} ChiakiHeadlessRuntimeStateSnapshot;

typedef struct chiaki_headless_runtime_capabilities_t
{
	uint32_t api_version;
	size_t min_state_snapshot_size;
	size_t min_runtime_diagnostics_snapshot_size;
	size_t min_runtime_recovery_decision_size;
	size_t min_runtime_recovery_result_size;
	size_t min_runtime_recovery_tuning_size;
	size_t min_runtime_recovery_status_size;
	size_t min_runtime_recovery_config_size;
	size_t min_runtime_recovery_simulation_step_input_size;
	size_t min_runtime_recovery_simulation_health_step_input_size;
	size_t min_runtime_recovery_simulation_step_output_size;
	size_t min_runtime_recovery_simulation_report_size;
	size_t min_runtime_recovery_auto_loop_timeline_step_size;
	size_t min_runtime_recovery_auto_loop_timeline_summary_size;
	size_t min_runtime_host_status_size;
	size_t min_runtime_playback_readiness_status_size;
	size_t min_runtime_playback_continuity_status_size;
	size_t min_runtime_video_frame_metadata_size;
	size_t min_runtime_video_frame_poll_size;
	size_t min_runtime_audio_sink_config_size;
	size_t min_runtime_audio_sink_diagnostics_size;
	size_t min_runtime_recovery_parity_fixture_expected_size;
	size_t min_runtime_recovery_parity_fixture_result_size;
	size_t min_runtime_recovery_parity_smoke_result_size;
	size_t min_runtime_recovery_parity_smoke_runner_result_size;
	size_t min_runtime_recovery_parity_baseline_scenario_count_size;
	size_t min_runtime_recovery_parity_baseline_execution_detail_size;
	size_t min_runtime_recovery_core_diagnostics_summary_size;
	bool supports_runtime_struct_start;
	bool supports_runtime_cloud_start_strings;
	bool supports_runtime_cloud_stop;
	bool supports_runtime_overrides_bundle;
	bool supports_runtime_overrides_patch;
	bool supports_runtime_policy_overrides;
	bool supports_runtime_sanity_report;
	bool supports_runtime_state_snapshot;
	bool supports_runtime_state_snapshot_compat;
	bool supports_runtime_media_diagnostics_snapshot;
	bool supports_runtime_media_diagnostics_snapshot_compat;
	bool supports_runtime_media_diagnostics_snapshot_with_profile_key;
	bool supports_runtime_media_diagnostics_snapshot_with_profile_key_compat;
	bool supports_runtime_recovery_decision;
	bool supports_runtime_recovery_decision_compat;
	bool supports_runtime_recovery_decision_with_profile_key;
	bool supports_runtime_recovery_decision_with_profile_key_compat;
	bool supports_runtime_recovery_apply;
	bool supports_runtime_recovery_apply_compat;
	bool supports_runtime_recover_result;
	bool supports_runtime_recover_result_compat;
	bool supports_runtime_recover_result_with_profile_key;
	bool supports_runtime_recover_result_with_profile_key_compat;
	bool supports_runtime_recover_tuned;
	bool supports_runtime_recover_tuned_compat;
	bool supports_runtime_recovery_status;
	bool supports_runtime_recovery_status_compat;
	bool supports_runtime_recovery_status_reset;
	bool supports_runtime_recovery_config_set_get;
	bool supports_runtime_recovery_config_compat;
	bool supports_runtime_recovery_profile_key_set_get;
	bool supports_runtime_recover_auto;
	bool supports_runtime_recover_auto_compat;
	bool supports_runtime_recover_auto_with_status;
	bool supports_runtime_recover_auto_with_status_compat;
	bool supports_runtime_recovery_simulation;
	bool supports_runtime_recovery_simulation_compat;
	bool supports_runtime_recovery_simulation_sequence;
	bool supports_runtime_recovery_simulation_report;
	bool supports_runtime_recovery_simulation_sequence_compat;
	bool supports_runtime_recovery_simulation_report_compat;
	bool supports_runtime_recovery_simulation_sequence_with_report;
	bool supports_runtime_recovery_simulation_sequence_with_report_compat;
	bool supports_runtime_recovery_simulation_report_from_outputs;
	bool supports_runtime_recovery_simulation_report_from_outputs_compat;
	bool supports_runtime_recovery_simulation_sequence_with_profile_key;
	bool supports_runtime_recovery_simulation_report_with_profile_key;
	bool supports_runtime_recovery_simulation_sequence_with_profile_key_compat;
	bool supports_runtime_recovery_simulation_report_with_profile_key_compat;
	bool supports_runtime_recovery_simulation_sequence_with_profile_key_with_report;
	bool supports_runtime_recovery_simulation_sequence_with_profile_key_with_report_compat;
	bool supports_runtime_recovery_simulation_health_sequence_with_profile_key;
	bool supports_runtime_recovery_simulation_health_report_with_profile_key;
	bool supports_runtime_recovery_simulation_health_sequence_with_profile_key_compat;
	bool supports_runtime_recovery_simulation_health_report_with_profile_key_compat;
	bool supports_runtime_recovery_simulation_health_sequence_with_profile_key_with_report;
	bool supports_runtime_recovery_simulation_health_sequence_with_profile_key_with_report_compat;
	bool supports_runtime_recovery_auto_loop_timeline;
	bool supports_runtime_recovery_auto_loop_timeline_compat;
	bool supports_runtime_host_status;
	bool supports_runtime_host_status_compat;
	bool supports_runtime_playback_readiness_status;
	bool supports_runtime_playback_readiness_status_compat;
	bool supports_runtime_playback_continuity_status;
	bool supports_runtime_playback_continuity_status_compat;
	bool supports_runtime_video_frame_metadata;
	bool supports_runtime_video_frame_metadata_compat;
	bool supports_runtime_video_frame_poll;
	bool supports_runtime_video_frame_poll_compat;
	bool supports_runtime_audio_sink_config_set_get;
	bool supports_runtime_audio_sink_diagnostics;
	bool supports_runtime_audio_sink_diagnostics_compat;
	bool supports_runtime_audio_sink_underrun_report;
	bool supports_runtime_recovery_parity_fixture_eval;
	bool supports_runtime_recovery_parity_fixture_eval_compat;
	bool supports_runtime_recovery_parity_fixture_export;
	bool supports_runtime_recovery_parity_fixture_export_compat;
	bool supports_runtime_recovery_parity_smoke;
	bool supports_runtime_recovery_parity_smoke_compat;
	bool supports_runtime_recovery_parity_smoke_runner;
	bool supports_runtime_recovery_parity_smoke_runner_compat;
	bool supports_runtime_recovery_parity_baseline_smoke;
	bool supports_runtime_recovery_parity_baseline_smoke_compat;
	bool supports_runtime_recovery_parity_baseline_scenario_count;
	bool supports_runtime_recovery_parity_baseline_scenario_count_compat;
	bool supports_runtime_recovery_parity_baseline_scenario_label;
	bool supports_runtime_recovery_parity_baseline_scenario_label_compat;
	bool supports_runtime_recovery_parity_baseline_execution_details;
	bool supports_runtime_recovery_parity_baseline_execution_details_compat;
	bool supports_runtime_recovery_core_diagnostics;
	bool supports_runtime_recovery_core_diagnostics_compat;
	bool supports_runtime_display_only_host_video_sink_mode;
	size_t min_runtime_external_video_capabilities_size;
	bool supports_runtime_external_video_capabilities;
	bool supports_runtime_external_video_capabilities_compat;
	bool supports_runtime_external_video_dmabuf;
} ChiakiHeadlessRuntimeCapabilities;

typedef enum chiaki_headless_runtime_recovery_action_t
{
	CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE = 0,
	CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR = 1,
	CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_STOP_RUNTIME = 2,
} ChiakiHeadlessRuntimeRecoveryAction;

typedef struct chiaki_headless_runtime_recovery_decision_t
{
	uint32_t api_version;
	uint32_t health_state;
	ChiakiHeadlessRuntimeRecoveryAction action;
	bool runtime_session_active;
	bool effective_enable_idr_on_fec_failure;
	uint64_t event_count;
	uint64_t error_event_count;
	uint64_t ready_event_count;
} ChiakiHeadlessRuntimeRecoveryDecision;

typedef struct chiaki_headless_runtime_recovery_result_t
{
	uint32_t api_version;
	ChiakiHeadlessRuntimeRecoveryDecision decision;
	ChiakiHeadlessRuntimeRecoveryAction applied_action;
	ChiakiErrorCode apply_error;
	bool apply_succeeded;
	bool action_transitioned;
	uint64_t recover_monotonic_us;
} ChiakiHeadlessRuntimeRecoveryResult;

typedef struct chiaki_headless_runtime_recovery_tuning_t
{
	uint32_t api_version;
	uint32_t degraded_streak_threshold;
	uint32_t idr_cooldown_sec;
	bool stop_on_terminal;
} ChiakiHeadlessRuntimeRecoveryTuning;

typedef struct chiaki_headless_runtime_recovery_status_t
{
	uint32_t api_version;
	uint64_t degraded_streak;
	uint64_t last_idr_request_monotonic_us;
	bool runtime_session_active;
	ChiakiHeadlessRuntimeRecoveryAction last_recommended_action;
	ChiakiHeadlessRuntimeRecoveryAction last_applied_action;
	uint64_t recover_attempt_count;
	uint64_t recover_success_count;
	uint64_t recover_failure_count;
} ChiakiHeadlessRuntimeRecoveryStatus;

typedef struct chiaki_headless_runtime_recovery_config_t
{
	uint32_t api_version;
	uint32_t profile;
	ChiakiHeadlessRuntimeRecoveryTuning tuning;
} ChiakiHeadlessRuntimeRecoveryConfig;

typedef struct chiaki_headless_runtime_host_status_t
{
	uint32_t api_version;
	uint32_t health_state;
	ChiakiHeadlessRuntimeRecoveryAction recommended_action;
	ChiakiHeadlessRuntimeRecoveryAction applied_action;
	bool recover_apply_succeeded;
	bool recover_action_transitioned;
	uint64_t recover_monotonic_us;
	bool runtime_session_active;
	uint64_t degraded_streak;
	uint64_t last_idr_request_monotonic_us;
	uint64_t recover_attempt_count;
	uint64_t recover_success_count;
	uint64_t recover_failure_count;
	uint64_t event_count;
	uint64_t error_event_count;
	uint64_t ready_event_count;
	uint64_t packets_received;
	uint64_t packets_lost;
	double measured_bitrate_kbps;
	uint64_t video_decode_lost_frames;
	uint64_t video_decode_recovered_frames;
	uint64_t video_decode_gap_event_count;
} ChiakiHeadlessRuntimeHostStatus;

typedef struct chiaki_headless_runtime_playback_readiness_status_t
{
	uint32_t api_version;
	ChiakiHeadlessRuntimeHostStatus host_status;
	uint64_t recover_result_count;
	uint64_t recover_apply_success_count;
	uint64_t recover_apply_failure_count;
	uint64_t recover_action_transition_count;
	uint64_t recover_counter_observed_count;
	bool recover_counters_consistent;
} ChiakiHeadlessRuntimePlaybackReadinessStatus;

typedef struct chiaki_headless_runtime_playback_continuity_status_t
{
	uint32_t api_version;
	ChiakiHeadlessRuntimeHostStatus host_status;
	uint64_t video_frame_count;
	uint64_t audio_frame_count;
	uint64_t frame_counter_observed_count;
	uint64_t decode_counter_observed_count;
	bool has_video_frame_activity;
	bool has_decode_gap_activity;
	bool decode_gap_implies_decode_activity;
} ChiakiHeadlessRuntimePlaybackContinuityStatus;

typedef struct chiaki_headless_runtime_video_frame_metadata_t
{
	uint32_t api_version;
	bool runtime_session_active;
	bool has_video_frame;
	ChiakiHeadlessVideoFormat format;
	uint32_t width;
	uint32_t height;
	double pts_seconds;
	double duration_seconds;
	int32_t frames_lost;
	bool frame_recovered;
	uint64_t monotonic_time_us;
	uint64_t video_frame_count;
	uint64_t video_decode_lost_frames;
	uint64_t video_decode_recovered_frames;
	uint64_t video_decode_gap_event_count;
} ChiakiHeadlessRuntimeVideoFrameMetadata;

typedef struct chiaki_headless_runtime_video_frame_poll_t
{
	uint32_t api_version;
	ChiakiHeadlessRuntimeVideoFrameMetadata metadata;
	uint8_t *planes[4];
	size_t plane_sizes[4];
	int32_t strides[4];
	uint8_t plane_count;
} ChiakiHeadlessRuntimeVideoFramePoll;

typedef struct chiaki_headless_runtime_audio_sink_diagnostics_t
{
	uint32_t api_version;
	bool runtime_session_active;
	bool sink_enabled;
	bool sink_started;
	uint32_t channels;
	uint32_t sample_rate;
	ChiakiHeadlessAudioFormat format;
	uint64_t submitted_frame_count;
	uint64_t submitted_byte_count;
	uint64_t dropped_frame_count;
	uint64_t underrun_count;
	uint64_t start_count;
	uint64_t stop_count;
	uint64_t last_submit_monotonic_us;
	uint64_t legacy_callback_frame_count;
	uint64_t suppressed_legacy_callback_frame_count;
} ChiakiHeadlessRuntimeAudioSinkDiagnostics;

typedef struct chiaki_headless_runtime_external_video_capabilities_t
{
	uint32_t api_version;
	uint32_t abi_revision;
	size_t min_external_video_capabilities_size;
	bool supports_runtime_external_video_capabilities;
	bool supports_runtime_external_video_capabilities_compat;
	bool supports_runtime_external_video_dmabuf;
	uint8_t dmabuf_max_planes;
	bool dmabuf_includes_fd;
	bool dmabuf_includes_pitch;
	bool dmabuf_includes_offset;
	bool dmabuf_includes_modifier;
	bool dmabuf_includes_drm_format;
} ChiakiHeadlessRuntimeExternalVideoCapabilities;

typedef struct chiaki_headless_runtime_recovery_simulation_step_input_t
{
	const void *snapshot;
	size_t snapshot_size;
	uint64_t monotonic_now_us;
} ChiakiHeadlessRuntimeRecoverySimulationStepInput;

typedef struct chiaki_headless_runtime_recovery_simulation_health_step_input_t
{
	uint32_t health_state;
	uint64_t event_count;
	uint64_t error_event_count;
	uint64_t ready_event_count;
	uint64_t monotonic_now_us;
} ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput;

typedef struct chiaki_headless_runtime_recovery_simulation_step_output_t
{
	ChiakiHeadlessRuntimeRecoveryResult result;
	ChiakiHeadlessRuntimeRecoveryStatus status;
} ChiakiHeadlessRuntimeRecoverySimulationStepOutput;

typedef struct chiaki_headless_runtime_recovery_simulation_report_t
{
	uint32_t api_version;
	size_t step_count;
	size_t idr_action_count;
	size_t stop_action_count;
	size_t none_action_count;
	size_t healthy_step_count;
	size_t degraded_step_count;
	size_t terminal_step_count;
	size_t first_idr_step_index;
	size_t first_stop_step_index;
	size_t first_none_step_index;
	bool saw_terminal_health;
	ChiakiHeadlessRuntimeRecoveryStatus final_status;
} ChiakiHeadlessRuntimeRecoverySimulationReport;

typedef struct chiaki_headless_runtime_recovery_auto_loop_timeline_step_t
{
	uint32_t api_version;
	ChiakiHeadlessRuntimeRecoveryAction decision_action;
	ChiakiHeadlessRuntimeRecoveryAction applied_action;
	uint64_t degraded_streak;
	uint64_t last_idr_request_monotonic_us;
	uint32_t health_state;
	uint64_t event_count;
	uint64_t error_event_count;
	uint64_t ready_event_count;
} ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineStep;

typedef struct chiaki_headless_runtime_recovery_auto_loop_timeline_summary_t
{
	uint32_t api_version;
	size_t step_count;
	size_t idr_action_count;
	size_t stop_action_count;
	size_t none_action_count;
	size_t healthy_step_count;
	size_t degraded_step_count;
	size_t terminal_step_count;
	size_t first_idr_step_index;
	size_t first_stop_step_index;
	size_t first_none_step_index;
	bool saw_terminal_health;
	ChiakiHeadlessRuntimeRecoveryStatus final_status;
} ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineSummary;

typedef struct chiaki_headless_runtime_recovery_parity_fixture_expected_t
{
	uint32_t api_version;
	const char *scenario_label;
	size_t expected_idr_action_count;
	size_t expected_stop_action_count;
	size_t expected_none_action_count;
	bool validate_first_none_step_index;
	size_t expected_first_none_step_index;
} ChiakiHeadlessRuntimeRecoveryParityFixtureExpected;

typedef struct chiaki_headless_runtime_recovery_parity_fixture_result_t
{
	uint32_t api_version;
	size_t scenario_count;
	size_t pass_count;
	size_t fail_count;
} ChiakiHeadlessRuntimeRecoveryParityFixtureResult;

typedef struct chiaki_headless_runtime_recovery_parity_smoke_result_t
{
	uint32_t api_version;
	size_t scenario_count;
	size_t pass_count;
	size_t fail_count;
	size_t first_failure_index;
} ChiakiHeadlessRuntimeRecoveryParitySmokeResult;

typedef struct chiaki_headless_runtime_recovery_parity_smoke_runner_result_t
{
	uint32_t api_version;
	ChiakiHeadlessRuntimeRecoveryParityFixtureResult fixture_result;
	ChiakiHeadlessRuntimeRecoveryParitySmokeResult smoke_result;
} ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult;

#define CHIAKI_HEADLESS_RUNTIME_RECOVERY_PARITY_BASELINE_SCENARIO_LABEL_MAX 64

typedef struct chiaki_headless_runtime_recovery_parity_baseline_execution_detail_t
{
	uint32_t api_version;
	size_t scenario_index;
	char scenario_label[CHIAKI_HEADLESS_RUNTIME_RECOVERY_PARITY_BASELINE_SCENARIO_LABEL_MAX];
	bool pass;
	bool mismatch_idr_action_count;
	bool mismatch_stop_action_count;
	bool mismatch_none_action_count;
	bool mismatch_first_none_step_index;
	size_t expected_idr_action_count;
	size_t actual_idr_action_count;
	size_t expected_stop_action_count;
	size_t actual_stop_action_count;
	size_t expected_none_action_count;
	size_t actual_none_action_count;
	bool validate_first_none_step_index;
	size_t expected_first_none_step_index;
	size_t actual_first_none_step_index;
} ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail;

typedef struct chiaki_headless_runtime_recovery_core_diagnostics_summary_t
{
	uint32_t api_version;
	size_t baseline_scenario_count;
	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult baseline_aggregate_result;
	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineSummary auto_loop_timeline_summary;
	bool e3_master_gate_enabled;
	bool e3_audio_gate_enabled;
	bool e3_sync_gate_enabled;
	bool e3_audio_effective;
	bool e3_sync_effective;
} ChiakiHeadlessRuntimeRecoveryCoreDiagnosticsSummary;

typedef struct chiaki_media_capabilities_t
{
	uint32_t api_version;
	uint32_t media_capabilities_version;
	uint32_t media_event_schema_version;
	size_t min_session_stats_size;
	size_t min_readiness_report_size;
	size_t min_health_report_size;
	size_t min_health_policy_size;
	size_t min_profile_info_size;
	size_t min_profile_resolution_size;
	size_t min_diagnostics_snapshot_size;
	bool supports_capabilities_query;
	bool supports_texture_create;
	bool supports_session_create_destroy;
	/*
	 * API-level capability markers (not runtime gate-state markers).
	 * Runtime E2 gating may still return CHIAKI_ERR_UNINITIALIZED at call time.
	 */
	bool supports_start_cloud_strings;
	bool supports_stop;
	bool supports_session_stats;
	bool supports_session_stats_compat;
	bool supports_session_readiness;
	bool supports_session_readiness_compat;
	bool supports_session_health_report;
	bool supports_session_health_report_compat;
	bool supports_session_health_report_with_policy;
	bool supports_session_health_report_with_policy_compat;
	bool supports_health_policy_defaults_query;
	bool supports_health_policy_defaults_compat;
	bool supports_health_policy_profiles;
	bool supports_health_policy_profiles_compat;
	bool supports_session_health_report_with_profile;
	bool supports_session_health_report_with_profile_compat;
	bool supports_health_policy_profile_catalog;
	bool supports_health_policy_profile_catalog_compat;
	bool supports_health_policy_profile_metadata;
	bool supports_health_policy_profile_lookup_by_key;
	bool supports_health_policy_profile_lookup_by_key_compat;
	bool supports_health_policy_profile_key_from_profile;
	bool supports_health_policy_profile_key_normalization;
	bool supports_health_policy_profile_key_normalization_compat;
	bool supports_health_policy_profile_resolution;
	bool supports_health_policy_profile_resolution_compat;
	bool supports_session_health_report_with_profile_key;
	bool supports_session_health_report_with_profile_key_compat;
	bool supports_session_diagnostics_snapshot;
	bool supports_session_diagnostics_snapshot_compat;
	bool supports_session_diagnostics_snapshot_with_policy;
	bool supports_session_diagnostics_snapshot_with_policy_compat;
	bool supports_session_diagnostics_snapshot_with_profile;
	bool supports_session_diagnostics_snapshot_with_profile_compat;
	bool supports_session_diagnostics_snapshot_with_profile_key;
	bool supports_session_diagnostics_snapshot_with_profile_key_compat;
	bool supports_capabilities_compat;
} ChiakiMediaCapabilities;

typedef struct chiaki_media_session_t ChiakiMediaSession;

typedef enum chiaki_media_session_state_t
{
	CHIAKI_MEDIA_SESSION_STATE_CREATED = 0,
	CHIAKI_MEDIA_SESSION_STATE_STARTING,
	CHIAKI_MEDIA_SESSION_STATE_RUNNING,
	CHIAKI_MEDIA_SESSION_STATE_STOPPING,
	CHIAKI_MEDIA_SESSION_STATE_STOPPED,
	CHIAKI_MEDIA_SESSION_STATE_ERROR,
} ChiakiMediaSessionState;

typedef struct chiaki_media_create_info_t
{
	const ChiakiHeadlessCallbacks *callbacks;
} ChiakiMediaCreateInfo;

typedef struct chiaki_media_session_stats_t
{
	bool started;
	ChiakiMediaSessionState state;
	bool e3_master_gate_enabled;
	bool e3_audio_gate_enabled;
	bool e3_sync_gate_enabled;
	bool e3_audio_effective;
	bool e3_sync_effective;
	uint64_t video_frame_count;
	uint64_t audio_frame_count;
	uint64_t packets_received;
	uint64_t packets_lost;
	double measured_bitrate_kbps;
	uint64_t video_decode_lost_frames;
	uint64_t video_decode_recovered_frames;
	uint64_t video_decode_gap_event_count;
	uint64_t e3_audio_hook_frame_count;
	uint64_t e3_audio_hook_sample_count;
	uint64_t e3_sync_observation_count;
	int64_t e3_last_av_delta_us;
	uint64_t input_rumble_event_count;
	uint64_t input_trigger_effect_event_count;
	uint64_t input_motion_reset_event_count;
	uint64_t input_haptic_intensity_event_count;
	uint64_t input_trigger_intensity_event_count;
	uint64_t input_player_index_event_count;
	uint64_t input_haptics_event_count;
	uint64_t input_event_last_monotonic_us;
	uint64_t event_count;
	uint64_t ready_event_count;
	uint64_t stopped_event_count;
	uint64_t error_event_count;
	uint64_t last_event_monotonic_us;
	ChiakiHeadlessEventType last_event_type;
} ChiakiMediaSessionStats;

typedef struct chiaki_media_readiness_report_t
{
	bool started;
	ChiakiMediaSessionState state;
	bool seen_ready_event;
	bool seen_video_frame;
	bool seen_terminal_event;
	ChiakiHeadlessEventType last_event_type;
	uint64_t video_frame_count;
	uint64_t event_count;
	uint64_t ready_event_count;
	uint64_t stopped_event_count;
	uint64_t error_event_count;
	uint64_t last_event_monotonic_us;
} ChiakiMediaReadinessReport;

typedef enum chiaki_media_health_state_t
{
	CHIAKI_MEDIA_HEALTH_IDLE = 0,
	CHIAKI_MEDIA_HEALTH_STARTING,
	CHIAKI_MEDIA_HEALTH_READY,
	CHIAKI_MEDIA_HEALTH_DEGRADED,
	CHIAKI_MEDIA_HEALTH_TERMINAL,
} ChiakiMediaHealthState;

typedef struct chiaki_media_health_report_t
{
	ChiakiMediaHealthState health_state;
	ChiakiMediaSessionState session_state;
	bool started;
	bool seen_video_frame;
	bool seen_ready_event;
	bool seen_terminal_event;
	uint64_t video_frame_count;
	uint64_t event_count;
	uint64_t error_event_count;
	uint64_t last_event_monotonic_us;
	ChiakiHeadlessEventType last_event_type;
} ChiakiMediaHealthReport;

typedef struct chiaki_media_health_policy_t
{
	uint64_t terminal_error_event_count;
	uint64_t degraded_no_video_min_event_count;
	bool treat_stopped_as_terminal;
	bool treat_error_event_as_terminal;
} ChiakiMediaHealthPolicy;

typedef enum chiaki_media_health_policy_profile_t
{
	CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT = 0,
	CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE = 1,
	CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE = 2,
} ChiakiMediaHealthPolicyProfile;

typedef struct chiaki_media_health_policy_profile_info_t
{
	ChiakiMediaHealthPolicyProfile profile;
	bool is_default_profile;
	const char *profile_key;
	const char *display_label;
} ChiakiMediaHealthPolicyProfileInfo;

typedef struct chiaki_media_health_policy_profile_resolution_t
{
	ChiakiMediaHealthPolicyProfile profile;
	const char *canonical_profile_key;
	const char *display_label;
	bool is_default_profile;
} ChiakiMediaHealthPolicyProfileResolution;

typedef struct chiaki_media_session_diagnostics_snapshot_t
{
	uint32_t api_version;
	ChiakiMediaSessionStats stats;
	ChiakiMediaReadinessReport readiness;
	ChiakiMediaHealthReport health;
} ChiakiMediaSessionDiagnosticsSnapshot;

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_create(ChiakiHeadlessSession **out_session, const ChiakiHeadlessCreateInfo *create_info);
CHIAKI_EXPORT void chiaki_headless_session_destroy(ChiakiHeadlessSession *session);

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_start(ChiakiHeadlessSession *session);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_stop(ChiakiHeadlessSession *session);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_join(ChiakiHeadlessSession *session);

CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_send_controller_state(ChiakiHeadlessSession *session, ChiakiControllerState *state);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_request_idr(ChiakiHeadlessSession *session);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_toggle_microphone(ChiakiHeadlessSession *session, bool muted);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_go_home(ChiakiHeadlessSession *session);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_session_set_login_pin(ChiakiHeadlessSession *session, const uint8_t *pin, size_t pin_size);

/**
 * Build a cloud-direct connect-info from launch data.
 * Output memory is fully owned by caller. The caller must keep pointed strings
 * (`host`, `session_id`, `launch_spec`) alive while the session is in use.
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_connect_info_init_cloud_direct(
	ChiakiConnectInfo *out_connect_info,
	const ChiakiHeadlessCloudLaunchInfo *launch_info);

/**
 * Lightweight ABI / linkage probe helpers for host integrations.
 * These do not create a streaming session and are safe to call at startup.
 */
CHIAKI_EXPORT uint32_t chiaki_headless_api_version(void);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_probe(void);
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
	ChiakiCodec codec);
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
	ChiakiCodec codec);
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
	const char *ffmpeg_hw_decoder_name);
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
	const char *ffmpeg_hw_decoder_name);

/**
 * Experimental runtime-managed headless cloud session helpers.
 * These keep session ownership inside chiaki core so host apps can
 * start/stop without constructing internal structs over FFI.
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_cloud_start(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	const char *ffmpeg_hw_decoder_name);
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
	const char *ffmpeg_hw_decoder_name);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_cloud_stop(void);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_request_idr(void);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_send_controller_state_compat(
	uint32_t buttons,
	uint8_t l2_state,
	uint8_t r2_state,
	int16_t left_x,
	int16_t left_y,
	int16_t right_x,
	int16_t right_y);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_tap_button(uint32_t button_mask);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_callbacks(const ChiakiHeadlessCallbacks *callbacks);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_stream_profile_overrides(const ChiakiHeadlessStreamProfileOverrides *overrides);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_launch_overrides(const ChiakiHeadlessLaunchOverrides *overrides);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_overrides(const ChiakiHeadlessRuntimeOverrides *overrides);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_patch_overrides(
	const ChiakiHeadlessRuntimeOverrides *overrides,
	bool clear_unspecified);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_overrides(ChiakiHeadlessRuntimeOverrides *out_overrides);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_effective_launch_info(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	ChiakiHeadlessCloudLaunchInfo *out_launch_info);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_effective_stream_profile(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	ChiakiHeadlessStreamProfile *out_profile);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_effective_connect_info(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	ChiakiConnectInfo *out_connect_info);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_sanity_report(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	ChiakiHeadlessRuntimeSanityReport *out_report);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_state_snapshot(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	ChiakiHeadlessRuntimeStateSnapshot *out_snapshot);
CHIAKI_EXPORT size_t chiaki_headless_runtime_state_snapshot_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_state_snapshot_init(
	ChiakiHeadlessRuntimeStateSnapshot *snapshot);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_state_snapshot_compat(
	const ChiakiHeadlessCloudLaunchInfo *launch_info,
	void *out_snapshot_buf,
	size_t out_snapshot_size);
CHIAKI_EXPORT size_t chiaki_headless_runtime_capabilities_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_capabilities_init(
	ChiakiHeadlessRuntimeCapabilities *capabilities);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_capabilities(
	ChiakiHeadlessRuntimeCapabilities *out_capabilities);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_capabilities_compat(
	void *out_capabilities_buf,
	size_t out_capabilities_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_media_diagnostics_snapshot(
	uint32_t readiness_timeout_ms,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_media_diagnostics_snapshot_compat(
	uint32_t readiness_timeout_ms,
	void *out_snapshot_buf,
	size_t out_snapshot_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_media_diagnostics_snapshot_with_profile_key(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_media_diagnostics_snapshot_with_profile_key_compat(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	void *out_snapshot_buf,
	size_t out_snapshot_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_decision(
	uint32_t readiness_timeout_ms,
	ChiakiHeadlessRuntimeRecoveryDecision *out_decision);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_decision_compat(
	uint32_t readiness_timeout_ms,
	void *out_decision_buf,
	size_t out_decision_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_decision_with_profile_key(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	ChiakiHeadlessRuntimeRecoveryDecision *out_decision);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_decision_with_profile_key_compat(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	void *out_decision_buf,
	size_t out_decision_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_apply_recovery_decision(
	const ChiakiHeadlessRuntimeRecoveryDecision *decision,
	ChiakiHeadlessRuntimeRecoveryAction *out_applied_action);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_apply_recovery_decision_compat(
	const void *decision_buf,
	size_t decision_size,
	void *out_applied_action_buf,
	size_t out_applied_action_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_with_profile_key(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	ChiakiHeadlessRuntimeRecoveryAction *out_applied_action);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover(
	uint32_t readiness_timeout_ms,
	ChiakiHeadlessRuntimeRecoveryAction *out_applied_action);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recover_result(
	uint32_t readiness_timeout_ms,
	ChiakiHeadlessRuntimeRecoveryResult *out_result);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recover_result_with_profile_key(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	ChiakiHeadlessRuntimeRecoveryResult *out_result);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recover_result_compat(
	uint32_t readiness_timeout_ms,
	void *out_result_buf,
	size_t out_result_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recover_result_with_profile_key_compat(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	void *out_result_buf,
	size_t out_result_size);
CHIAKI_EXPORT size_t chiaki_headless_runtime_recovery_tuning_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_recovery_tuning_init(
	ChiakiHeadlessRuntimeRecoveryTuning *tuning);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_with_profile_key_tuned(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	ChiakiHeadlessRuntimeRecoveryResult *out_result);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_tuned(
	uint32_t readiness_timeout_ms,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	ChiakiHeadlessRuntimeRecoveryResult *out_result);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_with_profile_key_tuned_compat(
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	const void *tuning_buf,
	size_t tuning_size,
	void *out_result_buf,
	size_t out_result_size);
CHIAKI_EXPORT size_t chiaki_headless_runtime_recovery_status_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_recovery_status_init(
	ChiakiHeadlessRuntimeRecoveryStatus *status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_status(
	ChiakiHeadlessRuntimeRecoveryStatus *out_status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_status_compat(
	void *out_status_buf,
	size_t out_status_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_reset_recovery_status(void);
CHIAKI_EXPORT size_t chiaki_headless_runtime_recovery_config_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_recovery_config_init(
	ChiakiHeadlessRuntimeRecoveryConfig *config);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_recovery_config(
	const ChiakiHeadlessRuntimeRecoveryConfig *config);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_config(
	ChiakiHeadlessRuntimeRecoveryConfig *out_config);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_recovery_config_compat(
	const void *config_buf,
	size_t config_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_config_compat(
	void *out_config_buf,
	size_t out_config_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_recovery_profile_key(
	const char *profile_key);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_recovery_profile_key(
	char *out_profile_key,
	size_t out_profile_key_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_auto(
	uint32_t readiness_timeout_ms,
	ChiakiHeadlessRuntimeRecoveryResult *out_result);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_auto_compat(
	uint32_t readiness_timeout_ms,
	void *out_result_buf,
	size_t out_result_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_auto_with_status(
	uint32_t readiness_timeout_ms,
	ChiakiHeadlessRuntimeRecoveryResult *out_result,
	ChiakiHeadlessRuntimeRecoveryStatus *out_status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recover_auto_with_status_compat(
	uint32_t readiness_timeout_ms,
	void *out_result_buf,
	size_t out_result_size,
	void *out_status_buf,
	size_t out_status_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_with_profile_key(
	const ChiakiMediaSessionDiagnosticsSnapshot *snapshot,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *status_in,
	ChiakiHeadlessRuntimeRecoveryResult *out_result,
	ChiakiHeadlessRuntimeRecoveryStatus *out_status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery(
	const ChiakiMediaSessionDiagnosticsSnapshot *snapshot,
	const ChiakiHeadlessRuntimeRecoveryConfig *config,
	const ChiakiHeadlessRuntimeRecoveryStatus *status_in,
	ChiakiHeadlessRuntimeRecoveryResult *out_result,
	ChiakiHeadlessRuntimeRecoveryStatus *out_status);
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
	size_t out_status_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryConfig *config,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	ChiakiHeadlessRuntimeRecoveryStatus *out_final_status);
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
	size_t out_final_status_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_report(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryConfig *config,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_report_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const void *config_buf,
	size_t config_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *out_report_buf,
	size_t out_report_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_with_report(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryConfig *config,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	ChiakiHeadlessRuntimeRecoveryStatus *out_final_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report);
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
	size_t out_report_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_simulation_report_from_outputs(
	const ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryStatus *final_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_simulation_report_from_outputs_compat(
	const void *step_outputs_buf,
	size_t step_count,
	size_t step_output_stride,
	size_t step_output_size,
	const void *final_status_buf,
	size_t final_status_size,
	void *out_report_buf,
	size_t out_report_size);
CHIAKI_EXPORT size_t chiaki_headless_runtime_host_status_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_host_status_init(
	ChiakiHeadlessRuntimeHostStatus *status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_build_host_status(
	const ChiakiMediaSessionDiagnosticsSnapshot *snapshot,
	const ChiakiHeadlessRuntimeRecoveryResult *recovery_result,
	const ChiakiHeadlessRuntimeRecoveryStatus *recovery_status,
	ChiakiHeadlessRuntimeHostStatus *out_status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_build_host_status_compat(
	const void *snapshot_buf,
	size_t snapshot_size,
	const void *recovery_result_buf,
	size_t recovery_result_size,
	const void *recovery_status_buf,
	size_t recovery_status_size,
	void *out_status_buf,
	size_t out_status_size);
CHIAKI_EXPORT size_t chiaki_headless_runtime_playback_readiness_status_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_playback_readiness_status_init(
	ChiakiHeadlessRuntimePlaybackReadinessStatus *status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_build_playback_readiness_status(
	const ChiakiMediaSessionDiagnosticsSnapshot *snapshot,
	const ChiakiHeadlessRuntimeRecoveryResult *recovery_result,
	const ChiakiHeadlessRuntimeRecoveryStatus *recovery_status,
	ChiakiHeadlessRuntimePlaybackReadinessStatus *out_status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_build_playback_readiness_status_compat(
	const void *snapshot_buf,
	size_t snapshot_size,
	const void *recovery_result_buf,
	size_t recovery_result_size,
	const void *recovery_status_buf,
	size_t recovery_status_size,
	void *out_status_buf,
	size_t out_status_size);
CHIAKI_EXPORT size_t chiaki_headless_runtime_playback_continuity_status_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_playback_continuity_status_init(
	ChiakiHeadlessRuntimePlaybackContinuityStatus *status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_build_playback_continuity_status(
	const ChiakiMediaSessionDiagnosticsSnapshot *snapshot,
	const ChiakiHeadlessRuntimeRecoveryResult *recovery_result,
	const ChiakiHeadlessRuntimeRecoveryStatus *recovery_status,
	ChiakiHeadlessRuntimePlaybackContinuityStatus *out_status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_build_playback_continuity_status_compat(
	const void *snapshot_buf,
	size_t snapshot_size,
	const void *recovery_result_buf,
	size_t recovery_result_size,
	const void *recovery_status_buf,
	size_t recovery_status_size,
	void *out_status_buf,
	size_t out_status_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_playback_continuity_status(
	uint32_t readiness_timeout_ms,
	ChiakiHeadlessRuntimePlaybackContinuityStatus *out_status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_playback_continuity_status_compat(
	uint32_t readiness_timeout_ms,
	void *out_status_buf,
	size_t out_status_size);
CHIAKI_EXPORT size_t chiaki_headless_runtime_video_frame_metadata_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_video_frame_metadata_init(
	ChiakiHeadlessRuntimeVideoFrameMetadata *metadata);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_video_frame_metadata(
	ChiakiHeadlessRuntimeVideoFrameMetadata *out_metadata);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_video_frame_metadata_compat(
	void *out_metadata_buf,
	size_t out_metadata_size);
CHIAKI_EXPORT size_t chiaki_headless_runtime_video_frame_poll_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_video_frame_poll_init(
	ChiakiHeadlessRuntimeVideoFramePoll *frame_poll);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_poll_video_frame(
	ChiakiHeadlessRuntimeVideoFramePoll *inout_frame_poll);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_poll_video_frame_compat(
	void *inout_frame_poll_buf,
	size_t inout_frame_poll_size);
CHIAKI_EXPORT size_t chiaki_headless_runtime_audio_sink_config_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_audio_sink_config_init(
	ChiakiHeadlessRuntimeAudioSinkConfig *config);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_set_audio_sink_config(
	const ChiakiHeadlessRuntimeAudioSinkConfig *config);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_audio_sink_config(
	ChiakiHeadlessRuntimeAudioSinkConfig *out_config);
CHIAKI_EXPORT size_t chiaki_headless_runtime_audio_sink_diagnostics_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_audio_sink_diagnostics_init(
	ChiakiHeadlessRuntimeAudioSinkDiagnostics *diagnostics);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_audio_sink_diagnostics(
	ChiakiHeadlessRuntimeAudioSinkDiagnostics *out_diagnostics);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_audio_sink_diagnostics_compat(
	void *out_diagnostics_buf,
	size_t out_diagnostics_size);
CHIAKI_EXPORT size_t chiaki_headless_runtime_external_video_capabilities_size(void);
CHIAKI_EXPORT void chiaki_headless_runtime_external_video_capabilities_init(
	ChiakiHeadlessRuntimeExternalVideoCapabilities *capabilities);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_external_video_capabilities(
	ChiakiHeadlessRuntimeExternalVideoCapabilities *out_capabilities);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_get_external_video_capabilities_compat(
	void *out_capabilities_buf,
	size_t out_capabilities_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_audio_sink_report_underrun(
	uint64_t underrun_count);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_fixture_eval(
	const ChiakiHeadlessRuntimeRecoveryParityFixtureExpected *expected_markers,
	const ChiakiHeadlessRuntimeRecoverySimulationReport *actual_reports,
	size_t scenario_count,
	ChiakiHeadlessRuntimeRecoveryParityFixtureResult *out_result);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_fixture_eval_compat(
	const void *expected_markers_buf,
	size_t scenario_count,
	size_t expected_marker_stride,
	size_t expected_marker_size,
	const void *actual_reports_buf,
	size_t actual_report_stride,
	size_t actual_report_size,
	void *out_result_buf,
	size_t out_result_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_fixture_export(
	const char *scenario_label,
	const ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryStatus *final_status,
	bool validate_first_none_step_index,
	ChiakiHeadlessRuntimeRecoveryParityFixtureExpected *out_expected,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report);
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
	size_t out_report_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_smoke(
	const ChiakiHeadlessRuntimeRecoveryParityFixtureExpected *expected_markers,
	const ChiakiHeadlessRuntimeRecoverySimulationReport *actual_reports,
	size_t scenario_count,
	ChiakiHeadlessRuntimeRecoveryParitySmokeResult *out_result);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_smoke_compat(
	const void *expected_markers_buf,
	size_t scenario_count,
	size_t expected_marker_stride,
	size_t expected_marker_size,
	const void *actual_reports_buf,
	size_t actual_report_stride,
	size_t actual_report_size,
	void *out_result_buf,
	size_t out_result_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_smoke_runner(
	const ChiakiHeadlessRuntimeRecoveryParityFixtureExpected *expected_markers,
	const ChiakiHeadlessRuntimeRecoverySimulationReport *actual_reports,
	size_t scenario_count,
	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult *out_result);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_smoke_runner_compat(
	const void *expected_markers_buf,
	size_t scenario_count,
	size_t expected_marker_stride,
	size_t expected_marker_size,
	const void *actual_reports_buf,
	size_t actual_report_stride,
	size_t actual_report_size,
	void *out_result_buf,
	size_t out_result_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_smoke(
	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult *out_result);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_smoke_compat(
	void *out_result_buf,
	size_t out_result_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_scenario_count(
	size_t *out_scenario_count);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_scenario_count_compat(
	void *out_scenario_count_buf,
	size_t out_scenario_count_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_scenario_label(
	size_t scenario_index,
	const char **out_scenario_label);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_scenario_label_compat(
	size_t scenario_index,
	char *out_scenario_label,
	size_t out_scenario_label_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_execution_details(
	ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail *out_details,
	size_t detail_count,
	ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult *out_aggregate_result);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_parity_baseline_execution_details_compat(
	void *out_details_buf,
	size_t detail_count,
	size_t detail_stride,
	size_t detail_size,
	void *out_aggregate_result_buf,
	size_t out_aggregate_result_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_recovery_core_diagnostics(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryConfig *config,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail *out_baseline_details,
	size_t out_baseline_detail_count,
	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineStep *out_timeline_steps,
	ChiakiHeadlessRuntimeRecoveryCoreDiagnosticsSummary *out_summary);
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
	size_t out_summary_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	ChiakiHeadlessRuntimeRecoveryStatus *out_final_status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_report_with_profile_key(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report);
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
	size_t out_final_status_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_report_with_profile_key_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const void *tuning_buf,
	size_t tuning_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *out_report_buf,
	size_t out_report_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key_with_report(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	ChiakiHeadlessRuntimeRecoveryStatus *out_final_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report);
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
	size_t out_report_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key(
	const ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	ChiakiHeadlessRuntimeRecoveryStatus *out_final_status);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_health_sequence_report_with_profile_key(
	const ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report);
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
	size_t out_final_status_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_health_sequence_report_with_profile_key_compat(
	const ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const void *tuning_buf,
	size_t tuning_size,
	const void *initial_status_buf,
	size_t initial_status_size,
	void *out_report_buf,
	size_t out_report_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key_with_report(
	const ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput *step_inputs,
	size_t step_count,
	const char *profile_key,
	const ChiakiHeadlessRuntimeRecoveryTuning *tuning,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput *step_outputs,
	ChiakiHeadlessRuntimeRecoveryStatus *out_final_status,
	ChiakiHeadlessRuntimeRecoverySimulationReport *out_report);
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
	size_t out_report_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_headless_runtime_simulate_recovery_auto_loop_timeline(
	const ChiakiHeadlessRuntimeRecoverySimulationStepInput *step_inputs,
	size_t step_count,
	const ChiakiHeadlessRuntimeRecoveryConfig *config,
	const ChiakiHeadlessRuntimeRecoveryStatus *initial_status,
	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineStep *timeline_steps,
	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineSummary *out_summary);
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
	size_t out_summary_size);

CHIAKI_EXPORT size_t chiaki_media_capabilities_size(void);
CHIAKI_EXPORT void chiaki_media_capabilities_init(ChiakiMediaCapabilities *capabilities);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_capabilities(ChiakiMediaCapabilities *out_capabilities);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_capabilities_compat(
	void *out_capabilities_buf,
	size_t out_capabilities_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_create(
	ChiakiMediaSession **out_session,
	const ChiakiMediaCreateInfo *create_info);
CHIAKI_EXPORT void chiaki_media_session_destroy(ChiakiMediaSession *session);
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
	const char *ffmpeg_hw_decoder_name);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_stop(ChiakiMediaSession *session);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_stats(
	ChiakiMediaSession *session,
	ChiakiMediaSessionStats *out_stats);
CHIAKI_EXPORT size_t chiaki_media_session_stats_size(void);
CHIAKI_EXPORT void chiaki_media_session_stats_init(ChiakiMediaSessionStats *stats);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_stats_compat(
	ChiakiMediaSession *session,
	void *out_stats_buf,
	size_t out_stats_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_wait_for_video_frame(
	ChiakiMediaSession *session,
	uint32_t timeout_ms,
	bool *out_received);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_wait_for_event(
	ChiakiMediaSession *session,
	ChiakiHeadlessEventType event_type,
	uint32_t timeout_ms,
	bool *out_received);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_wait_for_readiness(
	ChiakiMediaSession *session,
	uint32_t timeout_ms,
	ChiakiMediaReadinessReport *out_report);
CHIAKI_EXPORT size_t chiaki_media_readiness_report_size(void);
CHIAKI_EXPORT void chiaki_media_readiness_report_init(ChiakiMediaReadinessReport *report);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_wait_for_readiness_compat(
	ChiakiMediaSession *session,
	uint32_t timeout_ms,
	void *out_report_buf,
	size_t out_report_size);
CHIAKI_EXPORT size_t chiaki_media_health_report_size(void);
CHIAKI_EXPORT void chiaki_media_health_report_init(ChiakiMediaHealthReport *report);
CHIAKI_EXPORT size_t chiaki_media_health_policy_size(void);
CHIAKI_EXPORT void chiaki_media_health_policy_init(ChiakiMediaHealthPolicy *policy);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_defaults(
	ChiakiMediaHealthPolicy *out_policy);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_defaults_compat(
	void *out_policy_buf,
	size_t out_policy_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_from_profile(
	ChiakiMediaHealthPolicyProfile profile,
	ChiakiMediaHealthPolicy *out_policy);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_from_profile_compat(
	ChiakiMediaHealthPolicyProfile profile,
	void *out_policy_buf,
	size_t out_policy_size);
CHIAKI_EXPORT size_t chiaki_media_health_policy_profile_count(void);
CHIAKI_EXPORT size_t chiaki_media_health_policy_profile_info_size(void);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_info(
	size_t index,
	ChiakiMediaHealthPolicyProfileInfo *out_info);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_info_compat(
	size_t index,
	void *out_info_buf,
	size_t out_info_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_from_key(
	const char *profile_key,
	ChiakiMediaHealthPolicyProfile *out_profile);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_from_key_compat(
	const char *profile_key,
	void *out_profile_buf,
	size_t out_profile_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_key_from_profile(
	ChiakiMediaHealthPolicyProfile profile,
	const char **out_profile_key);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_key_normalize(
	const char *input_profile_key,
	const char **out_canonical_profile_key);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_key_normalize_compat(
	const char *input_profile_key,
	void *out_profile_buf,
	size_t out_profile_size);
CHIAKI_EXPORT size_t chiaki_media_health_policy_profile_resolution_size(void);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_resolve(
	const char *input_profile_key,
	ChiakiMediaHealthPolicyProfileResolution *out_resolution);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_policy_profile_resolve_compat(
	const char *input_profile_key,
	void *out_resolution_buf,
	size_t out_resolution_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_health_evaluate_readiness(
	const ChiakiMediaReadinessReport *readiness,
	const ChiakiMediaHealthPolicy *policy,
	ChiakiMediaHealthReport *out_report);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report(
	ChiakiMediaSession *session,
	ChiakiMediaHealthReport *out_report);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_compat(
	ChiakiMediaSession *session,
	void *out_report_buf,
	size_t out_report_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_with_policy(
	ChiakiMediaSession *session,
	const ChiakiMediaHealthPolicy *policy,
	ChiakiMediaHealthReport *out_report);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_with_policy_compat(
	ChiakiMediaSession *session,
	const ChiakiMediaHealthPolicy *policy,
	void *out_report_buf,
	size_t out_report_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_with_profile(
	ChiakiMediaSession *session,
	ChiakiMediaHealthPolicyProfile profile,
	ChiakiMediaHealthReport *out_report);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_with_profile_compat(
	ChiakiMediaSession *session,
	ChiakiMediaHealthPolicyProfile profile,
	void *out_report_buf,
	size_t out_report_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_with_profile_key(
	ChiakiMediaSession *session,
	const char *profile_key,
	ChiakiMediaHealthReport *out_report);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_health_report_with_profile_key_compat(
	ChiakiMediaSession *session,
	const char *profile_key,
	void *out_report_buf,
	size_t out_report_size);
CHIAKI_EXPORT size_t chiaki_media_session_diagnostics_snapshot_size(void);
CHIAKI_EXPORT void chiaki_media_session_diagnostics_snapshot_init(
	ChiakiMediaSessionDiagnosticsSnapshot *snapshot);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_compat(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	void *out_snapshot_buf,
	size_t out_snapshot_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_with_policy(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	const ChiakiMediaHealthPolicy *policy,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_with_policy_compat(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	const ChiakiMediaHealthPolicy *policy,
	void *out_snapshot_buf,
	size_t out_snapshot_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_with_profile(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	ChiakiMediaHealthPolicyProfile profile,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_with_profile_compat(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	ChiakiMediaHealthPolicyProfile profile,
	void *out_snapshot_buf,
	size_t out_snapshot_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_with_profile_key(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	ChiakiMediaSessionDiagnosticsSnapshot *out_snapshot);
CHIAKI_EXPORT ChiakiErrorCode chiaki_media_session_get_diagnostics_snapshot_with_profile_key_compat(
	ChiakiMediaSession *session,
	uint32_t readiness_timeout_ms,
	const char *profile_key,
	void *out_snapshot_buf,
	size_t out_snapshot_size);

/**
 * Frames are valid only for the duration of the callback invocation.
 * Consumers must copy frame/audio buffers if they need them after return.
 */

#ifdef __cplusplus
}
#endif

#endif
