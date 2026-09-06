// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "audio-decoder.h"
#include "audio-output.h"
#include "deckstation-jni.h"
#include "video-decoder.h"

#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <chiaki/base64.h>
#include <chiaki/controller.h>
#include <chiaki/frameprocessor.h>
#include <chiaki/headless.h>
#include <chiaki/packetstats.h>
#include <chiaki/session.h>
#include <chiaki/time.h>

#define DS_TAG "DeckStationChiaki"
#define DS_STREAM_STATS_VALUE_COUNT 15
#define DS_STREAM_STATS_INTERVAL_US 1000000

typedef struct deckstation_android_session_t
{
	ChiakiSession session;
	ChiakiLog log;
	AndroidChiakiVideoDecoder video_decoder;
	AndroidChiakiAudioDecoder audio_decoder;
	void *audio_output;
	JavaVM *vm;
	jobject callback;
	jmethodID callback_method;
	jmethodID stats_callback_method;
	char *host;
	char *session_id;
	char *launch_spec;
	bool session_init;
	bool video_init;
	bool audio_init;
	bool started;
	atomic_bool local_stop_requested;
	uint64_t video_samples;
	uint64_t audio_frames;
	uint64_t audio_samples;
	uint64_t video_decode_lost_frames;
	uint64_t video_decode_recovered_frames;
	uint64_t video_decode_gap_event_count;
	uint64_t last_stats_emit_monotonic_us;
} DeckStationAndroidSession;

static pthread_mutex_t g_session_mutex = PTHREAD_MUTEX_INITIALIZER;
static DeckStationAndroidSession *g_session = NULL;

static void deckstation_log_cb(ChiakiLogLevel level, const char *message, void *user)
{
	(void)user;
	int priority = ANDROID_LOG_INFO;
	if(level == CHIAKI_LOG_ERROR)
		priority = ANDROID_LOG_ERROR;
	else if(level == CHIAKI_LOG_WARNING)
		priority = ANDROID_LOG_WARN;
	else if(level == CHIAKI_LOG_DEBUG || level == CHIAKI_LOG_VERBOSE)
		priority = ANDROID_LOG_DEBUG;
	__android_log_write(priority, DS_TAG, message ? message : "");
}

static JNIEnv *deckstation_attach(DeckStationAndroidSession *session, bool *did_attach)
{
	*did_attach = false;
	JNIEnv *env = NULL;
	if((*session->vm)->GetEnv(session->vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK)
		return env;
	if((*session->vm)->AttachCurrentThread(session->vm, (void **)&env, NULL) != JNI_OK)
		return NULL;
	*did_attach = true;
	return env;
}

static void deckstation_emit(DeckStationAndroidSession *session, const char *type,
		const char *detail, int64_t value0, int64_t value1)
{
	if(!session || !session->callback || !session->callback_method)
		return;
	bool did_attach = false;
	JNIEnv *env = deckstation_attach(session, &did_attach);
	if(!env)
		return;
	jstring java_type = (*env)->NewStringUTF(env, type ? type : "");
	jstring java_detail = (*env)->NewStringUTF(env, detail ? detail : "");
	(*env)->CallVoidMethod(env, session->callback, session->callback_method,
		java_type, java_detail, (jlong)value0, (jlong)value1);
	(*env)->DeleteLocalRef(env, java_type);
	(*env)->DeleteLocalRef(env, java_detail);
	if((*env)->ExceptionCheck(env))
		(*env)->ExceptionClear(env);
	if(did_attach)
		(*session->vm)->DetachCurrentThread(session->vm);
}

static void deckstation_emit_stream_stats(DeckStationAndroidSession *session)
{
	if(!session || !session->stats_callback_method || !session->session_init)
		return;
	uint64_t now = chiaki_time_now_monotonic_us();
	if(session->last_stats_emit_monotonic_us != 0
		&& now - session->last_stats_emit_monotonic_us < DS_STREAM_STATS_INTERVAL_US)
		return;
	session->last_stats_emit_monotonic_us = now;

	ChiakiStreamConnection *connection = &session->session.stream_connection;
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

	jdouble values[DS_STREAM_STATS_VALUE_COUNT] = {
		(jdouble)connection->measured_bitrate,
		(jdouble)connection->last_connection_quality_rtt_ms,
		(jdouble)connection->last_connection_quality_target_bitrate,
		(jdouble)connection->last_connection_quality_upstream_bitrate,
		(jdouble)connection->last_connection_quality_upstream_loss,
		(jdouble)connection->last_connection_quality_loss_raw,
		(jdouble)packets_received,
		(jdouble)packets_lost,
		(jdouble)video_stream_bytes,
		(jdouble)video_stream_frames,
		(jdouble)android_chiaki_video_decoder_rendered_frames(&session->video_decoder),
		(jdouble)session->audio_frames,
		(jdouble)session->video_decode_lost_frames,
		(jdouble)session->video_decode_recovered_frames,
		(jdouble)session->video_decode_gap_event_count,
	};

	bool did_attach = false;
	JNIEnv *env = deckstation_attach(session, &did_attach);
	if(!env)
		return;
	jdoubleArray java_values = (*env)->NewDoubleArray(env, DS_STREAM_STATS_VALUE_COUNT);
	if(java_values)
	{
		(*env)->SetDoubleArrayRegion(env, java_values, 0,
			DS_STREAM_STATS_VALUE_COUNT, values);
		(*env)->CallVoidMethod(env, session->callback,
			session->stats_callback_method, java_values);
		(*env)->DeleteLocalRef(env, java_values);
	}
	if((*env)->ExceptionCheck(env))
		(*env)->ExceptionClear(env);
	if(did_attach)
		(*session->vm)->DetachCurrentThread(session->vm);
}

static void deckstation_session_event(ChiakiEvent *event, void *user)
{
	DeckStationAndroidSession *session = user;
	if(!event || !session)
		return;
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			deckstation_emit(session, "ready", "Chiaki session connected", 0, 0);
			break;
		case CHIAKI_EVENT_VIDEO_FEC_FAILURE:
			deckstation_emit(session, "warning", "Video FEC failure",
				(int64_t)event->video_fec_failure.frame_index,
				event->video_fec_failure.idr_request_sent ? 1 : 0);
			break;
		case CHIAKI_EVENT_QUIT:
			if(!atomic_load(&session->local_stop_requested))
			{
				deckstation_emit(session, "terminal",
					event->quit.reason_str ? event->quit.reason_str : "Chiaki session stopped",
					(int64_t)event->quit.reason, 0);
			}
			else
			{
				CHIAKI_LOGI(&session->log,
					"Suppressing terminal event for locally requested stop");
			}
			break;
		default:
			break;
	}
}

static bool deckstation_video_sample(uint8_t *buf, size_t buf_size,
	int frames_lost, bool frame_recovered, void *user)
{
	DeckStationAndroidSession *session = user;
	session->video_samples++;
	if(frames_lost > 0)
	{
		session->video_decode_lost_frames += (uint64_t)frames_lost;
		session->video_decode_gap_event_count++;
	}
	if(frame_recovered)
		session->video_decode_recovered_frames++;
	bool accepted = android_chiaki_video_decoder_video_sample(buf, buf_size,
		frames_lost, frame_recovered, &session->video_decoder);
	deckstation_emit_stream_stats(session);
	return accepted;
}

static void deckstation_audio_settings(uint32_t channels, uint32_t rate, void *user)
{
	DeckStationAndroidSession *session = user;
	android_chiaki_audio_output_settings(channels, rate, session->audio_output);
	deckstation_emit(session, "audioReady", "Android audio sink started", channels, rate);
}

static void deckstation_audio_frame(int16_t *buf, size_t samples_count, void *user)
{
	DeckStationAndroidSession *session = user;
	session->audio_frames++;
	session->audio_samples += samples_count;
	android_chiaki_audio_output_frame(buf, samples_count, session->audio_output);
}

static int deckstation_hex_nibble(char value)
{
	if(value >= '0' && value <= '9') return value - '0';
	if(value >= 'a' && value <= 'f') return value - 'a' + 10;
	if(value >= 'A' && value <= 'F') return value - 'A' + 10;
	return -1;
}

static bool deckstation_decode_hex(const char *value, uint8_t *out, size_t out_size)
{
	if(!value)
		return false;
	size_t len = strlen(value);
	if(len > out_size * 2 || (len % 2) != 0)
		return false;
	memset(out, 0, out_size);
	for(size_t i = 0; i < len / 2; i++)
	{
		int hi = deckstation_hex_nibble(value[i * 2]);
		int lo = deckstation_hex_nibble(value[i * 2 + 1]);
		if(hi < 0 || lo < 0)
			return false;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

static void deckstation_session_free(JNIEnv *env, DeckStationAndroidSession *session)
{
	if(!session)
		return;
	if(session->started)
	{
		atomic_store(&session->local_stop_requested, true);
		chiaki_session_stop(&session->session);
		chiaki_session_join(&session->session);
		session->started = false;
	}
	if(session->session_init)
		chiaki_session_fini(&session->session);
	if(session->video_init)
		android_chiaki_video_decoder_fini(&session->video_decoder);
	if(session->audio_init)
		android_chiaki_audio_decoder_fini(&session->audio_decoder);
	if(session->audio_output)
		android_chiaki_audio_output_free(session->audio_output);
	if(session->callback)
		(*env)->DeleteGlobalRef(env, session->callback);
	free(session->host);
	free(session->session_id);
	free(session->launch_spec);
	free(session);
}

static jint deckstation_native_start(
	JNIEnv *env, jobject bridge, jobject surface, jstring host_value, jint stream_port,
	jstring session_id_value, jstring launch_spec_value, jstring morning_value,
	jstring regist_key_value, jint resolution, jint fps, jint bitrate, jint codec,
	jboolean ps5, jboolean enable_dualsense, jboolean enable_keyboard,
	jint takion_protocol_version, jint psn_wrapper_type)
{
	if(!surface || !host_value || !session_id_value || !launch_spec_value || !morning_value)
		return CHIAKI_ERR_INVALID_DATA;

	pthread_mutex_lock(&g_session_mutex);
	if(g_session)
	{
		deckstation_session_free(env, g_session);
		g_session = NULL;
	}

	DeckStationAndroidSession *session = calloc(1, sizeof(*session));
	if(!session)
	{
		pthread_mutex_unlock(&g_session_mutex);
		return CHIAKI_ERR_MEMORY;
	}
	atomic_init(&session->local_stop_requested, false);
	chiaki_log_init(&session->log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE,
		deckstation_log_cb, NULL);
	(*env)->GetJavaVM(env, &session->vm);
	session->callback = (*env)->NewGlobalRef(env, bridge);
	jclass bridge_class = (*env)->GetObjectClass(env, bridge);
	if(session->callback && bridge_class)
		session->callback_method = (*env)->GetMethodID(env, bridge_class,
			"onNativeEvent", "(Ljava/lang/String;Ljava/lang/String;JJ)V");
	if((*env)->ExceptionCheck(env))
		(*env)->ExceptionClear(env);
	if(session->callback_method)
		session->stats_callback_method = (*env)->GetMethodID(env, bridge_class,
			"onNativeStreamStats", "([D)V");
	if((*env)->ExceptionCheck(env))
		(*env)->ExceptionClear(env);
	if(bridge_class)
		(*env)->DeleteLocalRef(env, bridge_class);
	if(!session->callback || !session->callback_method || !session->stats_callback_method)
	{
		CHIAKI_LOGE(&session->log,
			"DeckStation Android JNI callbacks are unavailable; check release keep rules");
		deckstation_session_free(env, session);
		pthread_mutex_unlock(&g_session_mutex);
		return CHIAKI_ERR_UNINITIALIZED;
	}

	const char *host = (*env)->GetStringUTFChars(env, host_value, NULL);
	const char *session_id = (*env)->GetStringUTFChars(env, session_id_value, NULL);
	const char *launch_spec = (*env)->GetStringUTFChars(env, launch_spec_value, NULL);
	const char *morning_b64 = (*env)->GetStringUTFChars(env, morning_value, NULL);
	const char *regist_key_hex = regist_key_value
		? (*env)->GetStringUTFChars(env, regist_key_value, NULL) : NULL;
	session->host = strdup(host);
	session->session_id = strdup(session_id);
	session->launch_spec = strdup(launch_spec);

	uint8_t morning[CHIAKI_HANDSHAKE_KEY_SIZE] = {0};
	size_t morning_size = sizeof(morning);
	uint8_t regist_key[CHIAKI_SESSION_AUTH_SIZE] = {0};
	ChiakiErrorCode err = session->host && session->session_id && session->launch_spec
		? chiaki_base64_decode(morning_b64, strlen(morning_b64), morning, &morning_size)
		: CHIAKI_ERR_MEMORY;
	if(err == CHIAKI_ERR_SUCCESS && morning_size != sizeof(morning))
		err = CHIAKI_ERR_INVALID_DATA;
	if(err == CHIAKI_ERR_SUCCESS && regist_key_hex
		&& !deckstation_decode_hex(regist_key_hex, regist_key, sizeof(regist_key)))
		err = CHIAKI_ERR_INVALID_DATA;

	ChiakiHeadlessCloudLaunchInfo launch = {
		.host = session->host,
		.stream_port = (uint16_t)stream_port,
		.session_id = session->session_id,
		.launch_spec = session->launch_spec,
		.morning = morning,
		.morning_size = sizeof(morning),
		.regist_key = regist_key,
		.regist_key_size = sizeof(regist_key),
		.ps5 = ps5,
		.enable_dualsense = enable_dualsense,
		.enable_keyboard = enable_keyboard,
		.takion_protocol_version = (uint8_t)takion_protocol_version,
		.psn_wrapper_type = (uint8_t)psn_wrapper_type,
		.resolution = (ChiakiVideoResolutionPreset)resolution,
		.fps = (ChiakiVideoFPSPreset)fps,
		.bitrate = (unsigned int)bitrate,
		.codec = (ChiakiCodec)codec,
	};
	ChiakiConnectInfo connect_info = {0};
	if(err == CHIAKI_ERR_SUCCESS)
		err = chiaki_headless_runtime_build_cloud_connect_info(
			&connect_info, &launch);

	if(err == CHIAKI_ERR_SUCCESS)
	{
		err = android_chiaki_video_decoder_init(&session->video_decoder, &session->log,
			connect_info.video_profile.width, connect_info.video_profile.height,
			connect_info.video_profile.codec);
		if(err == CHIAKI_ERR_SUCCESS)
			session->video_init = true;
	}
	if(err == CHIAKI_ERR_SUCCESS)
	{
		android_chiaki_video_decoder_set_surface(&session->video_decoder, env, surface);
		err = android_chiaki_audio_decoder_init(&session->audio_decoder, &session->log);
		if(err == CHIAKI_ERR_SUCCESS)
			session->audio_init = true;
	}
	if(err == CHIAKI_ERR_SUCCESS)
	{
		session->audio_output = android_chiaki_audio_output_new(&session->log);
		android_chiaki_audio_decoder_set_cb(&session->audio_decoder,
			deckstation_audio_settings, deckstation_audio_frame, session);
		err = chiaki_session_init(&session->session, &connect_info, &session->log);
		if(err == CHIAKI_ERR_SUCCESS)
			session->session_init = true;
	}
	if(err == CHIAKI_ERR_SUCCESS)
	{
		chiaki_session_set_event_cb(&session->session, deckstation_session_event, session);
		chiaki_session_set_video_sample_cb(&session->session, deckstation_video_sample, session);
		ChiakiAudioSink audio_sink;
		android_chiaki_audio_decoder_get_sink(&session->audio_decoder, &audio_sink);
		chiaki_session_set_audio_sink(&session->session, &audio_sink);
		err = chiaki_session_start(&session->session);
		if(err == CHIAKI_ERR_SUCCESS)
			session->started = true;
	}

	(*env)->ReleaseStringUTFChars(env, host_value, host);
	(*env)->ReleaseStringUTFChars(env, session_id_value, session_id);
	(*env)->ReleaseStringUTFChars(env, launch_spec_value, launch_spec);
	(*env)->ReleaseStringUTFChars(env, morning_value, morning_b64);
	if(regist_key_hex)
		(*env)->ReleaseStringUTFChars(env, regist_key_value, regist_key_hex);

	if(err == CHIAKI_ERR_SUCCESS)
	{
		g_session = session;
		deckstation_emit(session, "starting", "Android Chiaki session starting", 0, 0);
	}
	else
	{
		deckstation_session_free(env, session);
	}
	pthread_mutex_unlock(&g_session_mutex);
	return err;
}

static jint deckstation_native_stop(JNIEnv *env, jobject bridge)
{
	(void)bridge;
	pthread_mutex_lock(&g_session_mutex);
	DeckStationAndroidSession *session = g_session;
	g_session = NULL;
	pthread_mutex_unlock(&g_session_mutex);
	deckstation_session_free(env, session);
	return CHIAKI_ERR_SUCCESS;
}

static jlongArray deckstation_native_stats(JNIEnv *env, jobject bridge)
{
	(void)bridge;
	jlong values[7] = {0, 0, 0, 0, 0, 0, 0};
	pthread_mutex_lock(&g_session_mutex);
	if(g_session)
	{
		values[0] = (jlong)g_session->video_samples;
		values[1] = (jlong)android_chiaki_video_decoder_rendered_frames(&g_session->video_decoder);
		values[2] = (jlong)g_session->audio_frames;
		values[3] = (jlong)g_session->audio_samples;
		values[4] = g_session->started ? 1 : 0;
		values[5] = 0;
		values[6] = (jlong)android_chiaki_video_decoder_input_failures(
			&g_session->video_decoder);
	}
	pthread_mutex_unlock(&g_session_mutex);
	jlongArray result = (*env)->NewLongArray(env, 7);
	(*env)->SetLongArrayRegion(env, result, 0, 7, values);
	return result;
}

static jint deckstation_native_send_controller_state(
	JNIEnv *env, jobject bridge, jint buttons, jint l2_state, jint r2_state,
	jint left_x, jint left_y, jint right_x, jint right_y,
	jint touch0_id, jint touch0_x, jint touch0_y,
	jint touch1_id, jint touch1_x, jint touch1_y)
{
	(void)env;
	(void)bridge;
	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);
	state.buttons = (uint32_t)buttons;
	state.l2_state = (uint8_t)l2_state;
	state.r2_state = (uint8_t)r2_state;
	state.left_x = (int16_t)left_x;
	state.left_y = (int16_t)left_y;
	state.right_x = (int16_t)right_x;
	state.right_y = (int16_t)right_y;
	state.touches[0].id = (int8_t)touch0_id;
	state.touches[0].x = (uint16_t)touch0_x;
	state.touches[0].y = (uint16_t)touch0_y;
	state.touches[1].id = (int8_t)touch1_id;
	state.touches[1].x = (uint16_t)touch1_x;
	state.touches[1].y = (uint16_t)touch1_y;

	pthread_mutex_lock(&g_session_mutex);
	ChiakiErrorCode err = g_session && g_session->session_init
		? chiaki_session_set_controller_state(&g_session->session, &state)
		: CHIAKI_ERR_UNINITIALIZED;
	pthread_mutex_unlock(&g_session_mutex);
	return err;
}

static jstring deckstation_native_set_cloud_controller_variant(
	JNIEnv *env, jobject bridge, jstring value)
{
	(void)bridge;
	const char *variant = (*env)->GetStringUTFChars(env, value, NULL);
	setenv("CHIAKI_CLOUD_CONTROLLER_VARIANT", variant, 1);
	(*env)->ReleaseStringUTFChars(env, value, variant);
	return (jstring)(*env)->NewLocalRef(env, value);
}

int deckstation_jni_register(JNIEnv *env)
{
	static const char *bridge_class_name =
		"com/tribestick/deck_station/DeckStationAndroidBridge";
	static const JNINativeMethod methods[] = {
		{
			"nativeStart",
			"(Landroid/view/Surface;Ljava/lang/String;ILjava/lang/String;"
			"Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IIIIZZZII)I",
			(void *)deckstation_native_start,
		},
		{ "nativeStop", "()I", (void *)deckstation_native_stop },
		{ "nativeStats", "()[J", (void *)deckstation_native_stats },
		{
			"nativeSendControllerState",
			"(IIIIIIIIIIIII)I",
			(void *)deckstation_native_send_controller_state,
		},
		{
			"nativeSetCloudControllerVariant",
			"(Ljava/lang/String;)Ljava/lang/String;",
			(void *)deckstation_native_set_cloud_controller_variant,
		},
	};

	jclass bridge_class = (*env)->FindClass(env, bridge_class_name);
	if(!bridge_class)
	{
		if((*env)->ExceptionCheck(env))
			(*env)->ExceptionClear(env);
		__android_log_print(ANDROID_LOG_ERROR, DS_TAG,
			"Unable to find JNI bridge class %s", bridge_class_name);
		return JNI_ERR;
	}
	int result = (*env)->RegisterNatives(env, bridge_class, methods,
		(jint)(sizeof(methods) / sizeof(methods[0])));
	(*env)->DeleteLocalRef(env, bridge_class);
	if(result != JNI_OK)
	{
		if((*env)->ExceptionCheck(env))
			(*env)->ExceptionClear(env);
		__android_log_print(ANDROID_LOG_ERROR, DS_TAG,
			"RegisterNatives failed for %s", bridge_class_name);
		return JNI_ERR;
	}
	__android_log_print(ANDROID_LOG_INFO, DS_TAG,
		"Registered DeckStation Android bridge natives");
	return JNI_OK;
}
