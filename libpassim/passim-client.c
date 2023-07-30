/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gunixfdlist.h>

#include "passim-client.h"

/**
 * PassimClient:
 *
 * A shared client.
 */

typedef struct {
	GDBusProxy *proxy;
	gchar *version;
} PassimClientPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(PassimClient, passim_client, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (passim_client_get_instance_private(o))

/**
 * passim_client_get_version:
 * @self: a #PassimClient
 *
 * Gets the daemon version.
 *
 * Returns: the version string, or %NULL if unset
 *
 * Since: 0.1.0
 **/
const gchar *
passim_client_get_version(PassimClient *self)
{
	PassimClientPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(PASSIM_IS_CLIENT(self), NULL);
	return priv->version;
}

/**
 * passim_client_load:
 * @self: a #PassimClient
 * @error: (nullable): optional return location for an error
 *
 * Loads the client from a file on disk.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.1.0
 **/
gboolean
passim_client_load(PassimClient *self, GError **error)
{
	PassimClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GVariant) version = NULL;

	g_return_val_if_fail(PASSIM_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	priv->proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
						    G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
						    NULL,
						    PASSIM_DBUS_SERVICE,
						    PASSIM_DBUS_PATH,
						    PASSIM_DBUS_INTERFACE,
						    NULL,
						    error);
	if (priv->proxy == NULL)
		return FALSE;
	version = g_dbus_proxy_get_cached_property(priv->proxy, "DaemonVersion");
	if (version != NULL)
		priv->version = g_variant_dup_string(version, NULL);

	/* success */
	return TRUE;
}

static GPtrArray *
passim_item_array_from_variant(GVariant *value)
{
	GPtrArray *items = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(GVariant) untuple = g_variant_get_child_value(value, 0);
	gsize sz = g_variant_n_children(untuple);
	for (guint i = 0; i < sz; i++) {
		g_autoptr(GVariant) data = g_variant_get_child_value(untuple, i);
		g_ptr_array_add(items, passim_item_from_variant(data));
	}
	return items;
}

/**
 * passim_client_get_items:
 * @self: a #PassimClient
 * @error: (nullable): optional return location for an error
 *
 * Get items currently published by the daemon.
 *
 * Returns: (element-type PassimItem) (transfer container): items, or %NULL for error
 *
 * Since: 0.1.0
 **/
GPtrArray *
passim_client_get_items(PassimClient *self, GError **error)
{
	PassimClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail(PASSIM_IS_CLIENT(self), NULL);
	g_return_val_if_fail(priv->proxy != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	val = g_dbus_proxy_call_sync(priv->proxy,
				     "GetItems",
				     NULL,
				     G_DBUS_CALL_FLAGS_NONE,
				     1500,
				     NULL,
				     error);
	if (val == NULL)
		return FALSE;
	return passim_item_array_from_variant(val);
}

/**
 * passim_client_unpublish:
 * @self: a #PassimClient
 * @hash: (not nullable): an item hash value
 * @error: (nullable): optional return location for an error
 *
 * Unpublish a file from the index.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.1.0
 **/
gboolean
passim_client_unpublish(PassimClient *self, const gchar *hash, GError **error)
{
	PassimClientPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail(PASSIM_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(priv->proxy != NULL, FALSE);
	g_return_val_if_fail(hash != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	val = g_dbus_proxy_call_sync(priv->proxy,
				     "Unpublish",
				     g_variant_new("(s)", hash),
				     G_DBUS_CALL_FLAGS_NONE,
				     1500,
				     NULL,
				     error);
	return val != NULL;
}

/**
 * passim_client_publish:
 * @self: a #PassimClient
 * @item: (not nullable): a #PassimItem
 * @error: (nullable): optional return location for an error
 *
 * Connects to the remote server.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.1.0
 **/
gboolean
passim_client_publish(PassimClient *self, PassimItem *item, GError **error)
{
	PassimClientPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *filename = NULL;
	g_autoptr(GDBusMessage) reply = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GIOChannel) io = NULL;
	g_autoptr(GUnixFDList) fd_list = g_unix_fd_list_new();

	g_return_val_if_fail(PASSIM_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(PASSIM_IS_ITEM(item), FALSE);
	g_return_val_if_fail(priv->proxy != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* set out of band file descriptor */
	filename = g_file_get_path(passim_item_get_file(item));
	io = g_io_channel_new_file(filename, "r", error);
	if (io == NULL)
		return FALSE;
	g_unix_fd_list_append(fd_list, g_io_channel_unix_get_fd(io), NULL);
	request = g_dbus_message_new_method_call(g_dbus_proxy_get_name(priv->proxy),
						 g_dbus_proxy_get_object_path(priv->proxy),
						 g_dbus_proxy_get_interface_name(priv->proxy),
						 "Publish");
	g_dbus_message_set_unix_fd_list(request, fd_list);

	/* call into daemon */
	g_dbus_message_set_body(request,
				g_variant_new("(hstuu)",
					      g_io_channel_unix_get_fd(io),
					      passim_item_get_basename(item),
					      passim_item_get_flags(item),
					      passim_item_get_max_age(item),
					      passim_item_get_share_limit(item)));
	reply =
	    g_dbus_connection_send_message_with_reply_sync(g_dbus_proxy_get_connection(priv->proxy),
							   request,
							   G_DBUS_SEND_MESSAGE_FLAGS_NONE,
							   G_MAXINT,
							   NULL,
							   NULL, /* cancellable */
							   error);
	if (reply == NULL)
		return FALSE;
	if (g_dbus_message_to_gerror(reply, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
passim_client_init(PassimClient *self)
{
}

static void
passim_client_finalize(GObject *object)
{
	PassimClient *self = PASSIM_CLIENT(object);
	PassimClientPrivate *priv = GET_PRIVATE(self);

	if (priv->proxy != NULL)
		g_object_unref(priv->proxy);
	g_free(priv->version);

	G_OBJECT_CLASS(passim_client_parent_class)->finalize(object);
}

static void
passim_client_class_init(PassimClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = passim_client_finalize;
}

/**
 * passim_client_new:
 *
 * Creates a new client.
 *
 * Returns: a new #PassimClient
 *
 * Since: 0.1.0
 **/
PassimClient *
passim_client_new(void)
{
	PassimClient *self;
	self = g_object_new(PASSIM_TYPE_CLIENT, NULL);
	return PASSIM_CLIENT(self);
}
