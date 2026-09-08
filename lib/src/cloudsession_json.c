// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// String-oriented host boundary for the shared cloud provisioning flow.

#include <chiaki/cloudsession.h>

#include "cloudcatalog_internal.h"

#include <stdlib.h>
#include <string.h>

static const char *json_string(struct json_object *obj, const char *key)
{
	return cc_json_str(obj, key);
}

static char *result_to_json(const ChiakiCloudProvisionResult *result)
{
	struct json_object *obj = json_object_new_object();
	if(!obj)
		return NULL;
	json_object_object_add(obj, "err", json_object_new_int((int)result->err));
	json_object_object_add(obj, "serverIp", json_object_new_string(result->server_ip));
	json_object_object_add(obj, "serverPort", json_object_new_int(result->server_port));
	json_object_object_add(obj, "handshakeKey", json_object_new_string(result->handshake_key ? result->handshake_key : ""));
	json_object_object_add(obj, "launchSpec", json_object_new_string(result->launch_spec ? result->launch_spec : ""));
	json_object_object_add(obj, "sessionId", json_object_new_string(result->session_id ? result->session_id : ""));
	json_object_object_add(obj, "entitlementId", json_object_new_string(result->entitlement_id));
	json_object_object_add(obj, "platform", json_object_new_string(result->platform));
	json_object_object_add(obj, "psnWrapperType", json_object_new_int(result->psn_wrapper_type));
	json_object_object_add(obj, "mtuIn", json_object_new_int((int)result->mtu_in));
	json_object_object_add(obj, "mtuOut", json_object_new_int((int)result->mtu_out));
	json_object_object_add(obj, "rttUs", json_object_new_int64((int64_t)result->rtt_us));
	json_object_object_add(obj, "errorMessage", json_object_new_string(result->error_message ? result->error_message : ""));
	struct json_object *pings = result->datacenter_pings && *result->datacenter_pings
		? json_tokener_parse(result->datacenter_pings) : NULL;
	if(pings && json_object_get_type(pings) == json_type_array)
		json_object_object_add(obj, "datacenterPings", pings);
	else
	{
		if(pings) json_object_put(pings);
		json_object_object_add(obj, "datacenterPings", json_object_new_array());
	}
	const char *encoded = json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PLAIN);
	char *copy = encoded ? strdup(encoded) : NULL;
	json_object_put(obj);
	return copy;
}

static ChiakiErrorCode cloud_provision_session_json(
	const char *config_json,
	char **result_json,
	ChiakiCloudProvisionProgressCallback progress,
	void *user)
{
	if(result_json) *result_json = NULL;
	if(!config_json || !result_json)
		return CHIAKI_ERR_INVALID_DATA;

	struct json_object *input = json_tokener_parse(config_json);
	if(!input || json_object_get_type(input) != json_type_object)
	{
		if(input) json_object_put(input);
		return CHIAKI_ERR_INVALID_DATA;
	}

	ChiakiCloudProvisionConfig cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.service_type = json_string(input, "serviceType");
	cfg.game_identifier = json_string(input, "gameIdentifier");
	cfg.game_name = json_string(input, "gameName");
	cfg.npsso = json_string(input, "npsso");
	cfg.store_country = json_string(input, "storeCountry");
	cfg.store_lang = json_string(input, "storeLang");
	cfg.owned_entitlement_id = json_string(input, "ownedEntitlementId");
	cfg.owned_platform = json_string(input, "ownedPlatform");
	cfg.catalog_is_foreign = cc_json_bool(input, "catalogIsForeign");
	cfg.skip_account_attr_check = cc_json_bool(input, "skipAccountAttrCheck");
	cfg.forced_datacenter = json_string(input, "forcedDatacenter");
	cfg.prior_datacenters_json = json_string(input, "priorDatacentersJson");
	cfg.game_language = json_string(input, "gameLanguage");
	cfg.resolution = cc_json_int(input, "resolution");
	cfg.bitrate_kbps = cc_json_int(input, "bitrateKbps");
	cfg.progress = progress;
	cfg.user = user;

	ChiakiCloudProvisionResult result;
	ChiakiLog log;
	chiaki_log_init(&log, CHIAKI_LOG_ALL, NULL, NULL);
	ChiakiErrorCode err = chiaki_cloud_provision_session(&cfg, &result, &log);
	result.err = err;
	*result_json = result_to_json(&result);
	chiaki_cloud_provision_result_fini(&result);
	json_object_put(input);
	return *result_json ? err : CHIAKI_ERR_MEMORY;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cloud_provision_session_json(
	const char *config_json, char **result_json)
{
	return cloud_provision_session_json(config_json, result_json, NULL, NULL);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cloud_provision_session_json_with_progress(
	const char *config_json,
	char **result_json,
	ChiakiCloudProvisionProgressCallback progress,
	void *user)
{
	return cloud_provision_session_json(config_json, result_json, progress, user);
}

CHIAKI_EXPORT void chiaki_cloud_provision_json_free(char *json)
{
	free(json);
}
