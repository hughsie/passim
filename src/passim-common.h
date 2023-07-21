/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#define PACKAGE_DBUS_SERVICE   "org.freedesktop.Passim"
#define PACKAGE_DBUS_INTERFACE "org.freedesktop.Passim"
#define PACKAGE_DBUS_PATH      "/"

GKeyFile *
passim_config_load(GError **error);
guint16
passim_config_get_port(GKeyFile *kf);
gchar *
passim_config_get_path(GKeyFile *kf);
gchar *
passim_compute_checksum_for_filename(const gchar *filename, GError **error);
const gchar *
passim_http_code_to_string(guint error_code);
gboolean
passim_xattr_set_value(const gchar *filename, const gchar *name, guint32 value, GError **error);
guint64
passim_xattr_get_value(const gchar *filename,
		       const gchar *name,
		       guint32 value_fallback,
		       GError **error);
gboolean
passim_mkdir(const gchar *dirname, GError **error);
