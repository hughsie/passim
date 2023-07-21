/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

void
passim_avahi_service_browser_async(GDBusProxy *proxy,
				   const gchar *hash,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer callback_data);
GPtrArray *
passim_avahi_service_browser_finish(GAsyncResult *res, GError **error);
