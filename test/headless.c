// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>

#include <chiaki/headless.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static MunitResult test_headless_validate_create(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	ChiakiHeadlessSession *session = (ChiakiHeadlessSession *)0x1;
	ChiakiErrorCode err = chiaki_headless_session_create(&session, NULL);
	munit_assert_int(err, ==, CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_null_ops(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	munit_assert_int(chiaki_headless_session_start(NULL), ==, CHIAKI_ERR_INVALID_DATA);
	munit_assert_int(chiaki_headless_session_stop(NULL), ==, CHIAKI_ERR_INVALID_DATA);
	munit_assert_int(chiaki_headless_session_join(NULL), ==, CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_probe(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	munit_assert_uint32(chiaki_headless_api_version(), ==, 40);
	munit_assert_int(chiaki_headless_probe(), ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static void test_headless_runtime_noop_event_cb(const ChiakiHeadlessEvent *event, void *user)
{
	(void)event;
	(void)user;
}

static MunitResult test_headless_runtime_set_callbacks(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	ChiakiHeadlessCallbacks callbacks = {
		.video_frame_cb = NULL,
		.audio_frame_cb = NULL,
		.event_cb = test_headless_runtime_noop_event_cb,
		.user = NULL,
	};
	munit_assert_int(chiaki_headless_runtime_set_callbacks(&callbacks), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_headless_runtime_set_callbacks(NULL), ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_set_stream_profile_overrides(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	ChiakiHeadlessStreamProfileOverrides overrides = {
		.use_resolution = true,
		.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p,
		.use_fps = true,
		.fps = CHIAKI_VIDEO_FPS_PRESET_60,
		.use_bitrate = true,
		.bitrate = 18000,
		.use_codec = true,
		.codec = CHIAKI_CODEC_H265,
	};
	munit_assert_int(chiaki_headless_runtime_set_stream_profile_overrides(&overrides), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_headless_runtime_set_stream_profile_overrides(NULL), ==, CHIAKI_ERR_SUCCESS);

	overrides.use_bitrate = true;
	overrides.bitrate = 0;
	munit_assert_int(chiaki_headless_runtime_set_stream_profile_overrides(&overrides), ==, CHIAKI_ERR_INVALID_DATA);

	overrides.use_bitrate = false;
	overrides.use_fps = true;
	overrides.fps = (ChiakiVideoFPSPreset)59;
	munit_assert_int(chiaki_headless_runtime_set_stream_profile_overrides(&overrides), ==, CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_set_launch_overrides(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	ChiakiHeadlessLaunchOverrides overrides = {
		.use_ps5 = true,
		.ps5 = false,
		.use_enable_dualsense = true,
		.enable_dualsense = false,
		.use_enable_keyboard = true,
		.enable_keyboard = false,
	};
	munit_assert_int(chiaki_headless_runtime_set_launch_overrides(&overrides), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_headless_runtime_set_launch_overrides(NULL), ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_set_get_overrides(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	ChiakiHeadlessRuntimeOverrides overrides = {
		.has_launch_overrides = true,
		.launch_overrides = {
			.use_ps5 = true,
			.ps5 = false,
			.use_enable_dualsense = true,
			.enable_dualsense = false,
			.use_enable_keyboard = true,
			.enable_keyboard = false,
		},
		.has_stream_profile_overrides = true,
		.stream_profile_overrides = {
			.use_resolution = true,
			.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p,
			.use_fps = true,
			.fps = CHIAKI_VIDEO_FPS_PRESET_60,
			.use_bitrate = true,
			.bitrate = 20000,
			.use_codec = true,
			.codec = CHIAKI_CODEC_H265,
		},
		.has_policy_overrides = true,
		.policy_overrides = {
			.use_packet_loss_max = true,
			.packet_loss_max = 0.25,
			.use_display_only_host_video_sink = true,
			.display_only_host_video_sink = true,
		},
	};
	munit_assert_int(chiaki_headless_runtime_set_overrides(&overrides), ==, CHIAKI_ERR_SUCCESS);

	ChiakiHeadlessRuntimeOverrides out = {0};
	munit_assert_int(chiaki_headless_runtime_get_overrides(&out), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_true(out.has_launch_overrides);
	munit_assert_true(out.has_stream_profile_overrides);
	munit_assert_true(out.has_policy_overrides);
	munit_assert_false(out.launch_overrides.ps5);
	munit_assert_false(out.launch_overrides.enable_dualsense);
	munit_assert_false(out.launch_overrides.enable_keyboard);
	munit_assert_int(out.stream_profile_overrides.resolution, ==, CHIAKI_VIDEO_RESOLUTION_PRESET_1080p);
	munit_assert_int(out.stream_profile_overrides.fps, ==, CHIAKI_VIDEO_FPS_PRESET_60);
	munit_assert_int(out.stream_profile_overrides.bitrate, ==, 20000);
	munit_assert_int(out.stream_profile_overrides.codec, ==, CHIAKI_CODEC_H265);
	munit_assert_true(out.policy_overrides.use_packet_loss_max);
	munit_assert_double_equal(out.policy_overrides.packet_loss_max, 0.25, 10);
	munit_assert_true(out.policy_overrides.use_display_only_host_video_sink);
	munit_assert_true(out.policy_overrides.display_only_host_video_sink);

	munit_assert_int(chiaki_headless_runtime_set_overrides(NULL), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_headless_runtime_get_overrides(&out), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_false(out.has_launch_overrides);
	munit_assert_false(out.has_stream_profile_overrides);

	overrides.has_launch_overrides = false;
	overrides.has_stream_profile_overrides = true;
	overrides.stream_profile_overrides.use_bitrate = true;
	overrides.stream_profile_overrides.bitrate = 0;
	munit_assert_int(chiaki_headless_runtime_set_overrides(&overrides), ==, CHIAKI_ERR_INVALID_DATA);
	overrides.stream_profile_overrides.use_bitrate = false;
	overrides.has_policy_overrides = true;
	overrides.policy_overrides.use_packet_loss_max = true;
	overrides.policy_overrides.packet_loss_max = 1.5;
	munit_assert_int(chiaki_headless_runtime_set_overrides(&overrides), ==, CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_patch_overrides(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	ChiakiHeadlessRuntimeOverrides base = {
		.has_launch_overrides = true,
		.launch_overrides = {
			.use_ps5 = true,
			.ps5 = false,
		},
		.has_stream_profile_overrides = false,
	};
	munit_assert_int(chiaki_headless_runtime_set_overrides(&base), ==, CHIAKI_ERR_SUCCESS);

	ChiakiHeadlessRuntimeOverrides patch = {
		.has_launch_overrides = false,
		.has_stream_profile_overrides = true,
		.stream_profile_overrides = {
			.use_bitrate = true,
			.bitrate = 21000,
		},
		.has_policy_overrides = true,
		.policy_overrides = {
			.use_video_profile_auto_downgrade = true,
			.video_profile_auto_downgrade = false,
			.use_display_only_host_video_sink = true,
			.display_only_host_video_sink = true,
		},
	};
	munit_assert_int(chiaki_headless_runtime_patch_overrides(&patch, false), ==, CHIAKI_ERR_SUCCESS);

	ChiakiHeadlessRuntimeOverrides out = {0};
	munit_assert_int(chiaki_headless_runtime_get_overrides(&out), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_true(out.has_launch_overrides);
	munit_assert_true(out.launch_overrides.use_ps5);
	munit_assert_false(out.launch_overrides.ps5);
	munit_assert_true(out.has_stream_profile_overrides);
	munit_assert_true(out.stream_profile_overrides.use_bitrate);
	munit_assert_int(out.stream_profile_overrides.bitrate, ==, 21000);
	munit_assert_true(out.has_policy_overrides);
	munit_assert_true(out.policy_overrides.use_video_profile_auto_downgrade);
	munit_assert_false(out.policy_overrides.video_profile_auto_downgrade);
	munit_assert_true(out.policy_overrides.use_display_only_host_video_sink);
	munit_assert_true(out.policy_overrides.display_only_host_video_sink);

	munit_assert_int(chiaki_headless_runtime_patch_overrides(&patch, true), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_headless_runtime_get_overrides(&out), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_false(out.has_launch_overrides);
	munit_assert_true(out.has_stream_profile_overrides);
	munit_assert_true(out.has_policy_overrides);

	patch.has_stream_profile_overrides = true;
	patch.stream_profile_overrides.use_bitrate = true;
	patch.stream_profile_overrides.bitrate = 0;
	munit_assert_int(chiaki_headless_runtime_patch_overrides(&patch, false), ==, CHIAKI_ERR_INVALID_DATA);
	patch.stream_profile_overrides.use_bitrate = false;
	patch.has_policy_overrides = true;
	patch.policy_overrides.use_packet_loss_max = true;
	patch.policy_overrides.packet_loss_max = -0.1;
	munit_assert_int(chiaki_headless_runtime_patch_overrides(&patch, false), ==, CHIAKI_ERR_INVALID_DATA);

	munit_assert_int(chiaki_headless_runtime_set_overrides(NULL), ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_get_effective_stream_profile(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	ChiakiHeadlessCloudLaunchInfo launch = {
		.host = "127.0.0.1",
		.stream_port = 9295,
		.session_id = "sid",
		.launch_spec = "spec",
		.morning = morning,
		.morning_size = sizeof(morning),
		.ps5 = true,
		.enable_dualsense = true,
		.enable_keyboard = true,
		.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p,
		.fps = CHIAKI_VIDEO_FPS_PRESET_30,
		.bitrate = 5000,
		.codec = CHIAKI_CODEC_H264,
	};

	ChiakiHeadlessStreamProfile profile = {0};
	munit_assert_int(chiaki_headless_runtime_get_effective_stream_profile(&launch, &profile), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(profile.resolution, ==, CHIAKI_VIDEO_RESOLUTION_PRESET_540p);
	munit_assert_int(profile.fps, ==, CHIAKI_VIDEO_FPS_PRESET_30);
	munit_assert_int(profile.bitrate, ==, 5000);
	munit_assert_int(profile.codec, ==, CHIAKI_CODEC_H264);

	ChiakiHeadlessStreamProfileOverrides overrides = {
		.use_resolution = true,
		.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p,
		.use_fps = true,
		.fps = CHIAKI_VIDEO_FPS_PRESET_60,
		.use_bitrate = true,
		.bitrate = 17000,
		.use_codec = true,
		.codec = CHIAKI_CODEC_H265,
	};
	munit_assert_int(chiaki_headless_runtime_set_stream_profile_overrides(&overrides), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_headless_runtime_get_effective_stream_profile(&launch, &profile), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(profile.resolution, ==, CHIAKI_VIDEO_RESOLUTION_PRESET_1080p);
	munit_assert_int(profile.fps, ==, CHIAKI_VIDEO_FPS_PRESET_60);
	munit_assert_int(profile.bitrate, ==, 17000);
	munit_assert_int(profile.codec, ==, CHIAKI_CODEC_H265);
	munit_assert_int(chiaki_headless_runtime_set_stream_profile_overrides(NULL), ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_get_effective_launch_info(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	ChiakiHeadlessCloudLaunchInfo launch = {
		.host = "127.0.0.1",
		.stream_port = 9295,
		.session_id = "sid",
		.launch_spec = "spec",
		.morning = morning,
		.morning_size = sizeof(morning),
		.ps5 = true,
		.enable_dualsense = true,
		.enable_keyboard = true,
		.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p,
		.fps = CHIAKI_VIDEO_FPS_PRESET_30,
		.bitrate = 5000,
		.codec = CHIAKI_CODEC_H264,
	};

	ChiakiHeadlessCloudLaunchInfo effective = {0};
	munit_assert_int(chiaki_headless_runtime_get_effective_launch_info(&launch, &effective), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_true(effective.ps5);
	munit_assert_true(effective.enable_dualsense);
	munit_assert_true(effective.enable_keyboard);

	ChiakiHeadlessLaunchOverrides launch_overrides = {
		.use_ps5 = true,
		.ps5 = false,
		.use_enable_dualsense = true,
		.enable_dualsense = false,
		.use_enable_keyboard = true,
		.enable_keyboard = false,
	};
	ChiakiHeadlessStreamProfileOverrides stream_overrides = {
		.use_resolution = true,
		.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p,
		.use_fps = true,
		.fps = CHIAKI_VIDEO_FPS_PRESET_60,
		.use_bitrate = true,
		.bitrate = 19000,
		.use_codec = true,
		.codec = CHIAKI_CODEC_H265,
	};
	munit_assert_int(chiaki_headless_runtime_set_launch_overrides(&launch_overrides), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_headless_runtime_set_stream_profile_overrides(&stream_overrides), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_headless_runtime_get_effective_launch_info(&launch, &effective), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_false(effective.ps5);
	munit_assert_false(effective.enable_dualsense);
	munit_assert_false(effective.enable_keyboard);
	munit_assert_int(effective.resolution, ==, CHIAKI_VIDEO_RESOLUTION_PRESET_1080p);
	munit_assert_int(effective.fps, ==, CHIAKI_VIDEO_FPS_PRESET_60);
	munit_assert_int(effective.bitrate, ==, 19000);
	munit_assert_int(effective.codec, ==, CHIAKI_CODEC_H265);
	munit_assert_int(chiaki_headless_runtime_set_stream_profile_overrides(NULL), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_headless_runtime_set_launch_overrides(NULL), ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_get_effective_connect_info(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	uint8_t regist[CHIAKI_SESSION_AUTH_SIZE] = {0xAA, 0xBB, 0xCC, 0xDD};
	ChiakiHeadlessCloudLaunchInfo launch = {
		.host = "127.0.0.1",
		.stream_port = 9295,
		.session_id = "sid",
		.launch_spec = "spec",
		.morning = morning,
		.morning_size = sizeof(morning),
		.regist_key = regist,
		.regist_key_size = sizeof(regist),
		.ps5 = true,
		.enable_dualsense = true,
		.enable_keyboard = false,
		.takion_protocol_version = 9,
		.psn_wrapper_type = 0x59,
		.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
		.fps = CHIAKI_VIDEO_FPS_PRESET_60,
		.bitrate = 9000,
		.codec = CHIAKI_CODEC_H264,
	};

	ChiakiConnectInfo connect = {0};
	munit_assert_int(chiaki_headless_runtime_get_effective_connect_info(&launch, &connect), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_true(connect.ps5);
	munit_assert_true(connect.enable_dualsense);
	munit_assert_false(connect.enable_keyboard);
	munit_assert_int(connect.cloud_takion_protocol_version, ==, 9);
	munit_assert_int(connect.cloud_psn_wrapper_type, ==, 0x59);
	munit_assert_int(connect.video_profile.bitrate, ==, 9000);
	munit_assert_int(connect.video_profile.codec, ==, CHIAKI_CODEC_H264);

	ChiakiHeadlessRuntimeOverrides overrides = {
		.has_launch_overrides = true,
		.launch_overrides = {
			.use_ps5 = true,
			.ps5 = false,
			.use_enable_keyboard = true,
			.enable_keyboard = true,
		},
		.has_stream_profile_overrides = true,
		.stream_profile_overrides = {
			.use_bitrate = true,
			.bitrate = 23000,
			.use_codec = true,
			.codec = CHIAKI_CODEC_H265,
		},
		.has_policy_overrides = true,
		.policy_overrides = {
			.use_packet_loss_max = true,
			.packet_loss_max = 0.4,
			.use_video_profile_auto_downgrade = true,
			.video_profile_auto_downgrade = false,
		},
	};
	munit_assert_int(chiaki_headless_runtime_set_overrides(&overrides), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_headless_runtime_get_effective_connect_info(&launch, &connect), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_false(connect.ps5);
	munit_assert_true(connect.enable_keyboard);
	munit_assert_int(connect.cloud_takion_protocol_version, ==, 9);
	munit_assert_int(connect.cloud_psn_wrapper_type, ==, 0x59);
	munit_assert_int(connect.video_profile.bitrate, ==, 23000);
	munit_assert_int(connect.video_profile.codec, ==, CHIAKI_CODEC_H265);
	munit_assert_double_equal(connect.packet_loss_max, 0.4, 10);
	munit_assert_false(connect.video_profile_auto_downgrade);

	munit_assert_int(chiaki_headless_runtime_set_overrides(NULL), ==, CHIAKI_ERR_SUCCESS);
	ChiakiHeadlessRuntimeOverrides cleared = {0};
	munit_assert_int(chiaki_headless_runtime_get_overrides(&cleared), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_false(cleared.has_launch_overrides);
	munit_assert_false(cleared.has_stream_profile_overrides);
	launch.codec = (ChiakiCodec)99;
	ChiakiHeadlessCloudLaunchInfo effective_launch = {0};
	munit_assert_int(chiaki_headless_runtime_get_effective_launch_info(&launch, &effective_launch), ==, CHIAKI_ERR_INVALID_DATA);
	munit_assert_int(chiaki_headless_runtime_get_effective_connect_info(&launch, &connect), ==, CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_get_sanity_report(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	ChiakiHeadlessCloudLaunchInfo launch = {
		.host = "127.0.0.1",
		.stream_port = 9295,
		.session_id = "sid",
		.launch_spec = "spec",
		.morning = morning,
		.morning_size = sizeof(morning),
		.ps5 = true,
		.enable_dualsense = true,
		.enable_keyboard = false,
		.takion_protocol_version = 9,
		.psn_wrapper_type = 0x59,
		.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
		.fps = CHIAKI_VIDEO_FPS_PRESET_60,
		.bitrate = 8000,
		.codec = CHIAKI_CODEC_H264,
	};

	ChiakiHeadlessRuntimeSanityReport report = {0};
	munit_assert_int(chiaki_headless_runtime_get_sanity_report(&launch, &report), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(report.validation_error, ==, CHIAKI_ERR_SUCCESS);
	munit_assert_false(report.runtime_session_active);
	munit_assert_false(report.has_overrides);
	munit_assert_int(report.effective_stream_profile.bitrate, ==, 8000);
	munit_assert_int(report.effective_stream_profile.codec, ==, CHIAKI_CODEC_H264);

	ChiakiHeadlessRuntimeOverrides overrides = {
		.has_stream_profile_overrides = true,
		.stream_profile_overrides = {
			.use_bitrate = true,
			.bitrate = 26000,
		},
	};
	munit_assert_int(chiaki_headless_runtime_set_overrides(&overrides), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_headless_runtime_get_sanity_report(&launch, &report), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_true(report.has_overrides);
	munit_assert_int(report.validation_error, ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(report.effective_stream_profile.bitrate, ==, 26000);

	launch.codec = (ChiakiCodec)99;
	munit_assert_int(chiaki_headless_runtime_get_sanity_report(&launch, &report), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(report.validation_error, ==, CHIAKI_ERR_INVALID_DATA);
	munit_assert_int(chiaki_headless_runtime_set_overrides(NULL), ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_get_state_snapshot(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	ChiakiHeadlessCloudLaunchInfo launch = {
		.host = "127.0.0.1",
		.stream_port = 9295,
		.session_id = "sid",
		.launch_spec = "spec",
		.morning = morning,
		.morning_size = sizeof(morning),
		.ps5 = true,
		.enable_dualsense = true,
		.enable_keyboard = false,
		.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
		.fps = CHIAKI_VIDEO_FPS_PRESET_60,
		.bitrate = 7000,
		.codec = CHIAKI_CODEC_H264,
	};

	ChiakiHeadlessRuntimeStateSnapshot snapshot = {0};
	munit_assert_int(chiaki_headless_runtime_get_state_snapshot(&launch, &snapshot), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(snapshot.api_version, ==, chiaki_headless_api_version());
	munit_assert_false(snapshot.overrides.has_launch_overrides);
	munit_assert_false(snapshot.overrides.has_stream_profile_overrides);
	munit_assert_int(snapshot.sanity_report.validation_error, ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(snapshot.sanity_report.effective_stream_profile.bitrate, ==, 7000);

	ChiakiHeadlessRuntimeOverrides overrides = {
		.has_launch_overrides = true,
		.launch_overrides = {
			.use_enable_keyboard = true,
			.enable_keyboard = true,
		},
		.has_stream_profile_overrides = true,
		.stream_profile_overrides = {
			.use_bitrate = true,
			.bitrate = 25000,
		},
	};
	munit_assert_int(chiaki_headless_runtime_set_overrides(&overrides), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_headless_runtime_get_state_snapshot(&launch, &snapshot), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_true(snapshot.overrides.has_launch_overrides);
	munit_assert_true(snapshot.overrides.has_stream_profile_overrides);
	munit_assert_true(snapshot.sanity_report.effective_launch_info.enable_keyboard);
	munit_assert_int(snapshot.sanity_report.effective_stream_profile.bitrate, ==, 25000);
	munit_assert_int(chiaki_headless_runtime_set_overrides(NULL), ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_state_snapshot_compat(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	ChiakiHeadlessCloudLaunchInfo launch = {
		.host = "127.0.0.1",
		.stream_port = 9295,
		.session_id = "sid",
		.launch_spec = "spec",
		.morning = morning,
		.morning_size = sizeof(morning),
		.ps5 = true,
		.enable_dualsense = true,
		.enable_keyboard = false,
		.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
		.fps = CHIAKI_VIDEO_FPS_PRESET_60,
		.bitrate = 7000,
		.codec = CHIAKI_CODEC_H264,
	};

	size_t full_size = chiaki_headless_runtime_state_snapshot_size();
	munit_assert_size(full_size, ==, sizeof(ChiakiHeadlessRuntimeStateSnapshot));

	ChiakiHeadlessRuntimeStateSnapshot snapshot = {0};
	chiaki_headless_runtime_state_snapshot_init(&snapshot);
	munit_assert_uint32(snapshot.api_version, ==, chiaki_headless_api_version());

	struct
	{
		uint32_t api_version;
	} tiny = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_state_snapshot_compat(&launch, &tiny, sizeof(tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny.api_version, ==, chiaki_headless_api_version());

	munit_assert_int(
		chiaki_headless_runtime_get_state_snapshot_compat(&launch, &snapshot, sizeof(snapshot)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(snapshot.api_version, ==, chiaki_headless_api_version());
	munit_assert_int(snapshot.sanity_report.validation_error, ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_capabilities(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	size_t cap_size = chiaki_headless_runtime_capabilities_size();
	munit_assert_size(cap_size, ==, sizeof(ChiakiHeadlessRuntimeCapabilities));

	ChiakiHeadlessRuntimeCapabilities caps = {0};
	chiaki_headless_runtime_capabilities_init(&caps);
	munit_assert_uint32(caps.api_version, ==, chiaki_headless_api_version());

	munit_assert_int(chiaki_headless_runtime_get_capabilities(&caps), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_true(caps.supports_runtime_struct_start);
	munit_assert_true(caps.supports_runtime_cloud_start_strings);
	munit_assert_true(caps.supports_runtime_cloud_stop);
	munit_assert_true(caps.supports_runtime_overrides_bundle);
	munit_assert_true(caps.supports_runtime_overrides_patch);
	munit_assert_true(caps.supports_runtime_policy_overrides);
	munit_assert_true(caps.supports_runtime_sanity_report);
	munit_assert_true(caps.supports_runtime_state_snapshot);
	munit_assert_true(caps.supports_runtime_state_snapshot_compat);
	munit_assert_true(caps.supports_runtime_media_diagnostics_snapshot);
	munit_assert_true(caps.supports_runtime_media_diagnostics_snapshot_compat);
	munit_assert_true(caps.supports_runtime_media_diagnostics_snapshot_with_profile_key);
	munit_assert_true(caps.supports_runtime_media_diagnostics_snapshot_with_profile_key_compat);
	munit_assert_true(caps.supports_runtime_recovery_decision);
	munit_assert_true(caps.supports_runtime_recovery_decision_compat);
	munit_assert_true(caps.supports_runtime_recovery_decision_with_profile_key);
	munit_assert_true(caps.supports_runtime_recovery_decision_with_profile_key_compat);
	munit_assert_true(caps.supports_runtime_recovery_apply);
	munit_assert_true(caps.supports_runtime_recovery_apply_compat);
	munit_assert_size(
		caps.min_runtime_diagnostics_snapshot_size,
		==,
		sizeof(ChiakiMediaSessionDiagnosticsSnapshot));
	munit_assert_size(
		caps.min_runtime_recovery_decision_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoveryDecision));
	munit_assert_size(
		caps.min_runtime_recovery_result_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoveryResult));
	munit_assert_true(caps.supports_runtime_recover_result);
	munit_assert_true(caps.supports_runtime_recover_result_compat);
	munit_assert_true(caps.supports_runtime_recover_result_with_profile_key);
	munit_assert_true(caps.supports_runtime_recover_result_with_profile_key_compat);
	munit_assert_true(caps.supports_runtime_recover_tuned);
	munit_assert_true(caps.supports_runtime_recover_tuned_compat);
	munit_assert_size(
		caps.min_runtime_recovery_tuning_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoveryTuning));
	munit_assert_size(
		caps.min_runtime_recovery_status_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoveryStatus));
	munit_assert_size(
		caps.min_runtime_recovery_config_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoveryConfig));
	munit_assert_size(
		caps.min_runtime_recovery_simulation_step_input_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoverySimulationStepInput));
	munit_assert_size(
		caps.min_runtime_recovery_simulation_health_step_input_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput));
	munit_assert_size(
		caps.min_runtime_recovery_simulation_step_output_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoverySimulationStepOutput));
	munit_assert_size(
		caps.min_runtime_recovery_simulation_report_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoverySimulationReport));
	munit_assert_size(
		caps.min_runtime_recovery_auto_loop_timeline_step_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineStep));
	munit_assert_size(
		caps.min_runtime_recovery_auto_loop_timeline_summary_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineSummary));
	munit_assert_size(
		caps.min_runtime_host_status_size,
		==,
		sizeof(ChiakiHeadlessRuntimeHostStatus));
	munit_assert_size(
		caps.min_runtime_playback_readiness_status_size,
		==,
		sizeof(ChiakiHeadlessRuntimePlaybackReadinessStatus));
	munit_assert_size(
		caps.min_runtime_playback_continuity_status_size,
		==,
		sizeof(ChiakiHeadlessRuntimePlaybackContinuityStatus));
	munit_assert_size(
		caps.min_runtime_video_frame_metadata_size,
		==,
		sizeof(ChiakiHeadlessRuntimeVideoFrameMetadata));
	munit_assert_size(
		caps.min_runtime_video_frame_poll_size,
		==,
		sizeof(ChiakiHeadlessRuntimeVideoFramePoll));
	munit_assert_size(
		caps.min_runtime_recovery_parity_smoke_runner_result_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult));
	munit_assert_size(
		caps.min_runtime_recovery_parity_baseline_scenario_count_size,
		==,
		sizeof(size_t));
	munit_assert_size(
		caps.min_runtime_recovery_parity_baseline_execution_detail_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail));
	munit_assert_size(
		caps.min_runtime_recovery_core_diagnostics_summary_size,
		==,
		sizeof(ChiakiHeadlessRuntimeRecoveryCoreDiagnosticsSummary));
	munit_assert_true(caps.supports_runtime_recovery_status);
	munit_assert_true(caps.supports_runtime_recovery_status_compat);
	munit_assert_true(caps.supports_runtime_recovery_status_reset);
	munit_assert_true(caps.supports_runtime_recovery_config_set_get);
	munit_assert_true(caps.supports_runtime_recovery_config_compat);
	munit_assert_true(caps.supports_runtime_recovery_profile_key_set_get);
	munit_assert_true(caps.supports_runtime_recover_auto);
	munit_assert_true(caps.supports_runtime_recover_auto_compat);
	munit_assert_true(caps.supports_runtime_recover_auto_with_status);
	munit_assert_true(caps.supports_runtime_recover_auto_with_status_compat);
	munit_assert_true(caps.supports_runtime_recovery_simulation);
	munit_assert_true(caps.supports_runtime_recovery_simulation_compat);
	munit_assert_true(caps.supports_runtime_recovery_simulation_sequence);
	munit_assert_true(caps.supports_runtime_recovery_simulation_report);
	munit_assert_true(caps.supports_runtime_recovery_simulation_sequence_compat);
	munit_assert_true(caps.supports_runtime_recovery_simulation_report_compat);
	munit_assert_true(caps.supports_runtime_recovery_simulation_sequence_with_report);
	munit_assert_true(caps.supports_runtime_recovery_simulation_sequence_with_report_compat);
	munit_assert_true(caps.supports_runtime_recovery_simulation_report_from_outputs);
	munit_assert_true(caps.supports_runtime_recovery_simulation_report_from_outputs_compat);
	munit_assert_true(caps.supports_runtime_recovery_simulation_sequence_with_profile_key);
	munit_assert_true(caps.supports_runtime_recovery_simulation_report_with_profile_key);
	munit_assert_true(caps.supports_runtime_recovery_simulation_sequence_with_profile_key_compat);
	munit_assert_true(caps.supports_runtime_recovery_simulation_report_with_profile_key_compat);
	munit_assert_true(caps.supports_runtime_recovery_simulation_sequence_with_profile_key_with_report);
	munit_assert_true(caps.supports_runtime_recovery_simulation_sequence_with_profile_key_with_report_compat);
	munit_assert_true(caps.supports_runtime_recovery_simulation_health_sequence_with_profile_key);
	munit_assert_true(caps.supports_runtime_recovery_simulation_health_report_with_profile_key);
	munit_assert_true(caps.supports_runtime_recovery_simulation_health_sequence_with_profile_key_compat);
	munit_assert_true(caps.supports_runtime_recovery_simulation_health_report_with_profile_key_compat);
	munit_assert_true(caps.supports_runtime_recovery_simulation_health_sequence_with_profile_key_with_report);
	munit_assert_true(caps.supports_runtime_recovery_simulation_health_sequence_with_profile_key_with_report_compat);
	munit_assert_true(caps.supports_runtime_recovery_auto_loop_timeline);
	munit_assert_true(caps.supports_runtime_recovery_auto_loop_timeline_compat);
	munit_assert_true(caps.supports_runtime_host_status);
	munit_assert_true(caps.supports_runtime_host_status_compat);
	munit_assert_true(caps.supports_runtime_playback_readiness_status);
	munit_assert_true(caps.supports_runtime_playback_readiness_status_compat);
	munit_assert_true(caps.supports_runtime_playback_continuity_status);
	munit_assert_true(caps.supports_runtime_playback_continuity_status_compat);
	munit_assert_true(caps.supports_runtime_video_frame_metadata);
	munit_assert_true(caps.supports_runtime_video_frame_metadata_compat);
	munit_assert_true(caps.supports_runtime_video_frame_poll);
	munit_assert_true(caps.supports_runtime_video_frame_poll_compat);
	munit_assert_true(caps.supports_runtime_recovery_parity_fixture_eval);
	munit_assert_true(caps.supports_runtime_recovery_parity_fixture_eval_compat);
	munit_assert_true(caps.supports_runtime_recovery_parity_fixture_export);
	munit_assert_true(caps.supports_runtime_recovery_parity_fixture_export_compat);
	munit_assert_true(caps.supports_runtime_recovery_parity_smoke);
	munit_assert_true(caps.supports_runtime_recovery_parity_smoke_compat);
	munit_assert_true(caps.supports_runtime_recovery_parity_smoke_runner);
	munit_assert_true(caps.supports_runtime_recovery_parity_smoke_runner_compat);
	munit_assert_true(caps.supports_runtime_recovery_parity_baseline_smoke);
	munit_assert_true(caps.supports_runtime_recovery_parity_baseline_smoke_compat);
	munit_assert_true(caps.supports_runtime_recovery_parity_baseline_scenario_count);
	munit_assert_true(caps.supports_runtime_recovery_parity_baseline_scenario_count_compat);
	munit_assert_true(caps.supports_runtime_recovery_parity_baseline_scenario_label);
	munit_assert_true(caps.supports_runtime_recovery_parity_baseline_scenario_label_compat);
	munit_assert_true(caps.supports_runtime_recovery_parity_baseline_execution_details);
	munit_assert_true(caps.supports_runtime_recovery_parity_baseline_execution_details_compat);
	munit_assert_true(caps.supports_runtime_recovery_core_diagnostics);
	munit_assert_true(caps.supports_runtime_recovery_core_diagnostics_compat);

	struct { uint32_t api_version; } tiny = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_capabilities_compat(&tiny, sizeof(tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny.api_version, ==, chiaki_headless_api_version());
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_host_status_contract(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;
	ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
	chiaki_media_session_diagnostics_snapshot_init(&snapshot);
	snapshot.health.health_state = CHIAKI_MEDIA_HEALTH_DEGRADED;
	snapshot.stats.event_count = 55;
	snapshot.stats.error_event_count = 8;
	snapshot.stats.ready_event_count = 21;
	snapshot.stats.packets_received = 1001;
	snapshot.stats.packets_lost = 33;
	snapshot.stats.measured_bitrate_kbps = 8123.5;
	snapshot.stats.video_decode_lost_frames = 11;
	snapshot.stats.video_decode_recovered_frames = 7;
	snapshot.stats.video_decode_gap_event_count = 5;

	ChiakiHeadlessRuntimeRecoveryResult recovery_result = {0};
	recovery_result.api_version = chiaki_headless_api_version();
	recovery_result.decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR;
	recovery_result.applied_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
	recovery_result.apply_succeeded = false;
	recovery_result.action_transitioned = true;
	recovery_result.recover_monotonic_us = 7654321;
	munit_assert_true(recovery_result.action_transitioned);

	ChiakiHeadlessRuntimeRecoveryStatus recovery_status = {0};
	chiaki_headless_runtime_recovery_status_init(&recovery_status);
	recovery_status.runtime_session_active = true;
	recovery_status.degraded_streak = 3;
	recovery_status.last_idr_request_monotonic_us = 1234567;
	recovery_status.last_recommended_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR;
	recovery_status.last_applied_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
	recovery_status.recover_attempt_count = 9;
	recovery_status.recover_success_count = 4;
	recovery_status.recover_failure_count = 5;

	ChiakiHeadlessRuntimeHostStatus host_status = {0};
	munit_assert_int(
		chiaki_headless_runtime_build_host_status(
			&snapshot,
			&recovery_result,
			&recovery_status,
			&host_status),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(host_status.api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(host_status.health_state, ==, CHIAKI_MEDIA_HEALTH_DEGRADED);
	munit_assert_int(host_status.recommended_action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_int(host_status.applied_action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);
	munit_assert_false(host_status.recover_apply_succeeded);
	munit_assert_true(host_status.recover_action_transitioned);
	munit_assert_uint64(host_status.recover_monotonic_us, ==, 7654321);
	munit_assert_true(host_status.runtime_session_active);
	munit_assert_uint64(host_status.degraded_streak, ==, 3);
	munit_assert_uint64(host_status.last_idr_request_monotonic_us, ==, 1234567);
	munit_assert_uint64(host_status.recover_attempt_count, ==, 9);
	munit_assert_uint64(host_status.recover_success_count, ==, 4);
	munit_assert_uint64(host_status.recover_failure_count, ==, 5);
	munit_assert_uint64(host_status.event_count, ==, 55);
	munit_assert_uint64(host_status.error_event_count, ==, 8);
	munit_assert_uint64(host_status.ready_event_count, ==, 21);
	munit_assert_uint64(host_status.packets_received, ==, 1001);
	munit_assert_uint64(host_status.packets_lost, ==, 33);
	munit_assert_double(host_status.measured_bitrate_kbps, ==, 8123.5);
	munit_assert_uint64(host_status.video_decode_lost_frames, ==, 11);
	munit_assert_uint64(host_status.video_decode_recovered_frames, ==, 7);
	munit_assert_uint64(host_status.video_decode_gap_event_count, ==, 5);

	struct
	{
		uint32_t api_version;
		uint32_t health_state;
	} tiny = {0};
	munit_assert_int(
		chiaki_headless_runtime_build_host_status_compat(
			&snapshot,
			sizeof(snapshot),
			&recovery_result,
			sizeof(recovery_result),
			&recovery_status,
			sizeof(recovery_status),
			&tiny,
			sizeof(tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny.api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(tiny.health_state, ==, CHIAKI_MEDIA_HEALTH_DEGRADED);

	munit_assert_int(
		chiaki_headless_runtime_build_host_status(NULL, &recovery_result, &recovery_status, &host_status),
		==,
		CHIAKI_ERR_INVALID_DATA);
	munit_assert_int(
		chiaki_headless_runtime_build_host_status_compat(
			NULL,
			sizeof(snapshot),
			&recovery_result,
			sizeof(recovery_result),
			&recovery_status,
			sizeof(recovery_status),
			&tiny,
			sizeof(tiny)),
		==,
		CHIAKI_ERR_INVALID_DATA);

	return MUNIT_OK;
}

static MunitResult test_headless_runtime_playback_readiness_status_contract(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;
	ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
	chiaki_media_session_diagnostics_snapshot_init(&snapshot);
	snapshot.health.health_state = CHIAKI_MEDIA_HEALTH_DEGRADED;
	snapshot.stats.event_count = 9;

	ChiakiHeadlessRuntimeRecoveryResult recovery_result = {0};
	recovery_result.api_version = chiaki_headless_api_version();
	recovery_result.decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR;
	recovery_result.applied_action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR;
	recovery_result.apply_succeeded = true;
	recovery_result.action_transitioned = true;

	ChiakiHeadlessRuntimeRecoveryStatus recovery_status = {0};
	chiaki_headless_runtime_recovery_status_init(&recovery_status);
	recovery_status.recover_attempt_count = 4;
	recovery_status.recover_success_count = 3;
	recovery_status.recover_failure_count = 1;

	ChiakiHeadlessRuntimePlaybackReadinessStatus readiness = {0};
	munit_assert_int(
		chiaki_headless_runtime_build_playback_readiness_status(
			&snapshot,
			&recovery_result,
			&recovery_status,
			&readiness),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(readiness.api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(readiness.host_status.health_state, ==, CHIAKI_MEDIA_HEALTH_DEGRADED);
	munit_assert_uint64(readiness.recover_result_count, ==, 1);
	munit_assert_uint64(readiness.recover_apply_success_count, ==, 1);
	munit_assert_uint64(readiness.recover_apply_failure_count, ==, 0);
	munit_assert_uint64(readiness.recover_action_transition_count, ==, 1);
	munit_assert_uint64(readiness.recover_counter_observed_count, ==, 4);
	munit_assert_true(readiness.recover_counters_consistent);

	recovery_status.recover_attempt_count = 7;
	munit_assert_int(
		chiaki_headless_runtime_build_playback_readiness_status(
			&snapshot,
			&recovery_result,
			&recovery_status,
			&readiness),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint64(readiness.recover_counter_observed_count, ==, 4);
	munit_assert_false(readiness.recover_counters_consistent);

	struct
	{
		uint32_t api_version;
		ChiakiHeadlessRuntimeHostStatus host_status;
	} tiny = {0};
	munit_assert_int(
		chiaki_headless_runtime_build_playback_readiness_status_compat(
			&snapshot,
			sizeof(snapshot),
			&recovery_result,
			sizeof(recovery_result),
			&recovery_status,
			sizeof(recovery_status),
			&tiny,
			sizeof(tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny.api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(tiny.host_status.health_state, ==, CHIAKI_MEDIA_HEALTH_DEGRADED);

	munit_assert_int(
		chiaki_headless_runtime_build_playback_readiness_status(NULL, &recovery_result, &recovery_status, &readiness),
		==,
		CHIAKI_ERR_INVALID_DATA);

	return MUNIT_OK;
}

static MunitResult test_headless_runtime_playback_continuity_status_contract(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;
	ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
	chiaki_media_session_diagnostics_snapshot_init(&snapshot);
	snapshot.health.health_state = CHIAKI_MEDIA_HEALTH_DEGRADED;
	snapshot.stats.video_frame_count = 18;
	snapshot.stats.audio_frame_count = 27;
	snapshot.stats.video_decode_lost_frames = 9;
	snapshot.stats.video_decode_recovered_frames = 4;
	snapshot.stats.video_decode_gap_event_count = 5;

	ChiakiHeadlessRuntimeRecoveryResult recovery_result = {0};
	recovery_result.api_version = chiaki_headless_api_version();
	recovery_result.decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR;

	ChiakiHeadlessRuntimeRecoveryStatus recovery_status = {0};
	chiaki_headless_runtime_recovery_status_init(&recovery_status);

	ChiakiHeadlessRuntimePlaybackContinuityStatus continuity = {0};
	munit_assert_int(
		chiaki_headless_runtime_build_playback_continuity_status(
			&snapshot,
			&recovery_result,
			&recovery_status,
			&continuity),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(continuity.api_version, ==, chiaki_headless_api_version());
	munit_assert_uint64(continuity.video_frame_count, ==, 18);
	munit_assert_uint64(continuity.audio_frame_count, ==, 27);
	munit_assert_uint64(continuity.frame_counter_observed_count, ==, 45);
	munit_assert_uint64(continuity.decode_counter_observed_count, ==, 13);
	munit_assert_true(continuity.has_video_frame_activity);
	munit_assert_true(continuity.has_decode_gap_activity);
	munit_assert_true(continuity.decode_gap_implies_decode_activity);

	snapshot.stats.video_decode_gap_event_count = 17;
	munit_assert_int(
		chiaki_headless_runtime_build_playback_continuity_status(
			&snapshot,
			&recovery_result,
			&recovery_status,
			&continuity),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_false(continuity.decode_gap_implies_decode_activity);

	struct
	{
		uint32_t api_version;
		ChiakiHeadlessRuntimeHostStatus host_status;
	} tiny = {0};
	munit_assert_int(
		chiaki_headless_runtime_build_playback_continuity_status_compat(
			&snapshot,
			sizeof(snapshot),
			&recovery_result,
			sizeof(recovery_result),
			&recovery_status,
			sizeof(recovery_status),
			&tiny,
			sizeof(tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny.api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(tiny.host_status.health_state, ==, CHIAKI_MEDIA_HEALTH_DEGRADED);

	munit_assert_int(
		chiaki_headless_runtime_build_playback_continuity_status(
			NULL,
			&recovery_result,
			&recovery_status,
			&continuity),
		==,
		CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_media_diagnostics_without_session(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;
	ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_media_diagnostics_snapshot(0, &snapshot),
		==,
		CHIAKI_ERR_INVALID_DATA);
	munit_assert_int(
		chiaki_headless_runtime_get_media_diagnostics_snapshot_with_profile_key(
			0,
			"default",
			&snapshot),
		==,
		CHIAKI_ERR_INVALID_DATA);
	struct { uint32_t api_version; } tiny = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_media_diagnostics_snapshot_compat(
			0,
			&tiny,
			sizeof(tiny)),
		==,
		CHIAKI_ERR_INVALID_DATA);
	ChiakiHeadlessRuntimePlaybackContinuityStatus continuity = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_playback_continuity_status(0, &continuity),
		==,
		CHIAKI_ERR_INVALID_DATA);
	munit_assert_int(
		chiaki_headless_runtime_get_playback_continuity_status_compat(
			0,
			&tiny,
			sizeof(tiny)),
		==,
		CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_video_frame_metadata_without_session(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;

	munit_assert_size(
		chiaki_headless_runtime_video_frame_metadata_size(),
		==,
		sizeof(ChiakiHeadlessRuntimeVideoFrameMetadata));

	ChiakiHeadlessRuntimeVideoFrameMetadata metadata = {0};
	chiaki_headless_runtime_video_frame_metadata_init(&metadata);
	munit_assert_uint32(metadata.api_version, ==, chiaki_headless_api_version());
	munit_assert_false(metadata.runtime_session_active);
	munit_assert_false(metadata.has_video_frame);
	munit_assert_int(metadata.format, ==, CHIAKI_HEADLESS_VIDEO_FORMAT_UNKNOWN);
	munit_assert_uint32(metadata.width, ==, 0);
	munit_assert_uint32(metadata.height, ==, 0);
	munit_assert_uint64(metadata.video_frame_count, ==, 0);

	munit_assert_int(
		chiaki_headless_runtime_get_video_frame_metadata(&metadata),
		==,
		CHIAKI_ERR_INVALID_DATA);

	struct
	{
		uint32_t api_version;
		bool runtime_session_active;
	} tiny = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_video_frame_metadata_compat(
			&tiny,
			sizeof(tiny)),
		==,
		CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_video_frame_poll_without_session(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;
	munit_assert_size(
		chiaki_headless_runtime_video_frame_poll_size(),
		==,
		sizeof(ChiakiHeadlessRuntimeVideoFramePoll));

	ChiakiHeadlessRuntimeVideoFramePoll poll = {0};
	chiaki_headless_runtime_video_frame_poll_init(&poll);
	munit_assert_uint32(poll.api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(poll.metadata.api_version, ==, chiaki_headless_api_version());
	munit_assert_int(
		chiaki_headless_runtime_poll_video_frame(&poll),
		==,
		CHIAKI_ERR_INVALID_DATA);
	munit_assert_int(
		chiaki_headless_runtime_poll_video_frame_compat(&poll, sizeof(poll)),
		==,
		CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_video_frame_poll_layout(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;

	munit_assert_size(
		offsetof(ChiakiHeadlessRuntimeVideoFramePoll, metadata),
		<,
		offsetof(ChiakiHeadlessRuntimeVideoFramePoll, planes));
	munit_assert_size(
		offsetof(ChiakiHeadlessRuntimeVideoFramePoll, planes),
		<,
		offsetof(ChiakiHeadlessRuntimeVideoFramePoll, plane_sizes));
	munit_assert_size(
		offsetof(ChiakiHeadlessRuntimeVideoFramePoll, plane_sizes),
		<,
		offsetof(ChiakiHeadlessRuntimeVideoFramePoll, strides));
	munit_assert_size(
		offsetof(ChiakiHeadlessRuntimeVideoFramePoll, strides),
		<,
		offsetof(ChiakiHeadlessRuntimeVideoFramePoll, plane_count));

	ChiakiHeadlessRuntimeVideoFramePoll poll = {
		.api_version = 123,
		.metadata = {
			.api_version = 456,
			.runtime_session_active = true,
			.has_video_frame = true,
			.width = 1920,
			.height = 1080,
		},
		.plane_sizes = {11, 22, 33, 44},
		.strides = {1, 2, 3, 4},
		.plane_count = 3,
	};
	chiaki_headless_runtime_video_frame_poll_init(&poll);
	munit_assert_uint32(poll.api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(poll.metadata.api_version, ==, chiaki_headless_api_version());
	munit_assert_false(poll.metadata.runtime_session_active);
	munit_assert_false(poll.metadata.has_video_frame);
	munit_assert_uint32(poll.metadata.width, ==, 0);
	munit_assert_uint32(poll.metadata.height, ==, 0);
	munit_assert_size(poll.plane_sizes[0], ==, 0);
	munit_assert_size(poll.plane_sizes[1], ==, 0);
	munit_assert_size(poll.plane_sizes[2], ==, 0);
	munit_assert_size(poll.plane_sizes[3], ==, 0);
	munit_assert_int(poll.strides[0], ==, 0);
	munit_assert_int(poll.strides[1], ==, 0);
	munit_assert_int(poll.strides[2], ==, 0);
	munit_assert_int(poll.strides[3], ==, 0);
	munit_assert_int(poll.plane_count, ==, 0);

	return MUNIT_OK;
}

static MunitResult test_headless_runtime_video_frame_poll_buffer_guard(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;

	struct
	{
		uint32_t api_version;
	} tiny = {0};
	munit_assert_int(
		chiaki_headless_runtime_poll_video_frame_compat(
			&tiny,
			sizeof(tiny)),
		==,
		CHIAKI_ERR_BUF_TOO_SMALL);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_recovery_decision_without_session(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;
	ChiakiHeadlessRuntimeRecoveryDecision decision = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_recovery_decision(0, &decision),
		==,
		CHIAKI_ERR_INVALID_DATA);
	munit_assert_int(
		chiaki_headless_runtime_get_recovery_decision_with_profile_key(
			0,
			"default",
			&decision),
		==,
		CHIAKI_ERR_INVALID_DATA);
	struct { uint32_t api_version; } tiny = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_recovery_decision_compat(
			0,
			&tiny,
			sizeof(tiny)),
		==,
		CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_recovery_apply_not_running(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;
	ChiakiHeadlessRuntimeRecoveryDecision none_decision = {0};
	none_decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
	ChiakiHeadlessRuntimeRecoveryAction applied = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR;
	munit_assert_int(
		chiaki_headless_runtime_apply_recovery_decision(&none_decision, &applied),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(applied, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);

	ChiakiHeadlessRuntimeRecoveryDecision idr_decision = {0};
	idr_decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR;
	munit_assert_int(
		chiaki_headless_runtime_apply_recovery_decision(&idr_decision, &applied),
		==,
		CHIAKI_ERR_INVALID_DATA);

	ChiakiHeadlessRuntimeRecoveryDecision stop_decision = {0};
	stop_decision.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_STOP_RUNTIME;
	munit_assert_int(
		chiaki_headless_runtime_apply_recovery_decision(&stop_decision, &applied),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(applied, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_STOP_RUNTIME);

	struct { uint32_t api_version; uint32_t health_state; int action; } tiny = {0};
	tiny.action = CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE;
	int applied_tiny = -1;
	munit_assert_int(
		chiaki_headless_runtime_apply_recovery_decision_compat(
			&tiny,
			sizeof(tiny),
			&applied_tiny,
			sizeof(applied_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(applied_tiny, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_recover_result_not_running(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;
	ChiakiHeadlessRuntimeRecoveryResult result = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_recover_result(0, &result),
		==,
		CHIAKI_ERR_INVALID_DATA);
	munit_assert_int(
		chiaki_headless_runtime_get_recover_result_with_profile_key(0, "default", &result),
		==,
		CHIAKI_ERR_INVALID_DATA);
	struct { uint32_t api_version; } tiny = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_recover_result_compat(0, &tiny, sizeof(tiny)),
		==,
		CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_recover_tuned_not_running(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;
	ChiakiHeadlessRuntimeRecoveryTuning tuning = {0};
	chiaki_headless_runtime_recovery_tuning_init(&tuning);
	ChiakiHeadlessRuntimeRecoveryResult result = {0};
	munit_assert_int(
		chiaki_headless_runtime_recover_tuned(0, &tuning, &result),
		==,
		CHIAKI_ERR_INVALID_DATA);
	struct { uint32_t api_version; uint32_t degraded_streak_threshold; } tiny_tuning = {0};
	tiny_tuning.api_version = chiaki_headless_api_version();
	tiny_tuning.degraded_streak_threshold = 3;
	struct { uint32_t api_version; } tiny_result = {0};
	munit_assert_int(
		chiaki_headless_runtime_recover_with_profile_key_tuned_compat(
			0,
			"default",
			&tiny_tuning,
			sizeof(tiny_tuning),
			&tiny_result,
			sizeof(tiny_result)),
		==,
		CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_recovery_status_reset(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;
	ChiakiHeadlessRuntimeRecoveryStatus status = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_recovery_status(&status),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(status.api_version, ==, chiaki_headless_api_version());
	munit_assert_false(status.runtime_session_active);
	munit_assert_int(status.last_recommended_action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);
	munit_assert_int(status.last_applied_action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);
	munit_assert_uint64(status.recover_attempt_count, ==, 0);
	munit_assert_uint64(status.recover_success_count, ==, 0);
	munit_assert_uint64(status.recover_failure_count, ==, 0);

	struct { uint32_t api_version; } tiny = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_recovery_status_compat(&tiny, sizeof(tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny.api_version, ==, chiaki_headless_api_version());

	munit_assert_int(
		chiaki_headless_runtime_reset_recovery_status(),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(
		chiaki_headless_runtime_get_recovery_status(&status),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint64(status.degraded_streak, ==, 0);
	munit_assert_uint64(status.last_idr_request_monotonic_us, ==, 0);
	munit_assert_int(status.last_recommended_action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);
	munit_assert_int(status.last_applied_action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);
	munit_assert_uint64(status.recover_attempt_count, ==, 0);
	munit_assert_uint64(status.recover_success_count, ==, 0);
	munit_assert_uint64(status.recover_failure_count, ==, 0);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_recovery_config_and_auto_not_running(
	const MunitParameter params[],
	void *user)
{
	(void)params;
	(void)user;
	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	chiaki_headless_runtime_recovery_config_init(&config);
	munit_assert_uint32(config.api_version, ==, chiaki_headless_api_version());
	munit_assert_int(config.profile, ==, CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT);
	munit_assert_uint32(config.tuning.degraded_streak_threshold, ==, 3);

	munit_assert_int(
		chiaki_headless_runtime_set_recovery_config(&config),
		==,
		CHIAKI_ERR_SUCCESS);
	memset(&config, 0, sizeof(config));
	munit_assert_int(
		chiaki_headless_runtime_get_recovery_config(&config),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(config.profile, ==, CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT);
	struct { uint32_t api_version; uint32_t profile; } tiny_cfg = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_recovery_config_compat(&tiny_cfg, sizeof(tiny_cfg)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_cfg.api_version, ==, chiaki_headless_api_version());
	munit_assert_int(
		chiaki_headless_runtime_set_recovery_profile_key("aggressive"),
		==,
		CHIAKI_ERR_SUCCESS);
	char key_buf[32] = {0};
	munit_assert_int(
		chiaki_headless_runtime_get_recovery_profile_key(key_buf, sizeof(key_buf)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_string_equal(key_buf, "aggressive");
	munit_assert_int(
		chiaki_headless_runtime_set_recovery_profile_key("default"),
		==,
		CHIAKI_ERR_SUCCESS);

	ChiakiHeadlessRuntimeRecoveryResult result = {0};
	ChiakiHeadlessRuntimeRecoveryStatus status = {0};
	status.api_version = 0;
	status.degraded_streak = UINT64_MAX;
	status.last_idr_request_monotonic_us = UINT64_MAX;
	status.runtime_session_active = true;
	munit_assert_int(
		chiaki_headless_runtime_recover_auto(0, &result),
		==,
		CHIAKI_ERR_INVALID_DATA);
	struct { uint32_t api_version; } tiny = {0};
	munit_assert_int(
		chiaki_headless_runtime_recover_auto_compat(0, &tiny, sizeof(tiny)),
		==,
		CHIAKI_ERR_INVALID_DATA);
	munit_assert_int(
		chiaki_headless_runtime_recover_auto_with_status(0, &result, &status),
		==,
		CHIAKI_ERR_INVALID_DATA);
	munit_assert_uint32(status.api_version, ==, chiaki_headless_api_version());
	munit_assert_uint64(status.degraded_streak, ==, 0);
	munit_assert_uint64(status.last_idr_request_monotonic_us, ==, 0);
	munit_assert_false(status.runtime_session_active);
	struct
	{
		uint32_t api_version;
		ChiakiHeadlessRuntimeRecoveryAction applied_action;
	} tiny_result = {0};
	struct { uint32_t api_version; bool runtime_session_active; } tiny_status = {0};
	munit_assert_int(
		chiaki_headless_runtime_recover_auto_with_status_compat(
			0,
			&tiny_result,
			sizeof(tiny_result),
			&tiny_status,
			sizeof(tiny_status)),
		==,
		CHIAKI_ERR_INVALID_DATA);
	munit_assert_uint32(tiny_status.api_version, ==, chiaki_headless_api_version());
	munit_assert_false(tiny_status.runtime_session_active);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_recovery_simulation(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	chiaki_headless_runtime_recovery_config_init(&config);
	config.profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE;

	ChiakiMediaSessionDiagnosticsSnapshot degraded = {0};
	chiaki_media_session_diagnostics_snapshot_init(&degraded);
	degraded.stats.started = true;
	degraded.stats.state = CHIAKI_MEDIA_SESSION_STATE_RUNNING;
	degraded.stats.error_event_count = 2;
	degraded.health.health_state = CHIAKI_MEDIA_HEALTH_DEGRADED;

	ChiakiMediaSessionDiagnosticsSnapshot terminal = degraded;
	terminal.health.health_state = CHIAKI_MEDIA_HEALTH_TERMINAL;

	ChiakiMediaSessionDiagnosticsSnapshot ready = degraded;
	ready.health.health_state = CHIAKI_MEDIA_HEALTH_READY;

	ChiakiHeadlessRuntimeRecoveryStatus status = {0};
	chiaki_headless_runtime_recovery_status_init(&status);
	ChiakiHeadlessRuntimeRecoveryResult result = {0};
	ChiakiHeadlessRuntimeRecoveryStatus next = {0};

	/* Case 1: degraded + threshold=1 + cooldown=0 => IDR */
	config.tuning.degraded_streak_threshold = 1;
	config.tuning.idr_cooldown_sec = 0;
	config.tuning.stop_on_terminal = true;
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery(
			&degraded,
			&config,
			&status,
			&result,
			&next),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_uint64(next.degraded_streak, ==, 1);

	/* Case 2: degraded but threshold not reached => NONE */
	config.tuning.degraded_streak_threshold = 2;
	status = (ChiakiHeadlessRuntimeRecoveryStatus){0};
	chiaki_headless_runtime_recovery_status_init(&status);
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery(
			&degraded,
			&config,
			&status,
			&result,
			&next),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);
	munit_assert_uint64(next.degraded_streak, ==, 1);

	/* Case 3: cooldown blocks second immediate IDR */
	config.tuning.degraded_streak_threshold = 1;
	config.tuning.idr_cooldown_sec = 60;
	status = (ChiakiHeadlessRuntimeRecoveryStatus){0};
	chiaki_headless_runtime_recovery_status_init(&status);
	status.degraded_streak = 1;
	status.last_idr_request_monotonic_us = (uint64_t)1 << 62;
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery(
			&degraded,
			&config,
			&status,
			&result,
			&next),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);

	/* Case 4: terminal + stop_on_terminal => STOP_RUNTIME */
	config.tuning.idr_cooldown_sec = 0;
	config.tuning.stop_on_terminal = true;
	status = (ChiakiHeadlessRuntimeRecoveryStatus){0};
	chiaki_headless_runtime_recovery_status_init(&status);
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery(
			&terminal,
			&config,
			&status,
			&result,
			&next),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_STOP_RUNTIME);

	/* Case 5: ready clears degraded streak */
	status.degraded_streak = 5;
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery(
			&ready,
			&config,
			&status,
			&result,
			&next),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint64(next.degraded_streak, ==, 0);

	struct { uint32_t api_version; } tiny_result = {0};
	struct { uint32_t api_version; } tiny_status = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_compat(
			&degraded,
			sizeof(degraded),
			&config,
			sizeof(config),
			&status,
			sizeof(status),
			&tiny_result,
			sizeof(tiny_result),
			&tiny_status,
			sizeof(tiny_status)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_result.api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(tiny_status.api_version, ==, chiaki_headless_api_version());
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_recovery_simulation_sequence(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	ChiakiHeadlessRuntimeRecoveryConfig config = {0};
	chiaki_headless_runtime_recovery_config_init(&config);
	config.profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE;
	config.tuning.degraded_streak_threshold = 1;
	config.tuning.idr_cooldown_sec = 2;

	ChiakiMediaSessionDiagnosticsSnapshot degraded = {0};
	chiaki_media_session_diagnostics_snapshot_init(&degraded);
	degraded.stats.started = true;
	degraded.stats.state = CHIAKI_MEDIA_SESSION_STATE_RUNNING;
	degraded.stats.error_event_count = 2;
	degraded.health.health_state = CHIAKI_MEDIA_HEALTH_DEGRADED;

	ChiakiHeadlessRuntimeRecoverySimulationStepInput inputs[3] = {
		{ .snapshot = &degraded, .snapshot_size = sizeof(degraded), .monotonic_now_us = 1000000ULL },
		{ .snapshot = &degraded, .snapshot_size = sizeof(degraded), .monotonic_now_us = 2000000ULL },
		{ .snapshot = &degraded, .snapshot_size = sizeof(degraded), .monotonic_now_us = 4000000ULL },
	};
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput outputs[3] = {0};
	ChiakiHeadlessRuntimeRecoveryStatus initial = {0};
	chiaki_headless_runtime_recovery_status_init(&initial);
	ChiakiHeadlessRuntimeRecoveryStatus final_status = {0};

	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence(
			inputs,
			3,
			&config,
			&initial,
			outputs,
			&final_status),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(outputs[0].result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_int(outputs[1].result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);
	munit_assert_int(outputs[2].result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_uint64(final_status.degraded_streak, ==, 3);

	ChiakiHeadlessRuntimeRecoverySimulationReport report = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence_report(
			inputs,
			3,
			&config,
			&initial,
			&report),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(report.api_version, ==, chiaki_headless_api_version());
	munit_assert_size(report.step_count, ==, 3);
	munit_assert_size(report.idr_action_count, ==, 2);
	munit_assert_size(report.none_action_count, ==, 1);
	munit_assert_size(report.stop_action_count, ==, 0);
	munit_assert_size(report.degraded_step_count, ==, 3);
	munit_assert_size(report.healthy_step_count, ==, 0);
	munit_assert_size(report.terminal_step_count, ==, 0);
	munit_assert_size(report.first_idr_step_index, ==, 0);
	munit_assert_size(report.first_stop_step_index, ==, 3);
	munit_assert_size(report.first_none_step_index, ==, 1);
	munit_assert_false(report.saw_terminal_health);

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput outputs_wr[3] = {0};
	ChiakiHeadlessRuntimeRecoveryStatus final_wr = {0};
	ChiakiHeadlessRuntimeRecoverySimulationReport report_wr = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence_with_report(
			inputs,
			3,
			&config,
			&initial,
			outputs_wr,
			&final_wr,
			&report_wr),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(outputs_wr[0].result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_int(outputs_wr[1].result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);
	munit_assert_int(outputs_wr[2].result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_size(report_wr.idr_action_count, ==, 2);
	munit_assert_size(report_wr.none_action_count, ==, 1);
	munit_assert_size(report_wr.first_none_step_index, ==, 1);

	struct { uint32_t api_version; } tiny_wr_step[3] = {0};
	struct { uint32_t api_version; } tiny_wr_final = {0};
	struct { uint32_t api_version; } tiny_wr_report = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence_with_report_compat(
			inputs,
			3,
			&config,
			sizeof(config),
			&initial,
			sizeof(initial),
			tiny_wr_step,
			sizeof(tiny_wr_step[0]),
			sizeof(tiny_wr_step[0]),
			&tiny_wr_final,
			sizeof(tiny_wr_final),
			&tiny_wr_report,
			sizeof(tiny_wr_report)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_wr_step[0].api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(tiny_wr_final.api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(tiny_wr_report.api_version, ==, chiaki_headless_api_version());

	ChiakiHeadlessRuntimeRecoverySimulationReport report_from_outputs = {0};
	munit_assert_int(
		chiaki_headless_runtime_recovery_simulation_report_from_outputs(
			outputs,
			3,
			&final_status,
			&report_from_outputs),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_size(report_from_outputs.idr_action_count, ==, report.idr_action_count);
	munit_assert_size(report_from_outputs.first_none_step_index, ==, report.first_none_step_index);

	struct { uint32_t api_version; } tiny_report_from_outputs = {0};
	munit_assert_int(
		chiaki_headless_runtime_recovery_simulation_report_from_outputs_compat(
			outputs,
			3,
			sizeof(outputs[0]),
			sizeof(outputs[0]),
			&final_status,
			sizeof(final_status),
			&tiny_report_from_outputs,
			sizeof(tiny_report_from_outputs)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_report_from_outputs.api_version, ==, chiaki_headless_api_version());

	struct { uint32_t api_version; } tiny_report = {0};
	struct { uint32_t api_version; } tiny_final = {0};
	struct { uint32_t api_version; } tiny_step[3] = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence_compat(
			inputs,
			3,
			&config,
			sizeof(config),
			&initial,
			sizeof(initial),
			tiny_step,
			sizeof(tiny_step[0]),
			sizeof(tiny_step[0]),
			&tiny_final,
			sizeof(tiny_final)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_step[0].api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(tiny_final.api_version, ==, chiaki_headless_api_version());
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence_report_compat(
			inputs,
			3,
			&config,
			sizeof(config),
			&initial,
			sizeof(initial),
			&tiny_report,
			sizeof(tiny_report)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_report.api_version, ==, chiaki_headless_api_version());

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput key_outputs[3] = {0};
	ChiakiHeadlessRuntimeRecoveryStatus key_final = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key(
			inputs,
			3,
			"aggressive",
			&config.tuning,
			&initial,
			key_outputs,
			&key_final),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(key_outputs[0].result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_int(key_outputs[1].result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);
	munit_assert_int(key_outputs[2].result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_uint64(key_final.degraded_streak, ==, 3);

	ChiakiHeadlessRuntimeRecoverySimulationReport key_report = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence_report_with_profile_key(
			inputs,
			3,
			"aggressive",
			&config.tuning,
			&initial,
			&key_report),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_size(key_report.idr_action_count, ==, 2);
	munit_assert_size(key_report.none_action_count, ==, 1);
	munit_assert_size(key_report.stop_action_count, ==, 0);
	munit_assert_size(key_report.degraded_step_count, ==, 3);
	munit_assert_size(key_report.first_idr_step_index, ==, 0);
	munit_assert_size(key_report.first_none_step_index, ==, 1);

	struct { uint32_t api_version; } tiny_key_step[3] = {0};
	struct { uint32_t api_version; } tiny_key_final = {0};
	struct { uint32_t api_version; } tiny_key_report = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key_compat(
			inputs,
			3,
			"aggressive",
			&config.tuning,
			sizeof(config.tuning),
			&initial,
			sizeof(initial),
			tiny_key_step,
			sizeof(tiny_key_step[0]),
			sizeof(tiny_key_step[0]),
			&tiny_key_final,
			sizeof(tiny_key_final)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_key_step[0].api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(tiny_key_final.api_version, ==, chiaki_headless_api_version());
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence_report_with_profile_key_compat(
			inputs,
			3,
			"aggressive",
			&config.tuning,
			sizeof(config.tuning),
			&initial,
			sizeof(initial),
			&tiny_key_report,
			sizeof(tiny_key_report)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_key_report.api_version, ==, chiaki_headless_api_version());

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput key_outputs_wr[3] = {0};
	ChiakiHeadlessRuntimeRecoveryStatus key_final_wr = {0};
	ChiakiHeadlessRuntimeRecoverySimulationReport key_report_wr = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key_with_report(
			inputs,
			3,
			"aggressive",
			&config.tuning,
			&initial,
			key_outputs_wr,
			&key_final_wr,
			&key_report_wr),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_size(key_report_wr.idr_action_count, ==, 2);
	munit_assert_size(key_report_wr.first_none_step_index, ==, 1);

	struct { uint32_t api_version; } tiny_key_wr_step[3] = {0};
	struct { uint32_t api_version; } tiny_key_wr_final = {0};
	struct { uint32_t api_version; } tiny_key_wr_report = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key_with_report_compat(
			inputs,
			3,
			"aggressive",
			&config.tuning,
			sizeof(config.tuning),
			&initial,
			sizeof(initial),
			tiny_key_wr_step,
			sizeof(tiny_key_wr_step[0]),
			sizeof(tiny_key_wr_step[0]),
			&tiny_key_wr_final,
			sizeof(tiny_key_wr_final),
			&tiny_key_wr_report,
			sizeof(tiny_key_wr_report)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_key_wr_report.api_version, ==, chiaki_headless_api_version());

	ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput health_inputs[3] = {
		{ .health_state = CHIAKI_MEDIA_HEALTH_DEGRADED, .event_count = 10, .error_event_count = 2, .ready_event_count = 0, .monotonic_now_us = 1000000ULL },
		{ .health_state = CHIAKI_MEDIA_HEALTH_DEGRADED, .event_count = 11, .error_event_count = 3, .ready_event_count = 0, .monotonic_now_us = 2000000ULL },
		{ .health_state = CHIAKI_MEDIA_HEALTH_DEGRADED, .event_count = 12, .error_event_count = 4, .ready_event_count = 0, .monotonic_now_us = 4000000ULL },
	};
	ChiakiHeadlessRuntimeRecoverySimulationStepOutput health_outputs[3] = {0};
	ChiakiHeadlessRuntimeRecoveryStatus health_final = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key(
			health_inputs,
			3,
			"aggressive",
			&config.tuning,
			&initial,
			health_outputs,
			&health_final),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(health_outputs[0].result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_int(health_outputs[1].result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);
	munit_assert_int(health_outputs[2].result.decision.action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_uint64(health_final.degraded_streak, ==, 3);

	ChiakiHeadlessRuntimeRecoverySimulationReport health_report = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_health_sequence_report_with_profile_key(
			health_inputs,
			3,
			"aggressive",
			&config.tuning,
			&initial,
			&health_report),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_size(health_report.idr_action_count, ==, 2);
	munit_assert_size(health_report.none_action_count, ==, 1);
	munit_assert_size(health_report.degraded_step_count, ==, 3);
	munit_assert_size(health_report.first_idr_step_index, ==, 0);
	munit_assert_size(health_report.first_none_step_index, ==, 1);

	struct { uint32_t api_version; } tiny_health_step[3] = {0};
	struct { uint32_t api_version; } tiny_health_final = {0};
	struct { uint32_t api_version; } tiny_health_report = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key_compat(
			health_inputs,
			3,
			"aggressive",
			&config.tuning,
			sizeof(config.tuning),
			&initial,
			sizeof(initial),
			tiny_health_step,
			sizeof(tiny_health_step[0]),
			sizeof(tiny_health_step[0]),
			&tiny_health_final,
			sizeof(tiny_health_final)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_health_step[0].api_version, ==, chiaki_headless_api_version());
	munit_assert_uint32(tiny_health_final.api_version, ==, chiaki_headless_api_version());
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_health_sequence_report_with_profile_key_compat(
			health_inputs,
			3,
			"aggressive",
			&config.tuning,
			sizeof(config.tuning),
			&initial,
			sizeof(initial),
			&tiny_health_report,
			sizeof(tiny_health_report)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_health_report.api_version, ==, chiaki_headless_api_version());

	ChiakiHeadlessRuntimeRecoverySimulationStepOutput health_outputs_wr[3] = {0};
	ChiakiHeadlessRuntimeRecoveryStatus health_final_wr = {0};
	ChiakiHeadlessRuntimeRecoverySimulationReport health_report_wr = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key_with_report(
			health_inputs,
			3,
			"aggressive",
			&config.tuning,
			&initial,
			health_outputs_wr,
			&health_final_wr,
			&health_report_wr),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_size(health_report_wr.idr_action_count, ==, 2);
	munit_assert_size(health_report_wr.first_none_step_index, ==, 1);

	struct { uint32_t api_version; } tiny_health_wr_step[3] = {0};
	struct { uint32_t api_version; } tiny_health_wr_final = {0};
	struct { uint32_t api_version; } tiny_health_wr_report = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key_with_report_compat(
			health_inputs,
			3,
			"aggressive",
			&config.tuning,
			sizeof(config.tuning),
			&initial,
			sizeof(initial),
			tiny_health_wr_step,
			sizeof(tiny_health_wr_step[0]),
			sizeof(tiny_health_wr_step[0]),
			&tiny_health_wr_final,
			sizeof(tiny_health_wr_final),
			&tiny_health_wr_report,
			sizeof(tiny_health_wr_report)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_health_wr_report.api_version, ==, chiaki_headless_api_version());

	/* Deterministic corpus + table-driven cross-path parity fixtures. */
	{
		typedef struct
		{
			const char *name;
			size_t fixture_index;
			size_t step_count;
			uint64_t degraded_streak_threshold;
			uint64_t idr_cooldown_sec;
			bool stop_on_terminal;
			bool expect_terminal_stop;
			bool expect_steady_ready;
			bool expect_burst_then_stable;
			bool expect_boundary_single_idr;
		} RecoveryParityScenario;

		typedef struct
		{
			ChiakiMediaHealthState health_states[10];
			uint64_t event_counts[10];
			uint64_t error_event_counts[10];
			uint64_t ready_event_counts[10];
			uint64_t monotonic_now_us[10];
		} RecoveryParityFixture;

		const RecoveryParityFixture fixtures[] = {
			{
				.health_states = {
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY
				},
				.event_counts = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 },
				.error_event_counts = { 2, 3, 4, 5, 5, 5, 5, 5, 5, 5 },
				.ready_event_counts = { 0, 0, 0, 0, 1, 2, 3, 4, 5, 6 },
				.monotonic_now_us = {
					1000000ULL, 2000000ULL, 3000000ULL, 4000000ULL, 5000000ULL,
					6000000ULL, 7000000ULL, 8000000ULL, 9000000ULL, 10000000ULL
				},
			},
			{
				.health_states = {
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_TERMINAL,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_TERMINAL,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY
				},
				.event_counts = { 20, 21, 22, 23, 24, 25, 26, 27, 28, 29 },
				.error_event_counts = { 1, 2, 2, 3, 3, 3, 3, 3, 3, 3 },
				.ready_event_counts = { 0, 0, 1, 1, 2, 3, 4, 5, 6, 7 },
				.monotonic_now_us = {
					1000000ULL, 2000000ULL, 3000000ULL, 4000000ULL, 5000000ULL,
					6000000ULL, 7000000ULL, 8000000ULL, 9000000ULL, 10000000ULL
				},
			},
			{
				.health_states = {
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_TERMINAL,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY
				},
				.event_counts = { 30, 31, 32, 33, 34, 35, 36, 37, 38, 39 },
				.error_event_counts = { 4, 5, 6, 7, 7, 7, 7, 7, 7, 7 },
				.ready_event_counts = { 0, 0, 0, 0, 1, 2, 3, 4, 5, 6 },
				.monotonic_now_us = {
					1000000ULL, 2000000ULL, 3000000ULL, 4000000ULL, 5000000ULL,
					6000000ULL, 7000000ULL, 8000000ULL, 9000000ULL, 10000000ULL
				},
			},
			{
				.health_states = {
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY
				},
				.event_counts = { 40, 41, 42, 43, 44, 45, 46, 47, 48, 49 },
				.error_event_counts = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
				.ready_event_counts = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 },
				.monotonic_now_us = {
					1000000ULL, 2000000ULL, 3000000ULL, 4000000ULL, 5000000ULL,
					6000000ULL, 7000000ULL, 8000000ULL, 9000000ULL, 10000000ULL
				},
			},
			{
				.health_states = {
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY
				},
				.event_counts = { 50, 51, 52, 53, 54, 55, 56, 57, 58, 59 },
				.error_event_counts = { 8, 9, 10, 11, 12, 13, 13, 13, 13, 13 },
				.ready_event_counts = { 0, 0, 0, 0, 0, 0, 1, 2, 3, 4 },
				.monotonic_now_us = {
					1000000ULL, 2000000ULL, 3000000ULL, 4000000ULL, 5000000ULL,
					7000000ULL, 9000000ULL, 11000000ULL, 13000000ULL, 15000000ULL
				},
			},
			{
				.health_states = {
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_DEGRADED,
					CHIAKI_MEDIA_HEALTH_READY,
					CHIAKI_MEDIA_HEALTH_READY
				},
				.event_counts = { 60, 61, 62, 63, 64, 65, 66, 67, 68, 69 },
				.error_event_counts = { 3, 4, 4, 5, 5, 6, 6, 7, 7, 7 },
				.ready_event_counts = { 0, 0, 1, 1, 2, 2, 3, 3, 4, 5 },
				.monotonic_now_us = {
					1000000ULL, 2000000ULL, 3000000ULL, 4000000ULL, 5000000ULL,
					6000000ULL, 7000000ULL, 8000000ULL, 9000000ULL, 10000000ULL
				},
			},
		};

		const RecoveryParityScenario scenarios[] = {
			{
				.name = "aggressive_degraded_cooldown",
				.fixture_index = 0,
				.step_count = 3,
				.degraded_streak_threshold = 1,
				.idr_cooldown_sec = 2,
				.stop_on_terminal = true,
				.expect_terminal_stop = false,
				.expect_steady_ready = false,
				.expect_burst_then_stable = false,
				.expect_boundary_single_idr = false,
			},
			{
				.name = "terminal_stop_path",
				.fixture_index = 1,
				.step_count = 3,
				.degraded_streak_threshold = 1,
				.idr_cooldown_sec = 0,
				.stop_on_terminal = true,
				.expect_terminal_stop = true,
				.expect_steady_ready = false,
				.expect_burst_then_stable = false,
				.expect_boundary_single_idr = false,
			},
			{
				.name = "steady_ready_path",
				.fixture_index = 3,
				.step_count = 3,
				.degraded_streak_threshold = 1,
				.idr_cooldown_sec = 0,
				.stop_on_terminal = true,
				.expect_terminal_stop = false,
				.expect_steady_ready = true,
				.expect_burst_then_stable = false,
				.expect_boundary_single_idr = false,
			},
			{
				.name = "cooldown_boundary_behavior",
				.fixture_index = 4,
				.step_count = 7,
				.degraded_streak_threshold = 1,
				.idr_cooldown_sec = 2,
				.stop_on_terminal = true,
				.expect_terminal_stop = false,
				.expect_steady_ready = false,
				.expect_burst_then_stable = false,
				.expect_boundary_single_idr = true,
			},
			{
				.name = "terminal_flap_recovery_transition",
				.fixture_index = 2,
				.step_count = 8,
				.degraded_streak_threshold = 2,
				.idr_cooldown_sec = 1,
				.stop_on_terminal = false,
				.expect_terminal_stop = false,
				.expect_steady_ready = false,
				.expect_burst_then_stable = false,
				.expect_boundary_single_idr = false,
			},
			{
				.name = "burst_errors_then_ready_stabilization",
				.fixture_index = 0,
				.step_count = 10,
				.degraded_streak_threshold = 1,
				.idr_cooldown_sec = 1,
				.stop_on_terminal = true,
				.expect_terminal_stop = false,
				.expect_steady_ready = false,
				.expect_burst_then_stable = true,
				.expect_boundary_single_idr = false,
			},
			{
				.name = "no_op_steady_ready_long_run",
				.fixture_index = 3,
				.step_count = 10,
				.degraded_streak_threshold = 1,
				.idr_cooldown_sec = 2,
				.stop_on_terminal = true,
				.expect_terminal_stop = false,
				.expect_steady_ready = true,
				.expect_burst_then_stable = false,
				.expect_boundary_single_idr = false,
			},
		};

		ChiakiHeadlessRuntimeRecoverySimulationReport all_pass_reports[sizeof(scenarios) / sizeof(scenarios[0])] = {0};
		ChiakiHeadlessRuntimeRecoveryParityFixtureExpected all_pass_expected[sizeof(scenarios) / sizeof(scenarios[0])] = {0};
		size_t all_pass_count = 0;
		size_t si = 0;
		for(si = 0; si < (sizeof(scenarios) / sizeof(scenarios[0])); ++si)
		{
			const RecoveryParityScenario *sc = &scenarios[si];
			const RecoveryParityFixture *fx = &fixtures[sc->fixture_index];
			ChiakiHeadlessRuntimeRecoveryConfig parity_config = {0};
			ChiakiHeadlessRuntimeRecoverySimulationStepInput parity_inputs[10] = {0};
			ChiakiHeadlessRuntimeRecoverySimulationHealthStepInput parity_health_inputs[10] = {0};
			ChiakiMediaSessionDiagnosticsSnapshot parity_snapshots[10] = {0};
			ChiakiHeadlessRuntimeRecoverySimulationStepOutput cfg_outputs[10] = {0};
			ChiakiHeadlessRuntimeRecoverySimulationStepOutput key_outputs2[10] = {0};
			ChiakiHeadlessRuntimeRecoverySimulationStepOutput health_outputs2[10] = {0};
			ChiakiHeadlessRuntimeRecoveryStatus cfg_final = {0};
			ChiakiHeadlessRuntimeRecoveryStatus key_final2 = {0};
			ChiakiHeadlessRuntimeRecoveryStatus health_final2 = {0};
			ChiakiHeadlessRuntimeRecoverySimulationReport cfg_report = {0};
			ChiakiHeadlessRuntimeRecoverySimulationReport key_report2 = {0};
			ChiakiHeadlessRuntimeRecoverySimulationReport health_report2 = {0};
			ChiakiHeadlessRuntimeRecoveryStatus parity_initial = {0};
			size_t i = 0;

			chiaki_headless_runtime_recovery_config_init(&parity_config);
			parity_config.profile = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE;
			parity_config.tuning.degraded_streak_threshold = sc->degraded_streak_threshold;
			parity_config.tuning.idr_cooldown_sec = sc->idr_cooldown_sec;
			parity_config.tuning.stop_on_terminal = sc->stop_on_terminal;
			chiaki_headless_runtime_recovery_status_init(&parity_initial);

			for(i = 0; i < sc->step_count; ++i)
			{
				chiaki_media_session_diagnostics_snapshot_init(&parity_snapshots[i]);
				parity_snapshots[i].stats.started = true;
				parity_snapshots[i].stats.state = CHIAKI_MEDIA_SESSION_STATE_RUNNING;
				parity_snapshots[i].stats.event_count = fx->event_counts[i];
				parity_snapshots[i].stats.error_event_count = fx->error_event_counts[i];
				parity_snapshots[i].stats.ready_event_count = fx->ready_event_counts[i];
				parity_snapshots[i].health.health_state = fx->health_states[i];

				parity_inputs[i].snapshot = &parity_snapshots[i];
				parity_inputs[i].snapshot_size = sizeof(parity_snapshots[i]);
				parity_inputs[i].monotonic_now_us = fx->monotonic_now_us[i];

				parity_health_inputs[i].health_state = fx->health_states[i];
				parity_health_inputs[i].event_count = fx->event_counts[i];
				parity_health_inputs[i].error_event_count = fx->error_event_counts[i];
				parity_health_inputs[i].ready_event_count = fx->ready_event_counts[i];
				parity_health_inputs[i].monotonic_now_us = fx->monotonic_now_us[i];
			}

			munit_assert_int(
				chiaki_headless_runtime_simulate_recovery_sequence_with_report(
					parity_inputs,
					sc->step_count,
					&parity_config,
					&parity_initial,
					cfg_outputs,
					&cfg_final,
					&cfg_report),
				==,
				CHIAKI_ERR_SUCCESS);
			munit_assert_int(
				chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key_with_report(
					parity_inputs,
					sc->step_count,
					"aggressive",
					&parity_config.tuning,
					&parity_initial,
					key_outputs2,
					&key_final2,
					&key_report2),
				==,
				CHIAKI_ERR_SUCCESS);
			munit_assert_int(
				chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key_with_report(
					parity_health_inputs,
					sc->step_count,
					"aggressive",
					&parity_config.tuning,
					&parity_initial,
					health_outputs2,
					&health_final2,
					&health_report2),
				==,
				CHIAKI_ERR_SUCCESS);

			munit_assert_size(cfg_report.idr_action_count, ==, key_report2.idr_action_count);
			munit_assert_size(cfg_report.idr_action_count, ==, health_report2.idr_action_count);
			munit_assert_size(cfg_report.none_action_count, ==, key_report2.none_action_count);
			munit_assert_size(cfg_report.none_action_count, ==, health_report2.none_action_count);
			munit_assert_size(cfg_report.stop_action_count, ==, key_report2.stop_action_count);
			munit_assert_size(cfg_report.stop_action_count, ==, health_report2.stop_action_count);
			munit_assert_size(cfg_report.first_idr_step_index, ==, key_report2.first_idr_step_index);
			munit_assert_size(cfg_report.first_idr_step_index, ==, health_report2.first_idr_step_index);
			munit_assert_size(cfg_report.first_none_step_index, ==, key_report2.first_none_step_index);
			munit_assert_size(cfg_report.first_none_step_index, ==, health_report2.first_none_step_index);
			munit_assert_size(cfg_report.terminal_step_count, ==, key_report2.terminal_step_count);
			munit_assert_size(cfg_report.terminal_step_count, ==, health_report2.terminal_step_count);
			munit_assert_size(cfg_report.degraded_step_count, ==, key_report2.degraded_step_count);
			munit_assert_size(cfg_report.degraded_step_count, ==, health_report2.degraded_step_count);
			munit_assert_size(cfg_report.healthy_step_count, ==, key_report2.healthy_step_count);
			munit_assert_size(cfg_report.healthy_step_count, ==, health_report2.healthy_step_count);
			{
				ChiakiHeadlessRuntimeRecoveryParityFixtureExpected exported_expected = {0};
				ChiakiHeadlessRuntimeRecoverySimulationReport exported_report = {0};
				struct { uint32_t api_version; } tiny_export_expected = {0};
				struct { uint32_t api_version; } tiny_export_report = {0};

				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_fixture_export(
						sc->name,
						cfg_outputs,
						sc->step_count,
						&cfg_final,
						true,
						&exported_expected,
						&exported_report),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_size(exported_report.idr_action_count, ==, cfg_report.idr_action_count);
				munit_assert_size(exported_report.none_action_count, ==, cfg_report.none_action_count);
				munit_assert_size(
					exported_expected.expected_first_none_step_index,
					==,
					cfg_report.first_none_step_index);

				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_fixture_export_compat(
						sc->name,
						cfg_outputs,
						sc->step_count,
						sizeof(cfg_outputs[0]),
						sizeof(cfg_outputs[0]),
						&cfg_final,
						sizeof(cfg_final),
						true,
						&tiny_export_expected,
						sizeof(tiny_export_expected),
						&tiny_export_report,
						sizeof(tiny_export_report)),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_uint32(tiny_export_expected.api_version, ==, chiaki_headless_api_version());
				munit_assert_uint32(tiny_export_report.api_version, ==, chiaki_headless_api_version());

				all_pass_reports[all_pass_count] = exported_report;
				all_pass_expected[all_pass_count] = exported_expected;
			}

			if(sc->expect_terminal_stop)
			{
				munit_assert_size(cfg_report.stop_action_count, >=, 1);
				munit_assert_size(cfg_report.terminal_step_count, >=, 1);
			}
			if(sc->expect_boundary_single_idr)
			{
				munit_assert_size(cfg_report.idr_action_count, >=, 1);
				munit_assert_size(cfg_report.idr_action_count, <, cfg_report.degraded_step_count);
			}
			if(sc->expect_burst_then_stable)
			{
				munit_assert_size(cfg_report.idr_action_count, >=, 1);
				munit_assert_size(cfg_report.healthy_step_count, >=, 4);
				munit_assert_size(cfg_report.degraded_step_count, >=, 3);
			}
			if(sc->expect_steady_ready)
			{
				munit_assert_size(cfg_report.idr_action_count, ==, 0);
				munit_assert_size(cfg_report.stop_action_count, ==, 0);
				munit_assert_size(cfg_report.none_action_count, ==, sc->step_count);
				munit_assert_size(cfg_report.healthy_step_count, ==, sc->step_count);
			}

			++all_pass_count;
		}

		/* Smoke API/compat parity fixture eval coverage. */
		{
			ChiakiHeadlessRuntimeRecoveryParityFixtureResult all_pass_result = {0};
			ChiakiHeadlessRuntimeRecoveryParityFixtureExpected mixed_expected[sizeof(scenarios) / sizeof(scenarios[0])] = {0};
			ChiakiHeadlessRuntimeRecoveryParityFixtureResult mixed_result = {0};
			size_t i = 0;
			for(i = 0; i < all_pass_count; ++i)
				mixed_expected[i] = all_pass_expected[i];
			if(all_pass_count > 1)
				mixed_expected[1].expected_idr_action_count = mixed_expected[1].expected_idr_action_count + 1U;

			munit_assert_int(
				chiaki_headless_runtime_recovery_parity_fixture_eval(
					all_pass_expected,
					all_pass_reports,
					all_pass_count,
					&all_pass_result),
				==,
				CHIAKI_ERR_SUCCESS);
			munit_assert_size(all_pass_result.pass_count, ==, all_pass_count);
			munit_assert_size(all_pass_result.fail_count, ==, 0);

			munit_assert_int(
				chiaki_headless_runtime_recovery_parity_fixture_eval(
					mixed_expected,
					all_pass_reports,
					all_pass_count,
					&mixed_result),
				==,
				CHIAKI_ERR_SUCCESS);
			munit_assert_size(mixed_result.pass_count, ==, all_pass_count - 1);
			munit_assert_size(mixed_result.fail_count, ==, 1);

			{
				ChiakiHeadlessRuntimeRecoveryParitySmokeResult smoke_all_pass = {0};
				ChiakiHeadlessRuntimeRecoveryParitySmokeResult smoke_mixed = {0};
				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_smoke(
						all_pass_expected,
						all_pass_reports,
						all_pass_count,
						&smoke_all_pass),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_size(smoke_all_pass.fail_count, ==, 0);
				munit_assert_size(smoke_all_pass.first_failure_index, ==, SIZE_MAX);

				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_smoke(
						mixed_expected,
						all_pass_reports,
						all_pass_count,
						&smoke_mixed),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_size(smoke_mixed.fail_count, ==, 1);
				munit_assert_size(smoke_mixed.first_failure_index, ==, 1);
			}

			{
				struct
				{
					uint32_t api_version;
					size_t scenario_count;
				} tiny_out = {0};
				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_smoke_compat(
						all_pass_expected,
						all_pass_count,
						sizeof(ChiakiHeadlessRuntimeRecoveryParityFixtureExpected),
						sizeof(ChiakiHeadlessRuntimeRecoveryParityFixtureExpected),
						all_pass_reports,
						sizeof(ChiakiHeadlessRuntimeRecoverySimulationReport),
						sizeof(ChiakiHeadlessRuntimeRecoverySimulationReport),
						&tiny_out,
						sizeof(tiny_out)),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_uint32(tiny_out.api_version, ==, chiaki_headless_api_version());
			}

			{
				ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult runner_all_pass = {0};
				ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult runner_mixed = {0};
				struct
				{
					uint32_t api_version;
					ChiakiHeadlessRuntimeRecoveryParityFixtureResult fixture_result;
				} tiny_runner = {0};

				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_smoke_runner(
						all_pass_expected,
						all_pass_reports,
						all_pass_count,
						&runner_all_pass),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_size(runner_all_pass.fixture_result.pass_count, ==, all_pass_count);
				munit_assert_size(runner_all_pass.fixture_result.fail_count, ==, 0);
				munit_assert_size(runner_all_pass.smoke_result.fail_count, ==, 0);
				munit_assert_size(runner_all_pass.smoke_result.first_failure_index, ==, SIZE_MAX);

				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_smoke_runner(
						mixed_expected,
						all_pass_reports,
						all_pass_count,
						&runner_mixed),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_size(runner_mixed.fixture_result.fail_count, ==, 1);
				munit_assert_size(runner_mixed.smoke_result.fail_count, ==, 1);
				munit_assert_size(runner_mixed.smoke_result.first_failure_index, ==, 1);

				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_smoke_runner_compat(
						all_pass_expected,
						all_pass_count,
						sizeof(ChiakiHeadlessRuntimeRecoveryParityFixtureExpected),
						sizeof(ChiakiHeadlessRuntimeRecoveryParityFixtureExpected),
						all_pass_reports,
						sizeof(ChiakiHeadlessRuntimeRecoverySimulationReport),
						sizeof(ChiakiHeadlessRuntimeRecoverySimulationReport),
						&tiny_runner,
						sizeof(tiny_runner)),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_uint32(tiny_runner.api_version, ==, chiaki_headless_api_version());
				munit_assert_size(tiny_runner.fixture_result.pass_count, ==, all_pass_count);
			}

			{
				ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult baseline = {0};
				struct
				{
					uint32_t api_version;
					ChiakiHeadlessRuntimeRecoveryParityFixtureResult fixture_result;
				} tiny_baseline = {0};

				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_smoke(&baseline),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_true(baseline.smoke_result.scenario_count >= 1);
				munit_assert_size(
					baseline.smoke_result.pass_count + baseline.smoke_result.fail_count,
					==,
					baseline.smoke_result.scenario_count);
				munit_assert_size(
					baseline.fixture_result.pass_count + baseline.fixture_result.fail_count,
					==,
					baseline.fixture_result.scenario_count);

				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_smoke_compat(
						&tiny_baseline,
						sizeof(tiny_baseline)),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_uint32(tiny_baseline.api_version, ==, chiaki_headless_api_version());
			}

			/* Baseline suite introspection (count + label + compat + bounds/tiny buffer). */
			{
				size_t baseline_count = 0;
				const char *baseline_label = NULL;
				char label_buf[128] = {0};
				char tiny_label_buf[2] = {0};
				unsigned char tiny_count_buf[1] = {0};

				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_scenario_count(NULL),
					==,
					CHIAKI_ERR_INVALID_DATA);
				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_scenario_count(&baseline_count),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_size(baseline_count, >=, 1);

				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_scenario_count_compat(
						NULL,
						sizeof(size_t)),
					==,
					CHIAKI_ERR_INVALID_DATA);
				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_scenario_count_compat(
						tiny_count_buf,
						sizeof(tiny_count_buf)),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_uint8(tiny_count_buf[0], !=, 0);

				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_scenario_label(
						0,
						NULL),
					==,
					CHIAKI_ERR_INVALID_DATA);
				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_scenario_label(
						baseline_count,
						&baseline_label),
					==,
					CHIAKI_ERR_INVALID_DATA);
				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_scenario_label(
						0,
						&baseline_label),
					==,
					CHIAKI_ERR_SUCCESS);
				munit_assert_not_null(baseline_label);
				munit_assert_size(strlen(baseline_label), >, 0);

				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_scenario_label_compat(
						0,
						NULL,
						sizeof(label_buf)),
					==,
					CHIAKI_ERR_INVALID_DATA);
				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_scenario_label_compat(
						0,
						label_buf,
						0),
					==,
					CHIAKI_ERR_INVALID_DATA);
				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_scenario_label_compat(
						baseline_count,
						label_buf,
						sizeof(label_buf)),
					==,
					CHIAKI_ERR_INVALID_DATA);
				munit_assert_int(
					chiaki_headless_runtime_recovery_parity_baseline_scenario_label_compat(
						0,
						tiny_label_buf,
						sizeof(tiny_label_buf)),
					!=,
					CHIAKI_ERR_SUCCESS);
					munit_assert_int(
						chiaki_headless_runtime_recovery_parity_baseline_scenario_label_compat(
							0,
							label_buf,
							sizeof(label_buf)),
						==,
						CHIAKI_ERR_SUCCESS);
					munit_assert_string_equal(label_buf, baseline_label);

					{
						ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail details[8] = {0};
						ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult aggregate = {0};
						struct { uint32_t api_version; size_t scenario_index; } tiny_detail = {0};
						struct { uint32_t api_version; } tiny_aggregate = {0};

						munit_assert_int(
							chiaki_headless_runtime_recovery_parity_baseline_execution_details(
								NULL,
								baseline_count,
								&aggregate),
							==,
							CHIAKI_ERR_INVALID_DATA);
						munit_assert_int(
							chiaki_headless_runtime_recovery_parity_baseline_execution_details(
								details,
								0,
								&aggregate),
							==,
							CHIAKI_ERR_INVALID_DATA);
						munit_assert_int(
							chiaki_headless_runtime_recovery_parity_baseline_execution_details(
								details,
								1,
								&aggregate),
							==,
							CHIAKI_ERR_BUF_TOO_SMALL);
						munit_assert_int(
							chiaki_headless_runtime_recovery_parity_baseline_execution_details(
								details,
								sizeof(details) / sizeof(details[0]),
								&aggregate),
							==,
							CHIAKI_ERR_SUCCESS);
						munit_assert_size(aggregate.smoke_result.scenario_count, ==, baseline_count);
						munit_assert_uint32(details[0].api_version, ==, chiaki_headless_api_version());
						munit_assert_size(details[0].scenario_index, ==, 0);
						munit_assert_size(strlen(details[0].scenario_label), >, 0);
						munit_assert_size(
							aggregate.smoke_result.pass_count + aggregate.smoke_result.fail_count,
							==,
							baseline_count);
						{
							size_t i = 0;
							for(i = 0; i < baseline_count; ++i)
								munit_assert_false(details[i].mismatch_idr_action_count
									|| details[i].mismatch_stop_action_count
									|| details[i].mismatch_none_action_count
									|| details[i].mismatch_first_none_step_index);
						}

						munit_assert_int(
							chiaki_headless_runtime_recovery_parity_baseline_execution_details_compat(
								NULL,
								baseline_count,
								sizeof(ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail),
								sizeof(ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail),
								&tiny_aggregate,
								sizeof(tiny_aggregate)),
							==,
							CHIAKI_ERR_INVALID_DATA);
						munit_assert_int(
							chiaki_headless_runtime_recovery_parity_baseline_execution_details_compat(
								&tiny_detail,
								baseline_count,
								sizeof(tiny_detail),
								sizeof(tiny_detail),
								NULL,
								sizeof(tiny_aggregate)),
							==,
							CHIAKI_ERR_INVALID_DATA);
						munit_assert_int(
							chiaki_headless_runtime_recovery_parity_baseline_execution_details_compat(
								&tiny_detail,
								1,
								sizeof(tiny_detail),
								sizeof(tiny_detail),
								&tiny_aggregate,
								sizeof(tiny_aggregate)),
							==,
							CHIAKI_ERR_BUF_TOO_SMALL);
						munit_assert_int(
							chiaki_headless_runtime_recovery_parity_baseline_execution_details_compat(
								&tiny_detail,
								baseline_count,
								sizeof(tiny_detail),
								sizeof(tiny_detail),
								&tiny_aggregate,
								sizeof(tiny_aggregate)),
							==,
							CHIAKI_ERR_SUCCESS);
						munit_assert_uint32(tiny_detail.api_version, ==, chiaki_headless_api_version());
						munit_assert_uint32(tiny_aggregate.api_version, ==, chiaki_headless_api_version());
					}
				}
		}
	}

	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineStep timeline_steps[3] = {0};
	ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineSummary timeline_summary = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_auto_loop_timeline(
			inputs,
			3,
			&config,
			&initial,
			timeline_steps,
			&timeline_summary),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(timeline_steps[0].decision_action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_int(timeline_steps[0].applied_action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_uint64(timeline_steps[0].degraded_streak, ==, 1);
	munit_assert_uint64(timeline_steps[0].last_idr_request_monotonic_us, ==, 1000000ULL);
	munit_assert_int(timeline_steps[0].health_state, ==, CHIAKI_MEDIA_HEALTH_DEGRADED);
	munit_assert_int(timeline_steps[1].decision_action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_NONE);
	munit_assert_uint64(timeline_steps[1].degraded_streak, ==, 2);
	munit_assert_uint64(timeline_steps[1].last_idr_request_monotonic_us, ==, 1000000ULL);
	munit_assert_int(timeline_steps[2].decision_action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);
	munit_assert_uint64(timeline_steps[2].degraded_streak, ==, 3);
	munit_assert_uint64(timeline_steps[2].last_idr_request_monotonic_us, ==, 4000000ULL);
	munit_assert_uint32(timeline_summary.api_version, ==, chiaki_headless_api_version());
	munit_assert_size(timeline_summary.step_count, ==, 3);
	munit_assert_size(timeline_summary.idr_action_count, ==, 2);
	munit_assert_size(timeline_summary.none_action_count, ==, 1);
	munit_assert_size(timeline_summary.stop_action_count, ==, 0);
	munit_assert_size(timeline_summary.degraded_step_count, ==, 3);
	munit_assert_size(timeline_summary.healthy_step_count, ==, 0);
	munit_assert_size(timeline_summary.terminal_step_count, ==, 0);
	munit_assert_size(timeline_summary.first_none_step_index, ==, 1);
	munit_assert_uint64(timeline_summary.final_status.degraded_streak, ==, 3);

	struct { uint32_t api_version; uint64_t degraded_streak; } tiny_timeline_steps[3] = {0};
	struct { uint32_t api_version; size_t step_count; } tiny_timeline_summary = {0};
	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_auto_loop_timeline_compat(
			inputs,
			3,
			&config,
			sizeof(config),
			&initial,
			sizeof(initial),
			tiny_timeline_steps,
			sizeof(tiny_timeline_steps[0]),
			sizeof(tiny_timeline_steps[0]),
			&tiny_timeline_summary,
			sizeof(tiny_timeline_summary)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny_timeline_steps[0].api_version, ==, chiaki_headless_api_version());
	munit_assert_uint64(tiny_timeline_steps[0].degraded_streak, ==, 1);
	munit_assert_uint32(tiny_timeline_summary.api_version, ==, chiaki_headless_api_version());
	munit_assert_size(tiny_timeline_summary.step_count, ==, 3);

	{
		size_t baseline_count = 0;
		munit_assert_int(
			chiaki_headless_runtime_recovery_parity_baseline_scenario_count(&baseline_count),
			==,
			CHIAKI_ERR_SUCCESS);
		ChiakiHeadlessRuntimeRecoveryParityBaselineExecutionDetail core_details[8] = {0};
		ChiakiHeadlessRuntimeRecoveryAutoLoopTimelineStep core_timeline_steps[3] = {0};
		ChiakiHeadlessRuntimeRecoveryCoreDiagnosticsSummary core_summary = {0};
		munit_assert_int(
			chiaki_headless_runtime_recovery_core_diagnostics(
				NULL,
				3,
				&config,
				&initial,
				core_details,
				sizeof(core_details) / sizeof(core_details[0]),
				core_timeline_steps,
				&core_summary),
			==,
			CHIAKI_ERR_INVALID_DATA);
		munit_assert_int(
			chiaki_headless_runtime_recovery_core_diagnostics(
				inputs,
				3,
				&config,
				&initial,
				core_details,
				1,
				core_timeline_steps,
				&core_summary),
			==,
			CHIAKI_ERR_BUF_TOO_SMALL);
		munit_assert_int(
			chiaki_headless_runtime_recovery_core_diagnostics(
				inputs,
				3,
				&config,
				&initial,
				core_details,
				sizeof(core_details) / sizeof(core_details[0]),
				core_timeline_steps,
				&core_summary),
			==,
			CHIAKI_ERR_SUCCESS);
		munit_assert_uint32(core_summary.api_version, ==, chiaki_headless_api_version());
		munit_assert_size(core_summary.baseline_scenario_count, ==, baseline_count);
		munit_assert_size(core_summary.auto_loop_timeline_summary.step_count, ==, 3);
		munit_assert_size(core_summary.auto_loop_timeline_summary.idr_action_count, ==, 2);
		munit_assert_size(core_summary.baseline_aggregate_result.smoke_result.scenario_count, ==, baseline_count);
		munit_assert_false(core_summary.e3_master_gate_enabled);
		munit_assert_false(core_summary.e3_audio_gate_enabled);
		munit_assert_false(core_summary.e3_sync_gate_enabled);
		munit_assert_false(core_summary.e3_audio_effective);
		munit_assert_false(core_summary.e3_sync_effective);
		munit_assert_false(core_details[0].mismatch_idr_action_count
			|| core_details[0].mismatch_stop_action_count
			|| core_details[0].mismatch_none_action_count
			|| core_details[0].mismatch_first_none_step_index);
		munit_assert_int(core_timeline_steps[0].decision_action, ==, CHIAKI_HEADLESS_RUNTIME_RECOVERY_ACTION_REQUEST_IDR);

		struct { uint32_t api_version; size_t baseline_scenario_count; } tiny_core_summary = {0};
		struct { uint32_t api_version; size_t scenario_index; bool pass; } tiny_core_details[8] = {0};
		struct { uint32_t api_version; uint64_t degraded_streak; } tiny_core_timeline[3] = {0};
		munit_assert_int(
			chiaki_headless_runtime_recovery_core_diagnostics_compat(
				NULL,
				3,
				&config,
				sizeof(config),
				&initial,
				sizeof(initial),
				tiny_core_details,
				baseline_count,
				sizeof(tiny_core_details[0]),
				sizeof(tiny_core_details[0]),
				tiny_core_timeline,
				sizeof(tiny_core_timeline[0]),
				sizeof(tiny_core_timeline[0]),
				&tiny_core_summary,
				sizeof(tiny_core_summary)),
			==,
			CHIAKI_ERR_INVALID_DATA);
		munit_assert_int(
			chiaki_headless_runtime_recovery_core_diagnostics_compat(
				inputs,
				3,
				&config,
				sizeof(config),
				&initial,
				sizeof(initial),
				tiny_core_details,
				1,
				sizeof(tiny_core_details[0]),
				sizeof(tiny_core_details[0]),
				tiny_core_timeline,
				sizeof(tiny_core_timeline[0]),
				sizeof(tiny_core_timeline[0]),
				&tiny_core_summary,
				sizeof(tiny_core_summary)),
			==,
			CHIAKI_ERR_BUF_TOO_SMALL);
		munit_assert_int(
			chiaki_headless_runtime_recovery_core_diagnostics_compat(
				inputs,
				3,
				&config,
				sizeof(config),
				&initial,
				sizeof(initial),
				tiny_core_details,
				baseline_count,
				sizeof(tiny_core_details[0]),
				sizeof(tiny_core_details[0]),
				tiny_core_timeline,
				sizeof(tiny_core_timeline[0]),
				sizeof(tiny_core_timeline[0]),
				&tiny_core_summary,
				sizeof(tiny_core_summary)),
			==,
			CHIAKI_ERR_SUCCESS);
		munit_assert_uint32(tiny_core_summary.api_version, ==, chiaki_headless_api_version());
		munit_assert_size(tiny_core_summary.baseline_scenario_count, ==, baseline_count);
		munit_assert_uint32(tiny_core_details[0].api_version, ==, chiaki_headless_api_version());
		munit_assert_uint32(tiny_core_timeline[0].api_version, ==, chiaki_headless_api_version());
		munit_assert_uint64(tiny_core_timeline[0].degraded_streak, ==, 1);

		setenv("DECKSTATION_MEDIA_E3_ENABLE", "1", 1);
		setenv("DECKSTATION_MEDIA_E3_AUDIO_ENABLE", "1", 1);
		unsetenv("DECKSTATION_MEDIA_E3_SYNC_ENABLE");
		munit_assert_int(
			chiaki_headless_runtime_recovery_core_diagnostics(
				inputs,
				3,
				&config,
				&initial,
				core_details,
				sizeof(core_details) / sizeof(core_details[0]),
				core_timeline_steps,
				&core_summary),
			==,
			CHIAKI_ERR_SUCCESS);
		munit_assert_true(core_summary.e3_master_gate_enabled);
		munit_assert_true(core_summary.e3_audio_gate_enabled);
		munit_assert_false(core_summary.e3_sync_gate_enabled);
		munit_assert_true(core_summary.e3_audio_effective);
		munit_assert_false(core_summary.e3_sync_effective);
		unsetenv("DECKSTATION_MEDIA_E3_ENABLE");
		unsetenv("DECKSTATION_MEDIA_E3_AUDIO_ENABLE");
		unsetenv("DECKSTATION_MEDIA_E3_SYNC_ENABLE");
	}

	munit_assert_int(
		chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key(
			inputs,
			3,
			"not-a-real-profile",
			&config.tuning,
			&initial,
			key_outputs,
			&key_final),
		==,
		CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_media_capabilities(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	unsetenv("CHIAKI_MEDIA_E2_ENABLE");
	size_t cap_size = chiaki_media_capabilities_size();
	munit_assert_size(cap_size, ==, sizeof(ChiakiMediaCapabilities));

	ChiakiMediaCapabilities caps = {0};
	chiaki_media_capabilities_init(&caps);
	munit_assert_uint32(caps.api_version, ==, chiaki_headless_api_version());

	munit_assert_int(chiaki_media_capabilities(&caps), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(caps.media_capabilities_version, ==, 16);
	munit_assert_uint32(caps.media_event_schema_version, ==, 1);
	munit_assert_size(caps.min_session_stats_size, ==, sizeof(ChiakiMediaSessionStats));
	munit_assert_size(caps.min_readiness_report_size, ==, sizeof(ChiakiMediaReadinessReport));
	munit_assert_size(caps.min_health_report_size, ==, sizeof(ChiakiMediaHealthReport));
	munit_assert_size(caps.min_health_policy_size, ==, sizeof(ChiakiMediaHealthPolicy));
	munit_assert_size(caps.min_profile_info_size, ==, sizeof(ChiakiMediaHealthPolicyProfileInfo));
	munit_assert_size(caps.min_profile_resolution_size, ==, sizeof(ChiakiMediaHealthPolicyProfileResolution));
	munit_assert_size(caps.min_diagnostics_snapshot_size, ==, sizeof(ChiakiMediaSessionDiagnosticsSnapshot));
	munit_assert_true(caps.supports_capabilities_query);
	munit_assert_true(caps.supports_session_create_destroy);
	munit_assert_true(caps.supports_capabilities_compat);
	munit_assert_true(caps.supports_session_stats);
	munit_assert_true(caps.supports_session_stats_compat);
	munit_assert_true(caps.supports_session_readiness);
	munit_assert_true(caps.supports_session_readiness_compat);
	munit_assert_true(caps.supports_session_health_report);
	munit_assert_true(caps.supports_session_health_report_compat);
	munit_assert_true(caps.supports_session_health_report_with_policy);
	munit_assert_true(caps.supports_session_health_report_with_policy_compat);
	munit_assert_true(caps.supports_health_policy_defaults_query);
	munit_assert_true(caps.supports_health_policy_defaults_compat);
	munit_assert_true(caps.supports_health_policy_profiles);
	munit_assert_true(caps.supports_health_policy_profiles_compat);
	munit_assert_true(caps.supports_session_health_report_with_profile);
	munit_assert_true(caps.supports_session_health_report_with_profile_compat);
	munit_assert_true(caps.supports_health_policy_profile_catalog);
	munit_assert_true(caps.supports_health_policy_profile_catalog_compat);
	munit_assert_true(caps.supports_health_policy_profile_metadata);
	munit_assert_true(caps.supports_health_policy_profile_lookup_by_key);
	munit_assert_true(caps.supports_health_policy_profile_lookup_by_key_compat);
	munit_assert_true(caps.supports_health_policy_profile_key_from_profile);
	munit_assert_true(caps.supports_health_policy_profile_key_normalization);
	munit_assert_true(caps.supports_health_policy_profile_key_normalization_compat);
	munit_assert_true(caps.supports_health_policy_profile_resolution);
	munit_assert_true(caps.supports_health_policy_profile_resolution_compat);
	munit_assert_true(caps.supports_session_health_report_with_profile_key);
	munit_assert_true(caps.supports_session_health_report_with_profile_key_compat);
	munit_assert_true(caps.supports_session_diagnostics_snapshot);
	munit_assert_true(caps.supports_session_diagnostics_snapshot_compat);
	munit_assert_true(caps.supports_session_diagnostics_snapshot_with_policy);
	munit_assert_true(caps.supports_session_diagnostics_snapshot_with_policy_compat);
	munit_assert_true(caps.supports_session_diagnostics_snapshot_with_profile);
	munit_assert_true(caps.supports_session_diagnostics_snapshot_with_profile_compat);
	munit_assert_true(caps.supports_session_diagnostics_snapshot_with_profile_key);
	munit_assert_true(caps.supports_session_diagnostics_snapshot_with_profile_key_compat);
	munit_assert_false(caps.supports_texture_create);
	munit_assert_true(caps.supports_start_cloud_strings);
	munit_assert_true(caps.supports_stop);

	/* Gate state should not change capability contract shape. */
	setenv("CHIAKI_MEDIA_E2_ENABLE", "1", 1);
	munit_assert_int(chiaki_media_capabilities(&caps), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_true(caps.supports_start_cloud_strings);
	munit_assert_true(caps.supports_stop);
	unsetenv("CHIAKI_MEDIA_E2_ENABLE");

	struct { uint32_t api_version; } tiny = {0};
	munit_assert_int(
		chiaki_media_capabilities_compat(&tiny, sizeof(tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(tiny.api_version, ==, chiaki_headless_api_version());
	return MUNIT_OK;
}

static MunitResult test_headless_media_session_scaffold(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	unsetenv("CHIAKI_MEDIA_E2_ENABLE");

	munit_assert_int(chiaki_media_session_create(NULL, NULL), ==, CHIAKI_ERR_INVALID_DATA);

	ChiakiMediaCreateInfo create_info = {0};
	ChiakiMediaSession *session = NULL;
	munit_assert_int(chiaki_media_session_create(&session, &create_info), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_not_null(session);
	ChiakiMediaSessionStats stats = {0};
	munit_assert_size(chiaki_media_session_stats_size(), ==, sizeof(ChiakiMediaSessionStats));
	munit_assert_int(chiaki_media_session_get_stats(session, &stats), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_false(stats.started);
	munit_assert_int(stats.state, ==, CHIAKI_MEDIA_SESSION_STATE_CREATED);
	munit_assert_false(stats.e3_master_gate_enabled);
	munit_assert_false(stats.e3_audio_gate_enabled);
	munit_assert_false(stats.e3_sync_gate_enabled);
	munit_assert_false(stats.e3_audio_effective);
	munit_assert_false(stats.e3_sync_effective);
	munit_assert_uint64(stats.video_frame_count, ==, 0);
	munit_assert_uint64(stats.audio_frame_count, ==, 0);
	munit_assert_uint64(stats.packets_received, ==, 0);
	munit_assert_uint64(stats.packets_lost, ==, 0);
	munit_assert_double(stats.measured_bitrate_kbps, ==, 0.0);
	munit_assert_uint64(stats.video_decode_lost_frames, ==, 0);
	munit_assert_uint64(stats.video_decode_recovered_frames, ==, 0);
	munit_assert_uint64(stats.video_decode_gap_event_count, ==, 0);
	munit_assert_uint64(stats.e3_audio_hook_frame_count, ==, 0);
	munit_assert_uint64(stats.e3_audio_hook_sample_count, ==, 0);
	munit_assert_uint64(stats.e3_sync_observation_count, ==, 0);
	munit_assert_int64(stats.e3_last_av_delta_us, ==, 0);
	munit_assert_uint64(stats.event_count, ==, 0);
	munit_assert_uint64(stats.ready_event_count, ==, 0);
	munit_assert_uint64(stats.stopped_event_count, ==, 0);
	munit_assert_uint64(stats.error_event_count, ==, 0);
	munit_assert_uint64(stats.last_event_monotonic_us, ==, 0);
	struct {
		bool started;
		ChiakiMediaSessionState state;
	} stats_tiny = {true, CHIAKI_MEDIA_SESSION_STATE_ERROR};
	munit_assert_int(
		chiaki_media_session_get_stats_compat(session, &stats_tiny, sizeof(stats_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_false(stats_tiny.started);
	munit_assert_int(stats_tiny.state, ==, CHIAKI_MEDIA_SESSION_STATE_CREATED);

	munit_assert_int(
		chiaki_media_session_start_cloud_strings(
			session,
			"127.0.0.1",
			9295,
			"sid",
			"spec",
			"AQIDBAUGBwgJCgsMDQ4PEA==",
			"cf97763265a80eac",
			true,
			true,
			false,
			CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
			CHIAKI_VIDEO_FPS_PRESET_60,
			10000,
			CHIAKI_CODEC_H264,
			NULL),
		==,
		CHIAKI_ERR_UNINITIALIZED);

	munit_assert_int(chiaki_media_session_stop(session), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_media_session_get_stats(session, &stats), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_false(stats.started);
	munit_assert_int(stats.state, ==, CHIAKI_MEDIA_SESSION_STATE_CREATED);
	bool got_video = true;
	munit_assert_int(
		chiaki_media_session_wait_for_video_frame(session, 5, &got_video),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_false(got_video);
	bool got_ready = true;
	munit_assert_int(
		chiaki_media_session_wait_for_event(session, CHIAKI_HEADLESS_EVENT_READY, 5, &got_ready),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_false(got_ready);
	ChiakiMediaReadinessReport report = {0};
	munit_assert_size(chiaki_media_readiness_report_size(), ==, sizeof(ChiakiMediaReadinessReport));
	munit_assert_int(
		chiaki_media_session_wait_for_readiness(session, 5, &report),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_false(report.started);
	munit_assert_int(report.state, ==, CHIAKI_MEDIA_SESSION_STATE_CREATED);
	munit_assert_int(report.last_event_type, ==, CHIAKI_HEADLESS_EVENT_CONNECTING);
	munit_assert_uint64(report.ready_event_count, ==, 0);
	munit_assert_uint64(report.stopped_event_count, ==, 0);
	munit_assert_uint64(report.error_event_count, ==, 0);
	munit_assert_uint64(report.last_event_monotonic_us, ==, 0);
	struct {
		bool started;
		ChiakiMediaSessionState state;
		bool seen_ready_event;
	} report_tiny = {true, CHIAKI_MEDIA_SESSION_STATE_ERROR, true};
	munit_assert_int(
		chiaki_media_session_wait_for_readiness_compat(session, 5, &report_tiny, sizeof(report_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_false(report_tiny.started);
	munit_assert_int(report_tiny.state, ==, CHIAKI_MEDIA_SESSION_STATE_CREATED);
	munit_assert_false(report_tiny.seen_ready_event);
	munit_assert_false(report.seen_ready_event);
	munit_assert_false(report.seen_video_frame);
	munit_assert_size(chiaki_media_health_report_size(), ==, sizeof(ChiakiMediaHealthReport));
	ChiakiMediaHealthReport health = {0};
	munit_assert_int(
		chiaki_media_session_get_health_report(session, &health),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(health.health_state, ==, CHIAKI_MEDIA_HEALTH_IDLE);
	munit_assert_int(health.session_state, ==, CHIAKI_MEDIA_SESSION_STATE_CREATED);
	munit_assert_false(health.started);
	munit_assert_false(health.seen_ready_event);
	munit_assert_false(health.seen_video_frame);
	munit_assert_false(health.seen_terminal_event);
	struct {
		ChiakiMediaHealthState health_state;
		ChiakiMediaSessionState session_state;
		bool started;
	} health_tiny = {CHIAKI_MEDIA_HEALTH_TERMINAL, CHIAKI_MEDIA_SESSION_STATE_ERROR, true};
	munit_assert_int(
		chiaki_media_session_get_health_report_compat(session, &health_tiny, sizeof(health_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(health_tiny.health_state, ==, CHIAKI_MEDIA_HEALTH_IDLE);
	munit_assert_int(health_tiny.session_state, ==, CHIAKI_MEDIA_SESSION_STATE_CREATED);
	munit_assert_false(health_tiny.started);
	ChiakiMediaHealthPolicy policy = {0};
	munit_assert_size(chiaki_media_health_policy_size(), ==, sizeof(ChiakiMediaHealthPolicy));
	chiaki_media_health_policy_init(&policy);
	munit_assert_uint64(policy.terminal_error_event_count, ==, 1);
	munit_assert_uint64(policy.degraded_no_video_min_event_count, ==, 3);
	munit_assert_true(policy.treat_stopped_as_terminal);
	munit_assert_true(policy.treat_error_event_as_terminal);
	ChiakiMediaHealthReport health_custom = {0};
	munit_assert_int(
		chiaki_media_session_get_health_report_with_policy(session, &policy, &health_custom),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(health_custom.health_state, ==, CHIAKI_MEDIA_HEALTH_IDLE);
	struct {
		ChiakiMediaHealthState health_state;
		ChiakiMediaSessionState session_state;
	} health_custom_tiny = {CHIAKI_MEDIA_HEALTH_TERMINAL, CHIAKI_MEDIA_SESSION_STATE_ERROR};
	munit_assert_int(
		chiaki_media_session_get_health_report_with_policy_compat(
			session,
			&policy,
			&health_custom_tiny,
			sizeof(health_custom_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(health_custom_tiny.health_state, ==, CHIAKI_MEDIA_HEALTH_IDLE);
	munit_assert_int(health_custom_tiny.session_state, ==, CHIAKI_MEDIA_SESSION_STATE_CREATED);
	ChiakiMediaHealthReport health_profile = {0};
	munit_assert_int(
		chiaki_media_session_get_health_report_with_profile(
			session,
			CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT,
			&health_profile),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(health_profile.health_state, ==, CHIAKI_MEDIA_HEALTH_IDLE);
	struct {
		ChiakiMediaHealthState health_state;
	} health_profile_tiny = {CHIAKI_MEDIA_HEALTH_TERMINAL};
	munit_assert_int(
		chiaki_media_session_get_health_report_with_profile_compat(
			session,
			CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT,
			&health_profile_tiny,
			sizeof(health_profile_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(health_profile_tiny.health_state, ==, CHIAKI_MEDIA_HEALTH_IDLE);
	ChiakiMediaHealthReport health_profile_key = {0};
	munit_assert_int(
		chiaki_media_session_get_health_report_with_profile_key(session, "default", &health_profile_key),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(health_profile_key.health_state, ==, CHIAKI_MEDIA_HEALTH_IDLE);
	struct {
		ChiakiMediaHealthState health_state;
	} health_profile_key_tiny = {CHIAKI_MEDIA_HEALTH_TERMINAL};
	munit_assert_int(
		chiaki_media_session_get_health_report_with_profile_key_compat(
			session,
			"default",
			&health_profile_key_tiny,
			sizeof(health_profile_key_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(health_profile_key_tiny.health_state, ==, CHIAKI_MEDIA_HEALTH_IDLE);
	munit_assert_size(
		chiaki_media_session_diagnostics_snapshot_size(),
		==,
		sizeof(ChiakiMediaSessionDiagnosticsSnapshot));
	ChiakiMediaSessionDiagnosticsSnapshot snapshot = {0};
	munit_assert_int(
		chiaki_media_session_get_diagnostics_snapshot(session, 5, &snapshot),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(snapshot.api_version, ==, chiaki_headless_api_version());
	munit_assert_false(snapshot.stats.started);
	munit_assert_false(snapshot.stats.e3_master_gate_enabled);
	munit_assert_false(snapshot.stats.e3_audio_gate_enabled);
	munit_assert_false(snapshot.stats.e3_sync_gate_enabled);
	munit_assert_false(snapshot.stats.e3_audio_effective);
	munit_assert_false(snapshot.stats.e3_sync_effective);
	munit_assert_uint64(snapshot.stats.packets_received, ==, 0);
	munit_assert_uint64(snapshot.stats.packets_lost, ==, 0);
	munit_assert_double(snapshot.stats.measured_bitrate_kbps, ==, 0.0);
	munit_assert_uint64(snapshot.stats.video_decode_lost_frames, ==, 0);
	munit_assert_uint64(snapshot.stats.video_decode_recovered_frames, ==, 0);
	munit_assert_uint64(snapshot.stats.video_decode_gap_event_count, ==, 0);
	munit_assert_uint64(snapshot.stats.e3_audio_hook_frame_count, ==, 0);
	munit_assert_uint64(snapshot.stats.e3_audio_hook_sample_count, ==, 0);
	munit_assert_uint64(snapshot.stats.e3_sync_observation_count, ==, 0);
	munit_assert_int64(snapshot.stats.e3_last_av_delta_us, ==, 0);
	munit_assert_int(snapshot.health.health_state, ==, CHIAKI_MEDIA_HEALTH_IDLE);
	struct {
		uint32_t api_version;
		ChiakiMediaSessionStats stats;
	} snapshot_tiny = {0};
	munit_assert_int(
		chiaki_media_session_get_diagnostics_snapshot_compat(session, 5, &snapshot_tiny, sizeof(snapshot_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(snapshot_tiny.api_version, ==, chiaki_headless_api_version());
	ChiakiMediaSessionDiagnosticsSnapshot snapshot_policy = {0};
	munit_assert_int(
		chiaki_media_session_get_diagnostics_snapshot_with_policy(session, 5, &policy, &snapshot_policy),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(snapshot_policy.health.health_state, ==, CHIAKI_MEDIA_HEALTH_IDLE);
	struct {
		uint32_t api_version;
		ChiakiMediaSessionStats stats;
	} snapshot_policy_tiny = {0};
	munit_assert_int(
		chiaki_media_session_get_diagnostics_snapshot_with_policy_compat(
			session,
			5,
			&policy,
			&snapshot_policy_tiny,
			sizeof(snapshot_policy_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(snapshot_policy_tiny.api_version, ==, chiaki_headless_api_version());
	ChiakiMediaSessionDiagnosticsSnapshot snapshot_profile = {0};
	munit_assert_int(
		chiaki_media_session_get_diagnostics_snapshot_with_profile(
			session,
			5,
			CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT,
			&snapshot_profile),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(snapshot_profile.health.health_state, ==, CHIAKI_MEDIA_HEALTH_IDLE);
	struct {
		uint32_t api_version;
	} snapshot_profile_tiny = {0};
	munit_assert_int(
		chiaki_media_session_get_diagnostics_snapshot_with_profile_compat(
			session,
			5,
			CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT,
			&snapshot_profile_tiny,
			sizeof(snapshot_profile_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(snapshot_profile_tiny.api_version, ==, chiaki_headless_api_version());
	ChiakiMediaSessionDiagnosticsSnapshot snapshot_profile_key = {0};
	munit_assert_int(
		chiaki_media_session_get_diagnostics_snapshot_with_profile_key(
			session,
			5,
			"default",
			&snapshot_profile_key),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(snapshot_profile_key.health.health_state, ==, CHIAKI_MEDIA_HEALTH_IDLE);
	struct {
		uint32_t api_version;
	} snapshot_profile_key_tiny = {0};
	munit_assert_int(
		chiaki_media_session_get_diagnostics_snapshot_with_profile_key_compat(
			session,
			5,
			"default",
			&snapshot_profile_key_tiny,
			sizeof(snapshot_profile_key_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint32(snapshot_profile_key_tiny.api_version, ==, chiaki_headless_api_version());
	ChiakiMediaHealthPolicy defaults = {0};
	munit_assert_int(chiaki_media_health_policy_defaults(&defaults), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_uint64(defaults.terminal_error_event_count, ==, 1);
	struct {
		uint64_t terminal_error_event_count;
	} defaults_tiny = {0};
	munit_assert_int(
		chiaki_media_health_policy_defaults_compat(&defaults_tiny, sizeof(defaults_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint64(defaults_tiny.terminal_error_event_count, ==, 1);
	ChiakiMediaHealthPolicy prof = {0};
	munit_assert_int(
		chiaki_media_health_policy_from_profile(CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE, &prof),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint64(prof.degraded_no_video_min_event_count, ==, 1);
	munit_assert_true(prof.treat_stopped_as_terminal);
	munit_assert_int(
		chiaki_media_health_policy_from_profile(CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE, &prof),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint64(prof.terminal_error_event_count, ==, 3);
	munit_assert_false(prof.treat_error_event_as_terminal);
	struct {
		uint64_t terminal_error_event_count;
	} prof_tiny = {0};
	munit_assert_int(
		chiaki_media_health_policy_from_profile_compat(
			CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE,
			&prof_tiny,
			sizeof(prof_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_uint64(prof_tiny.terminal_error_event_count, ==, 1);
	munit_assert_size(
		chiaki_media_health_policy_profile_info_size(),
		==,
		sizeof(ChiakiMediaHealthPolicyProfileInfo));
	munit_assert_size(chiaki_media_health_policy_profile_count(), ==, 3);
	ChiakiMediaHealthPolicyProfileInfo pinfo = {0};
	munit_assert_int(chiaki_media_health_policy_profile_info(0, &pinfo), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(pinfo.profile, ==, CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT);
	munit_assert_true(pinfo.is_default_profile);
	munit_assert_string_equal(pinfo.profile_key, "default");
	munit_assert_string_equal(pinfo.display_label, "Default");
	munit_assert_int(chiaki_media_health_policy_profile_info(1, &pinfo), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(pinfo.profile, ==, CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE);
	munit_assert_false(pinfo.is_default_profile);
	munit_assert_string_equal(pinfo.profile_key, "aggressive");
	munit_assert_string_equal(pinfo.display_label, "Aggressive");
	struct {
		ChiakiMediaHealthPolicyProfile profile;
	} pinfo_tiny = {CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE};
	munit_assert_int(
		chiaki_media_health_policy_profile_info_compat(2, &pinfo_tiny, sizeof(pinfo_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(pinfo_tiny.profile, ==, CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE);
	ChiakiMediaHealthPolicyProfile by_key = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	munit_assert_int(
		chiaki_media_health_policy_profile_from_key("aggressive", &by_key),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(by_key, ==, CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE);
	const char *canon_key = NULL;
	munit_assert_int(
		chiaki_media_health_policy_profile_key_normalize("Aggressive", &canon_key),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_string_equal(canon_key, "aggressive");
	struct {
		ChiakiMediaHealthPolicyProfile profile;
	} by_key_tiny = {CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT};
	munit_assert_int(
		chiaki_media_health_policy_profile_from_key_compat("conservative", &by_key_tiny, sizeof(by_key_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(by_key_tiny.profile, ==, CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE);
	ChiakiMediaHealthPolicyProfile by_norm = CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT;
	munit_assert_int(
		chiaki_media_health_policy_profile_key_normalize_compat("Con servative", &by_norm, sizeof(by_norm)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(by_norm, ==, CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE);
	const char *roundtrip_key = NULL;
	munit_assert_int(
		chiaki_media_health_policy_profile_key_from_profile(
			CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_CONSERVATIVE,
			&roundtrip_key),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_string_equal(roundtrip_key, "conservative");
	munit_assert_size(
		chiaki_media_health_policy_profile_resolution_size(),
		==,
		sizeof(ChiakiMediaHealthPolicyProfileResolution));
	ChiakiMediaHealthPolicyProfileResolution resolved = {0};
	munit_assert_int(
		chiaki_media_health_policy_profile_resolve("Agg ressive", &resolved),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(resolved.profile, ==, CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_AGGRESSIVE);
	munit_assert_string_equal(resolved.canonical_profile_key, "aggressive");
	munit_assert_string_equal(resolved.display_label, "Aggressive");
	munit_assert_false(resolved.is_default_profile);
	struct {
		ChiakiMediaHealthPolicyProfile profile;
	} resolved_tiny = {CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT};
	munit_assert_int(
		chiaki_media_health_policy_profile_resolve_compat("Default", &resolved_tiny, sizeof(resolved_tiny)),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(resolved_tiny.profile, ==, CHIAKI_MEDIA_HEALTH_POLICY_PROFILE_DEFAULT);

	ChiakiMediaReadinessReport synthetic = {0};
	chiaki_media_readiness_report_init(&synthetic);
	synthetic.started = false;
	synthetic.state = CHIAKI_MEDIA_SESSION_STATE_CREATED;
	synthetic.event_count = 4;
	synthetic.seen_video_frame = false;
	synthetic.last_event_type = CHIAKI_HEADLESS_EVENT_STREAM_STATS;
	ChiakiMediaHealthReport synthetic_health = {0};
	munit_assert_int(
		chiaki_media_health_evaluate_readiness(&synthetic, &policy, &synthetic_health),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(synthetic_health.health_state, ==, CHIAKI_MEDIA_HEALTH_DEGRADED);

	synthetic.error_event_count = 1;
	synthetic.last_event_type = CHIAKI_HEADLESS_EVENT_ERROR;
	synthetic.seen_terminal_event = true;
	munit_assert_int(
		chiaki_media_health_evaluate_readiness(&synthetic, &policy, &synthetic_health),
		==,
		CHIAKI_ERR_SUCCESS);
	munit_assert_int(synthetic_health.health_state, ==, CHIAKI_MEDIA_HEALTH_TERMINAL);
	chiaki_media_session_destroy(session);
	return MUNIT_OK;
}

static MunitResult test_headless_media_e3_gate_semantics(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	unsetenv("DECKSTATION_MEDIA_E3_ENABLE");
	unsetenv("DECKSTATION_MEDIA_E3_AUDIO_ENABLE");
	unsetenv("DECKSTATION_MEDIA_E3_SYNC_ENABLE");
	ChiakiMediaSessionStats stats = {0};
	chiaki_media_session_stats_init(&stats);
	munit_assert_false(stats.e3_master_gate_enabled);
	munit_assert_false(stats.e3_audio_gate_enabled);
	munit_assert_false(stats.e3_sync_gate_enabled);
	munit_assert_false(stats.e3_audio_effective);
	munit_assert_false(stats.e3_sync_effective);

	setenv("DECKSTATION_MEDIA_E3_ENABLE", "1", 1);
	setenv("DECKSTATION_MEDIA_E3_AUDIO_ENABLE", "1", 1);
	unsetenv("DECKSTATION_MEDIA_E3_SYNC_ENABLE");
	chiaki_media_session_stats_init(&stats);
	munit_assert_true(stats.e3_master_gate_enabled);
	munit_assert_true(stats.e3_audio_gate_enabled);
	munit_assert_false(stats.e3_sync_gate_enabled);
	munit_assert_true(stats.e3_audio_effective);
	munit_assert_false(stats.e3_sync_effective);

	setenv("DECKSTATION_MEDIA_E3_SYNC_ENABLE", "1", 1);
	chiaki_media_session_stats_init(&stats);
	munit_assert_true(stats.e3_master_gate_enabled);
	munit_assert_true(stats.e3_audio_gate_enabled);
	munit_assert_true(stats.e3_sync_gate_enabled);
	munit_assert_true(stats.e3_audio_effective);
	munit_assert_true(stats.e3_sync_effective);

	unsetenv("DECKSTATION_MEDIA_E3_ENABLE");
	chiaki_media_session_stats_init(&stats);
	munit_assert_false(stats.e3_audio_effective);
	munit_assert_false(stats.e3_sync_effective);
	unsetenv("DECKSTATION_MEDIA_E3_AUDIO_ENABLE");
	unsetenv("DECKSTATION_MEDIA_E3_SYNC_ENABLE");

	return MUNIT_OK;
}

static MunitResult test_headless_cloud_connect_info(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	const uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
	};
	const uint8_t regist[CHIAKI_SESSION_AUTH_SIZE] = {
		0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
		0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
	};

	ChiakiHeadlessCloudLaunchInfo launch = {
		.host = "104.142.181.224",
		.stream_port = 41113,
		.session_id = "SESSION123",
		.launch_spec = "SPEC_BASE64",
		.morning = morning,
		.morning_size = sizeof(morning),
		.regist_key = regist,
		.regist_key_size = sizeof(regist),
		.ps5 = true,
		.enable_dualsense = true,
		.enable_keyboard = false,
		.takion_protocol_version = 9,
		.psn_wrapper_type = 0x59,
		.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
		.fps = CHIAKI_VIDEO_FPS_PRESET_60,
		.bitrate = 10000,
		.codec = CHIAKI_CODEC_H264,
	};

	ChiakiConnectInfo out = {0};
	ChiakiErrorCode err = chiaki_headless_connect_info_init_cloud_direct(&out, &launch);
	munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);
	munit_assert_ptr_equal(out.host, launch.host);
	munit_assert_true(out.cloud_direct);
	munit_assert_int(out.cloud_takion_protocol_version, ==, 9);
	munit_assert_int(out.cloud_psn_wrapper_type, ==, 0x59);
	munit_assert_int(out.stream_port, ==, 41113);
	munit_assert_ptr_equal(out.cloud_session_id, launch.session_id);
	munit_assert_ptr_equal(out.cloud_launch_spec_b64, launch.launch_spec);
	munit_assert_int(out.enable_dualsense, ==, 1);
	munit_assert_int(out.enable_keyboard, ==, 0);
	munit_assert_memory_equal(CHIAKI_HANDSHAKE_KEY_SIZE, out.morning, morning);
	munit_assert_memory_equal(CHIAKI_SESSION_AUTH_SIZE, out.regist_key, regist);
	munit_assert_int(out.video_profile.bitrate, ==, 10000);
	munit_assert_int(out.video_profile.max_fps, ==, 60);
	munit_assert_int(out.video_profile.codec, ==, CHIAKI_CODEC_H264);
	return MUNIT_OK;
}

static MunitResult test_headless_probe_cloud_launch(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	const uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
	};
	const uint8_t regist[CHIAKI_SESSION_AUTH_SIZE] = {
		0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
		0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
	};

	ChiakiErrorCode err = chiaki_headless_probe_cloud_launch(
		"104.142.181.224",
		41113,
		"SESSION123",
		"SPEC_BASE64",
		morning,
		sizeof(morning),
		regist,
		sizeof(regist),
		true,
		true,
		false,
		CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
		CHIAKI_VIDEO_FPS_PRESET_60,
		10000,
		CHIAKI_CODEC_H264);
	munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_headless_probe_cloud_launch_strings(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	ChiakiErrorCode err = chiaki_headless_probe_cloud_launch_strings(
		"104.142.181.224",
		41113,
		"SESSION123",
		"SPEC_BASE64",
		"AQIDBAUGBwgJCgsMDQ4PEA==",
		"b0b1b2b3b4b5b6b7b8b9babbbcbdbebf",
		true,
		true,
		false,
		CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
		CHIAKI_VIDEO_FPS_PRESET_60,
		10000,
		CHIAKI_CODEC_H264);
	munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);

	// Real cloud logs often provide a shorter regist key hex; this must be accepted
	// and zero-padded internally (same behavior as byte-path helper).
	err = chiaki_headless_probe_cloud_launch_strings(
		"104.142.181.224",
		41113,
		"SESSION123",
		"SPEC_BASE64",
		"AQIDBAUGBwgJCgsMDQ4PEA==",
		"cf97763265a80eac",
		true,
		true,
		false,
		CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
		CHIAKI_VIDEO_FPS_PRESET_60,
		10000,
		CHIAKI_CODEC_H264);
	munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_headless_probe_create_session_cloud_launch_strings(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	ChiakiErrorCode err = chiaki_headless_probe_create_session_cloud_launch_strings(
		"104.142.181.224",
		41113,
		"SESSION123",
		"SPEC_BASE64",
		"AQIDBAUGBwgJCgsMDQ4PEA==",
		"cf97763265a80eac",
		true,
		true,
		false,
		CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
		CHIAKI_VIDEO_FPS_PRESET_60,
		10000,
		CHIAKI_CODEC_H264,
		NULL);
	munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_headless_probe_start_stop_session_cloud_launch_strings_validate(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	// Invalid morning b64 size should fail fast before any network/session start.
	ChiakiErrorCode err = chiaki_headless_probe_start_stop_session_cloud_launch_strings(
		"104.142.181.224",
		41113,
		"SESSION123",
		"SPEC_BASE64",
		"AQID",
		"cf97763265a80eac",
		true,
		true,
		false,
		CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
		CHIAKI_VIDEO_FPS_PRESET_60,
		10000,
		CHIAKI_CODEC_H264,
		NULL);
	munit_assert_int(err, ==, CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_cloud_stop_not_running(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	munit_assert_int(chiaki_headless_runtime_cloud_stop(), ==, CHIAKI_ERR_SUCCESS);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_request_idr_not_running(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	munit_assert_int(chiaki_headless_runtime_request_idr(), ==, CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_cloud_start_strings_validate(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	ChiakiErrorCode err = chiaki_headless_runtime_cloud_start_strings(
		"104.142.181.224",
		41113,
		"SESSION123",
		"SPEC_BASE64",
		"AQID",
		"cf97763265a80eac",
		true,
		true,
		false,
		CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
		CHIAKI_VIDEO_FPS_PRESET_60,
		10000,
		CHIAKI_CODEC_H264,
		NULL);
	munit_assert_int(err, ==, CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

static MunitResult test_headless_runtime_cloud_start_validate(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;

	munit_assert_int(chiaki_headless_runtime_cloud_start(NULL, NULL), ==, CHIAKI_ERR_INVALID_DATA);

	ChiakiHeadlessCloudLaunchInfo launch = {0};
	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	launch.host = "127.0.0.1";
	launch.stream_port = 9295;
	launch.session_id = "sid";
	launch.launch_spec = "spec";
	launch.morning = morning;
	launch.morning_size = sizeof(morning);
	launch.ps5 = true;
	launch.enable_dualsense = true;
	launch.enable_keyboard = true;
	launch.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
	launch.fps = CHIAKI_VIDEO_FPS_PRESET_60;
	launch.codec = CHIAKI_CODEC_H265;

	/*
	 * With placeholder transport values and no live cloud endpoint, runtime
	 * start should fail somewhere after argument validation, but not as
	 * CHIAKI_ERR_INVALID_DATA.
	 */
	munit_assert_int(chiaki_headless_runtime_cloud_start(&launch, NULL), !=, CHIAKI_ERR_INVALID_DATA);
	chiaki_headless_runtime_cloud_stop();
	return MUNIT_OK;
}

MunitTest tests_headless[] = {
	{
		"/validate_create",
		test_headless_validate_create,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/null_ops",
		test_headless_null_ops,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/probe",
		test_headless_probe,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_set_callbacks",
		test_headless_runtime_set_callbacks,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_set_stream_profile_overrides",
		test_headless_runtime_set_stream_profile_overrides,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_set_launch_overrides",
		test_headless_runtime_set_launch_overrides,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_set_get_overrides",
		test_headless_runtime_set_get_overrides,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_patch_overrides",
		test_headless_runtime_patch_overrides,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_get_effective_stream_profile",
		test_headless_runtime_get_effective_stream_profile,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_get_effective_launch_info",
		test_headless_runtime_get_effective_launch_info,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_get_effective_connect_info",
		test_headless_runtime_get_effective_connect_info,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_get_sanity_report",
		test_headless_runtime_get_sanity_report,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_get_state_snapshot",
		test_headless_runtime_get_state_snapshot,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_state_snapshot_compat",
		test_headless_runtime_state_snapshot_compat,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
		{
			"/runtime_capabilities",
			test_headless_runtime_capabilities,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_media_diagnostics_without_session",
			test_headless_runtime_media_diagnostics_without_session,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_recovery_decision_without_session",
			test_headless_runtime_recovery_decision_without_session,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_recovery_apply_not_running",
			test_headless_runtime_recovery_apply_not_running,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_recover_result_not_running",
			test_headless_runtime_recover_result_not_running,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_recover_tuned_not_running",
			test_headless_runtime_recover_tuned_not_running,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_recovery_status_reset",
			test_headless_runtime_recovery_status_reset,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_recovery_config_and_auto_not_running",
			test_headless_runtime_recovery_config_and_auto_not_running,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_recovery_simulation",
			test_headless_runtime_recovery_simulation,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_recovery_simulation_sequence",
			test_headless_runtime_recovery_simulation_sequence,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_host_status_contract",
			test_headless_runtime_host_status_contract,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_playback_readiness_status_contract",
			test_headless_runtime_playback_readiness_status_contract,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_playback_continuity_status_contract",
			test_headless_runtime_playback_continuity_status_contract,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_video_frame_metadata_without_session",
			test_headless_runtime_video_frame_metadata_without_session,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_video_frame_poll_without_session",
			test_headless_runtime_video_frame_poll_without_session,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_video_frame_poll_layout",
			test_headless_runtime_video_frame_poll_layout,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/runtime_video_frame_poll_buffer_guard",
			test_headless_runtime_video_frame_poll_buffer_guard,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/media_capabilities",
			test_headless_media_capabilities,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/media_session_scaffold",
			test_headless_media_session_scaffold,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
		{
			"/media_e3_gate_semantics",
			test_headless_media_e3_gate_semantics,
			NULL,
			NULL,
			MUNIT_TEST_OPTION_NONE,
			NULL,
		},
	{
		"/cloud_connect_info",
		test_headless_cloud_connect_info,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/probe_cloud_launch",
		test_headless_probe_cloud_launch,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/probe_cloud_launch_strings",
		test_headless_probe_cloud_launch_strings,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/probe_create_session_cloud_launch_strings",
		test_headless_probe_create_session_cloud_launch_strings,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/probe_start_stop_session_cloud_launch_strings_validate",
		test_headless_probe_start_stop_session_cloud_launch_strings_validate,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_cloud_stop_not_running",
		test_headless_runtime_cloud_stop_not_running,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_request_idr_not_running",
		test_headless_runtime_request_idr_not_running,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_cloud_start_strings_validate",
		test_headless_runtime_cloud_start_strings_validate,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{
		"/runtime_cloud_start_validate",
		test_headless_runtime_cloud_start_validate,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL,
	},
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
};
