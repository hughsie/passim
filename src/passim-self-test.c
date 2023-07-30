/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib/gstdio.h>
#include <passim.h>

#include "passim-common.h"

#if 0
static GMainLoop *_test_loop = NULL;
static guint _test_loop_timeout_id = 0;

static gboolean
passim_test_hang_check_cb(gpointer user_data)
{
	g_main_loop_quit(_test_loop);
	_test_loop_timeout_id = 0;
	return G_SOURCE_REMOVE;
}

static void
passim_test_loop_run_with_timeout(guint timeout_ms)
{
	g_assert_cmpint(_test_loop_timeout_id, ==, 0);
	g_assert_null(_test_loop);
	_test_loop = g_main_loop_new(NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add(timeout_ms, passim_test_hang_check_cb, NULL);
	g_main_loop_run(_test_loop);
}

static void
passim_test_loop_quit(void)
{
	if (_test_loop_timeout_id > 0) {
		g_source_remove(_test_loop_timeout_id);
		_test_loop_timeout_id = 0;
	}
	if (_test_loop != NULL) {
		g_main_loop_quit(_test_loop);
		g_main_loop_unref(_test_loop);
		_test_loop = NULL;
	}
}
#endif

static void
passim_common_func(void)
{
	const gchar *xargs_fn = "/var/tmp/passim/test.conf";
	gboolean ret;
	guint32 value_u32;
	g_autofree gchar *boot_time = NULL;
	g_autofree gchar *value_str1 = NULL;
	g_autofree gchar *value_str2 = NULL;
	g_autoptr(GError) error = NULL;

	/* ensure we got *something* */
	boot_time = passim_get_boot_time();
	g_assert_cmpstr(boot_time, !=, NULL);

	/* create dir for next step */
	ret = passim_mkdir("/var/tmp/passim", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = passim_mkdir("/var/tmp/passim", &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check xargs */
	g_unlink(xargs_fn);
	ret = g_file_set_contents(xargs_fn, "[daemon]", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = passim_xattr_set_uint32(xargs_fn, "user.test_u32", 123, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = passim_xattr_set_string(xargs_fn, "user.test_str", "hey", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	value_u32 = passim_xattr_get_uint32(xargs_fn, "user.test_u32", 456, &error);
	g_assert_no_error(error);
	g_assert_cmpint(value_u32, ==, 123);
	value_u32 = passim_xattr_get_uint32(xargs_fn, "user.test_MISSING", 456, &error);
	g_assert_no_error(error);
	g_assert_cmpint(value_u32, ==, 456);
	value_str1 = passim_xattr_get_string(xargs_fn, "user.test_str", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(value_str1, ==, "hey");
	value_str2 = passim_xattr_get_string(xargs_fn, "user.test_MISSING", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(value_str2, ==, "");

	/* check HTTP status codes */
	g_assert_cmpstr(passim_http_code_to_string(404), ==, "Not Found");
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	g_test_add_func("/passim/common", passim_common_func);
	return g_test_run();
}
