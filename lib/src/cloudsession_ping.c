// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Datacenter ping for cloud provisioning.  The probe uses the existing
// cloud-direct Senkusha implementation so the result includes the negotiated
// RTT and MTU values expected by the Gaikai allocation API.

#include "cloudsession_internal.h"

#include <chiaki/senkusha.h>
#include <chiaki/session.h>

#include <stdlib.h>
#include <string.h>

ChiakiErrorCode cc_ping_datacenter(ChiakiLog *log, const char *public_ip, int port,
	const char *session_key, const char *service_type,
	int64_t *out_rtt_us, uint32_t *out_mtu_in, uint32_t *out_mtu_out)
{
	if(out_rtt_us) *out_rtt_us = -1;
	if(out_mtu_in) *out_mtu_in = 0;
	if(out_mtu_out) *out_mtu_out = 0;
	if(!public_ip || !*public_ip || port <= 0)
		return CHIAKI_ERR_INVALID_DATA;

	// chiaki_session_init owns address resolution and initializes the internal
	// synchronization objects Senkusha expects.  Cloud-direct mode selects the
	// resolved address before the probe and makes Senkusha use stream_port.
	ChiakiConnectInfo connect;
	memset(&connect, 0, sizeof(connect));
	connect.ps5 = service_type && strcmp(service_type, "pscloud") == 0;
	connect.host = public_ip;
	connect.cloud_direct = true;
	connect.cloud_takion_protocol_version = 9;
	connect.cloud_psn_wrapper_type = connect.ps5 ? 0 : 1;
	connect.stream_port = (uint16_t)port;
	connect.cloud_launch_spec_b64 = session_key;
	connect.audio_video_disabled = CHIAKI_AUDIO_VIDEO_DISABLED;
	connect.video_profile_auto_downgrade = false;

	ChiakiSession session;
	ChiakiErrorCode err = chiaki_session_init(&session, &connect, log);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	ChiakiSenkusha senkusha;
	err = chiaki_senkusha_init(&senkusha, &session);
	if(err == CHIAKI_ERR_SUCCESS)
	{
		uint32_t mtu_in = 0, mtu_out = 0;
		uint64_t rtt_us = 0;
		err = chiaki_senkusha_run(&senkusha, &mtu_in, &mtu_out, &rtt_us, NULL);
		chiaki_senkusha_fini(&senkusha);
		if(err == CHIAKI_ERR_SUCCESS && rtt_us != 0)
		{
			if(out_rtt_us) *out_rtt_us = (int64_t)rtt_us;
			if(out_mtu_in) *out_mtu_in = mtu_in > 0 ? mtu_in : 1454;
			if(out_mtu_out) *out_mtu_out = mtu_out > 0 ? mtu_out : 1254;
		}
	}

	chiaki_session_fini(&session);
	return err;
}
