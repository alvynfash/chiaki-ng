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
	// synchronization objects Senkusha expects. Senkusha recognizes this as a
	// cloud probe from the v9 protocol, stream port, and session key. Only PS Now
	// uses the four-byte PSN packet wrapper; PS Cloud probes are unwrapped.
	ChiakiConnectInfo connect;
	memset(&connect, 0, sizeof(connect));
	bool pscloud = service_type && strcmp(service_type, "pscloud") == 0;
	connect.ps5 = pscloud;
	connect.host = public_ip;
	connect.cloud_direct = !pscloud;
	connect.cloud_takion_protocol_version = 9;
	connect.cloud_psn_wrapper_type = pscloud ? 0 : 1;
	connect.stream_port = (uint16_t)port;
	connect.cloud_launch_spec_b64 = session_key;
	connect.audio_video_disabled = CHIAKI_AUDIO_VIDEO_DISABLED;
	connect.video_profile_auto_downgrade = false;

	ChiakiSession session;
	ChiakiErrorCode err = chiaki_session_init(&session, &connect, log);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGW(log, "[PING] session init failed for %s:%d service=%s: %s",
			public_ip, port, pscloud ? "pscloud" : "psnow", chiaki_error_string(err));
		return err;
	}
	if(!session.connect_info.host_addrinfo_selected)
		session.connect_info.host_addrinfo_selected = session.connect_info.host_addrinfos;
	if(!session.connect_info.host_addrinfo_selected)
	{
		CHIAKI_LOGW(log, "[PING] no resolved address for %s:%d service=%s",
			public_ip, port, pscloud ? "pscloud" : "psnow");
		chiaki_session_fini(&session);
		return CHIAKI_ERR_HOST_DOWN;
	}

	CHIAKI_LOGI(log, "[PING] probing %s:%d service=%s protocol=9 wrapper=%s",
		public_ip, port, pscloud ? "pscloud" : "psnow", pscloud ? "none" : "0x01");

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
		else
		{
			CHIAKI_LOGW(log, "[PING] probe failed for %s:%d service=%s: %s rtt=%llu",
				public_ip, port, pscloud ? "pscloud" : "psnow",
				chiaki_error_string(err), (unsigned long long)rtt_us);
		}
	}

	chiaki_session_fini(&session);
	return err;
}
