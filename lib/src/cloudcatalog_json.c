// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// String-oriented host boundary for the shared unified catalog flow.

#include <chiaki/cloudcatalog.h>

#include "cloudcatalog_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool array_contains_ci(struct json_object *array, const char *value)
{
	if(!array || json_object_get_type(array) != json_type_array || !value)
		return false;
	size_t n = json_object_array_length(array);
	for(size_t i = 0; i < n; i++)
	{
		const char *candidate = json_object_get_string(json_object_array_get_idx(array, i));
		if(candidate && strcasecmp(candidate, value) == 0)
			return true;
	}
	return false;
}

static void array_add_unique_ci(struct json_object *array, const char *value)
{
	if(value && *value && !array_contains_ci(array, value))
		json_object_array_add(array, json_object_new_string(value));
}

static const char *query_game_name(struct json_object *game)
{
	return cc_json_str(game, "name");
}

static bool contains_ci(const char *haystack, const char *needle)
{
	if(!needle || !*needle)
		return true;
	if(!haystack)
		return false;
	for(const char *start = haystack; *start; start++)
	{
		const char *left = start;
		const char *right = needle;
		while(*left && *right
			&& tolower((unsigned char)*left) == tolower((unsigned char)*right))
		{
			left++;
			right++;
		}
		if(!*right)
			return true;
	}
	return false;
}

static int sort_name_az(const void *a, const void *b)
{
	struct json_object *left = *(struct json_object *const *)a;
	struct json_object *right = *(struct json_object *const *)b;
	return strcasecmp(query_game_name(left), query_game_name(right));
}

static int sort_name_za(const void *a, const void *b)
{
	return -sort_name_az(a, b);
}

static int sort_playable_first(const void *a, const void *b)
{
	struct json_object *left = *(struct json_object *const *)a;
	struct json_object *right = *(struct json_object *const *)b;
	bool left_playable = strcasecmp(cc_json_str(left, "category"), "purchaseable") != 0;
	bool right_playable = strcasecmp(cc_json_str(right, "category"), "purchaseable") != 0;
	if(left_playable != right_playable)
		return left_playable ? -1 : 1;
	return sort_name_az(a, b);
}

static bool game_has_genre(struct json_object *game, struct json_object *selected)
{
	if(!selected || json_object_array_length(selected) == 0)
		return true;
	const char *keys[] = { "genres", "genre", NULL };
	for(size_t key = 0; keys[key]; key++)
	{
		struct json_object *value = NULL;
		if(!json_object_object_get_ex(game, keys[key], &value) || !value)
			continue;
		if(json_object_get_type(value) == json_type_string
			&& array_contains_ci(selected, json_object_get_string(value)))
			return true;
		if(json_object_get_type(value) == json_type_array)
		{
			size_t n = json_object_array_length(value);
			for(size_t i = 0; i < n; i++)
				if(array_contains_ci(selected,
					json_object_get_string(json_object_array_get_idx(value, i))))
					return true;
		}
	}
	return false;
}

static bool game_is_favorite(struct json_object *game, struct json_object *favorite_keys)
{
	const char *product_id = cc_json_str(game, "productId");
	if(!*product_id || !favorite_keys)
		return false;
	size_t n = json_object_array_length(favorite_keys);
	for(size_t i = 0; i < n; i++)
	{
		const char *key = json_object_get_string(json_object_array_get_idx(favorite_keys, i));
		if(!key)
			continue;
		if(strcasecmp(key, product_id) == 0)
			return true;
		const char *separator = strchr(key, ':');
		if(separator && separator - key == 7 && strncasecmp(key, "product", 7) == 0
			&& strcasecmp(separator + 1, product_id) == 0)
			return true;
	}
	return false;
}

static const char *game_catalog_name(struct json_object *game)
{
	if(strcasecmp(cc_json_str(game, "serviceType"), "psnow") == 0)
		return "";
	return cc_json_bool(game, "plusCatalog") ? "plusGames" : "allPs5";
}

static bool game_matches_scope(struct json_object *game, struct json_object *input)
{
	const char *scope = cc_json_str(input, "scope");
	if(strcasecmp(scope, "owned") == 0)
		return cc_json_bool(game, "isOwned");
	if(strcasecmp(scope, "psnow") == 0)
		return strcasecmp(cc_json_str(game, "serviceType"), "psnow") == 0;
	if(strcasecmp(scope, "psplus") == 0)
		return strcasecmp(cc_json_str(game, "serviceType"), "psnow") != 0
			&& cc_json_bool(game, "plusCatalog");
	return true;
}

static bool game_matches_query(struct json_object *game, struct json_object *input)
{
	const char *search_query = cc_json_str(input, "searchQuery");
	if(*search_query && !contains_ci(query_game_name(game), search_query))
		return false;

	struct json_object *platforms = cc_json_arr(input, "platforms");
	if(platforms && json_object_array_length(platforms) > 0
		&& !array_contains_ci(platforms, cc_json_str(game, "platform")))
		return false;

	struct json_object *genres = cc_json_arr(input, "genres");
	if(!game_has_genre(game, genres))
		return false;

	struct json_object *catalogs = cc_json_arr(input, "catalogs");
	if(catalogs && json_object_array_length(catalogs) > 0
		&& !array_contains_ci(catalogs, game_catalog_name(game)))
		return false;

	struct json_object *types = cc_json_arr(input, "types");
	if(types && json_object_array_length(types) > 0)
	{
		const char *category = cc_json_str(game, "category");
		bool matches = array_contains_ci(types, category);
		if(!matches && strcasecmp(category, "purchaseable") == 0)
			matches = array_contains_ci(types, "inStore");
		if(!matches && array_contains_ci(types, "favorite"))
			matches = game_is_favorite(game, cc_json_arr(input, "favoriteKeys"));
		if(!matches)
			return false;
	}
	return true;
}

static void collect_genres(struct json_object *game, struct json_object *genres)
{
	const char *keys[] = { "genres", "genre", NULL };
	for(size_t key = 0; keys[key]; key++)
	{
		struct json_object *value = NULL;
		if(!json_object_object_get_ex(game, keys[key], &value) || !value)
			continue;
		if(json_object_get_type(value) == json_type_string)
			array_add_unique_ci(genres, json_object_get_string(value));
		else if(json_object_get_type(value) == json_type_array)
		{
			size_t n = json_object_array_length(value);
			for(size_t i = 0; i < n; i++)
				array_add_unique_ci(genres,
					json_object_get_string(json_object_array_get_idx(value, i)));
		}
	}
}

// Pylux's CloudPlayView filtering and sorting, moved to the shared native JSON
// boundary so paged clients never need the complete catalog in their UI runtime.
static void apply_catalog_query(struct json_object *catalog, struct json_object *input)
{
	struct json_object *games = cc_json_arr(catalog, "games");
	if(!games)
		return;

	struct json_object *available_platforms = json_object_new_array();
	struct json_object *available_genres = json_object_new_array();
	struct json_object *available_catalogs = json_object_new_array();
	struct json_object *filtered = json_object_new_array();
	size_t count = json_object_array_length(games);
	for(size_t i = 0; i < count; i++)
	{
		struct json_object *game = json_object_array_get_idx(games, i);
		if(!game_matches_scope(game, input))
			continue;
		array_add_unique_ci(available_platforms, cc_json_str(game, "platform"));
		collect_genres(game, available_genres);
		array_add_unique_ci(available_catalogs, game_catalog_name(game));
		if(game_matches_query(game, input))
			json_object_array_add(filtered, json_object_get(game));
	}

	const char *sort_value = cc_json_str(input, "sortValue");
	if(strcasecmp(sort_value, "za") == 0)
		json_object_array_sort(filtered, sort_name_za);
	else if(strcasecmp(sort_value, "az") == 0)
		json_object_array_sort(filtered, sort_name_az);
	else
		json_object_array_sort(filtered, sort_playable_first);

	int total = (int)json_object_array_length(filtered);
	int skip = cc_json_int(input, "skip");
	if(skip < 0)
		skip = 0;
	if(skip > total)
		skip = total;
	int take = total - skip;
	struct json_object *take_value = NULL;
	if(json_object_object_get_ex(input, "take", &take_value)
		&& take_value && json_object_get_type(take_value) != json_type_null)
	{
		take = json_object_get_int(take_value);
		if(take < 0)
			take = 0;
	}
	int end = skip + take;
	if(end > total)
		end = total;

	struct json_object *page = json_object_new_array();
	for(int i = skip; i < end; i++)
		json_object_array_add(page,
			json_object_get(json_object_array_get_idx(filtered, (size_t)i)));

	json_object_object_del(catalog, "games");
	json_object_object_add(catalog, "games", page);
	json_object_object_del(catalog, "total");
	json_object_object_add(catalog, "total", json_object_new_int(total));
	json_object_object_add(catalog, "availablePlatforms", available_platforms);
	json_object_object_add(catalog, "availableGenres", available_genres);
	json_object_object_add(catalog, "availableCatalogs", available_catalogs);
	json_object_put(filtered);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cloudcatalog_fetch_unified_json(
	const char *config_json, char **result_json)
{
	if(result_json)
		*result_json = NULL;
	if(!config_json || !result_json)
		return CHIAKI_ERR_INVALID_DATA;

	struct json_object *input = json_tokener_parse(config_json);
	if(!input || json_object_get_type(input) != json_type_object)
	{
		if(input)
			json_object_put(input);
		return CHIAKI_ERR_INVALID_DATA;
	}

	ChiakiCloudCatalogConfig config;
	memset(&config, 0, sizeof(config));
	config.npsso = cc_json_str(input, "npsso");
	config.locale = cc_json_str(input, "locale");
	config.cache_dir = cc_json_str(input, "cacheDir");
	config.force_refresh = cc_json_bool(input, "forceRefresh");
	config.scope = cc_json_str(input, "scope");

	ChiakiLog log;
	chiaki_log_init(&log, CHIAKI_LOG_ALL, NULL, NULL);
	ChiakiCloudCatalogResult result;
	memset(&result, 0, sizeof(result));
	const ChiakiErrorCode err =
		chiaki_cloudcatalog_fetch_unified(&config, &result, &log);

	struct json_object *output = json_object_new_object();
	if(!output)
	{
		chiaki_cloudcatalog_result_fini(&result);
		json_object_put(input);
		return CHIAKI_ERR_MEMORY;
	}
	json_object_object_add(output, "err", json_object_new_int((int)err));
	json_object_object_add(output, "errorMessage",
		json_object_new_string(result.error_message ? result.error_message : ""));
	struct json_object *catalog = result.json
		? json_tokener_parse(result.json) : NULL;
	if(catalog && json_object_get_type(catalog) == json_type_object)
	{
		apply_catalog_query(catalog, input);
		json_object_object_add(output, "catalog", catalog);
	}
	else
	{
		if(catalog)
			json_object_put(catalog);
		json_object_object_add(output, "catalog", json_object_new_null());
	}

	const char *encoded =
		json_object_to_json_string_ext(output, JSON_C_TO_STRING_PLAIN);
	*result_json = encoded ? strdup(encoded) : NULL;
	json_object_put(output);
	chiaki_cloudcatalog_result_fini(&result);
	json_object_put(input);
	return *result_json ? err : CHIAKI_ERR_MEMORY;
}

CHIAKI_EXPORT void chiaki_cloudcatalog_json_free(char *json)
{
	free(json);
}
