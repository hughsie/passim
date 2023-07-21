/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

typedef struct {
	gint32 interface;
	gint32 protocol;
	gchar *name;
	gchar *type;
	gchar *domain;
	guint32 flags;
} PassimAvahiService;

void
passim_avahi_service_free(PassimAvahiService *service);
void
passim_avahi_service_print(PassimAvahiService *service);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(PassimAvahiService, passim_avahi_service_free)
