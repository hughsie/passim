/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define PASSIM_TYPE_ITEM (passim_item_get_type())
G_DECLARE_DERIVABLE_TYPE(PassimItem, passim_item, PASSIM, ITEM, GObject)

struct _PassimItemClass {
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

PassimItem *
passim_item_new(void);
gchar *
passim_item_to_string(PassimItem *self);

const gchar *
passim_item_get_hash(PassimItem *self);
void
passim_item_set_hash(PassimItem *self, const gchar *hash);
const gchar *
passim_item_get_basename(PassimItem *self);
void
passim_item_set_basename(PassimItem *self, const gchar *basename);
guint32
passim_item_get_max_age(PassimItem *self);
void
passim_item_set_max_age(PassimItem *self, guint32 max_age);
guint32
passim_item_get_share_limit(PassimItem *self);
void
passim_item_set_share_limit(PassimItem *self, guint32 share_limit);
guint32
passim_item_get_share_count(PassimItem *self);
void
passim_item_set_share_count(PassimItem *self, guint32 share_count);
GFile *
passim_item_get_file(PassimItem *self);
void
passim_item_set_file(PassimItem *self, GFile *file);
GDateTime *
passim_item_get_ctime(PassimItem *self);
void
passim_item_set_ctime(PassimItem *self, GDateTime *ctime);

gboolean
passim_item_load_filename(PassimItem *self, const gchar *filename, GError **error);

PassimItem *
passim_item_from_variant(GVariant *value);
GVariant *
passim_item_to_variant(PassimItem *self);

G_END_DECLS
