// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_FEEDBACKSENDER_H
#define CHIAKI_FEEDBACKSENDER_H

#include "controller.h"
#include "takion.h"
#include "thread.h"
#include "common.h"

#define CHIAKI_FEEDBACK_HISTORY_PACKET_BUF_SIZE 0x300
#define CHIAKI_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE 0x40
#define CHIAKI_FEEDBACK_PAD_COUNT 4

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_feedback_sender_t
{
	ChiakiLog *log;
	ChiakiTakion *takion;
	ChiakiThread thread;

	ChiakiSeqNum16 state_seq_num;

	ChiakiSeqNum16 history_seq_num;
	ChiakiFeedbackHistoryBuffer history_buf;
	uint8_t history_packets[CHIAKI_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE][CHIAKI_FEEDBACK_HISTORY_PACKET_BUF_SIZE];
	size_t history_packet_sizes[CHIAKI_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE];
	uint8_t history_packet_event_counts[CHIAKI_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE];
	size_t history_packet_begin;
	size_t history_packet_len;

	bool should_stop;
	ChiakiControllerState controller_state_prev[CHIAKI_FEEDBACK_PAD_COUNT];
	ChiakiControllerState controller_state_history_prev[CHIAKI_FEEDBACK_PAD_COUNT];
	ChiakiControllerState controller_state[CHIAKI_FEEDBACK_PAD_COUNT];
	bool controller_state_changed[CHIAKI_FEEDBACK_PAD_COUNT];
	uint8_t active_pad_mask;
	bool history_dirty;
	ChiakiMutex state_mutex;
	ChiakiCond state_cond;
} ChiakiFeedbackSender;

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_init(ChiakiFeedbackSender *feedback_sender, ChiakiTakion *takion);
CHIAKI_EXPORT void chiaki_feedback_sender_fini(ChiakiFeedbackSender *feedback_sender);
CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_set_controller_state(ChiakiFeedbackSender *feedback_sender, ChiakiControllerState *state);
CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_set_controller_state_for_pad(ChiakiFeedbackSender *feedback_sender, uint8_t controller_id, ChiakiControllerState *state);
CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_set_pad_active(ChiakiFeedbackSender *feedback_sender, uint8_t controller_id, bool active);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_FEEDBACKSENDER_H
