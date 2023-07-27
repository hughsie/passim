/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "passim-item.h"

G_BEGIN_DECLS

#define PASSIM_TYPE_CLIENT (passim_client_get_type())
G_DECLARE_DERIVABLE_TYPE(PassimClient, passim_client, PASSIM, CLIENT, GObject)

struct _PassimClientClass {
	GObjectClass parent_class;
	/*< private >*/
	void (*_passim_reserved1)(void);
	void (*_passim_reserved2)(void);
	void (*_passim_reserved3)(void);
	void (*_passim_reserved4)(void);
	void (*_passim_reserved5)(void);
	void (*_passim_reserved6)(void);
	void (*_passim_reserved7)(void);
};

#define PASSIM_DBUS_SERVICE   "org.freedesktop.Passim"
#define PASSIM_DBUS_INTERFACE "org.freedesktop.Passim"
#define PASSIM_DBUS_PATH      "/"

PassimClient *
passim_client_new(void);
const gchar *
passim_client_get_version(PassimClient *self);
gboolean
passim_client_load(PassimClient *self, GError **error);
GPtrArray *
passim_client_get_items(PassimClient *self, GError **error);
gboolean
passim_client_publish(PassimClient *self, PassimItem *item, GError **error);

G_END_DECLS
