/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "passim-avahi-service-resolver.h"
#include "passim-avahi.h"

typedef struct {
	gchar *object_path;
	gchar *signal_name;
	GVariant *parameters;
} PassimAvahiSignal;

static void
passim_avahi_signal_free(PassimAvahiSignal *signal)
{
	g_free(signal->object_path);
	g_free(signal->signal_name);
	g_variant_unref(signal->parameters);
	g_free(signal);
}

typedef struct {
	GDBusProxy *proxy;
	gchar *object_path;
	gchar *address;
	gulong signal_id;
	GDBusConnection *connection; /* no-ref -- not needed with new Avahi */
	guint subscription_id;	     /* not needed with new Avahi */
	GPtrArray *signals; /* element-type PassimAvahiSignal -- not needed with new Avahi */
} PassimAvahiServiceResolverHelper;

static void
passim_avahi_service_resolver_helper_free(PassimAvahiServiceResolverHelper *helper)
{
	if (helper->signal_id > 0)
		g_signal_handler_disconnect(helper->proxy, helper->signal_id);
	if (helper->proxy != NULL)
		g_object_unref(helper->proxy);
	if (helper->subscription_id != 0)
		g_dbus_connection_signal_unsubscribe(helper->connection, helper->subscription_id);
	g_ptr_array_unref(helper->signals);
	g_free(helper->address);
	g_free(helper->object_path);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(PassimAvahiServiceResolverHelper,
			      passim_avahi_service_resolver_helper_free)

static void
passim_avahi_service_resolver_free_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GVariant) val = NULL;
	PassimAvahiServiceResolverHelper *helper = g_task_get_task_data(task);

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	g_task_return_pointer(task, g_steal_pointer(&helper->address), g_free);
}

static void
passim_avahi_service_resolver_free(GTask *task)
{
	PassimAvahiServiceResolverHelper *helper = g_task_get_task_data(task);

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
			  passim_avahi_service_resolver_free_cb,
			  task);
}

static void
passim_avahi_service_resolver_signal(GTask *task, const gchar *signal_name, GVariant *parameters)
{
	PassimAvahiServiceResolverHelper *helper = g_task_get_task_data(task);
	if (g_strcmp0(signal_name, "Failure") == 0) {
		const gchar *errmsg = NULL;
		g_variant_get(parameters, "(&s)", &errmsg);
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", errmsg);
		return;
	}
	if (g_strcmp0(signal_name, "Found") == 0) {
		const gchar *host = NULL;
		guint16 port = 0;
		g_autoptr(GSocketAddress) socket_addr = NULL;
		g_variant_get(parameters,
			      "(iissssisqaayu)",
			      NULL,
			      NULL,
			      NULL,
			      NULL,
			      NULL,
			      NULL,
			      NULL,
			      &host,
			      &port,
			      NULL,
			      NULL);
		socket_addr = g_inet_socket_address_new_from_string(host, port);
		if (g_socket_address_get_family(socket_addr) == G_SOCKET_FAMILY_IPV6) {
			helper->address = g_strdup_printf("[%s]:%i", host, port);
		} else {
			helper->address = g_strdup_printf("%s:%i", host, port);
		}
		passim_avahi_service_resolver_free(task);
		return;
	}
	g_warning("unhandled ServiceResolver signal: %s %s",
		  signal_name,
		  g_variant_get_type_string(parameters));
}

static void
passim_avahi_service_resolver_signal_cb(GDBusProxy *proxy,
					const gchar *sender_name,
					const gchar *signal_name,
					GVariant *parameters,
					gpointer user_data)
{
	GTask *task = G_TASK(user_data);
	PassimAvahiServiceResolverHelper *helper = g_task_get_task_data(task);

	/* if we got here then we're either lucky or running with an Avahi that includes the
	 * fix in https://github.com/lathiat/avahi/pull/468 */
	if (helper->subscription_id != 0) {
		g_dbus_connection_signal_unsubscribe(helper->connection, helper->subscription_id);
		helper->subscription_id = 0;
	}
	passim_avahi_service_resolver_signal(task, signal_name, parameters);
}

static void
passim_avahi_service_resolver_start_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GTask *task = G_TASK(user_data); /* unref when we get the signal */
	PassimAvahiServiceResolverHelper *helper = g_task_get_task_data(task);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		g_object_unref(task);
		return;
	}
	g_debug("started %s", helper->object_path);
	for (guint i = 0; i < helper->signals->len; i++) {
		PassimAvahiSignal *signal = g_ptr_array_index(helper->signals, i);
		if (g_strcmp0(signal->object_path, helper->object_path) != 0) {
			g_debug("ignoring %s from %s", signal->signal_name, helper->object_path);
			continue;
		}
		g_info("working around Ahavi bug: %s sent before Start(), see "
		       "https://github.com/lathiat/avahi/pull/468",
		       signal->signal_name);
		passim_avahi_service_resolver_signal(task, signal->signal_name, signal->parameters);
	}
}

static void
passim_avahi_service_resolver_new_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	PassimAvahiServiceResolverHelper *helper = g_task_get_task_data(task);

	helper->proxy = g_dbus_proxy_new_for_bus_finish(res, &error);
	if (helper->proxy == NULL) {
		g_prefix_error(&error, "failed to use ServiceResolver %s: ", helper->object_path);
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}
	helper->signal_id = g_signal_connect(helper->proxy,
					     "g-signal",
					     G_CALLBACK(passim_avahi_service_resolver_signal_cb),
					     task);
	g_dbus_proxy_call(helper->proxy,
			  "Start",
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  PASSIM_SERVER_TIMEOUT,
			  g_task_get_cancellable(task),
			  passim_avahi_service_resolver_start_cb,
			  g_object_ref(task));
}

static void
passim_avahi_service_resolver_prepare_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = NULL;
	PassimAvahiServiceResolverHelper *helper = g_task_get_task_data(task);

	val = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (val == NULL) {
		g_prefix_error(&error, "failed to create a new ServiceResolver: ");
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
				 "org.freedesktop.Avahi.ServiceResolver",
				 g_task_get_cancellable(task),
				 passim_avahi_service_resolver_new_cb,
				 g_object_ref(task));
}

static void
passim_avahi_service_resolver_signal_fallback_cb(GDBusConnection *connection,
						 const gchar *sender_name,
						 const gchar *object_path,
						 const gchar *interface_name,
						 const gchar *signal_name,
						 GVariant *parameters,
						 gpointer user_data)
{
	GTask *task = G_TASK(user_data);
	PassimAvahiServiceResolverHelper *helper = g_task_get_task_data(task);
	PassimAvahiSignal *signal = g_new0(PassimAvahiSignal, 1);
	signal->object_path = g_strdup(object_path);
	signal->signal_name = g_strdup(signal_name);
	signal->parameters = g_variant_ref(parameters);
	g_ptr_array_add(helper->signals, signal);
}

void
passim_avahi_service_resolver_async(GDBusProxy *proxy,
				    PassimAvahiService *service,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer callback_data)
{
	g_autoptr(GTask) task = NULL;
	g_autoptr(PassimAvahiServiceResolverHelper) helper =
	    g_new0(PassimAvahiServiceResolverHelper, 1);

	task = g_task_new(proxy, cancellable, callback, callback_data);

	/* work around a bug in Avahi, see https://github.com/lathiat/avahi/issues/446 */
	helper->signals = g_ptr_array_new_with_free_func((GDestroyNotify)passim_avahi_signal_free);
	helper->connection = g_dbus_proxy_get_connection(proxy);
	helper->subscription_id =
	    g_dbus_connection_signal_subscribe(helper->connection,
					       g_dbus_proxy_get_name(proxy),
					       "org.freedesktop.Avahi.ServiceResolver",
					       NULL,
					       NULL,
					       NULL, /* argv */
					       G_DBUS_SIGNAL_FLAGS_NONE,
					       passim_avahi_service_resolver_signal_fallback_cb,
					       g_object_ref(task),
					       (GDestroyNotify)g_object_unref);
	g_task_set_task_data(task,
			     g_steal_pointer(&helper),
			     (GDestroyNotify)passim_avahi_service_resolver_helper_free);
	g_dbus_proxy_call(proxy,
			  "ServiceResolverPrepare",
			  g_variant_new("(iisssiu)",
					service->interface,
					service->protocol,
					service->name,
					service->type,
					service->domain,
					service->protocol,
					0), /* flags */
			  G_DBUS_CALL_FLAGS_NONE,
			  PASSIM_SERVER_TIMEOUT,
			  cancellable,
			  passim_avahi_service_resolver_prepare_cb,
			  g_steal_pointer(&task));
}

gchar *
passim_avahi_service_resolver_finish(GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(res != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}
