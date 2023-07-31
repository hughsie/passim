/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "passim-item.h"

/**
 * PassimItem:
 *
 * A shared item.
 */

typedef struct {
	gchar *hash;
	PassimItemFlags flags;
	gchar *basename;
	gchar *cmdline;
	guint32 max_age;
	guint32 share_limit;
	guint32 share_count;
	GFile *file;
	GBytes *bytes;
	GDateTime *ctime;
} PassimItemPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(PassimItem, passim_item, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (passim_item_get_instance_private(o))

/**
 * passim_item_get_hash:
 * @self: a #PassimItem
 *
 * Gets the file hash.
 *
 * Returns: the typically in SHA-256 lowercase form, or %NULL if unset
 *
 * Since: 0.1.0
 **/
const gchar *
passim_item_get_hash(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(PASSIM_IS_ITEM(self), NULL);
	return priv->hash;
}

/**
 * passim_item_set_hash:
 * @self: a #PassimItem
 * @hash: (nullable): the hash, typically in SHA-256 lowercase form
 *
 * Sets the file hash.
 *
 * Since: 0.1.0
 **/
void
passim_item_set_hash(PassimItem *self, const gchar *hash)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(PASSIM_IS_ITEM(self));

	/* not changed */
	if (g_strcmp0(priv->hash, hash) == 0)
		return;

	g_free(priv->hash);
	priv->hash = g_strdup(hash);
}

/**
 * passim_item_get_basename:
 * @self: a #PassimItem
 *
 * Gets the basename of the file that was published.
 *
 * Returns: the test basename, or %NULL if unset
 *
 * Since: 0.1.0
 **/
const gchar *
passim_item_get_basename(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(PASSIM_IS_ITEM(self), NULL);
	return priv->basename;
}

/**
 * passim_item_set_basename:
 * @self: a #PassimItem
 * @basename: (nullable): the basename name
 *
 * Sets the basename of the file that was published.
 *
 * Since: 0.1.0
 **/
void
passim_item_set_basename(PassimItem *self, const gchar *basename)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(PASSIM_IS_ITEM(self));

	/* not changed */
	if (g_strcmp0(priv->basename, basename) == 0)
		return;

	g_free(priv->basename);
	priv->basename = g_strdup(basename);
}

/**
 * passim_item_get_cmdline:
 * @self: a #PassimItem
 *
 * Gets the cmdline of the binary that published the item.
 *
 * Returns: the binary name, or %NULL if unset
 *
 * Since: 0.1.0
 **/
const gchar *
passim_item_get_cmdline(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(PASSIM_IS_ITEM(self), NULL);
	return priv->cmdline;
}

/**
 * passim_item_set_cmdline:
 * @self: a #PassimItem
 * @cmdline: (nullable): the binary name
 *
 * Sets the cmdline of the binary that published the item.
 *
 * Since: 0.1.0
 **/
void
passim_item_set_cmdline(PassimItem *self, const gchar *cmdline)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(PASSIM_IS_ITEM(self));

	/* not changed */
	if (g_strcmp0(priv->cmdline, cmdline) == 0)
		return;

	g_free(priv->cmdline);
	priv->cmdline = g_strdup(cmdline);
}

/**
 * passim_item_get_age:
 * @self: a #PassimItem
 *
 * Gets the current file age.
 *
 * Returns: time in seconds, or 0 for invalid.
 *
 * Since: 0.1.0
 **/
guint32
passim_item_get_age(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GDateTime) dt_now = g_date_time_new_now_utc();
	g_return_val_if_fail(PASSIM_IS_ITEM(self), 0);
	if (priv->ctime == NULL)
		return 0;
	return g_date_time_difference(dt_now, priv->ctime) / G_TIME_SPAN_SECOND;
}

/**
 * passim_item_get_max_age:
 * @self: a #PassimItem
 *
 * Gets the maximum permitted file age.
 *
 * Returns: time in seconds
 *
 * Since: 0.1.0
 **/
guint32
passim_item_get_max_age(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(PASSIM_IS_ITEM(self), 0);
	return priv->max_age;
}

/**
 * passim_item_set_max_age:
 * @self: a #PassimItem
 * @max_age: time in seconds
 *
 * Sets the maximum permitted file age.
 *
 * Since: 0.1.0
 **/
void
passim_item_set_max_age(PassimItem *self, guint32 max_age)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(PASSIM_IS_ITEM(self));
	priv->max_age = max_age;
}

/**
 * passim_item_get_share_limit:
 * @self: a #PassimItem
 *
 * Gets the maximum number of times that the file can be shared.
 *
 * Returns: share limit, or 0 if unset
 *
 * Since: 0.1.0
 **/
guint32
passim_item_get_share_limit(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(PASSIM_IS_ITEM(self), 0);
	return priv->share_limit;
}

/**
 * passim_item_set_share_limit:
 * @self: a #PassimItem
 * @share_limit: the share limit, or 0
 *
 * Sets the maximum number of times that the file can be shared.
 *
 * Since: 0.1.0
 **/
void
passim_item_set_share_limit(PassimItem *self, guint32 share_limit)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(PASSIM_IS_ITEM(self));
	priv->share_limit = share_limit;
}

/**
 * passim_item_get_share_count:
 * @self: a #PassimItem
 *
 * Gets the current number of times the item has been shared to other machines.
 *
 * Returns: the count, or 0 if unset
 *
 * Since: 0.1.0
 **/
guint32
passim_item_get_share_count(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(PASSIM_IS_ITEM(self), 0);
	return priv->share_count;
}

/**
 * passim_item_set_share_count:
 * @self: a #PassimItem
 * @share_count: the count, or 0 to unset
 *
 * Sets the current number of times the item has been shared to other machines.
 *
 * Since: 0.1.0
 **/
void
passim_item_set_share_count(PassimItem *self, guint32 share_count)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(PASSIM_IS_ITEM(self));
	priv->share_count = share_count;
}

/**
 * passim_item_get_file:
 * @self: a #PassimItem
 *
 * Gets the local file in the cache.
 *
 * Returns: (transfer none): a #GFile, or %NULL if unset
 *
 * Since: 0.1.0
 **/
GFile *
passim_item_get_file(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(PASSIM_IS_ITEM(self), NULL);
	return priv->file;
}

/**
 * passim_item_set_file:
 * @self: a #PassimItem
 * @file: (nullable): a #GFile
 *
 * Sets the local file in the cache.
 *
 * Since: 0.1.0
 **/
void
passim_item_set_file(PassimItem *self, GFile *file)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(PASSIM_IS_ITEM(self));
	g_set_object(&priv->file, file);
}

/**
 * passim_item_get_bytes:
 * @self: a #PassimItem
 *
 * Gets the local bytes in the cache.
 *
 * Returns: (transfer none): a #GBytes, or %NULL if unset
 *
 * Since: 0.1.0
 **/
GBytes *
passim_item_get_bytes(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(PASSIM_IS_ITEM(self), NULL);
	return priv->bytes;
}

/**
 * passim_item_set_bytes:
 * @self: a #PassimItem
 * @bytes: (nullable): a #GBytes
 *
 * Sets the local bytes in the cache.
 *
 * Since: 0.1.0
 **/
void
passim_item_set_bytes(PassimItem *self, GBytes *bytes)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(PASSIM_IS_ITEM(self));

	/* unchanged */
	if (bytes == priv->bytes)
		return;
	if (priv->bytes != NULL) {
		g_bytes_unref(priv->bytes);
		priv->bytes = NULL;
	}
	if (bytes != NULL)
		priv->bytes = g_bytes_ref(bytes);

	/* generate checksum */
	if (bytes != NULL && priv->hash == NULL)
		priv->hash = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, bytes);
}

/**
 * passim_item_get_ctime:
 * @self: a #PassimItem
 *
 * Gets the creation time of the file.
 *
 * Returns: (transfer none): the creation time, or %NULL if unset
 *
 * Since: 0.1.0
 **/
GDateTime *
passim_item_get_ctime(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(PASSIM_IS_ITEM(self), NULL);
	return priv->ctime;
}

/**
 * passim_item_set_ctime:
 * @self: a #PassimItem
 * @ctime: (nullable): a #GDateTime
 *
 * Sets the creation time of the file.
 *
 * Since: 0.1.0
 **/
void
passim_item_set_ctime(PassimItem *self, GDateTime *ctime)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(PASSIM_IS_ITEM(self));

	/* not changed */
	if (priv->ctime == ctime)
		return;
	if (priv->ctime != NULL) {
		g_date_time_unref(priv->ctime);
		priv->ctime = NULL;
	}
	if (ctime != NULL)
		priv->ctime = g_date_time_ref(ctime);
}

#if !GLIB_CHECK_VERSION(2, 70, 0)
static GDateTime *
g_file_info_get_creation_date_time(GFileInfo *info)
{
	guint64 ctime = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_CREATED);
	return g_date_time_new_from_unix_utc(ctime);
}
#endif

/**
 * passim_item_load_filename:
 * @self: a #PassimItem
 * @filename: (not nullable): a filename with full path
 * @error: (nullable): optional return location for an error
 *
 * Loads the item from a file on disk.
 *
 * Since: 0.1.0
 **/
gboolean
passim_item_load_filename(PassimItem *self, const gchar *filename, GError **error)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GFile) file = NULL;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GFileInfo) info = NULL;

	g_return_val_if_fail(PASSIM_IS_ITEM(self), FALSE);
	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* set file and bytes (which also sets the hash too) */
	file = g_file_new_for_path(filename);
	passim_item_set_file(self, file);
	bytes = g_file_load_bytes(file, NULL, NULL, error);
	if (bytes == NULL)
		return FALSE;
	passim_item_set_bytes(self, bytes);

	/* get ctime */
	info = g_file_query_info(file,
				 G_FILE_ATTRIBUTE_TIME_CREATED,
				 G_FILE_QUERY_INFO_NONE,
				 NULL,
				 error);
	if (info == NULL)
		return FALSE;
	priv->ctime = g_file_info_get_creation_date_time(info);

	/* if not already set */
	if (priv->basename == NULL)
		priv->basename = g_path_get_basename(filename);

	/* success */
	return TRUE;
}

/**
 * passim_item_to_variant:
 * @self: a #PassimItem
 *
 * Serialize the item data.
 *
 * Returns: the serialized data, or %NULL for error
 *
 * Since: 0.1.0
 **/
GVariant *
passim_item_to_variant(PassimItem *self)
{
	GVariantBuilder builder;
	PassimItemPrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(PASSIM_IS_ITEM(self), NULL);

	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&builder, "{sv}", "filename", g_variant_new_string(priv->basename));
	g_variant_builder_add(&builder, "{sv}", "cmdline", g_variant_new_string(priv->cmdline));
	g_variant_builder_add(&builder, "{sv}", "hash", g_variant_new_string(priv->hash));
	g_variant_builder_add(&builder, "{sv}", "max-age", g_variant_new_uint32(priv->max_age));
	g_variant_builder_add(&builder, "{sv}", "flags", g_variant_new_uint64(priv->flags));
	g_variant_builder_add(&builder,
			      "{sv}",
			      "share-limit",
			      g_variant_new_uint32(priv->share_limit));
	g_variant_builder_add(&builder,
			      "{sv}",
			      "share-count",
			      g_variant_new_uint32(priv->share_count));
	return g_variant_builder_end(&builder);
}

/**
 * passim_item_from_variant:
 * @value: (not nullable): the serialized data
 *
 * Creates a new item using serialized data.
 *
 * Returns: (transfer full): a new #PassimItem, or %NULL if @value was invalid
 *
 * Since: 0.1.0
 **/
PassimItem *
passim_item_from_variant(GVariant *variant)
{
	GVariant *value;
	const gchar *key;
	g_autoptr(GVariantIter) iter = NULL;
	g_autoptr(PassimItem) self = passim_item_new();
	PassimItemPrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(variant != NULL, NULL);

	g_variant_get(variant, "a{sv}", &iter);
	while (g_variant_iter_next(iter, "{&sv}", &key, &value)) {
		if (g_strcmp0(key, "filename") == 0)
			priv->basename = g_variant_dup_string(value, NULL);
		if (g_strcmp0(key, "cmdline") == 0)
			priv->cmdline = g_variant_dup_string(value, NULL);
		if (g_strcmp0(key, "hash") == 0)
			priv->hash = g_variant_dup_string(value, NULL);
		if (g_strcmp0(key, "max-age") == 0)
			priv->max_age = g_variant_get_uint32(value);
		if (g_strcmp0(key, "share-limit") == 0)
			priv->share_limit = g_variant_get_uint32(value);
		if (g_strcmp0(key, "share-count") == 0)
			priv->share_count = g_variant_get_uint32(value);
		if (g_strcmp0(key, "flags") == 0)
			priv->flags = g_variant_get_uint64(value);
		g_variant_unref(value);
	}
	return g_steal_pointer(&self);
}

/**
 * passim_item_get_flags:
 * @self: a #PassimItem
 *
 * Gets the item flags.
 *
 * Returns: item flags, or 0 if unset
 *
 * Since: 0.1.0
 **/
guint64
passim_item_get_flags(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(PASSIM_IS_ITEM(self), 0);
	return priv->flags;
}

/**
 * passim_item_get_flags_as_string:
 * @self: a #PassimItem
 *
 * Gets the item flags.
 *
 * Returns: string
 *
 * Since: 0.1.0
 **/
gchar *
passim_item_get_flags_as_string(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GString) str = g_string_new(NULL);

	g_return_val_if_fail(PASSIM_IS_ITEM(self), 0);

	for (guint i = 0; i < 64; i++) {
		if ((priv->flags & ((guint64)1 << i)) == 0)
			continue;
		if (str->len > 0)
			g_string_append(str, ",");
		g_string_append(str, passim_item_flag_to_string((guint64)1 << i));
	}
	if (str->len == 0)
		g_string_append(str, passim_item_flag_to_string(PASSIM_ITEM_FLAG_NONE));
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/**
 * passim_item_set_flags:
 * @self: a #PassimItem
 * @flags: item flags, e.g. %PASSIM_ITEM_FLAG_NEXT_REBOOT
 *
 * Sets the item flags.
 *
 * Since: 0.1.0
 **/
void
passim_item_set_flags(PassimItem *self, guint64 flags)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(PASSIM_IS_ITEM(self));
	if (priv->flags == flags)
		return;
	priv->flags = flags;
}

/**
 * passim_item_add_flag:
 * @self: a #PassimItem
 * @flag: the #PassimItemFlags
 *
 * Adds a specific item flag to the item.
 *
 * Since: 0.1.0
 **/
void
passim_item_add_flag(PassimItem *self, PassimItemFlags flag)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(PASSIM_IS_ITEM(self));
	if (flag == 0)
		return;
	if ((priv->flags & flag) > 0)
		return;
	priv->flags |= flag;
}

/**
 * passim_item_remove_flag:
 * @self: a #PassimItem
 * @flag: a item flag
 *
 * Removes a specific item flag from the item.
 *
 * Since: 0.1.0
 **/
void
passim_item_remove_flag(PassimItem *self, PassimItemFlags flag)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(PASSIM_IS_ITEM(self));
	if (flag == 0)
		return;
	if ((priv->flags & flag) == 0)
		return;
	priv->flags &= ~flag;
}

/**
 * passim_item_has_flag:
 * @self: a #PassimItem
 * @flag: a item flag
 *
 * Finds if the item has a specific item flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 0.1.0
 **/
gboolean
passim_item_has_flag(PassimItem *self, PassimItemFlags flag)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(PASSIM_IS_ITEM(self), FALSE);
	return (priv->flags & flag) > 0;
}

/**
 * passim_item_flag_to_string:
 * @item_flag: item flags, e.g. %PASSIM_ITEM_FLAG_NEXT_REBOOT
 *
 * Converts an enumerated item flag to a string.
 *
 * Returns: identifier string
 *
 * Since: 0.1.0
 **/
const gchar *
passim_item_flag_to_string(PassimItemFlags item_flag)
{
	if (item_flag == PASSIM_ITEM_FLAG_NONE)
		return "none";
	if (item_flag == PASSIM_ITEM_FLAG_DISABLED)
		return "disabled";
	if (item_flag == PASSIM_ITEM_FLAG_NEXT_REBOOT)
		return "next-reboot";
	return NULL;
}

/**
 * passim_item_flag_from_string:
 * @item_flag: (nullable): a string, e.g. `next-reboot`
 *
 * Converts a string to an enumerated item flag.
 *
 * Returns: enumerated value
 *
 * Since: 0.1.0
 **/
PassimItemFlags
passim_item_flag_from_string(const gchar *item_flag)
{
	if (g_strcmp0(item_flag, "none") == 0)
		return PASSIM_ITEM_FLAG_NONE;
	if (g_strcmp0(item_flag, "disabled") == 0)
		return PASSIM_ITEM_FLAG_DISABLED;
	if (g_strcmp0(item_flag, "next-reboot") == 0)
		return PASSIM_ITEM_FLAG_NEXT_REBOOT;
	return PASSIM_ITEM_FLAG_UNKNOWN;
}

/**
 * passim_item_to_string:
 * @self: a #PassimItem
 *
 * Builds a text representation of the object.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 0.1.0
 **/
gchar *
passim_item_to_string(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *flags = passim_item_get_flags_as_string(self);
	g_return_val_if_fail(PASSIM_IS_ITEM(self), NULL);
	return g_strdup_printf("%s %s (flags: %s, cmdline: %s, age: %u/%u, share: %u/%u)",
			       priv->hash,
			       priv->basename,
			       flags,
			       priv->cmdline,
			       passim_item_get_age(self),
			       priv->max_age,
			       priv->share_count,
			       priv->share_limit);
}

static void
passim_item_init(PassimItem *self)
{
	PassimItemPrivate *priv = GET_PRIVATE(self);
	priv->max_age = 24 * 60 * 60;
	priv->share_limit = 5;
}

static void
passim_item_finalize(GObject *object)
{
	PassimItem *self = PASSIM_ITEM(object);
	PassimItemPrivate *priv = GET_PRIVATE(self);

	if (priv->file != NULL)
		g_object_unref(priv->file);
	if (priv->bytes != NULL)
		g_bytes_unref(priv->bytes);
	if (priv->ctime != NULL)
		g_date_time_unref(priv->ctime);
	g_free(priv->hash);
	g_free(priv->basename);
	g_free(priv->cmdline);

	G_OBJECT_CLASS(passim_item_parent_class)->finalize(object);
}

static void
passim_item_class_init(PassimItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = passim_item_finalize;
}

/**
 * passim_item_new:
 *
 * Creates a new item.
 *
 * Returns: a new #PassimItem
 *
 * Since: 0.1.0
 **/
PassimItem *
passim_item_new(void)
{
	PassimItem *self;
	self = g_object_new(PASSIM_TYPE_ITEM, NULL);
	return PASSIM_ITEM(self);
}
