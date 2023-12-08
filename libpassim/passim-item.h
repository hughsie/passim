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

/**
 * PASSIM_ITEM_FLAG_NONE:
 *
 * No item flags are set.
 *
 * Since: 0.1.0
 */
#define PASSIM_ITEM_FLAG_NONE 0u

/**
 * PASSIM_ITEM_FLAG_DISABLED:
 *
 * The item is not active for some reason.
 *
 * Since: 0.1.0
 */
#define PASSIM_ITEM_FLAG_DISABLED (1llu << 0)

/**
 * PASSIM_ITEM_FLAG_NEXT_REBOOT:
 *
 * Only register the item when the machine has been rebooted.
 *
 * Since: 0.1.0
 */
#define PASSIM_ITEM_FLAG_NEXT_REBOOT (1llu << 1)

/**
 * PASSIM_ITEM_FLAG_UNKNOWN:
 *
 * The item flag is unknown.
 *
 * This is usually caused by a mismatched libpassimplugin and daemon.
 *
 * Since: 0.1.0
 */
#define PASSIM_ITEM_FLAG_UNKNOWN G_MAXUINT64

/**
 * PassimItemFlags:
 *
 * Flags used to represent item attributes
 */
typedef guint64 PassimItemFlags;

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
const gchar *
passim_item_get_cmdline(PassimItem *self);
void
passim_item_set_cmdline(PassimItem *self, const gchar *cmdline);
guint32
passim_item_get_age(PassimItem *self);
guint32
passim_item_get_max_age(PassimItem *self);
void
passim_item_set_max_age(PassimItem *self, guint32 max_age);
guint32
passim_item_get_share_limit(PassimItem *self);
void
passim_item_set_share_limit(PassimItem *self, guint32 share_limit);
guint64
passim_item_get_size(PassimItem *self);
void
passim_item_set_size(PassimItem *self, guint64 size);
guint32
passim_item_get_share_count(PassimItem *self);
void
passim_item_set_share_count(PassimItem *self, guint32 share_count);
GFile *
passim_item_get_file(PassimItem *self);
void
passim_item_set_file(PassimItem *self, GFile *file);
GBytes *
passim_item_get_bytes(PassimItem *self);
void
passim_item_set_bytes(PassimItem *self, GBytes *bytes);
GInputStream *
passim_item_get_stream(PassimItem *self);
void
passim_item_set_stream(PassimItem *self, GInputStream *stream);
GDateTime *
passim_item_get_ctime(PassimItem *self);
void
passim_item_set_ctime(PassimItem *self, GDateTime *ctime);

guint64
passim_item_get_flags(PassimItem *self);
gchar *
passim_item_get_flags_as_string(PassimItem *self);
void
passim_item_set_flags(PassimItem *self, guint64 flags);
void
passim_item_add_flag(PassimItem *self, PassimItemFlags flag);
void
passim_item_remove_flag(PassimItem *self, PassimItemFlags flag);
gboolean
passim_item_has_flag(PassimItem *self, PassimItemFlags flag);

const gchar *
passim_item_flag_to_string(PassimItemFlags item_flag);
PassimItemFlags
passim_item_flag_from_string(const gchar *item_flag);

gboolean
passim_item_load_filename(PassimItem *self, const gchar *filename, GError **error);

PassimItem *
passim_item_from_variant(GVariant *value);
GVariant *
passim_item_to_variant(PassimItem *self);

G_END_DECLS
