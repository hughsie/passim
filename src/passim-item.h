/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

typedef struct {
	gchar *hash;
	gchar *basename;
	guint32 max_age;
	guint32 share_limit;
	guint32 share_count;
	GFile *file;
} PassimItem;

PassimItem *
passim_item_new(void);
PassimItem *
passim_item_from_variant(GVariant *variant);
void
passim_item_free(PassimItem *item);
GVariant *
passim_item_to_variant(PassimItem *item);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(PassimItem, passim_item_free)
