// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef DECKSTATION_IOS_RUNTIME_H
#define DECKSTATION_IOS_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*DeckStationIOSVideoCallback)(void *user, void *pixel_buffer,
	int32_t width, int32_t height, int64_t monotonic_time_us);
typedef void (*DeckStationIOSAudioCallback)(void *user, const int16_t *samples,
	uint32_t sample_count, uint32_t channels, uint32_t sample_rate);
typedef void (*DeckStationIOSEventCallback)(void *user, const char *type,
	const char *detail, int64_t value0, int64_t value1);
typedef void (*DeckStationIOSStatsCallback)(void *user, const double *values,
	int32_t value_count);

void deckstation_ios_runtime_set_callbacks(void *user,
	DeckStationIOSVideoCallback video_callback,
	DeckStationIOSAudioCallback audio_callback,
	DeckStationIOSEventCallback event_callback,
	DeckStationIOSStatsCallback stats_callback);
int32_t deckstation_ios_runtime_start_json(const char *json);
int32_t deckstation_ios_runtime_stop(void);
int32_t deckstation_ios_runtime_stats(uint64_t *values, int32_t value_count);
int32_t deckstation_ios_runtime_send_controller_state(
	int32_t buttons, int32_t l2_state, int32_t r2_state,
	int32_t left_x, int32_t left_y, int32_t right_x, int32_t right_y,
	int32_t touch0_id, int32_t touch0_x, int32_t touch0_y,
	int32_t touch1_id, int32_t touch1_x, int32_t touch1_y);
const char *deckstation_ios_runtime_set_cloud_controller_variant(const char *value);

#ifdef __cplusplus
}
#endif

#endif
