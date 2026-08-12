// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_JNI_VIDEO_DECODER_H
#define CHIAKI_JNI_VIDEO_DECODER_H

#include <jni.h>

#include <chiaki/thread.h>
#include <chiaki/log.h>

typedef struct AMediaCodec AMediaCodec;
typedef struct ANativeWindow ANativeWindow;

typedef struct android_chiaki_video_decoder_t
{
	ChiakiLog *log;
	ChiakiMutex codec_mutex;
	ChiakiMutex state_mutex;
	AMediaCodec *codec;
	ANativeWindow *window;
	uint64_t timestamp_cur;
	ChiakiThread output_thread;
	bool output_thread_started;
	bool shutdown_output;
	int32_t target_width;
	int32_t target_height;
	ChiakiCodec target_codec;
	uint64_t rendered_frames;
	uint64_t input_failures;
} AndroidChiakiVideoDecoder;

ChiakiErrorCode android_chiaki_video_decoder_init(AndroidChiakiVideoDecoder *decoder, ChiakiLog *log, int32_t target_width, int32_t target_height, ChiakiCodec codec);
void android_chiaki_video_decoder_fini(AndroidChiakiVideoDecoder *decoder);
void android_chiaki_video_decoder_set_surface(AndroidChiakiVideoDecoder *decoder, JNIEnv *env, jobject surface);
bool android_chiaki_video_decoder_video_sample(uint8_t *buf, size_t buf_size,
	int frames_lost, bool frame_recovered, void *user);
uint64_t android_chiaki_video_decoder_rendered_frames(AndroidChiakiVideoDecoder *decoder);
uint64_t android_chiaki_video_decoder_input_failures(AndroidChiakiVideoDecoder *decoder);

#endif
