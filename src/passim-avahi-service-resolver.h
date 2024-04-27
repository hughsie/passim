/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "passim-avahi-service.h"

void
passim_avahi_service_resolver_async(GDBusProxy *proxy,
				    PassimAvahiService *service,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer callback_data);
gchar *
passim_avahi_service_resolver_finish(GAsyncResult *res, GError **error);
