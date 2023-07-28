/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

GKeyFile *
passim_config_load(GError **error);
guint16
passim_config_get_port(GKeyFile *kf);
gsize
passim_config_get_max_item_size(GKeyFile *kf);
gchar *
passim_config_get_path(GKeyFile *kf);
const gchar *
passim_http_code_to_string(guint error_code);
gboolean
passim_xattr_set_uint32(const gchar *filename, const gchar *name, guint32 value, GError **error);
guint64
passim_xattr_get_uint32(const gchar *filename,
			const gchar *name,
			guint32 value_fallback,
			GError **error);
gboolean
passim_xattr_set_string(const gchar *filename,
			const gchar *name,
			const gchar *value,
			GError **error);
gchar *
passim_xattr_get_string(const gchar *filename, const gchar *name, GError **error);
gboolean
passim_mkdir(const gchar *dirname, GError **error);
GBytes *
passim_load_input_stream(GInputStream *stream, gsize count, GError **error);
