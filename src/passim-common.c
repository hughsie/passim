/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <sys/xattr.h>

#include "passim-common.h"

#define PASSIM_CONFIG_GROUP "daemon"
#define PASSIM_CONFIG_PORT  "Port"
#define PASSIM_CONFIG_PATH  "Path"

GKeyFile *
passim_config_load(GError **error)
{
	g_autoptr(GKeyFile) kf = g_key_file_new();
	g_autofree gchar *fn = g_build_filename(PACKAGE_SYSCONFDIR, "passim.conf", NULL);

	if (g_file_test(fn, G_FILE_TEST_EXISTS)) {
		if (!g_key_file_load_from_file(kf, fn, G_KEY_FILE_NONE, error))
			return NULL;
	} else {
		g_debug("not loading %s as it does not exist", fn);
	}

	if (!g_key_file_has_key(kf, PASSIM_CONFIG_GROUP, PASSIM_CONFIG_PORT, NULL))
		g_key_file_set_integer(kf, PASSIM_CONFIG_GROUP, PASSIM_CONFIG_PORT, 27500);
	if (!g_key_file_has_key(kf, PASSIM_CONFIG_GROUP, PASSIM_CONFIG_PATH, NULL)) {
		g_autofree gchar *path =
		    g_build_filename(PACKAGE_LOCALSTATEDIR, "lib", PACKAGE_NAME, "data", NULL);
		g_key_file_set_string(kf, PASSIM_CONFIG_GROUP, PASSIM_CONFIG_PATH, path);
	}

	return g_steal_pointer(&kf);
}

guint16
passim_config_get_port(GKeyFile *kf)
{
	return g_key_file_get_integer(kf, PASSIM_CONFIG_GROUP, PASSIM_CONFIG_PORT, NULL);
}

gchar *
passim_config_get_path(GKeyFile *kf)
{
	return g_key_file_get_string(kf, PASSIM_CONFIG_GROUP, PASSIM_CONFIG_PATH, NULL);
}

gchar *
passim_compute_checksum_for_filename(const gchar *filename, GError **error)
{
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	if (!g_file_get_contents(filename, &buf, &bufsz, error))
		return FALSE;
	return g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guchar *)buf, bufsz);
}

gboolean
passim_xattr_set_value(const gchar *filename, const gchar *name, guint32 value, GError **error)
{
	ssize_t rc = setxattr(filename, name, &value, sizeof(value), XATTR_CREATE);
	if (rc < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    g_io_error_from_errno(errno),
			    "failed to get %s: %s",
			    name,
			    strerror(errno));
		return FALSE;
	}
	return TRUE;
}

guint64
passim_xattr_get_value(const gchar *filename,
		       const gchar *name,
		       guint32 value_fallback,
		       GError **error)
{
	guint32 value = 0;
	ssize_t rc = getxattr(filename, name, &value, sizeof(value));
	if (rc < 0) {
		if (errno == ENODATA) {
			g_debug("using fallback %s=%u for %s",
				name,
				(guint)value_fallback,
				filename);
			return value_fallback;
		}
		g_set_error(error,
			    G_IO_ERROR,
			    g_io_error_from_errno(errno),
			    "failed to get %s: %s",
			    name,
			    strerror(errno));
		return G_MAXUINT32;
	}
	if (value == G_MAXUINT32) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid data for %s",
			    name);
		return G_MAXUINT32;
	}
	return value;
}

const gchar *
passim_http_code_to_string(guint error_code)
{
	if (error_code == 200)
		return "OK";
	if (error_code == 303)
		return "See Other";
	if (error_code == 400)
		return "Bad Request";
	if (error_code == 404)
		return "Not Found";
	if (error_code == 501)
		return "Only GET implemented";
	if (error_code == 505)
		return "HTTP Version Not Supported";
	if (error_code == 404)
		return "Not Found";
	return "Unknown";
}

gboolean
passim_mkdir(const gchar *dirname, GError **error)
{
	g_return_val_if_fail(dirname != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!g_file_test(dirname, G_FILE_TEST_IS_DIR))
		g_debug("creating path %s", dirname);
	if (g_mkdir_with_parents(dirname, 0755) == -1) {
		g_set_error(error,
			    G_IO_ERROR,
			    g_io_error_from_errno(errno),
			    "failed to create '%s': %s",
			    dirname,
			    g_strerror(errno));
		return FALSE;
	}
	return TRUE;
}
