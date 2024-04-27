/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <sys/xattr.h>

#include "passim-common.h"

#define PASSIM_CONFIG_GROUP	    "daemon"
#define PASSIM_CONFIG_PORT	    "Port"
#define PASSIM_CONFIG_IPV6	    "IPv6"
#define PASSIM_CONFIG_PATH	    "Path"
#define PASSIM_CONFIG_MAX_ITEM_SIZE "MaxItemSize"
#define PASSIM_CONFIG_CARBON_COST   "CarbonCost"

const gchar *
passim_status_to_string(PassimStatus status)
{
	if (status == PASSIM_STATUS_STARTING)
		return "starting";
	if (status == PASSIM_STATUS_LOADING)
		return "loading";
	if (status == PASSIM_STATUS_DISABLED_METERED)
		return "disabled-metered";
	if (status == PASSIM_STATUS_RUNNING)
		return "running";
	return NULL;
}

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
	if (!g_key_file_has_key(kf, PASSIM_CONFIG_GROUP, PASSIM_CONFIG_MAX_ITEM_SIZE, NULL)) {
		g_key_file_set_uint64(kf,
				      PASSIM_CONFIG_GROUP,
				      PASSIM_CONFIG_MAX_ITEM_SIZE,
				      100 * 1024 * 1024);
	}
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

gboolean
passim_config_get_ipv6(GKeyFile *kf)
{
	return g_key_file_get_boolean(kf, PASSIM_CONFIG_GROUP, PASSIM_CONFIG_IPV6, NULL);
}

gsize
passim_config_get_max_item_size(GKeyFile *kf)
{
	return g_key_file_get_uint64(kf, PASSIM_CONFIG_GROUP, PASSIM_CONFIG_MAX_ITEM_SIZE, NULL);
}

gdouble
passim_config_get_carbon_cost(GKeyFile *kf)
{
	gdouble carbon_cost =
	    g_key_file_get_double(kf, PASSIM_CONFIG_GROUP, PASSIM_CONFIG_CARBON_COST, NULL);
	if (carbon_cost < 0.00001) {
		/* using
		 * https://www.carbonbrief.org/factcheck-what-is-the-carbon-footprint-of-streaming-video-on-netflix/
		 * we can see that 0.018 kg CO2e for 30 mins, where 3 GB/hr -- so this gives a
		 * kg/GB of ~0.018 kg x (3h / 2) */
		carbon_cost = 0.026367;
	}
	return carbon_cost;
}

gchar *
passim_config_get_path(GKeyFile *kf)
{
	return g_key_file_get_string(kf, PASSIM_CONFIG_GROUP, PASSIM_CONFIG_PATH, NULL);
}

gboolean
passim_xattr_set_string(const gchar *filename,
			const gchar *name,
			const gchar *value,
			GError **error)
{
	ssize_t rc = setxattr(filename, name, value, strlen(value), XATTR_CREATE);
	if (rc < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    g_io_error_from_errno(errno),
			    "failed to set %s: %s",
			    name,
			    strerror(errno));
		return FALSE;
	}
	return TRUE;
}

gchar *
passim_xattr_get_string(const gchar *filename, const gchar *name, GError **error)
{
	ssize_t rc;
	g_autofree gchar *buf = NULL;

	rc = getxattr(filename, name, NULL, 0);
	if (rc < 0) {
		if (errno == ENODATA)
			return g_strdup("");
		g_set_error(error,
			    G_IO_ERROR,
			    g_io_error_from_errno(errno),
			    "failed to get %s: %s",
			    name,
			    strerror(errno));
		return NULL;
	}
	if (rc == 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid data for %s",
			    name);
		return NULL;
	}

	/* copy out with appended NUL */
	buf = g_new0(gchar, rc + 1);
	rc = getxattr(filename, name, buf, rc + 1);
	if (rc < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    g_io_error_from_errno(errno),
			    "failed to get %s: %s",
			    name,
			    strerror(errno));
		return NULL;
	}
	return g_steal_pointer(&buf);
}

gboolean
passim_xattr_set_uint32(const gchar *filename, const gchar *name, guint32 value, GError **error)
{
	ssize_t rc = setxattr(filename, name, &value, sizeof(value), XATTR_CREATE);
	if (rc < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    g_io_error_from_errno(errno),
			    "failed to set %s: %s",
			    name,
			    strerror(errno));
		return FALSE;
	}
	return TRUE;
}

guint32
passim_xattr_get_uint32(const gchar *filename,
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

gboolean
passim_mkdir(const gchar *dirname, GError **error)
{
	g_return_val_if_fail(dirname != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!g_file_test(dirname, G_FILE_TEST_IS_DIR))
		g_debug("creating path %s", dirname);
	if (g_mkdir_with_parents(dirname, 0700) == -1) {
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

gboolean
passim_mkdir_parent(const gchar *filename, GError **error)
{
	g_autofree gchar *parent = NULL;

	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	parent = g_path_get_dirname(filename);
	return passim_mkdir(parent, error);
}

GBytes *
passim_load_input_stream(GInputStream *stream, gsize count, GError **error)
{
	guint8 tmp[0x8000] = {0x0};
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* this is invalid */
	if (count == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "A maximum read size must be specified");
		return NULL;
	}

	/* read from stream in 32kB chunks */
	while (TRUE) {
		gssize sz;
		sz = g_input_stream_read(stream, tmp, sizeof(tmp), NULL, &error_local);
		if (sz == 0)
			break;
		if (sz < 0) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    error_local->message);
			return NULL;
		}
		g_byte_array_append(buf, tmp, sz);
		if (buf->len > count) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NO_SPACE,
				    "cannot read from fd: 0x%x > 0x%x",
				    buf->len,
				    (guint)count);
			return NULL;
		}
	}
	return g_bytes_new(buf->data, buf->len);
}

gchar *
passim_get_boot_time(void)
{
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) lines = NULL;
	if (!g_file_get_contents("/proc/stat", &buf, NULL, NULL))
		return NULL;
	lines = g_strsplit(buf, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix(lines[i], "btime "))
			return g_strdup(lines[i] + 6);
	}
	return NULL;
}

gboolean
passim_file_set_contents(const gchar *filename, GBytes *bytes, GError **error)
{
	gsize size = 0;
	const gchar *data = g_bytes_get_data(bytes, &size);
	g_debug("writing %s with %" G_GSIZE_FORMAT " bytes", filename, size);
	return g_file_set_contents_full(filename,
					data,
					size,
					G_FILE_SET_CONTENTS_CONSISTENT,
					0600,
					error);
}

GBytes *
passim_file_get_contents(const gchar *filename, GError **error)
{
	gchar *data = NULL;
	gsize len = 0;
	if (!g_file_get_contents(filename, &data, &len, error))
		return NULL;
	g_debug("reading %s with %" G_GSIZE_FORMAT " bytes", filename, len);
	return g_bytes_new_take(data, len);
}
