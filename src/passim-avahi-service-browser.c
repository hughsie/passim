/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "passim-avahi-service-browser.h"
#include "passim-avahi-service.h"
#include "passim-avahi.h"

typedef struct {
	GDBusProxy *proxy;
	GPtrArray *items; /* of PassimAvahiService */
	gchar *hash;
	gchar *object_path;
	gulong signal_id;
} PassimAvahiServiceBrowserHelper;

static void
passim_avahi_service_browser_helper_free(PassimAvahiServiceBrowserHelper *helper)
{
	if (helper->signal_id > 0)
		g_signal_handler_disconnect(helper->proxy, helper->signal_id);
	if (helper->proxy != NULL)
		g_object_unref(helper->proxy);
	g_ptr_array_unref(helper->items);
	g_free(helper->hash);
	g_free(helper->object_path);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(PassimAvahiServiceBrowserHelper,
			      passim_avahi_service_browser_helper_free)

static void
passim_avahi_service_browser_free_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GVariant) val = NULL;
	PassimAvahiServiceBrowserHelper *helper = g_task_get_task_data(task);

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	if (helper->items->len == 0) {
		g_task_return_new_error(task,
					G_IO_ERROR,
					G_IO_ERROR_FAILED,
					"failed to find %s",
					helper->hash);
		return;
	}
	g_task_return_pointer(task,
			      g_ptr_array_ref(helper->items),
			      (GDestroyNotify)g_ptr_array_unref);
}

static void
passim_avahi_service_browser_free(GTask *task)
{
	PassimAvahiServiceBrowserHelper *helper = g_task_get_task_data(task);

	/* needed? */
	if (helper->signal_id > 0) {
		g_signal_handler_disconnect(helper->proxy, helper->signal_id);
		helper->signal_id = 0;
	}
	g_dbus_proxy_call(helper->proxy,
			  "Free",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  PASSIM_SERVER_TIMEOUT,
			  g_task_get_cancellable(task),
			  passim_avahi_service_browser_free_cb,
			  task);
}

static void
passim_avahi_service_browser_signal_cb(GDBusProxy *proxy,
				       const gchar *sender_name,
				       const gchar *signal_name,
				       GVariant *parameters,
				       gpointer user_data)
{
	GTask *task = G_TASK(user_data);
	PassimAvahiServiceBrowserHelper *helper = g_task_get_task_data(task);

	if (g_strcmp0(signal_name, "CacheExhausted") == 0)
		return;
	if (g_strcmp0(signal_name, "AllForNow") == 0) {
		passim_avahi_service_browser_free(task);
		return;
	}
	if (g_strcmp0(signal_name, "Failure") == 0) {
		const gchar *errmsg = NULL;
		g_variant_get(parameters, "(&s)", &errmsg);
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", errmsg);
		return;
	}
	if (g_strcmp0(signal_name, "ItemNew") == 0) {
		g_autoptr(PassimAvahiService) item = g_new0(PassimAvahiService, 1);
		g_variant_get(parameters,
			      "(iisssu)",
			      &item->interface,
			      &item->protocol,
			      &item->name,
			      &item->type,
			      &item->domain,
			      &item->flags);
		if (item->flags & AVAHI_LOOKUP_RESULT_LOCAL) {
			g_debug("ignoring local result on interface %i", item->interface);
			return;
		}
		g_ptr_array_add(helper->items, g_steal_pointer(&item));
		return;
	}
	g_warning("unhandled ServiceBrowser signal: %s %s",
		  signal_name,
		  g_variant_get_type_string(parameters));
}

static void
passim_avahi_service_browser_start_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GTask *task = G_TASK(user_data); /* unref when we get the signal */
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		g_object_unref(task);
		return;
	}
}

static void
passim_avahi_service_browser_new_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	PassimAvahiServiceBrowserHelper *helper = g_task_get_task_data(task);

	helper->proxy = g_dbus_proxy_new_for_bus_finish(res, &error);
	if (helper->proxy == NULL) {
		g_prefix_error(&error, "failed to use ServiceBrowser %s: ", helper->object_path);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	helper->signal_id = g_signal_connect(helper->proxy,
					     "g-signal",
					     G_CALLBACK(passim_avahi_service_browser_signal_cb),
					     task);
	g_dbus_proxy_call(helper->proxy,
			  "Start",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  PASSIM_SERVER_TIMEOUT,
			  g_task_get_cancellable(task),
			  passim_avahi_service_browser_start_cb,
			  g_object_ref(task));
}

static void
passim_avahi_service_browser_prepare_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;
	PassimAvahiServiceBrowserHelper *helper = g_task_get_task_data(task);

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		g_prefix_error(&error, "failed to create a new ServiceBrowser: ");
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	g_variant_get(val, "(o)", &helper->object_path);
	g_debug("connecting to %s", helper->object_path);
	g_dbus_proxy_new_for_bus(G_BUS_TYPE_SYSTEM,
				 G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
				 NULL,
				 "org.freedesktop.Avahi",
				 helper->object_path,
				 "org.freedesktop.Avahi.ServiceBrowser",
				 g_task_get_cancellable(task),
				 passim_avahi_service_browser_new_cb,
				 g_object_ref(task));
}

void
passim_avahi_service_browser_async(GDBusProxy *proxy,
				   const gchar *hash,
				   AvahiProtocol protocol,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer callback_data)
{
	g_autofree gchar *subtype = passim_avahi_build_subtype_for_hash(hash);
	g_autoptr(GTask) task = NULL;
	g_autoptr(PassimAvahiServiceBrowserHelper) helper =
	    g_new0(PassimAvahiServiceBrowserHelper, 1);

	helper->hash = g_strdup(hash);
	helper->items = g_ptr_array_new_with_free_func((GDestroyNotify)passim_avahi_service_free);

	task = g_task_new(proxy, cancellable, callback, callback_data);
	g_task_set_task_data(task,
			     g_steal_pointer(&helper),
			     (GDestroyNotify)passim_avahi_service_browser_helper_free);
	g_dbus_proxy_call(proxy,
			  "ServiceBrowserPrepare",
			  g_variant_new("(iissu)",
					AVAHI_IF_UNSPEC,
					protocol,
					subtype,
					PASSIM_SERVER_DOMAIN,
					0), /* flags */
			  G_DBUS_CALL_FLAGS_NONE,
			  PASSIM_SERVER_TIMEOUT,
			  cancellable,
			  passim_avahi_service_browser_prepare_cb,
			  g_steal_pointer(&task));
}

/* element-type: PassimAvahiService */
GPtrArray *
passim_avahi_service_browser_finish(GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(res != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}
