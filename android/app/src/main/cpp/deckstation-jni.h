// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef DECKSTATION_JNI_H
#define DECKSTATION_JNI_H

#include <jni.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int deckstation_jni_register(JNIEnv *env);

// Typed native entry points shared by JNI and DeckStation's input router.
// Both preserve the runtime session mutex and perform no Java round trip.
int32_t deckstation_android_runtime_send_controller_state_for_pad(
    uint8_t controller_id,
    int32_t buttons, int32_t l2_state, int32_t r2_state,
    int32_t left_x, int32_t left_y, int32_t right_x, int32_t right_y,
    int32_t touch0_id, int32_t touch0_x, int32_t touch0_y,
    int32_t touch1_id, int32_t touch1_x, int32_t touch1_y);
int32_t deckstation_android_runtime_set_player_pad_active(
    uint8_t controller_id, uint8_t active, const char *name);

#ifdef __cplusplus
}
#endif

#endif
