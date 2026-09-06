// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "deckstation-ios-video-decoder.hpp"

#include <CoreVideo/CoreVideo.h>

#include <algorithm>
#include <cstring>

#include <chiaki/time.h>

DeckStationIOSVideoDecoder::DeckStationIOSVideoDecoder(
	ChiakiLog *log, ChiakiCodec codec, DeckStationIOSVideoCallback callback,
	void *callback_user)
	: log_(log), codec_(codec), callback_(callback), callback_user_(callback_user)
{
}

DeckStationIOSVideoDecoder::~DeckStationIOSVideoDecoder()
{
	std::lock_guard<std::mutex> lock(mutex_);
	ResetSessionLocked();
}

std::vector<DeckStationIOSVideoDecoder::NalUnit>
DeckStationIOSVideoDecoder::SplitAnnexB(const uint8_t *data, size_t size)
{
	std::vector<NalUnit> result;
	if(!data || size == 0)
		return result;

	auto start_code_size = [data, size](size_t offset) -> size_t {
		if(offset + 3 <= size && data[offset] == 0 && data[offset + 1] == 0
			&& data[offset + 2] == 1)
			return 3;
		if(offset + 4 <= size && data[offset] == 0 && data[offset + 1] == 0
			&& data[offset + 2] == 0 && data[offset + 3] == 1)
			return 4;
		return 0;
	};

	size_t first = 0;
	while(first < size && start_code_size(first) == 0)
		first++;
	if(first == size)
	{
		result.push_back({data, size});
		return result;
	}

	size_t cursor = first;
	while(cursor < size)
	{
		size_t prefix = start_code_size(cursor);
		if(prefix == 0)
		{
			cursor++;
			continue;
		}
		size_t payload_start = cursor + prefix;
		size_t next = payload_start;
		while(next < size && start_code_size(next) == 0)
			next++;
		if(next > payload_start)
			result.push_back({data + payload_start, next - payload_start});
		cursor = next;
	}
	return result;
}

bool DeckStationIOSVideoDecoder::CaptureParameterSets(
	const std::vector<NalUnit> &units)
{
	bool changed = false;
	for(const NalUnit &unit : units)
	{
		if(unit.size == 0)
			continue;
		const int type = chiaki_codec_is_h265(codec_)
			? ((unit.data[0] >> 1) & 0x3f)
			: (unit.data[0] & 0x1f);
		std::vector<uint8_t> *destination = nullptr;
		if(chiaki_codec_is_h265(codec_))
		{
			if(type == 32) destination = &vps_;
			else if(type == 33) destination = &sps_;
			else if(type == 34) destination = &pps_;
		}
		else
		{
			if(type == 7) destination = &sps_;
			else if(type == 8) destination = &pps_;
		}
		if(destination)
		{
			std::vector<uint8_t> next(unit.data, unit.data + unit.size);
			if(*destination != next)
			{
				*destination = std::move(next);
				changed = true;
			}
		}
	}
	if(changed)
		parameter_sets_dirty_ = true;
	return changed;
}

bool DeckStationIOSVideoDecoder::CreateFormatDescriptionLocked()
{
	if(sps_.empty() || pps_.empty()
		|| (chiaki_codec_is_h265(codec_) && vps_.empty()))
		return false;

	OSStatus status = noErr;
	if(chiaki_codec_is_h265(codec_))
	{
		const uint8_t *sets[] = {vps_.data(), sps_.data(), pps_.data()};
		const size_t sizes[] = {vps_.size(), sps_.size(), pps_.size()};
		status = CMVideoFormatDescriptionCreateFromHEVCParameterSets(
			kCFAllocatorDefault, 3, sets, sizes, 4, nullptr,
			&format_description_);
	}
	else
	{
		const uint8_t *sets[] = {sps_.data(), pps_.data()};
		const size_t sizes[] = {sps_.size(), pps_.size()};
		status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
			kCFAllocatorDefault, 2, sets, sizes, 4, &format_description_);
	}
	if(status != noErr)
	{
		CHIAKI_LOGE(log_, "iOS VideoToolbox format creation failed: %d",
			(int)status);
		format_description_ = nullptr;
		return false;
	}
	parameter_sets_dirty_ = false;
	return true;
}

bool DeckStationIOSVideoDecoder::EnsureSessionLocked()
{
	if(session_ && !parameter_sets_dirty_)
		return true;
	if(session_ || format_description_)
		ResetSessionLocked();
	if(!CreateFormatDescriptionLocked())
		return false;

	NSDictionary *attributes = @{
		(__bridge NSString *)kCVPixelBufferPixelFormatTypeKey:
			@(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
		(__bridge NSString *)kCVPixelBufferIOSurfacePropertiesKey: @{},
		(__bridge NSString *)kCVPixelBufferMetalCompatibilityKey: @YES,
	};
	VTDecompressionOutputCallbackRecord callback_record = {
		.decompressionOutputCallback = OutputCallback,
		.decompressionOutputRefCon = this,
	};
	OSStatus status = VTDecompressionSessionCreate(
		kCFAllocatorDefault, format_description_, nullptr,
		(__bridge CFDictionaryRef)attributes, &callback_record, &session_);
	if(status != noErr)
	{
		CHIAKI_LOGE(log_, "iOS VideoToolbox session creation failed: %d",
			(int)status);
		session_ = nullptr;
		return false;
	}
	CHIAKI_LOGI(log_, "iOS VideoToolbox decoder initialized");
	return true;
}

void DeckStationIOSVideoDecoder::ResetSessionLocked()
{
	if(session_)
	{
		VTDecompressionSessionFinishDelayedFrames(session_);
		VTDecompressionSessionWaitForAsynchronousFrames(session_);
		VTDecompressionSessionInvalidate(session_);
		CFRelease(session_);
		session_ = nullptr;
	}
	if(format_description_)
	{
		CFRelease(format_description_);
		format_description_ = nullptr;
	}
}

bool DeckStationIOSVideoDecoder::Submit(const uint8_t *data, size_t size,
	int frames_lost, bool frame_recovered)
{
	(void)frames_lost;
	(void)frame_recovered;
	submitted_frames_++;
	const std::vector<NalUnit> units = SplitAnnexB(data, size);
	if(units.empty())
	{
		rejected_frames_++;
		return false;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	CaptureParameterSets(units);
	bool has_vcl = false;
	for(const NalUnit &unit : units)
	{
		if(unit.size == 0)
			continue;
		const int type = chiaki_codec_is_h265(codec_)
			? ((unit.data[0] >> 1) & 0x3f)
			: (unit.data[0] & 0x1f);
		has_vcl = has_vcl || (chiaki_codec_is_h265(codec_)
			? type <= 31 : (type >= 1 && type <= 5));
	}

	// The profile header is delivered through the same callback as frames. It
	// contains codec configuration only; submitting it as a compressed frame
	// makes VideoToolbox report codecBadDataErr.
	if(!has_vcl)
		return !sps_.empty() || !pps_.empty() || !vps_.empty();
	if(!EnsureSessionLocked())
	{
		// Initial access units commonly contain only parameter sets. They are
		// accepted while VideoToolbox waits for the first decodable frame.
		return !sps_.empty() || !pps_.empty() || !vps_.empty();
	}

	std::vector<uint8_t> avcc;
	avcc.reserve(size + units.size() * 4);
	for(const NalUnit &unit : units)
	{
		const uint32_t length = static_cast<uint32_t>(unit.size);
		avcc.push_back(static_cast<uint8_t>(length >> 24));
		avcc.push_back(static_cast<uint8_t>(length >> 16));
		avcc.push_back(static_cast<uint8_t>(length >> 8));
		avcc.push_back(static_cast<uint8_t>(length));
		avcc.insert(avcc.end(), unit.data, unit.data + unit.size);
	}

	CMBlockBufferRef block = nullptr;
	OSStatus status = CMBlockBufferCreateWithMemoryBlock(
		kCFAllocatorDefault, nullptr, avcc.size(), kCFAllocatorDefault, nullptr,
		0, avcc.size(), 0, &block);
	if(status == noErr)
		status = CMBlockBufferReplaceDataBytes(avcc.data(), block, 0, avcc.size());
	CMSampleBufferRef sample = nullptr;
	if(status == noErr)
	{
		const size_t sample_size = avcc.size();
		status = CMSampleBufferCreateReady(
			kCFAllocatorDefault, block, format_description_, 1, 0, nullptr, 1,
			&sample_size, &sample);
	}
	if(status == noErr)
	{
		VTDecodeFrameFlags flags =
			kVTDecodeFrame_EnableAsynchronousDecompression
			| kVTDecodeFrame_EnableTemporalProcessing;
		status = VTDecompressionSessionDecodeFrame(
			session_, sample, flags, nullptr, nullptr);
	}
	if(sample) CFRelease(sample);
	if(block) CFRelease(block);
	if(status != noErr)
	{
		CHIAKI_LOGW(log_, "iOS VideoToolbox rejected frame: %d", (int)status);
		rejected_frames_++;
		if(status == kVTInvalidSessionErr || status == kVTVideoDecoderMalfunctionErr)
		{
			ResetSessionLocked();
			parameter_sets_dirty_ = true;
		}
		return false;
	}
	return true;
}

void DeckStationIOSVideoDecoder::OutputCallback(
	void *decompression_output_ref_con, void *source_frame_ref_con,
	OSStatus status, VTDecodeInfoFlags info_flags, CVImageBufferRef image_buffer,
	CMTime presentation_time_stamp, CMTime presentation_duration)
{
	(void)source_frame_ref_con;
	(void)info_flags;
	(void)presentation_time_stamp;
	(void)presentation_duration;
	auto *decoder = static_cast<DeckStationIOSVideoDecoder *>(
		decompression_output_ref_con);
	if(!decoder)
		return;
	if(status != noErr || !image_buffer)
	{
		const uint64_t error_index = ++decoder->output_errors_;
		if(error_index <= 12 || error_index % 300 == 0)
			CHIAKI_LOGW(decoder->log_,
				"iOS VideoToolbox output rejected frame %llu: status=%d flags=0x%x image=%d",
				(unsigned long long)error_index, (int)status,
				(unsigned int)info_flags, image_buffer ? 1 : 0);
		return;
	}
	decoder->rendered_frames_++;
	if(!decoder->callback_)
		return;
	CVPixelBufferRetain(image_buffer);
	decoder->callback_(decoder->callback_user_, image_buffer,
		(int32_t)CVPixelBufferGetWidth(image_buffer),
		(int32_t)CVPixelBufferGetHeight(image_buffer),
		(int64_t)chiaki_time_now_monotonic_us());
}
