/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "passim-common.h"

#define PASSIM_TYPE_AVAHI (passim_avahi_get_type())
G_DECLARE_FINAL_TYPE(PassimAvahi, passim_avahi, PASSIM, AVAHI, GObject)

#define AVAHI_IF_UNSPEC	   -1
#define AVAHI_PROTO_UNSPEC -1
#define AVAHI_PROTO_INET   0 /* IPv4 */
#define AVAHI_PROTO_INET6  1 /* IPv6 */

typedef enum {
	AVAHI_LOOKUP_USE_WIDE_AREA = 1,
	AVAHI_LOOKUP_USE_MULTICAST = 2,
	AVAHI_LOOKUP_NO_TXT = 4,
	AVAHI_LOOKUP_NO_ADDRESS = 8,
} AvahiLookupFlags;

typedef enum {
	AVAHI_LOOKUP_RESULT_CACHED = 1,
	AVAHI_LOOKUP_RESULT_WIDE_AREA = 2,
	AVAHI_LOOKUP_RESULT_MULTICAST = 4,
	AVAHI_LOOKUP_RESULT_LOCAL = 8,
	AVAHI_LOOKUP_RESULT_OUR_OWN = 16,
	AVAHI_LOOKUP_RESULT_STATIC = 32,
} AvahiLookupResultFlags;

#define PASSIM_SERVER_DOMAIN  ""
#define PASSIM_SERVER_HOST    ""
#define PASSIM_SERVER_TYPE    "_cache._tcp"
#define PASSIM_SERVER_TIMEOUT 150 /* ms */

PassimAvahi *
passim_avahi_new(GKeyFile *config);
gboolean
passim_avahi_connect(PassimAvahi *self, GError **error);
gboolean
passim_avahi_unregister(PassimAvahi *self, GError **error);
gboolean
passim_avahi_register(PassimAvahi *self, gchar **keys, GError **error);
const gchar *
passim_avahi_get_name(PassimAvahi *self);
gchar *
passim_avahi_build_subtype_for_hash(const gchar *hash);

void
passim_avahi_find_async(PassimAvahi *self,
			const gchar *hash,
			GCancellable *cancellable,
			GAsyncReadyCallback callback,
			gpointer callback_data);
GPtrArray *
passim_avahi_find_finish(PassimAvahi *self, GAsyncResult *res, GError **error);
