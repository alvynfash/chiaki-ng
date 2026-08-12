// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "video-decoder.h"

#include <jni.h>

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/native_window_jni.h>

#include <string.h>

#define INPUT_BUFFER_TIMEOUT_US 100000
#define OUTPUT_BUFFER_TIMEOUT_US 10000

static void *android_chiaki_video_decoder_output_thread_func(void *user);
static ChiakiErrorCode start_decoder_locked(AndroidChiakiVideoDecoder *decoder);
static void stop_decoder_codec(AndroidChiakiVideoDecoder *decoder, bool send_eos);

ChiakiErrorCode android_chiaki_video_decoder_init(AndroidChiakiVideoDecoder *decoder, ChiakiLog *log, int32_t target_width, int32_t target_height, ChiakiCodec codec)
{
	decoder->log = log;
	decoder->codec = NULL;
	decoder->timestamp_cur = 0;
	decoder->target_width = target_width;
	decoder->target_height = target_height;
	decoder->target_codec = codec;
	decoder->output_thread_started = false;
	decoder->shutdown_output = false;
	decoder->rendered_frames = 0;
	decoder->input_failures = 0;
	ChiakiErrorCode err = chiaki_mutex_init(&decoder->codec_mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	err = chiaki_mutex_init(&decoder->state_mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		chiaki_mutex_fini(&decoder->codec_mutex);
	return err;
}

uint64_t android_chiaki_video_decoder_rendered_frames(AndroidChiakiVideoDecoder *decoder)
{
	chiaki_mutex_lock(&decoder->state_mutex);
	uint64_t rendered_frames = decoder->rendered_frames;
	chiaki_mutex_unlock(&decoder->state_mutex);
	return rendered_frames;
}

uint64_t android_chiaki_video_decoder_input_failures(AndroidChiakiVideoDecoder *decoder)
{
	chiaki_mutex_lock(&decoder->state_mutex);
	uint64_t input_failures = decoder->input_failures;
	chiaki_mutex_unlock(&decoder->state_mutex);
	return input_failures;
}

static void stop_decoder_codec(AndroidChiakiVideoDecoder *decoder, bool send_eos)
{
	chiaki_mutex_lock(&decoder->codec_mutex);
	if(!decoder->codec)
	{
		chiaki_mutex_unlock(&decoder->codec_mutex);
		return;
	}
	AMediaCodec *codec = decoder->codec;
	chiaki_mutex_lock(&decoder->state_mutex);
	decoder->shutdown_output = true;
	chiaki_mutex_unlock(&decoder->state_mutex);
	if(send_eos)
	{
		ssize_t codec_buf_index = AMediaCodec_dequeueInputBuffer(codec, 1000);
		if(codec_buf_index >= 0)
		{
			CHIAKI_LOGI(decoder->log, "Video Decoder sending EOS buffer");
			AMediaCodec_queueInputBuffer(codec, (size_t)codec_buf_index, 0, 0,
				decoder->timestamp_cur++, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
		}
		else
			CHIAKI_LOGE(decoder->log,
				"Failed to get input buffer for shutting down Video Decoder!");
	}
	AMediaCodec_stop(codec);
	bool join_output_thread = decoder->output_thread_started;
	chiaki_mutex_unlock(&decoder->codec_mutex);

	// Never join while holding codec_mutex: the output thread must remain free
	// to drain MediaCodec while a large access unit is being submitted.
	if(join_output_thread)
		chiaki_thread_join(&decoder->output_thread, NULL);

	chiaki_mutex_lock(&decoder->codec_mutex);
	decoder->output_thread_started = false;
	if(decoder->codec == codec)
	{
		AMediaCodec_delete(codec);
		decoder->codec = NULL;
	}
	chiaki_mutex_lock(&decoder->state_mutex);
	decoder->shutdown_output = false;
	chiaki_mutex_unlock(&decoder->state_mutex);
	chiaki_mutex_unlock(&decoder->codec_mutex);
}

static void kill_decoder(AndroidChiakiVideoDecoder *decoder)
{
	stop_decoder_codec(decoder, true);
	chiaki_mutex_lock(&decoder->codec_mutex);
	if(decoder->window)
	{
		ANativeWindow_release(decoder->window);
		decoder->window = NULL;
	}
	chiaki_mutex_unlock(&decoder->codec_mutex);
}

void android_chiaki_video_decoder_fini(AndroidChiakiVideoDecoder *decoder)
{
	kill_decoder(decoder);
	chiaki_mutex_fini(&decoder->state_mutex);
	chiaki_mutex_fini(&decoder->codec_mutex);
}

void android_chiaki_video_decoder_set_surface(AndroidChiakiVideoDecoder *decoder, JNIEnv *env, jobject surface)
{
	if(!surface)
	{
		if(decoder->codec || decoder->window)
		{
			kill_decoder(decoder);
			CHIAKI_LOGI(decoder->log, "Decoder shut down after surface was removed");
		}
		return;
	}

	chiaki_mutex_lock(&decoder->codec_mutex);

	if(decoder->codec)
	{
#if __ANDROID_API__ >= 23
		CHIAKI_LOGI(decoder->log, "Video decoder already initialized, swapping surface");
		ANativeWindow *new_window = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
		AMediaCodec_setOutputSurface(decoder->codec, new_window);
		ANativeWindow_release(decoder->window);
		decoder->window = new_window;
#else
		CHIAKI_LOGE(decoder->log, "Video Decoder already initialized");
#endif
		goto beach;
	}

	decoder->window = ANativeWindow_fromSurface(env, surface);
	ChiakiErrorCode err = start_decoder_locked(decoder);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		ANativeWindow_release(decoder->window);
		decoder->window = NULL;
	}

beach:
	chiaki_mutex_unlock(&decoder->codec_mutex);
}

static ChiakiErrorCode start_decoder_locked(AndroidChiakiVideoDecoder *decoder)
{
	if(!decoder->window)
		return CHIAKI_ERR_INVALID_DATA;

	const char *mime = chiaki_codec_is_h265(decoder->target_codec) ? "video/hevc" : "video/avc";
	CHIAKI_LOGI(decoder->log, "Initializing decoder with mime %s", mime);

	decoder->codec = AMediaCodec_createDecoderByType(mime);
	if(!decoder->codec)
	{
		CHIAKI_LOGE(decoder->log, "Failed to create AMediaCodec for mime type %s", mime);
		return CHIAKI_ERR_UNKNOWN;
	}

	AMediaFormat *format = AMediaFormat_new();
	AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, decoder->target_width);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, decoder->target_height);

	media_status_t r = AMediaCodec_configure(decoder->codec, format, decoder->window, NULL, 0);
	if(r != AMEDIA_OK)
	{
		CHIAKI_LOGE(decoder->log, "AMediaCodec_configure() failed: %d", (int)r);
		AMediaFormat_delete(format);
		goto error_codec;
	}

	r = AMediaCodec_start(decoder->codec);
	AMediaFormat_delete(format);
	if(r != AMEDIA_OK)
	{
		CHIAKI_LOGE(decoder->log, "AMediaCodec_start() failed: %d", (int)r);
		goto error_codec;
	}

	ChiakiErrorCode err = chiaki_thread_create(&decoder->output_thread,
		android_chiaki_video_decoder_output_thread_func, decoder);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(decoder->log, "Failed to create output thread for AMediaCodec");
		goto error_codec;
	}
	decoder->output_thread_started = true;

	return CHIAKI_ERR_SUCCESS;

error_codec:
	AMediaCodec_delete(decoder->codec);
	decoder->codec = NULL;
	return CHIAKI_ERR_UNKNOWN;
}

bool android_chiaki_video_decoder_video_sample(uint8_t *buf, size_t buf_size,
	int frames_lost, bool frame_recovered, void *user)
{
	(void)frames_lost;
	(void)frame_recovered;
	bool r = true;
	AndroidChiakiVideoDecoder *decoder = user;
	chiaki_mutex_lock(&decoder->codec_mutex);

	if(!decoder->codec)
	{
		CHIAKI_LOGE(decoder->log, "Received video data, but decoder is not initialized!");
		r = false;
		goto beach;
	}

	uint64_t sample_timestamp = decoder->timestamp_cur++;
	size_t sample_size = buf_size;
	size_t chunk_index = 0;
	while(buf_size > 0)
	{
		ssize_t codec_buf_index = AMediaCodec_dequeueInputBuffer(
			decoder->codec, INPUT_BUFFER_TIMEOUT_US);
		if(codec_buf_index < 0)
		{
			chiaki_mutex_lock(&decoder->state_mutex);
			decoder->input_failures++;
			chiaki_mutex_unlock(&decoder->state_mutex);
			CHIAKI_LOGE(decoder->log,
				"Failed to get input buffer status=%zd sample_size=%zu remaining=%zu chunk=%zu",
				codec_buf_index, sample_size, buf_size, chunk_index);
			r = false;
			goto beach;
		}

		size_t codec_buf_size;
		uint8_t *codec_buf = AMediaCodec_getInputBuffer(decoder->codec, (size_t)codec_buf_index, &codec_buf_size);
		if(!codec_buf || codec_buf_size == 0)
		{
			chiaki_mutex_lock(&decoder->state_mutex);
			decoder->input_failures++;
			chiaki_mutex_unlock(&decoder->state_mutex);
			CHIAKI_LOGE(decoder->log,
				"MediaCodec returned invalid input buffer index=%zd capacity=%zu",
				codec_buf_index, codec_buf_size);
			r = false;
			goto beach;
		}
		size_t codec_sample_size = buf_size;
		if(codec_sample_size > codec_buf_size)
			codec_sample_size = codec_buf_size;
		memcpy(codec_buf, buf, codec_sample_size);
		uint32_t flags = codec_sample_size < buf_size
			? AMEDIACODEC_BUFFER_FLAG_PARTIAL_FRAME
			: 0;
		media_status_t queue_status = AMediaCodec_queueInputBuffer(
			decoder->codec, (size_t)codec_buf_index, 0, codec_sample_size,
			sample_timestamp, flags);
		if(queue_status != AMEDIA_OK)
		{
			chiaki_mutex_lock(&decoder->state_mutex);
			decoder->input_failures++;
			chiaki_mutex_unlock(&decoder->state_mutex);
			CHIAKI_LOGE(decoder->log,
				"AMediaCodec_queueInputBuffer() failed status=%d sample_size=%zu chunk=%zu partial=%d",
				(int)queue_status, sample_size, chunk_index, flags != 0);
			r = false;
			goto beach;
		}
		buf += codec_sample_size;
		buf_size -= codec_sample_size;
		chunk_index++;
	}

beach:
	chiaki_mutex_unlock(&decoder->codec_mutex);
	return r;
}

static void *android_chiaki_video_decoder_output_thread_func(void *user)
{
	AndroidChiakiVideoDecoder *decoder = user;

	while(1)
	{
		AMediaCodecBufferInfo info;
		ssize_t status = AMediaCodec_dequeueOutputBuffer(
			decoder->codec, &info, OUTPUT_BUFFER_TIMEOUT_US);
		if(status >= 0)
		{
			AMediaCodec_releaseOutputBuffer(decoder->codec, (size_t)status, info.size != 0);
			if(info.size != 0)
			{
				chiaki_mutex_lock(&decoder->state_mutex);
				decoder->rendered_frames++;
				chiaki_mutex_unlock(&decoder->state_mutex);
			}
			if(info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
			{
				CHIAKI_LOGI(decoder->log, "AMediaCodec reported EOS");
				break;
			}
		}
		else
		{
			chiaki_mutex_lock(&decoder->state_mutex);
			bool shutdown = decoder->shutdown_output;
			chiaki_mutex_unlock(&decoder->state_mutex);
			if(shutdown)
			{
				CHIAKI_LOGI(decoder->log, "Video Decoder Output Thread detected shutdown after reported error");
				break;
			}
		}
	}

	CHIAKI_LOGI(decoder->log, "Video Decoder Output Thread exiting");

	return NULL;
}
