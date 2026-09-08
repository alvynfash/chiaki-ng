// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Small compatibility layer shared by the native cloud-session port. The full
// catalog implementation is intentionally not required by provisioning.

#include "cloudcatalog_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#ifndef CHIAKI_CLOUDCATALOG_HELPERS_EXTERNAL

static struct json_object *json_value(struct json_object *obj, const char *key)
{
	struct json_object *value = NULL;
	if(!obj || !key || !json_object_object_get_ex(obj, key, &value))
		return NULL;
	return value;
}

const char *cc_json_str(struct json_object *obj, const char *key)
{
	struct json_object *value = json_value(obj, key);
	if(!value || json_object_get_type(value) != json_type_string)
		return "";
	const char *result = json_object_get_string(value);
	return result ? result : "";
}

struct json_object *cc_json_obj(struct json_object *obj, const char *key)
{
	struct json_object *value = json_value(obj, key);
	return value && json_object_get_type(value) == json_type_object ? value : NULL;
}

struct json_object *cc_json_arr(struct json_object *obj, const char *key)
{
	struct json_object *value = json_value(obj, key);
	return value && json_object_get_type(value) == json_type_array ? value : NULL;
}

bool cc_json_bool(struct json_object *obj, const char *key)
{
	struct json_object *value = json_value(obj, key);
	return value ? json_object_get_boolean(value) != 0 : false;
}

int cc_json_int(struct json_object *obj, const char *key)
{
	struct json_object *value = json_value(obj, key);
	return value ? json_object_get_int(value) : 0;
}

bool cc_json_has(struct json_object *obj, const char *key)
{
	return json_value(obj, key) != NULL;
}

char *cc_strdup(const char *s)
{
	return s ? strdup(s) : NULL;
}

bool cc_ieq(const char *a, const char *b)
{
	if(!a || !b)
		return a == b;
	return strcasecmp(a, b) == 0;
}

bool cc_contains(const char *haystack, const char *needle)
{
	return haystack && needle && strstr(haystack, needle) != NULL;
}

bool cc_ends_with(const char *s, const char *suffix)
{
	if(!s || !suffix)
		return false;
	size_t slen = strlen(s);
	size_t suffix_len = strlen(suffix);
	return slen >= suffix_len && strcmp(s + slen - suffix_len, suffix) == 0;
}

void cc_json_set_str(struct json_object *obj, const char *key, const char *value)
{
	if(obj && key)
		json_object_object_add(obj, key, json_object_new_string(value ? value : ""));
}

void cc_json_set_bool(struct json_object *obj, const char *key, bool value)
{
	if(obj && key)
		json_object_object_add(obj, key, json_object_new_boolean(value));
}

struct json_object *cc_json_clone(struct json_object *src)
{
	if(!src)
		return NULL;
	const char *json = json_object_to_json_string_ext(src, JSON_C_TO_STRING_PLAIN);
	return json ? json_tokener_parse(json) : NULL;
}

static bool is_americas_classics_region(const char *country)
{
	static const char *const regions[] = {
		"US", "CA", "MX", "BR", "AR", "CL", "CO", "PE", "EC", "BO",
		"PY", "UY", "CR", "GT", "HN", "NI", "PA", "SV", "DO", NULL,
	};
	if(!country || !*country)
		return false;
	char upper[8] = {0};
	for(size_t i = 0; i < sizeof(upper) - 1 && country[i]; ++i)
		upper[i] = (char)toupper((unsigned char)country[i]);
	for(size_t i = 0; regions[i]; ++i)
		if(strcmp(upper, regions[i]) == 0)
			return true;
	return false;
}

const char *cc_classics_store_country(const char *account_country)
{
	return is_americas_classics_region(account_country) ? "US" : "GB";
}

void chiaki_cloud_gaikai_language(const char *locale, char *out, size_t out_sz)
{
	if(!out || out_sz == 0)
		return;
	out[0] = 0;
	const char *value = locale ? locale : "";
	while(*value && isspace((unsigned char)*value))
		++value;
	size_t i = 0;
	for(; value[i] && value[i] != '-' && value[i] != '_' && i < out_sz - 1; ++i)
		out[i] = (char)tolower((unsigned char)value[i]);
	out[i] = 0;
	if(!out[0])
		snprintf(out, out_sz, "en");
}

#endif
