/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "passim-avahi-service-browser.h"
#include "passim-avahi-service-resolver.h"
#include "passim-avahi-service.h"
#include "passim-avahi.h"

struct _PassimAvahi {
	GObject parent_instance;
	gchar *name;
	GKeyFile *config;
	GDBusProxy *proxy;
	GDBusProxy *proxy_eg;
};

G_DEFINE_TYPE(PassimAvahi, passim_avahi, G_TYPE_OBJECT)

const gchar *
passim_avahi_get_name(PassimAvahi *self)
{
	return self->name;
}

static gchar *
passim_avahi_truncate_hash(const gchar *hash)
{
	return g_strndup(hash, 60);
}

gchar *
passim_avahi_build_subtype_for_hash(const gchar *hash)
{
	g_autofree gchar *truncated_hash = passim_avahi_truncate_hash(hash);
	return g_strdup_printf("_%s._sub.%s", truncated_hash, PASSIM_SERVER_TYPE);
}

static void
passim_avahi_proxy_signal_cb(GDBusProxy *proxy,
			     char *sender_name,
			     char *signal_name,
			     GVariant *parameters,
			     gpointer user_data)
{
	g_info("signal_name: %s %s", signal_name, g_variant_get_type_string(parameters));
}

gboolean
passim_avahi_connect(PassimAvahi *self, GError **error)
{
	const gchar *object_path = NULL;
	g_autoptr(GVariant) object_pathv = NULL;

	g_return_val_if_fail(PASSIM_IS_AVAHI(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail(self->proxy == NULL, FALSE);

	/* connect to daemon */
	self->proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
						    G_DBUS_PROXY_FLAGS_NONE,
						    NULL,
						    "org.freedesktop.Avahi",
						    "/",
						    "org.freedesktop.Avahi.Server2",
						    NULL,
						    error);
	if (self->proxy == NULL) {
		g_prefix_error(error, "failed to contact Avahi: ");
		return FALSE;
	}
	g_signal_connect(self->proxy, "g-signal", G_CALLBACK(passim_avahi_proxy_signal_cb), self);

	/* create our entrygroup */
	object_pathv = g_dbus_proxy_call_sync(self->proxy,
					      "EntryGroupNew",
					      NULL,
					      G_DBUS_CALL_FLAGS_NONE,
					      PASSIM_SERVER_TIMEOUT,
					      NULL,
					      error);
	if (object_pathv == NULL) {
		g_prefix_error(error, "failed to create a new entry group: ");
		return FALSE;
	}
	g_variant_get(object_pathv, "(&o)", &object_path);
	g_debug("connecting to %s", object_path);
	self->proxy_eg = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
						       G_DBUS_PROXY_FLAGS_NONE,
						       NULL,
						       "org.freedesktop.Avahi",
						       object_path,
						       "org.freedesktop.Avahi.EntryGroup",
						       NULL,
						       error);
	if (self->proxy_eg == NULL) {
		g_prefix_error(error, "failed to use EntryGroup %s: ", object_path);
		return FALSE;
	}
	g_signal_connect(self->proxy_eg,
			 "g-signal",
			 G_CALLBACK(passim_avahi_proxy_signal_cb),
			 self);

	/* success */
	return TRUE;
}

static gboolean
passim_avahi_register_subtype(PassimAvahi *self, const gchar *hash, GError **error)
{
	g_autofree gchar *subtype = passim_avahi_build_subtype_for_hash(hash);
	g_autoptr(GVariant) val = NULL;

	g_debug("adding subtype %s", subtype);
	val = g_dbus_proxy_call_sync(self->proxy_eg,
				     "AddServiceSubtype",
				     g_variant_new("(iiussss)",
						   AVAHI_IF_UNSPEC,
						   AVAHI_PROTO_UNSPEC,
						   0 /* flags */,
						   self->name,
						   PASSIM_SERVER_TYPE,
						   PASSIM_SERVER_DOMAIN,
						   subtype),
				     G_DBUS_CALL_FLAGS_NONE,
				     PASSIM_SERVER_TIMEOUT,
				     NULL,
				     error);
	if (val == NULL) {
		g_prefix_error(error, "failed to add service subtype: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
passim_avahi_register(PassimAvahi *self, gchar **keys, GError **error)
{
	g_autoptr(GVariant) val1 = NULL;
	g_autoptr(GVariant) val2 = NULL;
	g_autoptr(GVariant) val4 = NULL;

	g_return_val_if_fail(PASSIM_IS_AVAHI(self), FALSE);
	g_return_val_if_fail(keys != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail(self->proxy != NULL, FALSE);

	g_debug("resetting %s", self->name);
	val1 = g_dbus_proxy_call_sync(self->proxy_eg,
				      "Reset",
				      NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      PASSIM_SERVER_TIMEOUT,
				      NULL,
				      error);
	if (val1 == NULL) {
		g_prefix_error(error, "failed to reset entry group: ");
		return FALSE;
	}
	val2 = g_dbus_proxy_call_sync(self->proxy_eg,
				      "AddService",
				      g_variant_new("(iiussssqaay)",
						    AVAHI_IF_UNSPEC,
						    AVAHI_PROTO_UNSPEC,
						    0 /* flags */,
						    self->name,
						    PASSIM_SERVER_TYPE,
						    PASSIM_SERVER_DOMAIN,
						    PASSIM_SERVER_HOST,
						    passim_config_get_port(self->config),
						    NULL),
				      G_DBUS_CALL_FLAGS_NONE,
				      PASSIM_SERVER_TIMEOUT,
				      NULL,
				      error);
	if (val2 == NULL) {
		g_prefix_error(error, "failed to add service: ");
		return FALSE;
	}
	for (guint i = 0; keys[i] != NULL; i++) {
		if (!passim_avahi_register_subtype(self, keys[i], error))
			return FALSE;
	}
	val4 = g_dbus_proxy_call_sync(self->proxy_eg,
				      "Commit",
				      NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      PASSIM_SERVER_TIMEOUT,
				      NULL,
				      error);
	if (val4 == NULL) {
		g_prefix_error(error, "failed to commit entry group: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

typedef struct {
	GDBusProxy *proxy;
	gchar *object_path;
	gchar *hash;
	gulong signal_id;
	GPtrArray *items;     /* of PassimAvahiService */
	GPtrArray *addresses; /* of utf-8 */
} PassimAvahiFindHelper;

static void
passim_avahi_service_resolve_next(GTask *task);

static void
passim_avahi_find_helper_free(PassimAvahiFindHelper *helper)
{
	if (helper->proxy != NULL)
		g_object_unref(helper->proxy);
	if (helper->items != NULL)
		g_ptr_array_unref(helper->items);
	if (helper->addresses != NULL)
		g_ptr_array_unref(helper->addresses);
	g_free(helper->hash);
	g_free(helper->object_path);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(PassimAvahiFindHelper, passim_avahi_find_helper_free)

static void
passim_avahi_service_resolve_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autofree gchar *address = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	PassimAvahiFindHelper *helper = g_task_get_task_data(task);

	address = passim_avahi_service_resolver_finish(res, &error);
	if (address == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	if (g_ptr_array_find_with_equal_func(helper->addresses, address, g_str_equal, NULL)) {
		g_debug("already found %s, ignoring", address);
	} else {
		g_debug("new address %s, adding", address);
		g_ptr_array_add(helper->addresses, g_steal_pointer(&address));
	}
	passim_avahi_service_resolve_next(g_steal_pointer(&task));
}

static void
passim_avahi_service_resolve_item(GTask *task, PassimAvahiService *item)
{
	PassimAvahi *self = PASSIM_AVAHI(g_task_get_source_object(task));

	g_debug(
	    "ServiceResolverPrepare{ iface:%i, proto:%i, name:%s, type:%s, domain:%s, flags:%u }",
	    item->interface,
	    item->protocol,
	    item->name,
	    item->type,
	    item->domain,
	    item->flags);
	passim_avahi_service_resolver_async(self->proxy,
					    item,
					    g_task_get_cancellable(task),
					    passim_avahi_service_resolve_cb,
					    task);
}

static void
passim_avahi_service_resolve_next(GTask *task)
{
	PassimAvahiFindHelper *helper = g_task_get_task_data(task);
	PassimAvahiService *item;

	if (helper->items->len == 0) {
		if (helper->addresses->len > 0) {
			g_task_return_pointer(task,
					      g_steal_pointer(&helper->addresses),
					      (GDestroyNotify)g_ptr_array_unref);
			g_object_unref(task);
			return;
		}
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "cannot find hash");
		g_object_unref(task);
		return;
	}

	/* resolve the next one */
	item = g_ptr_array_steal_index(helper->items, 0);
	passim_avahi_service_resolve_item(task, item);
}

static void
passim_avahi_service_browser_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	PassimAvahiFindHelper *helper = g_task_get_task_data(task);

	helper->items = passim_avahi_service_browser_finish(res, &error);
	if (helper->items == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	for (guint i = 0; i < helper->items->len; i++) {
		PassimAvahiService *item = g_ptr_array_index(helper->items, i);
		passim_avahi_service_print(item);
	}
	passim_avahi_service_resolve_next(g_steal_pointer(&task));
}

void
passim_avahi_find_async(PassimAvahi *self,
			const gchar *hash,
			GCancellable *cancellable,
			GAsyncReadyCallback callback,
			gpointer callback_data)
{
	g_autoptr(GTask) task = NULL;
	g_autofree gchar *truncated_hash = passim_avahi_truncate_hash(hash);
	g_autoptr(PassimAvahiFindHelper) helper = g_new0(PassimAvahiFindHelper, 1);

	g_return_if_fail(PASSIM_IS_AVAHI(self));
	g_return_if_fail(hash != NULL);
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));
	g_return_if_fail(self->proxy != NULL);

	helper->hash = g_strdup(hash);
	helper->addresses = g_ptr_array_new_with_free_func(g_free);

	task = g_task_new(self, cancellable, callback, callback_data);
	g_task_set_task_data(task,
			     g_steal_pointer(&helper),
			     (GDestroyNotify)passim_avahi_find_helper_free);
	passim_avahi_service_browser_async(self->proxy,
					   truncated_hash,
					   cancellable,
					   passim_avahi_service_browser_cb,
					   g_steal_pointer(&task));
}

/* element-type utf-8 */
GPtrArray *
passim_avahi_find_finish(PassimAvahi *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(PASSIM_IS_AVAHI(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_pointer(G_TASK(res), error);
}

static void
passim_avahi_init(PassimAvahi *self)
{
	self->name =
	    g_strdup_printf("%s-%04X", "Passim", (guint)g_random_int_range(0, G_MAXUINT16));
}

static void
passim_avahi_finalize(GObject *obj)
{
	PassimAvahi *self = PASSIM_AVAHI(obj);
	g_free(self->name);
	g_key_file_unref(self->config);
	if (self->proxy != NULL)
		g_object_unref(self->proxy);
	if (self->proxy_eg != NULL)
		g_object_unref(self->proxy_eg);
	G_OBJECT_CLASS(passim_avahi_parent_class)->finalize(obj);
}

static void
passim_avahi_class_init(PassimAvahiClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = passim_avahi_finalize;
}

PassimAvahi *
passim_avahi_new(GKeyFile *config)
{
	PassimAvahi *self;
	self = g_object_new(PASSIM_TYPE_AVAHI, NULL);
	self->config = g_key_file_ref(config);
	return PASSIM_AVAHI(self);
}
