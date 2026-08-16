// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef DECKSTATION_IOS_VIDEO_DECODER_HPP
#define DECKSTATION_IOS_VIDEO_DECODER_HPP

#include "deckstation-ios-runtime.h"

#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include <chiaki/common.h>
#include <chiaki/log.h>

class DeckStationIOSVideoDecoder
{
public:
	DeckStationIOSVideoDecoder(ChiakiLog *log, ChiakiCodec codec,
		DeckStationIOSVideoCallback callback, void *callback_user);
	~DeckStationIOSVideoDecoder();

	bool Submit(const uint8_t *data, size_t size, int frames_lost,
		bool frame_recovered);
	uint64_t SubmittedFrames() const { return submitted_frames_.load(); }
	uint64_t RenderedFrames() const { return rendered_frames_.load(); }
	uint64_t RejectedFrames() const { return rejected_frames_.load(); }

private:
	struct NalUnit
	{
		const uint8_t *data;
		size_t size;
	};

	static void OutputCallback(void *decompression_output_ref_con,
		void *source_frame_ref_con, OSStatus status,
		VTDecodeInfoFlags info_flags, CVImageBufferRef image_buffer,
		CMTime presentation_time_stamp, CMTime presentation_duration);
	static std::vector<NalUnit> SplitAnnexB(const uint8_t *data, size_t size);
	bool CaptureParameterSets(const std::vector<NalUnit> &units);
	bool EnsureSessionLocked();
	bool CreateFormatDescriptionLocked();
	void ResetSessionLocked();

	ChiakiLog *log_;
	ChiakiCodec codec_;
	DeckStationIOSVideoCallback callback_;
	void *callback_user_;
	std::mutex mutex_;
	CMVideoFormatDescriptionRef format_description_ = nullptr;
	VTDecompressionSessionRef session_ = nullptr;
	std::vector<uint8_t> vps_;
	std::vector<uint8_t> sps_;
	std::vector<uint8_t> pps_;
	bool parameter_sets_dirty_ = false;
	std::atomic<uint64_t> submitted_frames_{0};
	std::atomic<uint64_t> rendered_frames_{0};
	std::atomic<uint64_t> rejected_frames_{0};
};

#endif
