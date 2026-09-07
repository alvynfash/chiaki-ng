// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/cloudsession.h>
#include <munit.h>

static MunitResult test_cloudsession_json_invalid_input(
	const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	char *result = NULL;
	ChiakiErrorCode error = chiaki_cloud_provision_session_json("not-json", &result);
	munit_assert_int(error, ==, CHIAKI_ERR_INVALID_DATA);
	munit_assert_null(result);
	return MUNIT_OK;
}

static MunitResult test_cloudsession_json_missing_credentials(
	const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	char *result = NULL;
	ChiakiErrorCode error = chiaki_cloud_provision_session_json("{}", &result);
	munit_assert_int(error, ==, CHIAKI_ERR_INVALID_DATA);
	munit_assert_not_null(result);
	chiaki_cloud_provision_json_free(result);
	return MUNIT_OK;
}

MunitTest tests_cloudsession[] = {
	{
		"/json/invalid-input",
		test_cloudsession_json_invalid_input,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
	},
	{
		"/json/missing-credentials",
		test_cloudsession_json_missing_credentials,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
	},
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE },
};
