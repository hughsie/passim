/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>
#include <passim.h>
#include <string.h>

#include "passim-common.h"
#include "passim-gnutls.h"

static void
passim_common_func(void)
{
	gboolean ret;
	guint16 port;
	guint32 value_u32;
	g_autofree gchar *boot_time = NULL;
	g_autofree gchar *value_str1 = NULL;
	g_autofree gchar *value_str2 = NULL;
	g_autofree gchar *xargs_fn = NULL;
	g_autofree gchar *xargs_path = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GKeyFile) kf = g_key_file_new();

	/* port validation */
	g_key_file_set_integer(kf, "daemon", "Port", 27500);
	port = passim_config_get_port(kf);
	g_assert_cmpint(port, ==, 27500);

	/* out-of-range port should fall back to default */
	g_key_file_set_integer(kf, "daemon", "Port", 70000);
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "invalid port*");
	port = passim_config_get_port(kf);
	g_test_assert_expected_messages();
	g_assert_cmpint(port, ==, 27500);

	/* negative port should fall back to default */
	g_key_file_set_integer(kf, "daemon", "Port", -1);
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "invalid port*");
	port = passim_config_get_port(kf);
	g_test_assert_expected_messages();
	g_assert_cmpint(port, ==, 27500);

	/* zero port should fall back to default */
	g_key_file_set_integer(kf, "daemon", "Port", 0);
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "invalid port*");
	port = passim_config_get_port(kf);
	g_test_assert_expected_messages();
	g_assert_cmpint(port, ==, 27500);

	/* valid sha256 hash */
	g_assert_true(passim_is_valid_sha256(
	    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

	/* wrong length */
	g_assert_false(passim_is_valid_sha256("e3b0c44298fc1c14"));

	/* non-hex character in hash */
	g_assert_false(passim_is_valid_sha256(
	    "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"));

	/* NULL hash */
	g_assert_false(passim_is_valid_sha256(NULL));

	/* empty string hash */
	g_assert_false(passim_is_valid_sha256(""));

	/* ensure we got *something* */
	boot_time = passim_get_boot_time();
	g_assert_cmpstr(boot_time, !=, NULL);

	/* create dir for next step */
	xargs_fn = g_test_build_filename(G_TEST_BUILT, "tests", "test.conf", NULL);
	xargs_path = g_path_get_dirname(xargs_fn);
	ret = passim_mkdir(xargs_path, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = passim_mkdir(xargs_path, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check xargs */
	(void)g_unlink(xargs_fn);
	ret = g_file_set_contents(xargs_fn, "[daemon]", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = passim_xattr_set_uint32(xargs_fn, "user.test_u32", 123, &error);
	if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no xattr support");
		return;
	}
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
}

static void
passim_common_status_func(void)
{
	g_assert_cmpstr(passim_status_to_string(PASSIM_STATUS_STARTING), ==, "starting");
	g_assert_cmpstr(passim_status_to_string(PASSIM_STATUS_LOADING), ==, "loading");
	g_assert_cmpstr(passim_status_to_string(PASSIM_STATUS_DISABLED_METERED),
			==,
			"disabled-metered");
	g_assert_cmpstr(passim_status_to_string(PASSIM_STATUS_RUNNING), ==, "running");
	g_assert_null(passim_status_to_string(PASSIM_STATUS_UNKNOWN));
}

static void
passim_common_config_func(void)
{
	gdouble cost;
	g_autofree gchar *path = NULL;
	g_autoptr(GKeyFile) kf = g_key_file_new();
	g_autoptr(GKeyFile) kf2 = g_key_file_new();

	/* ipv6 defaults to false when not set */
	g_assert_false(passim_config_get_ipv6(kf));

	/* ipv6 set to true */
	g_key_file_set_boolean(kf, "daemon", "IPv6", TRUE);
	g_assert_true(passim_config_get_ipv6(kf));

	/* max item size */
	g_key_file_set_uint64(kf, "daemon", "MaxItemSize", 5000);
	g_assert_cmpuint(passim_config_get_max_item_size(kf), ==, 5000);

	/* cooldown */
	g_key_file_set_integer(kf, "daemon", "Cooldown", 7200);
	g_assert_cmpint(passim_config_get_cooldown(kf), ==, 7200);

	/* carbon cost -- default when not set */
	cost = passim_config_get_carbon_cost(kf2);
	g_assert_cmpfloat(cost, >, 0.02);
	g_assert_cmpfloat(cost, <, 0.03);

	/* carbon cost -- custom value */
	g_key_file_set_double(kf, "daemon", "CarbonCost", 0.05);
	g_assert_cmpfloat(passim_config_get_carbon_cost(kf), ==, 0.05);

	/* path */
	g_key_file_set_string(kf, "daemon", "Path", "/tmp/test");
	path = passim_config_get_path(kf);
	g_assert_cmpstr(path, ==, "/tmp/test");
}

static void
passim_common_mkdir_parent_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *parent = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_BUILT, "tests", "subdir", "file.txt", NULL);
	ret = passim_mkdir_parent(fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	parent = g_path_get_dirname(fn);
	g_assert_true(g_file_test(parent, G_FILE_TEST_IS_DIR));
}

static void
passim_common_file_contents_func(void)
{
	gboolean ret;
	const gchar *data;
	gsize size;
	g_autofree gchar *fn = NULL;
	g_autoptr(GBytes) bytes_in = NULL;
	g_autoptr(GBytes) bytes_out = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_BUILT, "tests", "file-contents-test.bin", NULL);
	ret = passim_mkdir_parent(fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* write */
	bytes_in = g_bytes_new_static("hello world", 11);
	ret = passim_file_set_contents(fn, bytes_in, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* read back */
	bytes_out = passim_file_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bytes_out);
	data = g_bytes_get_data(bytes_out, &size);
	g_assert_cmpuint(size, ==, 11);
	g_assert_cmpmem(data, size, "hello world", 11);

	(void)g_unlink(fn);
}

static void
passim_common_load_input_stream_func(void)
{
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GBytes) bytes2 = NULL;
	g_autoptr(GBytes) bytes3 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error2 = NULL;
	g_autoptr(GError) error3 = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GInputStream) stream2 = NULL;
	g_autoptr(GInputStream) stream3 = NULL;

	/* normal read */
	stream = g_memory_input_stream_new_from_data("test data", 9, NULL);
	bytes = passim_load_input_stream(stream, 1024, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bytes);
	g_assert_cmpuint(g_bytes_get_size(bytes), ==, 9);

	/* zero count should fail */
	stream2 = g_memory_input_stream_new_from_data("test", 4, NULL);
	bytes2 = passim_load_input_stream(stream2, 0, &error2);
	g_assert_error(error2, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_assert_null(bytes2);

	/* stream larger than count should fail */
	stream3 = g_memory_input_stream_new_from_data("abcdefghij", 10, NULL);
	bytes3 = passim_load_input_stream(stream3, 5, &error3);
	g_assert_error(error3, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
	g_assert_null(bytes3);
}

static void
passim_item_func(void)
{
	g_autoptr(PassimItem) item = passim_item_new();

	/* defaults */
	g_assert_null(passim_item_get_hash(item));
	g_assert_null(passim_item_get_basename(item));
	g_assert_null(passim_item_get_cmdline(item));
	g_assert_cmpuint(passim_item_get_flags(item), ==, PASSIM_ITEM_FLAG_NONE);
	g_assert_null(passim_item_get_file(item));
	g_assert_null(passim_item_get_bytes(item));
	g_assert_null(passim_item_get_stream(item));
	g_assert_null(passim_item_get_ctime(item));
	g_assert_cmpuint(passim_item_get_share_count(item), ==, 0);

	/* default max_age is 24h */
	g_assert_cmpuint(passim_item_get_max_age(item), ==, 24 * 60 * 60);

	/* default share_limit is 5 */
	g_assert_cmpuint(passim_item_get_share_limit(item), ==, 5);

	/* size defaults to 0 */
	g_assert_cmpuint(passim_item_get_size(item), ==, 0);

	/* set and get hash */
	passim_item_set_hash(item, "abc123");
	g_assert_cmpstr(passim_item_get_hash(item), ==, "abc123");

	/* set same value -- no-op */
	passim_item_set_hash(item, "abc123");
	g_assert_cmpstr(passim_item_get_hash(item), ==, "abc123");

	/* set different value */
	passim_item_set_hash(item, "def456");
	g_assert_cmpstr(passim_item_get_hash(item), ==, "def456");

	/* basename */
	passim_item_set_basename(item, "firmware.bin");
	g_assert_cmpstr(passim_item_get_basename(item), ==, "firmware.bin");

	/* set same basename -- no-op */
	passim_item_set_basename(item, "firmware.bin");
	g_assert_cmpstr(passim_item_get_basename(item), ==, "firmware.bin");

	/* cmdline */
	passim_item_set_cmdline(item, "/usr/bin/fwupd");
	g_assert_cmpstr(passim_item_get_cmdline(item), ==, "/usr/bin/fwupd");

	/* set same cmdline -- no-op */
	passim_item_set_cmdline(item, "/usr/bin/fwupd");
	g_assert_cmpstr(passim_item_get_cmdline(item), ==, "/usr/bin/fwupd");

	/* max_age */
	passim_item_set_max_age(item, 3600);
	g_assert_cmpuint(passim_item_get_max_age(item), ==, 3600);

	/* share_limit */
	passim_item_set_share_limit(item, 10);
	g_assert_cmpuint(passim_item_get_share_limit(item), ==, 10);

	/* share_count */
	passim_item_set_share_count(item, 3);
	g_assert_cmpuint(passim_item_get_share_count(item), ==, 3);

	/* size */
	passim_item_set_size(item, 1024);
	g_assert_cmpuint(passim_item_get_size(item), ==, 1024);
}

static void
passim_item_flags_func(void)
{
	g_autofree gchar *str_multiple = NULL;
	g_autofree gchar *str_none = NULL;
	g_autofree gchar *str_single = NULL;
	g_autoptr(PassimItem) item = passim_item_new();

	/* initial state */
	g_assert_cmpuint(passim_item_get_flags(item), ==, PASSIM_ITEM_FLAG_NONE);
	g_assert_false(passim_item_has_flag(item, PASSIM_ITEM_FLAG_DISABLED));
	g_assert_false(passim_item_has_flag(item, PASSIM_ITEM_FLAG_NEXT_REBOOT));

	/* add flag */
	passim_item_add_flag(item, PASSIM_ITEM_FLAG_DISABLED);
	g_assert_true(passim_item_has_flag(item, PASSIM_ITEM_FLAG_DISABLED));
	g_assert_cmpuint(passim_item_get_flags(item), ==, PASSIM_ITEM_FLAG_DISABLED);

	/* add same flag again -- no-op */
	passim_item_add_flag(item, PASSIM_ITEM_FLAG_DISABLED);
	g_assert_true(passim_item_has_flag(item, PASSIM_ITEM_FLAG_DISABLED));

	/* add another flag */
	passim_item_add_flag(item, PASSIM_ITEM_FLAG_NEXT_REBOOT);
	g_assert_true(passim_item_has_flag(item, PASSIM_ITEM_FLAG_DISABLED));
	g_assert_true(passim_item_has_flag(item, PASSIM_ITEM_FLAG_NEXT_REBOOT));

	/* add NONE flag -- no-op */
	passim_item_add_flag(item, PASSIM_ITEM_FLAG_NONE);
	g_assert_true(passim_item_has_flag(item, PASSIM_ITEM_FLAG_DISABLED));

	/* remove flag */
	passim_item_remove_flag(item, PASSIM_ITEM_FLAG_DISABLED);
	g_assert_false(passim_item_has_flag(item, PASSIM_ITEM_FLAG_DISABLED));
	g_assert_true(passim_item_has_flag(item, PASSIM_ITEM_FLAG_NEXT_REBOOT));

	/* remove flag that's not set -- no-op */
	passim_item_remove_flag(item, PASSIM_ITEM_FLAG_DISABLED);
	g_assert_false(passim_item_has_flag(item, PASSIM_ITEM_FLAG_DISABLED));

	/* remove NONE flag -- no-op */
	passim_item_remove_flag(item, PASSIM_ITEM_FLAG_NONE);

	/* set flags directly */
	passim_item_set_flags(item, PASSIM_ITEM_FLAG_DISABLED | PASSIM_ITEM_FLAG_NEXT_REBOOT);
	g_assert_cmpuint(passim_item_get_flags(item),
			 ==,
			 PASSIM_ITEM_FLAG_DISABLED | PASSIM_ITEM_FLAG_NEXT_REBOOT);

	/* set same flags -- no-op */
	passim_item_set_flags(item, PASSIM_ITEM_FLAG_DISABLED | PASSIM_ITEM_FLAG_NEXT_REBOOT);
	g_assert_cmpuint(passim_item_get_flags(item),
			 ==,
			 PASSIM_ITEM_FLAG_DISABLED | PASSIM_ITEM_FLAG_NEXT_REBOOT);

	/* flags as string -- multiple */
	str_multiple = passim_item_get_flags_as_string(item);
	g_assert_cmpstr(str_multiple, ==, "disabled,next-reboot");

	/* flags as string -- none */
	passim_item_set_flags(item, PASSIM_ITEM_FLAG_NONE);
	str_none = passim_item_get_flags_as_string(item);
	g_assert_cmpstr(str_none, ==, "none");

	/* flags as string -- single */
	passim_item_set_flags(item, PASSIM_ITEM_FLAG_NEXT_REBOOT);
	str_single = passim_item_get_flags_as_string(item);
	g_assert_cmpstr(str_single, ==, "next-reboot");
}

static void
passim_item_flag_string_func(void)
{
	/* to string */
	g_assert_cmpstr(passim_item_flag_to_string(PASSIM_ITEM_FLAG_NONE), ==, "none");
	g_assert_cmpstr(passim_item_flag_to_string(PASSIM_ITEM_FLAG_DISABLED), ==, "disabled");
	g_assert_cmpstr(passim_item_flag_to_string(PASSIM_ITEM_FLAG_NEXT_REBOOT),
			==,
			"next-reboot");
	g_assert_null(passim_item_flag_to_string(PASSIM_ITEM_FLAG_UNKNOWN));

	/* from string */
	g_assert_cmpuint(passim_item_flag_from_string("none"), ==, PASSIM_ITEM_FLAG_NONE);
	g_assert_cmpuint(passim_item_flag_from_string("disabled"), ==, PASSIM_ITEM_FLAG_DISABLED);
	g_assert_cmpuint(passim_item_flag_from_string("next-reboot"),
			 ==,
			 PASSIM_ITEM_FLAG_NEXT_REBOOT);
	g_assert_cmpuint(passim_item_flag_from_string("invalid"), ==, PASSIM_ITEM_FLAG_UNKNOWN);
	g_assert_cmpuint(passim_item_flag_from_string(NULL), ==, PASSIM_ITEM_FLAG_UNKNOWN);
}

static void
passim_item_bytes_func(void)
{
	g_autoptr(PassimItem) item = passim_item_new();
	g_autoptr(GBytes) bytes = g_bytes_new_static("hello", 5);

	/* setting bytes should auto-set hash and size */
	g_assert_null(passim_item_get_hash(item));
	g_assert_cmpuint(passim_item_get_size(item), ==, 0);

	passim_item_set_bytes(item, bytes);
	g_assert_nonnull(passim_item_get_hash(item));
	g_assert_cmpuint(passim_item_get_size(item), ==, 5);
	g_assert_nonnull(passim_item_get_bytes(item));

	/* verify it's a valid sha256 hash */
	g_assert_true(passim_is_valid_sha256(passim_item_get_hash(item)));

	/* set same bytes -- no-op */
	passim_item_set_bytes(item, bytes);
	g_assert_cmpuint(passim_item_get_size(item), ==, 5);

	/* clear bytes */
	passim_item_set_bytes(item, NULL);
	g_assert_null(passim_item_get_bytes(item));
}

static void
passim_item_file_func(void)
{
	g_autoptr(PassimItem) item = passim_item_new();
	g_autoptr(GFile) file = g_file_new_for_path("/tmp/firmware.bin");

	/* setting file should auto-set basename */
	g_assert_null(passim_item_get_basename(item));
	passim_item_set_file(item, file);
	g_assert_cmpstr(passim_item_get_basename(item), ==, "firmware.bin");
	g_assert_nonnull(passim_item_get_file(item));
}

static void
passim_item_ctime_func(void)
{
	g_autoptr(GDateTime) dt = g_date_time_new_utc(2023, 6, 15, 12, 0, 0);
	g_autoptr(GDateTime) dt2 = g_date_time_new_utc(2024, 1, 1, 0, 0, 0);
	g_autoptr(PassimItem) item = passim_item_new();

	/* initially null */
	g_assert_null(passim_item_get_ctime(item));
	g_assert_cmpuint(passim_item_get_age(item), ==, 0);

	/* set and get */
	passim_item_set_ctime(item, dt);
	g_assert_nonnull(passim_item_get_ctime(item));

	/* age should be non-zero for a date in the past */
	g_assert_cmpuint(passim_item_get_age(item), >, 0);

	/* set same -- no-op */
	passim_item_set_ctime(item, passim_item_get_ctime(item));
	g_assert_nonnull(passim_item_get_ctime(item));

	/* change to different time */
	passim_item_set_ctime(item, dt2);
	g_assert_nonnull(passim_item_get_ctime(item));

	/* clear */
	passim_item_set_ctime(item, NULL);
	g_assert_null(passim_item_get_ctime(item));
}

static void
passim_item_variant_func(void)
{
	g_autoptr(PassimItem) item = passim_item_new();
	g_autoptr(PassimItem) item2 = NULL;
	g_autoptr(GVariant) variant = NULL;
	g_autoptr(GDateTime) dt = g_date_time_new_utc(2023, 6, 15, 12, 0, 0);

	/* populate item */
	passim_item_set_hash(item,
			     "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	passim_item_set_basename(item, "test.bin");
	passim_item_set_cmdline(item, "/usr/bin/test");
	passim_item_set_max_age(item, 7200);
	passim_item_set_share_limit(item, 10);
	passim_item_set_share_count(item, 3);
	passim_item_set_size(item, 4096);
	passim_item_set_flags(item, PASSIM_ITEM_FLAG_DISABLED | PASSIM_ITEM_FLAG_NEXT_REBOOT);
	passim_item_set_ctime(item, dt);

	/* serialize */
	variant = passim_item_to_variant(item);
	g_assert_nonnull(variant);

	/* deserialize */
	item2 = passim_item_from_variant(variant);
	g_assert_nonnull(item2);

	/* verify all fields round-trip */
	g_assert_cmpstr(passim_item_get_hash(item2),
			==,
			"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	g_assert_cmpstr(passim_item_get_basename(item2), ==, "test.bin");
	g_assert_cmpstr(passim_item_get_cmdline(item2), ==, "/usr/bin/test");
	g_assert_cmpuint(passim_item_get_max_age(item2), ==, 7200);
	g_assert_cmpuint(passim_item_get_share_limit(item2), ==, 10);
	g_assert_cmpuint(passim_item_get_share_count(item2), ==, 3);
	g_assert_cmpuint(passim_item_get_size(item2), ==, 4096);
	g_assert_cmpuint(passim_item_get_flags(item2),
			 ==,
			 PASSIM_ITEM_FLAG_DISABLED | PASSIM_ITEM_FLAG_NEXT_REBOOT);
	g_assert_nonnull(passim_item_get_ctime(item2));
}

static void
passim_item_variant_empty_func(void)
{
	g_autoptr(PassimItem) item = passim_item_new();
	g_autoptr(PassimItem) item2 = NULL;
	g_autoptr(GVariant) variant = NULL;

	/* serialize item with only defaults */
	variant = passim_item_to_variant(item);
	g_assert_nonnull(variant);

	/* deserialize */
	item2 = passim_item_from_variant(variant);
	g_assert_nonnull(item2);
	g_assert_null(passim_item_get_hash(item2));
	g_assert_null(passim_item_get_basename(item2));
	g_assert_null(passim_item_get_cmdline(item2));
}

static void
passim_item_to_string_func(void)
{
	g_autoptr(PassimItem) item = passim_item_new();
	g_autofree gchar *str = NULL;

	passim_item_set_hash(item,
			     "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
	passim_item_set_basename(item, "test.bin");
	passim_item_set_cmdline(item, "fwupd");
	passim_item_set_size(item, 1024);
	passim_item_set_flags(item, PASSIM_ITEM_FLAG_DISABLED);

	str = passim_item_to_string(item);
	g_assert_nonnull(str);
	g_assert_true(
	    g_str_has_prefix(str,
			     "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"));
	g_assert_nonnull(g_strstr_len(str, -1, "test.bin"));
	g_assert_nonnull(g_strstr_len(str, -1, "flags:disabled"));
	g_assert_nonnull(g_strstr_len(str, -1, "cmdline:fwupd"));
	g_assert_nonnull(g_strstr_len(str, -1, "size:"));
}

static void
passim_item_to_string_share_func(void)
{
	g_autoptr(PassimItem) item = passim_item_new();
	g_autofree gchar *str = NULL;

	passim_item_set_hash(item,
			     "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
	passim_item_set_basename(item, "test.bin");
	passim_item_set_max_age(item, 3600);
	passim_item_set_share_limit(item, 10);
	passim_item_set_share_count(item, 2);

	str = passim_item_to_string(item);
	g_assert_nonnull(str);
	g_assert_nonnull(g_strstr_len(str, -1, "share:2/10"));
}

static void
passim_item_load_filename_func(void)
{
	gboolean ret;
	g_autoptr(PassimItem) item = passim_item_new();
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_BUILT, "tests", "load-test.bin", NULL);
	ret = passim_mkdir_parent(fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create a test file */
	ret = g_file_set_contents(fn, "test content for load", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load */
	ret = passim_item_load_filename(item, fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* should have auto-set various properties */
	g_assert_nonnull(passim_item_get_hash(item));
	g_assert_true(passim_is_valid_sha256(passim_item_get_hash(item)));
	g_assert_nonnull(passim_item_get_bytes(item));
	g_assert_cmpuint(passim_item_get_size(item), ==, 21);
	g_assert_cmpstr(passim_item_get_basename(item), ==, "load-test.bin");
	g_assert_nonnull(passim_item_get_file(item));

	/* load again -- should be no-op since already loaded */
	ret = passim_item_load_filename(item, fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	(void)g_unlink(fn);
}

static void
passim_gnutls_create_private_key_func(void)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = passim_gnutls_create_private_key(&error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	g_assert_cmpuint(g_bytes_get_size(blob), >, 100);
	g_assert_cmpint(memcmp(g_bytes_get_data(blob, NULL), "-----BEGIN ", 11), ==, 0);
}

static void
passim_gnutls_load_privkey_func(void)
{
	g_autoptr(GBytes) bad = g_bytes_new_static("not a key", 9);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error2 = NULL;
	g_auto(gnutls_privkey_t) bad_key = NULL;
	g_auto(gnutls_privkey_t) privkey = NULL;

	/* generate and load */
	blob = passim_gnutls_create_private_key(&error);
	g_assert_no_error(error);
	privkey = passim_gnutls_load_privkey_from_blob(blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(privkey);

	/* error path: invalid data */
	bad_key = passim_gnutls_load_privkey_from_blob(bad, &error2);
	g_assert_error(error2, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_assert_null(bad_key);
}

static void
passim_gnutls_create_certificate_func(void)
{
	g_autoptr(GBytes) key_blob = NULL;
	g_autoptr(GBytes) cert_blob = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(gnutls_privkey_t) privkey = NULL;

	key_blob = passim_gnutls_create_private_key(&error);
	g_assert_no_error(error);
	privkey = passim_gnutls_load_privkey_from_blob(key_blob, &error);
	g_assert_no_error(error);

	cert_blob = passim_gnutls_create_certificate(privkey, &error);
	g_assert_no_error(error);
	g_assert_nonnull(cert_blob);
	g_assert_cmpuint(g_bytes_get_size(cert_blob), >, 100);
	g_assert_cmpint(memcmp(g_bytes_get_data(cert_blob, NULL), "-----BEGIN ", 11), ==, 0);
}

static void
passim_gnutls_load_crt_func(void)
{
	g_autoptr(GBytes) bad = g_bytes_new_static("not a cert", 10);
	g_autoptr(GBytes) cert_blob = NULL;
	g_autoptr(GBytes) key_blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error2 = NULL;
	g_auto(gnutls_privkey_t) privkey = NULL;
	g_auto(gnutls_x509_crt_t) bad_crt = NULL;
	g_auto(gnutls_x509_crt_t) crt = NULL;

	/* generate key and cert, then load the cert back */
	key_blob = passim_gnutls_create_private_key(&error);
	g_assert_no_error(error);
	privkey = passim_gnutls_load_privkey_from_blob(key_blob, &error);
	g_assert_no_error(error);
	cert_blob = passim_gnutls_create_certificate(privkey, &error);
	g_assert_no_error(error);

	crt = passim_gnutls_load_crt_from_blob(cert_blob, GNUTLS_X509_FMT_PEM, &error);
	g_assert_no_error(error);
	g_assert_nonnull(crt);

	/* error path: invalid data */
	bad_crt = passim_gnutls_load_crt_from_blob(bad, GNUTLS_X509_FMT_PEM, &error2);
	g_assert_error(error2, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_assert_null(bad_crt);
}

static void
passim_gnutls_pubkey_func(void)
{
	g_autoptr(GBytes) key_blob = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(gnutls_privkey_t) privkey = NULL;
	g_auto(gnutls_pubkey_t) pubkey = NULL;

	key_blob = passim_gnutls_create_private_key(&error);
	g_assert_no_error(error);
	privkey = passim_gnutls_load_privkey_from_blob(key_blob, &error);
	g_assert_no_error(error);
	pubkey = passim_gnutls_load_pubkey_from_privkey(privkey, &error);
	g_assert_no_error(error);
	g_assert_nonnull(pubkey);
}

static void
passim_gnutls_datum_to_dn_str_func(void)
{
	g_autofree gchar *str = NULL;
	gnutls_datum_t raw = {0};

	/* empty datum should return NULL (import fails) */
	str = passim_gnutls_datum_to_dn_str(&raw);
	g_assert_null(str);
}

static void
passim_client_func(void)
{
	g_autoptr(PassimClient) client = passim_client_new();

	/* initial state */
	g_assert_nonnull(client);
	g_assert_null(passim_client_get_version(client));
	g_assert_null(passim_client_get_name(client));
	g_assert_null(passim_client_get_uri(client));
	g_assert_cmpint(passim_client_get_status(client), ==, PASSIM_STATUS_UNKNOWN);
	g_assert_cmpuint(passim_client_get_download_saving(client), ==, 0);
	g_assert_cmpfloat(passim_client_get_carbon_saving(client), ==, 0.0);
}

static void
passim_version_func(void)
{
	const gchar *ver = passim_version_string();
	g_assert_nonnull(ver);
	g_assert_true(g_strstr_len(ver, -1, ".") != NULL);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	(void)g_setenv("G_TEST_BUILDDIR", BUILDDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	g_test_add_func("/passim/common", passim_common_func);
	g_test_add_func("/passim/common/status", passim_common_status_func);
	g_test_add_func("/passim/common/config", passim_common_config_func);
	g_test_add_func("/passim/common/mkdir-parent", passim_common_mkdir_parent_func);
	g_test_add_func("/passim/common/file-contents", passim_common_file_contents_func);
	g_test_add_func("/passim/common/load-input-stream", passim_common_load_input_stream_func);
	g_test_add_func("/passim/item", passim_item_func);
	g_test_add_func("/passim/item/flags", passim_item_flags_func);
	g_test_add_func("/passim/item/flag-string", passim_item_flag_string_func);
	g_test_add_func("/passim/item/bytes", passim_item_bytes_func);
	g_test_add_func("/passim/item/file", passim_item_file_func);
	g_test_add_func("/passim/item/ctime", passim_item_ctime_func);
	g_test_add_func("/passim/item/variant", passim_item_variant_func);
	g_test_add_func("/passim/item/variant-empty", passim_item_variant_empty_func);
	g_test_add_func("/passim/item/to-string", passim_item_to_string_func);
	g_test_add_func("/passim/item/to-string-share", passim_item_to_string_share_func);
	g_test_add_func("/passim/item/load-filename", passim_item_load_filename_func);
	g_test_add_func("/passim/gnutls/create-private-key", passim_gnutls_create_private_key_func);
	g_test_add_func("/passim/gnutls/load-privkey", passim_gnutls_load_privkey_func);
	g_test_add_func("/passim/gnutls/create-certificate", passim_gnutls_create_certificate_func);
	g_test_add_func("/passim/gnutls/load-crt", passim_gnutls_load_crt_func);
	g_test_add_func("/passim/gnutls/pubkey", passim_gnutls_pubkey_func);
	g_test_add_func("/passim/gnutls/datum-to-dn-str", passim_gnutls_datum_to_dn_str_func);
	g_test_add_func("/passim/client", passim_client_func);
	g_test_add_func("/passim/version", passim_version_func);
	return g_test_run();
}
