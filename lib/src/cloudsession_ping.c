// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Datacenter ping for cloud provisioning. Senkusha endpoints reject this
// non-Portal BIG probe for both PS Now and PS Cloud after the UDP handshake,
// producing misleading protobuf errors. Use the same bounded regional
// estimate as Pylux for both cloud services instead.

#include "cloudsession_internal.h"

#include <chiaki/senkusha.h>
#include <chiaki/session.h>

#include <stdlib.h>
#include <string.h>

ChiakiErrorCode cc_ping_datacenter(ChiakiLog *log, const char *data_center,
	const char *public_ip, int port, const char *session_key, const char *service_type,
	int64_t *out_rtt_us, uint32_t *out_mtu_in, uint32_t *out_mtu_out)
{
	if(out_rtt_us) *out_rtt_us = -1;
	if(out_mtu_in) *out_mtu_in = 0;
	if(out_mtu_out) *out_mtu_out = 0;
	if(!public_ip || !*public_ip || port <= 0)
		return CHIAKI_ERR_INVALID_DATA;

	bool is_cloud_service = service_type &&
		(strcmp(service_type, "pscloud") == 0 || strcmp(service_type, "psnow") == 0);
	if(is_cloud_service)
	{
		// Senkusha's UDP endpoint is hardware-gated for cloud services. The TCP
		// proxy fallback used by Pylux is not useful here because the stream
		// endpoint is UDP-only, so preserve the service's regional ordering with
		// estimates that stay inside Gaikai's accepted range.
		int offset_ms = 10;
		if(data_center)
		{
			if(strcmp(data_center, "fraa") == 0) offset_ms = 0;
			else if(strcmp(data_center, "frab") == 0) offset_ms = 1;
			else if(strcmp(data_center, "mila") == 0) offset_ms = 4;
			else if(strcmp(data_center, "parb") == 0) offset_ms = 8;
			else if(strcmp(data_center, "lonb") == 0) offset_ms = 14;
		}
		const int estimated_rtt_ms = 16 + offset_ms;
		CHIAKI_LOGI(log, "[PING] %s %s Senkusha is hardware-gated; using estimated RTT %dms",
			data_center && *data_center ? data_center : "unknown",
			strcmp(service_type, "psnow") == 0 ? "PS Now" : "PS Cloud",
			estimated_rtt_ms);
		if(out_rtt_us) *out_rtt_us = (int64_t)estimated_rtt_ms * 1000;
		if(out_mtu_in) *out_mtu_in = 1454;
		if(out_mtu_out) *out_mtu_out = 1254;
		return CHIAKI_ERR_SUCCESS;
	}

	// chiaki_session_init owns address resolution and initializes the internal
	// Non-cloud callers retain the native Senkusha path.
	ChiakiConnectInfo connect;
	memset(&connect, 0, sizeof(connect));
	connect.ps5 = false;
	connect.host = public_ip;
	connect.cloud_direct = true;
	connect.cloud_takion_protocol_version = 9;
	connect.cloud_psn_wrapper_type = 1;
	connect.stream_port = (uint16_t)port;
	connect.cloud_launch_spec_b64 = session_key;
	connect.audio_video_disabled = CHIAKI_AUDIO_VIDEO_DISABLED;
	connect.video_profile_auto_downgrade = false;

	ChiakiSession session;
	ChiakiErrorCode err = chiaki_session_init(&session, &connect, log);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGW(log, "[PING] session init failed for %s:%d: %s",
			public_ip, port, chiaki_error_string(err));
		return err;
	}
	if(!session.connect_info.host_addrinfo_selected)
		session.connect_info.host_addrinfo_selected = session.connect_info.host_addrinfos;
	if(!session.connect_info.host_addrinfo_selected)
	{
		CHIAKI_LOGW(log, "[PING] no resolved address for %s:%d",
			public_ip, port);
		chiaki_session_fini(&session);
		return CHIAKI_ERR_HOST_DOWN;
	}

	CHIAKI_LOGI(log, "[PING] probing %s:%d protocol=9 wrapper=0x01",
		public_ip, port);

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
				public_ip, port, service_type ? service_type : "unknown",
				chiaki_error_string(err), (unsigned long long)rtt_us);
		}
	}

	chiaki_session_fini(&session);
	return err;
}
