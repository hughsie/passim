/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "passim-item.h"

PassimItem *
passim_item_new(void)
{
	PassimItem *item = g_new0(PassimItem, 1);
	return item;
}

void
passim_item_free(PassimItem *item)
{
	if (item->file != NULL)
		g_object_unref(item->file);
	if (item->ctime != NULL)
		g_date_time_unref(item->ctime);
	g_free(item->hash);
	g_free(item->basename);
	g_free(item);
}

GVariant *
passim_item_to_variant(PassimItem *item)
{
	GVariantBuilder builder;
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&builder, "{sv}", "filename", g_variant_new_string(item->basename));
	g_variant_builder_add(&builder, "{sv}", "hash", g_variant_new_string(item->hash));
	g_variant_builder_add(&builder, "{sv}", "max-age", g_variant_new_uint32(item->max_age));
	g_variant_builder_add(&builder,
			      "{sv}",
			      "share-limit",
			      g_variant_new_uint32(item->share_limit));
	g_variant_builder_add(&builder,
			      "{sv}",
			      "share-count",
			      g_variant_new_uint32(item->share_count));
	return g_variant_builder_end(&builder);
}

PassimItem *
passim_item_from_variant(GVariant *variant)
{
	PassimItem *item = passim_item_new();
	GVariant *value;
	const gchar *key;
	g_autoptr(GVariantIter) iter = NULL;

	g_variant_get(variant, "a{sv}", &iter);
	while (g_variant_iter_next(iter, "{&sv}", &key, &value)) {
		if (g_strcmp0(key, "filename") == 0)
			item->basename = g_variant_dup_string(value, NULL);
		if (g_strcmp0(key, "hash") == 0)
			item->hash = g_variant_dup_string(value, NULL);
		if (g_strcmp0(key, "max-age") == 0)
			item->max_age = g_variant_get_uint32(value);
		if (g_strcmp0(key, "share-limit") == 0)
			item->share_limit = g_variant_get_uint32(value);
		if (g_strcmp0(key, "share-count") == 0)
			item->share_count = g_variant_get_uint32(value);
		g_variant_unref(value);
	}
	return item;
}
