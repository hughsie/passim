/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <passim.h>

#include "passim-avahi.h"
#include "passim-common.h"
#include "passim-gnutls.h"

typedef struct {
	GDBusConnection *connection;
	GDBusNodeInfo *introspection_daemon;
	GDBusProxy *proxy_uid;
	GHashTable *items; /* utf-8:PassimItem */
	GFileMonitor *sysconfpkg_monitor;
	guint sysconfpkg_rescan_id;
	GKeyFile *kf;
	GMainLoop *loop;
	PassimAvahi *avahi;
	GNetworkMonitor *network_monitor;
	gchar *root;
	guint16 port;
	guint owner_id;
	guint poll_item_age_id;
	guint timed_exit_id;
	PassimStatus status;
} PassimServer;

static void
passim_server_free(PassimServer *self)
{
	if (self->sysconfpkg_rescan_id != 0)
		g_source_remove(self->sysconfpkg_rescan_id);
	if (self->poll_item_age_id != 0)
		g_source_remove(self->poll_item_age_id);
	if (self->timed_exit_id != 0)
		g_source_remove(self->timed_exit_id);
	if (self->loop != NULL)
		g_main_loop_unref(self->loop);
	if (self->avahi != NULL)
		g_object_unref(self->avahi);
	if (self->sysconfpkg_monitor != NULL)
		g_object_unref(self->sysconfpkg_monitor);
	if (self->items != NULL)
		g_hash_table_unref(self->items);
	if (self->kf != NULL)
		g_key_file_unref(self->kf);
	if (self->proxy_uid != NULL)
		g_object_unref(self->proxy_uid);
	if (self->connection != NULL)
		g_object_unref(self->connection);
	if (self->introspection_daemon != NULL)
		g_dbus_node_info_unref(self->introspection_daemon);
	g_free(self->root);
	g_free(self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(PassimServer, passim_server_free)

static void
passim_server_engine_changed(PassimServer *self)
{
	/* not yet connected */
	if (self->connection == NULL)
		return;
	g_dbus_connection_emit_signal(self->connection,
				      NULL,
				      PASSIM_DBUS_PATH,
				      PASSIM_DBUS_INTERFACE,
				      "Changed",
				      NULL,
				      NULL);
}

static void
passim_server_emit_property_changed(PassimServer *self,
				    const gchar *property_name,
				    GVariant *property_value)
{
	GVariantBuilder builder;
	GVariantBuilder invalidated_builder;

	/* not yet connected */
	if (self->connection == NULL) {
		g_variant_unref(g_variant_ref_sink(property_value));
		return;
	}

	/* build the dict */
	g_variant_builder_init(&invalidated_builder, G_VARIANT_TYPE("as"));
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&builder, "{sv}", property_name, property_value);
	g_dbus_connection_emit_signal(
	    self->connection,
	    NULL,
	    PASSIM_DBUS_PATH,
	    "org.freedesktop.DBus.Properties",
	    "PropertiesChanged",
	    g_variant_new("(sa{sv}as)", PASSIM_DBUS_INTERFACE, &builder, &invalidated_builder),
	    NULL);
	g_variant_builder_clear(&builder);
	g_variant_builder_clear(&invalidated_builder);
}

static void
passim_server_set_status(PassimServer *self, PassimStatus status)
{
	/* sanity check */
	if (self->status == status)
		return;
	self->status = status;
	g_debug("Emitting PropertyChanged('Status'='%s')", passim_status_to_string(status));
	passim_server_emit_property_changed(self, "Status", g_variant_new_uint32(status));
	passim_server_engine_changed(self);
}

static gboolean
passim_server_avahi_register(PassimServer *self, GError **error)
{
	guint keyidx = 0;
	g_autofree const gchar **keys = NULL;
	g_autoptr(GList) items = NULL;

	/* sanity check */
	if (self->status == PASSIM_STATUS_STARTING) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_MOUNTED,
			    "http server has not yet started");
		return FALSE;
	}

	/* never publish when on a metered connection */
	if (g_network_monitor_get_network_metered(self->network_monitor)) {
		g_info("on a metered connection, unregistering");
		passim_server_set_status(self, PASSIM_STATUS_DISABLED_METERED);
		return passim_avahi_unregister(self->avahi, error);
	}

	/* build a GStrv of hashes */
	items = g_hash_table_get_values(self->items);
	keys = g_new0(const gchar *, g_list_length(items) + 1);
	for (GList *l = items; l != NULL; l = l->next) {
		PassimItem *item = PASSIM_ITEM(l->data);
		if (passim_item_has_flag(item, PASSIM_ITEM_FLAG_DISABLED))
			continue;
		keys[keyidx++] = passim_item_get_hash(item);
	}
	if (!passim_avahi_register(self->avahi, (gchar **)keys, error))
		return FALSE;

	/* success */
	passim_server_set_status(self, PASSIM_STATUS_RUNNING);
	return TRUE;
}

static gboolean
passim_server_add_item(PassimServer *self, PassimItem *item, GError **error)
{
	g_debug("added https://localhost:%u/%s?sha256=%s",
		self->port,
		passim_item_get_basename(item),
		passim_item_get_hash(item));
	g_hash_table_insert(self->items, g_strdup(passim_item_get_hash(item)), g_object_ref(item));
	return TRUE;
}

static gboolean
passim_item_load_bytes_nofollow(PassimItem *item, const gchar *filename, GError **error)
{
	gint fd;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GInputStream) istream = NULL;
	g_autoptr(GMappedFile) mapped_file = NULL;

	/* load bytes from the fd to avoid TOCTOU */
	fd = g_open(filename, O_NOFOLLOW, S_IRUSR);
	if (fd < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_PERMISSION_DENIED,
			    "skipping symlink %s",
			    filename);
		return FALSE;
	}
	istream = g_unix_input_stream_new(fd, TRUE);
	if (istream == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "no stream to read from %s",
			    filename);
		return FALSE;
	}
	mapped_file = g_mapped_file_new_from_fd(fd, FALSE, error);
	if (mapped_file == NULL)
		return FALSE;
	bytes = g_mapped_file_get_bytes(mapped_file);
	passim_item_set_bytes(item, bytes);
	return TRUE;
}

static gboolean
passim_server_libdir_add(PassimServer *self, const gchar *filename, GError **error)
{
	guint32 value;
	g_autofree gchar *basename = g_path_get_basename(filename);
	g_autofree gchar *boot_time = NULL;
	g_autofree gchar *cmdline = NULL;
	g_auto(GStrv) split = g_strsplit(basename, "-", 2);
	g_autoptr(PassimItem) item = passim_item_new();

	/* this doesn't have to be a sha256 hash, but it has to be *something* */
	if (g_strv_length(split) != 2) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_FILENAME,
			    "expected {hash}-{filename} and got %s",
			    basename);
		return FALSE;
	}

	/* create new item */
	passim_item_set_basename(item, split[1]);
	if (!passim_item_load_bytes_nofollow(item, filename, error))
		return FALSE;
	if (!passim_item_load_filename(item, filename, error))
		return FALSE;

	/* not required now */
	passim_item_set_bytes(item, NULL);

	/* get optional attributes */
	value = passim_xattr_get_uint32(filename, "user.max_age", 24 * 60 * 60, error);
	if (value == G_MAXUINT32)
		return FALSE;
	passim_item_set_max_age(item, value);
	value = passim_xattr_get_uint32(filename, "user.share_limit", 5, error);
	if (value == G_MAXUINT32)
		return FALSE;
	passim_item_set_share_limit(item, value);
	cmdline = passim_xattr_get_string(filename, "user.cmdline", error);
	if (cmdline == NULL)
		return FALSE;
	passim_item_set_cmdline(item, cmdline);

	/* only allowed when rebooted */
	boot_time = passim_xattr_get_string(filename, "user.boot_time", NULL);
	if (boot_time != NULL) {
		g_autofree gchar *boot_time_now = passim_get_boot_time();
		if (g_strcmp0(boot_time_now, boot_time) == 0) {
			passim_item_add_flag(item, PASSIM_ITEM_FLAG_NEXT_REBOOT);
			passim_item_add_flag(item, PASSIM_ITEM_FLAG_DISABLED);
		}
	}
	return passim_server_add_item(self, item, error);
}

static gboolean
passim_server_libdir_scan(PassimServer *self, GError **error)
{
	g_autoptr(GDir) dir = NULL;
	const gchar *fn;

	/* sanity check */
	if (!g_file_test(self->root, G_FILE_TEST_EXISTS)) {
		g_debug("not loading resources from %s as it does not exist", self->root);
		return TRUE;
	}

	g_debug("loading resources from %s", self->root);
	dir = g_dir_open(self->root, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name(dir)) != NULL) {
		g_autofree gchar *path = g_build_filename(self->root, fn, NULL);
		if (!passim_server_libdir_add(self, path, error))
			return FALSE;
	}
	passim_server_engine_changed(self);
	return TRUE;
}

static gboolean
passim_server_sysconfpkgdir_add(PassimServer *self, const gchar *filename, GError **error)
{
	g_autofree gchar *hash = NULL;
	g_autoptr(PassimItem) item = passim_item_new();

	/* get optional attributes */
	hash = passim_xattr_get_string(filename, "user.checksum.sha256", NULL);
	if (hash != NULL && g_strcmp0(hash, "") != 0)
		passim_item_set_hash(item, hash);
	if (!passim_item_load_bytes_nofollow(item, filename, error))
		return FALSE;
	if (!passim_item_load_filename(item, filename, error))
		return FALSE;

	/* not required now */
	passim_item_set_bytes(item, NULL);

	/* never delete these */
	passim_item_set_max_age(item, G_MAXUINT32);
	passim_item_set_share_limit(item, G_MAXUINT32);

	/* save this for next time */
	if (hash == NULL || g_strcmp0(hash, "") == 0) {
		passim_xattr_set_string(filename,
					"user.checksum.sha256",
					passim_item_get_hash(item),
					NULL);
	}
	return passim_server_add_item(self, item, error);
}

static gboolean
passim_server_sysconfpkgdir_scan_path(PassimServer *self, const gchar *path, GError **error)
{
	g_autoptr(GDir) dir = NULL;
	const gchar *fn;

	/* sanity check */
	if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
		g_debug("not loading resources from %s as it does not exist", path);
		return TRUE;
	}

	g_debug("scanning %s", path);
	dir = g_dir_open(path, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name(dir)) != NULL) {
		g_autofree gchar *fn_tmp = g_build_filename(path, fn, NULL);
		g_autoptr(GError) error_local = NULL;
		if (!passim_server_sysconfpkgdir_add(self, fn_tmp, &error_local)) {
			if (g_error_matches(error_local,
					    G_IO_ERROR,
					    G_IO_ERROR_PERMISSION_DENIED)) {
				g_info("skipping %s as EPERM", fn_tmp);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
passim_server_sysconfpkgdir_scan_keyfile(PassimServer *self, const gchar *filename, GError **error)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GKeyFile) kf = g_key_file_new();

	if (!g_key_file_load_from_file(kf, filename, G_KEY_FILE_NONE, error))
		return FALSE;
	path = g_key_file_get_string(kf, "passim", "Path", error);
	if (path == NULL)
		return FALSE;
	return passim_server_sysconfpkgdir_scan_path(self, path, error);
}

static gboolean
passim_server_sysconfpkgdir_scan(PassimServer *self, GError **error);

static gboolean
passim_server_sysconfpkgdir_timeout_cb(gpointer user_data)
{
	PassimServer *self = (PassimServer *)user_data;
	g_autoptr(GError) error_local1 = NULL;
	g_autoptr(GError) error_local2 = NULL;

	/* done */
	self->sysconfpkg_rescan_id = 0;

	/* rescan and re-register */
	if (!passim_server_sysconfpkgdir_scan(self, &error_local1))
		g_printerr("failed to scan sysconfpkg directory: %s\n", error_local1->message);
	if (!passim_server_avahi_register(self, &error_local2))
		g_warning("failed to register: %s", error_local2->message);
	return G_SOURCE_REMOVE;
}

static void
passim_server_sysconfpkgdir_changed_cb(GFileMonitor *monitor,
				       GFile *file,
				       GFile *other_file,
				       GFileMonitorEvent event_type,
				       gpointer user_data)
{
	PassimServer *self = (PassimServer *)user_data;

	/* rate limit */
	if (self->sysconfpkg_rescan_id != 0)
		g_source_remove(self->sysconfpkg_rescan_id);
	self->sysconfpkg_rescan_id =
	    g_timeout_add(500, passim_server_sysconfpkgdir_timeout_cb, self);
}

static gboolean
passim_server_sysconfpkgdir_watch(PassimServer *self, GError **error)
{
	g_autofree gchar *sysconfpkgdir = g_build_filename(PACKAGE_SYSCONFDIR, "passim.d", NULL);
	g_autoptr(GFile) file = g_file_new_for_path(sysconfpkgdir);

	self->sysconfpkg_monitor = g_file_monitor_directory(file, G_FILE_MONITOR_NONE, NULL, error);
	if (self->sysconfpkg_monitor == NULL)
		return FALSE;
	g_signal_connect(self->sysconfpkg_monitor,
			 "changed",
			 G_CALLBACK(passim_server_sysconfpkgdir_changed_cb),
			 self);
	return TRUE;
}

static gboolean
passim_server_sysconfpkgdir_scan(PassimServer *self, GError **error)
{
	const gchar *fn;
	g_autofree gchar *sysconfpkgdir = g_build_filename(PACKAGE_SYSCONFDIR, "passim.d", NULL);
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GList) items = g_hash_table_get_values(self->items);

	/* remove all existing sysconfpkgdir items */
	for (GList *l = items; l != NULL; l = l->next) {
		PassimItem *item = PASSIM_ITEM(l->data);
		if (passim_item_get_cmdline(item) == NULL &&
		    passim_item_get_max_age(item) == G_MAXUINT32 &&
		    passim_item_get_share_limit(item) == G_MAXUINT32) {
			g_debug("removing %s due to rescan", passim_item_get_hash(item));
			g_hash_table_remove(self->items, passim_item_get_hash(item));
		}
	}

	/* sanity check */
	if (!g_file_test(sysconfpkgdir, G_FILE_TEST_EXISTS)) {
		g_debug("not loading resources from %s as it does not exist", sysconfpkgdir);
		return TRUE;
	}

	g_debug("loading sysconfpkgdir config from %s", sysconfpkgdir);
	dir = g_dir_open(sysconfpkgdir, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name(dir)) != NULL) {
		g_autofree gchar *fn_tmp = g_build_filename(sysconfpkgdir, fn, NULL);
		if (!g_str_has_suffix(fn_tmp, ".conf"))
			continue;
		if (!passim_server_sysconfpkgdir_scan_keyfile(self, fn_tmp, error))
			return FALSE;
	}
	passim_server_engine_changed(self);
	return TRUE;
}

typedef struct {
	PassimServer *self;
	SoupServerMessage *msg;
	gchar *hash;
	gchar *basename;
} PassimServerContext;

static void
passim_server_context_free(PassimServerContext *ctx)
{
	if (ctx->msg != NULL)
		g_object_unref(ctx->msg);
	g_free(ctx->hash);
	g_free(ctx->basename);
	g_free(ctx);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(PassimServerContext, passim_server_context_free)

static void
passim_server_msg_send_error(PassimServer *self,
			     SoupServerMessage *msg,
			     guint status_code,
			     const gchar *reason)
{
	g_autoptr(GString) html = g_string_new(NULL);
	const gchar *reason_fallback = reason ? reason : soup_status_get_phrase(status_code);
	g_string_append_printf(html,
			       "<html><head><title>%u %s</title></head>"
			       "<body>%s</body></html>",
			       status_code,
			       soup_status_get_phrase(status_code),
			       reason_fallback);
	soup_server_message_set_status(msg, status_code, NULL);
	soup_server_message_set_response(msg, "text/html", SOUP_MEMORY_COPY, html->str, html->len);
	soup_server_message_unpause(msg);
}

static void
passim_server_context_send_redirect(PassimServerContext *ctx, const gchar *location)
{
	SoupMessageHeaders *hdrs = soup_server_message_get_response_headers(ctx->msg);
	g_autoptr(GString) html = g_string_new(NULL);
	g_autofree gchar *uri =
	    g_strdup_printf("https://%s/%s?sha256=%s", location, ctx->basename, ctx->hash);
	g_string_append_printf(html,
			       "<html><body><a href=\"%s\">Redirecting</a>...</body></html>",
			       uri);
	soup_message_headers_append(hdrs, "Location", uri);
	soup_server_message_set_status(ctx->msg, SOUP_STATUS_MOVED_TEMPORARILY, NULL);
	soup_server_message_set_response(ctx->msg,
					 "text/html",
					 SOUP_MEMORY_COPY,
					 html->str,
					 html->len);
	soup_server_message_unpause(ctx->msg);
}

static void
passim_server_send_index(PassimServer *self, SoupServerMessage *msg)
{
	g_autoptr(GString) html = g_string_new(NULL);
	g_autoptr(GList) keys = g_hash_table_get_keys(self->items);

	g_string_append(html, "<html>\n");
	g_string_append(html, "<head>\n");
	g_string_append(html, "<meta charset=\"utf-8\" />\n");
	g_string_append(
	    html,
	    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n");
	g_string_append_printf(html, "<title>%s</title>\n", passim_avahi_get_name(self->avahi));
	g_string_append(html, "<link href=\"style.css\" rel=\"stylesheet\" />\n");
	g_string_append(html, "</head>");
	g_string_append(html, "<body>");
	g_string_append_printf(html, "<h1>%s</h1>\n", passim_avahi_get_name(self->avahi));
	g_string_append_printf(
	    html,
	    "<p>A <a href=\"https://github.com/hughsie/%s\">local caching server</a>, "
	    "version <code>%s</code> with status <code>%s</code>.</p>\n",
	    PACKAGE_NAME,
	    VERSION,
	    passim_status_to_string(self->status));
	if (keys == NULL) {
		g_string_append(html, "<em>There are no shared files on this computer.</em>\n");
	} else {
		g_string_append(html, "<h2>Shared Files:</h2>\n");
		g_string_append(html, "<table>\n");
		g_string_append(html, "<tr>\n");
		g_string_append(html, "<th>Filename</th>\n");
		g_string_append(html, "<th>Hash</th>\n");
		g_string_append(html, "<th>Binary</th>\n");
		g_string_append(html, "<th>Age</th>\n");
		g_string_append(html, "<th>Shared</th>\n");
		g_string_append(html, "<th>Size</th>\n");
		g_string_append(html, "<th>Flags</th>\n");
		g_string_append(html, "</tr>\n");
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *hash = l->data;
			PassimItem *item = g_hash_table_lookup(self->items, hash);
			g_autofree gchar *flags = passim_item_get_flags_as_string(item);
			g_autofree gchar *url = g_strdup_printf("https://localhost:%u/%s?sha256=%s",
								self->port,
								passim_item_get_basename(item),
								hash);
			g_string_append(html, "<tr>\n");
			g_string_append_printf(html,
					       "<td><a href=\"%s\">%s</a></td>\n",
					       url,
					       passim_item_get_basename(item));
			g_string_append_printf(html,
					       "<td><code>%s</code></td>\n",
					       passim_item_get_hash(item));
			if (passim_item_get_cmdline(item) == NULL) {
				g_string_append_printf(html, "<td><code>n/a</code></td>\n");
			} else {
				g_string_append_printf(html,
						       "<td><code>%s</code></td>\n",
						       passim_item_get_cmdline(item));
			}
			if (passim_item_get_max_age(item) == G_MAXUINT32) {
				g_string_append_printf(html,
						       "<td>%u/∞h</td>\n",
						       passim_item_get_age(item) / 3600u);
			} else {
				g_string_append_printf(html,
						       "<td>%u/%uh</td>\n",
						       passim_item_get_age(item) / 3600u,
						       passim_item_get_max_age(item) / 3600u);
			}
			if (passim_item_get_share_limit(item) == G_MAXUINT32) {
				g_string_append_printf(html,
						       "<td>%u/∞</td>\n",
						       passim_item_get_share_count(item));
			} else {
				g_string_append_printf(html,
						       "<td>%u/%u</td>\n",
						       passim_item_get_share_count(item),
						       passim_item_get_share_limit(item));
			}
			if (passim_item_get_size(item) == 0) {
				g_string_append(html, "<td>?</td>\n");
			} else {
				g_autofree gchar *size = g_format_size(passim_item_get_size(item));
				g_string_append_printf(html, "<td>%s</td>\n", size);
			}
			g_string_append_printf(html, "<td><code>%s</code></td>\n", flags);
			g_string_append(html, "</tr>");
		}
		g_string_append(html, "</table>\n");
	}
	g_string_append(html, "</body>\n");
	g_string_append(html, "</html>\n");
	soup_server_message_set_status(msg, SOUP_STATUS_OK, NULL);
	soup_server_message_set_response(msg, "text/html", SOUP_MEMORY_COPY, html->str, html->len);
}

static void
passim_server_msg_send_file(PassimServer *self, SoupServerMessage *msg, const gchar *path)
{
	SoupMessageHeaders *hdrs = soup_server_message_get_response_headers(msg);
	GMappedFile *mapping;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = g_file_new_for_path(path);
	g_autoptr(GFileInfo) info = NULL;

	mapping = g_mapped_file_new(path, FALSE, &error);
	if (mapping == NULL) {
		soup_server_message_set_status(msg,
					       SOUP_STATUS_INTERNAL_SERVER_ERROR,
					       error->message);
		return;
	}
	bytes = g_bytes_new_with_free_func(g_mapped_file_get_contents(mapping),
					   g_mapped_file_get_length(mapping),
					   (GDestroyNotify)g_mapped_file_unref,
					   mapping);
	if (g_bytes_get_size(bytes) > 0)
		soup_message_body_append_bytes(soup_server_message_get_response_body(msg), bytes);
	soup_server_message_set_status(msg, SOUP_STATUS_OK, NULL);

	info = g_file_query_info(file,
				 G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				 G_FILE_QUERY_INFO_NONE,
				 NULL,
				 &error);
	if (info == NULL) {
		soup_server_message_set_status(msg,
					       SOUP_STATUS_INTERNAL_SERVER_ERROR,
					       error->message);
		return;
	}
	if (g_file_info_get_content_type(info) != NULL) {
		g_autofree gchar *mime_type =
		    g_content_type_get_mime_type(g_file_info_get_content_type(info));
		if (mime_type != NULL)
			soup_message_headers_append(hdrs, "Content-Type", mime_type);
	}
}

static gboolean
passim_server_delete_item(PassimServer *self, PassimItem *item, GError **error)
{
	if (!g_file_delete(passim_item_get_file(item), NULL, error)) {
		g_prefix_error(error, "failed to delete %s: ", passim_item_get_hash(item));
		return FALSE;
	}
	g_hash_table_remove(self->items, passim_item_get_hash(item));
	if (!passim_server_avahi_register(self, error)) {
		g_prefix_error(error, "failed to register: ");
		return FALSE;
	}
	return TRUE;
}

static void
passim_server_msg_send_item(PassimServer *self, SoupServerMessage *msg, PassimItem *item)
{
	SoupMessageHeaders *hdrs = soup_server_message_get_response_headers(msg);
	g_autofree gchar *content_disposition = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *path = g_file_get_path(passim_item_get_file(item));

	filename = g_uri_escape_string(passim_item_get_basename(item), NULL, TRUE);
	content_disposition = g_strdup_printf("attachment; filename=\"%s\"", filename);
	soup_message_headers_append(hdrs, "Content-Disposition", content_disposition);

	passim_server_msg_send_file(self, msg, path);
	passim_item_set_share_count(item, passim_item_get_share_count(item) + 1);

	/* we've shared this enough now */
	if (passim_item_get_share_limit(item) > 0 &&
	    passim_item_get_share_count(item) >= passim_item_get_share_limit(item)) {
		g_autoptr(GError) error = NULL;
		g_debug("deleting %s as share limit reached", passim_item_get_hash(item));
		if (!passim_server_delete_item(self, item, &error))
			g_warning("failed: %s", error->message);
	}
}

static void
passim_server_avahi_find_cb(GObject *source_object, GAsyncResult *res, gpointer data)
{
	guint index_random;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) addresses = NULL;
	g_autoptr(PassimServerContext) ctx = (PassimServerContext *)data;

	addresses = passim_avahi_find_finish(PASSIM_AVAHI(source_object), res, &error);
	if (addresses == NULL) {
		passim_server_msg_send_error(ctx->self, ctx->msg, 404, error->message);
		return;
	}

	/* display all, and chose an option at random */
	index_random = g_random_int_range(0, addresses->len);
	for (guint i = 0; i < addresses->len; i++) {
		const gchar *address = g_ptr_array_index(addresses, i);
		if (i == index_random) {
			g_info("chosen address: %s", address);
			passim_server_context_send_redirect(ctx, address);
		} else {
			g_info("ignore address: %s", address);
		}
	}
}

static gboolean
passim_server_is_loopback(const gchar *inet_addr)
{
	g_autoptr(GInetAddress) address = g_inet_address_new_from_string(inet_addr);
	return g_inet_address_get_is_loopback(address);
}

static void
passim_server_handler_cb(SoupServer *server,
			 SoupServerMessage *msg,
			 const gchar *path,
			 GHashTable *query,
			 gpointer user_data)
{
	PassimServer *self = (PassimServer *)user_data;
	GInetAddress *inet_addr;
	GSocketAddress *socket_addr;
	PassimItem *item;
	GUri *uri = soup_server_message_get_uri(msg);
	gboolean is_loopback;
	g_autofree gchar *hash = NULL;
	g_autofree gchar *inet_addrstr = NULL;
	g_auto(GStrv) request = NULL;
	g_autoptr(PassimServerContext) ctx = g_new0(PassimServerContext, 1);

	/* only GET supported */
	if (soup_server_message_get_method(msg) != SOUP_METHOD_GET) {
		passim_server_msg_send_error(self, msg, SOUP_STATUS_FORBIDDEN, NULL);
		return;
	}

	/* who is connecting */
	socket_addr = soup_server_message_get_remote_address(msg);
	if (socket_addr == NULL) {
		passim_server_msg_send_error(self,
					     msg,
					     SOUP_STATUS_BAD_REQUEST,
					     "failed to get client connection address");
		return;
	}
	inet_addr = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(socket_addr));
	inet_addrstr = g_inet_address_to_string(inet_addr);
	is_loopback = passim_server_is_loopback(inet_addrstr);
	g_info("accepting HTTP/1.%u %s %s %s from %s:%u (%s)",
	       soup_server_message_get_http_version(msg),
	       soup_server_message_get_method(msg),
	       path,
	       g_uri_get_query(uri) != NULL ? g_uri_get_query(uri) : "",
	       inet_addrstr,
	       g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(socket_addr)),
	       is_loopback ? "loopback" : "remote");

	/* just return the index */
	if (g_strcmp0(path, "/") == 0) {
		if (!is_loopback) {
			passim_server_msg_send_error(self, msg, SOUP_STATUS_FORBIDDEN, NULL);
			return;
		}
		passim_server_send_index(self, msg);
		return;
	}
	if (g_strcmp0(path, "/favicon.ico") == 0 || g_strcmp0(path, "/style.css") == 0) {
		g_autofree gchar *fn = g_build_filename(PACKAGE_DATADIR, PACKAGE_NAME, path, NULL);
		if (!is_loopback) {
			passim_server_msg_send_error(self, msg, SOUP_STATUS_FORBIDDEN, NULL);
			return;
		}
		passim_server_msg_send_file(self, msg, fn);
		return;
	}

	/* find the request hash argument */
	if (g_uri_get_query(uri) == NULL) {
		passim_server_msg_send_error(self, msg, SOUP_STATUS_BAD_REQUEST, NULL);
		return;
	}
	request = g_strsplit(g_uri_get_query(uri), "&", -1);
	for (guint i = 0; request[i] != NULL; i++) {
		g_auto(GStrv) kv = g_strsplit(request[i], "=", -1);
		if (g_strv_length(kv) != 2)
			continue;
		if (g_strcmp0(kv[0], "sha256") == 0) {
			hash = g_strdup(kv[1]);
			break;
		}
	}
	if (hash == NULL) {
		passim_server_msg_send_error(self,
					     msg,
					     SOUP_STATUS_BAD_REQUEST,
					     "sha256= argument required");
		return;
	}
	if (!g_str_is_ascii(hash) || strlen(hash) != 64) {
		passim_server_msg_send_error(self,
					     msg,
					     SOUP_STATUS_NOT_ACCEPTABLE,
					     "sha256 hash is malformed");
		return;
	}

	/* already exists locally */
	item = g_hash_table_lookup(self->items, hash);
	if (item != NULL) {
		if (passim_item_has_flag(item, PASSIM_ITEM_FLAG_DISABLED)) {
			passim_server_msg_send_error(self, msg, SOUP_STATUS_LOCKED, NULL);
			return;
		}
		passim_server_msg_send_item(self, msg, item);
		return;
	}

	/* only localhost is allowed to scan for hashes */
	if (!is_loopback) {
		passim_server_msg_send_error(self, msg, SOUP_STATUS_FORBIDDEN, NULL);
		return;
	}

	/* create context */
	ctx->self = self;
	ctx->msg = g_object_ref(msg);
	ctx->hash = g_strdup(hash);
	ctx->basename = g_strdup(request[0]);

	/* look for remote servers with this hash */
	g_info("searching for %s", hash);
	soup_server_message_pause(msg);
	passim_avahi_find_async(self->avahi,
				hash,
				NULL,
				passim_server_avahi_find_cb,
				g_steal_pointer(&ctx));
}

static gboolean
passim_server_publish_file(PassimServer *self, GBytes *blob, PassimItem *item, GError **error)
{
	g_autofree gchar *hash = NULL;
	g_autofree gchar *localstate_dir = NULL;
	g_autofree gchar *localstate_filename = NULL;
	g_autofree gchar *hashed_filename = NULL;
	g_autoptr(GFile) file = NULL;

	hash = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, blob);
	if (g_hash_table_contains(self->items, hash)) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", hash);
		return FALSE;
	}
	hashed_filename = g_strdup_printf("%s-%s", hash, passim_item_get_basename(item));

	localstate_dir = g_build_filename(PACKAGE_LOCALSTATEDIR, "lib", PACKAGE_NAME, "data", NULL);
	if (!passim_mkdir(localstate_dir, error))
		return FALSE;
	localstate_filename = g_build_filename(localstate_dir, hashed_filename, NULL);
	if (g_file_test(localstate_filename, G_FILE_TEST_EXISTS)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_EXISTS,
			    "%s already exists",
			    localstate_filename);
		return FALSE;
	}
	if (!g_file_set_contents(localstate_filename,
				 g_bytes_get_data(blob, NULL),
				 g_bytes_get_size(blob),
				 error))
		return FALSE;
	if (!passim_xattr_set_uint32(localstate_filename,
				     "user.max_age",
				     passim_item_get_max_age(item),
				     error))
		return FALSE;
	if (!passim_xattr_set_uint32(localstate_filename,
				     "user.share_limit",
				     passim_item_get_share_limit(item),
				     error))
		return FALSE;
	if (!passim_xattr_set_string(localstate_filename,
				     "user.cmdline",
				     passim_item_get_cmdline(item),
				     error))
		return FALSE;

	/* only allowed when rebooted */
	if (passim_item_has_flag(item, PASSIM_ITEM_FLAG_NEXT_REBOOT)) {
		g_autofree gchar *boot_time = passim_get_boot_time();
		if (!passim_xattr_set_string(localstate_filename,
					     "user.boot_time",
					     boot_time,
					     error))
			return FALSE;
		passim_item_add_flag(item, PASSIM_ITEM_FLAG_DISABLED);
	}

	/* add to interface */
	file = g_file_new_for_path(localstate_filename);
	passim_item_set_hash(item, hash);
	passim_item_set_file(item, file);
	g_debug("added %s", localstate_filename);
	g_hash_table_insert(self->items, g_steal_pointer(&hash), g_object_ref(item));

	/* success */
	return passim_server_avahi_register(self, error);
}

static gboolean
passim_server_timed_exit_cb(gpointer user_data)
{
	PassimServer *self = (PassimServer *)user_data;
	self->timed_exit_id = 0;
	g_main_loop_quit(self->loop);
	return G_SOURCE_REMOVE;
}

static void
passim_server_check_item_age(PassimServer *self)
{
	g_autoptr(GList) items = g_hash_table_get_values(self->items);
	g_info("checking for max-age");
	for (GList *l = items; l != NULL; l = l->next) {
		PassimItem *item = PASSIM_ITEM(l->data);
		guint32 age = passim_item_get_age(item);

		if (passim_item_get_max_age(item) == G_MAXUINT32)
			continue;
		if (age > passim_item_get_max_age(item)) {
			g_autoptr(GError) error = NULL;
			g_debug("deleting %s [%s] as max-age reached",
				passim_item_get_hash(item),
				passim_item_get_basename(item));
			if (!passim_server_delete_item(self, item, &error))
				g_warning("failed: %s", error->message);
		} else {
			g_debug("%s [%s] has age %uh, maximum is %uh",
				passim_item_get_hash(item),
				passim_item_get_basename(item),
				(guint)age / 3600u,
				passim_item_get_max_age(item) / 3600u);
		}
	}
}

static gboolean
passim_server_check_item_age_cb(gpointer user_data)
{
	PassimServer *self = (PassimServer *)user_data;
	passim_server_check_item_age(self);
	return G_SOURCE_CONTINUE;
}

static gchar *
passim_server_sender_get_cmdline(PassimServer *self, const gchar *sender, GError **error)
{
	guint value = G_MAXUINT;
	g_autofree gchar *cmdline_buf = NULL;
	g_autofree gchar *cmdline_fn = NULL;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_sync(self->proxy_uid,
				     "GetConnectionUnixProcessID",
				     g_variant_new("(s)", sender),
				     G_DBUS_CALL_FLAGS_NONE,
				     2000,
				     NULL,
				     error);
	if (val == NULL) {
		g_prefix_error(error, "failed to read user id of caller: ");
		return NULL;
	}
	g_variant_get(val, "(u)", &value);
	cmdline_fn = g_strdup_printf("/proc/%u/cmdline", value);
	if (!g_file_get_contents(cmdline_fn, &cmdline_buf, NULL, error)) {
		g_prefix_error(error, "failed to caller cmdline: ");
		return NULL;
	}
	return g_path_get_basename(cmdline_buf);
}

static gboolean
passim_server_sender_check_uid(PassimServer *self, const gchar *sender, GError **error)
{
	guint value = G_MAXUINT;
	g_autoptr(GVariant) val = NULL;

	val = g_dbus_proxy_call_sync(self->proxy_uid,
				     "GetConnectionUnixUser",
				     g_variant_new("(s)", sender),
				     G_DBUS_CALL_FLAGS_NONE,
				     2000,
				     NULL,
				     error);
	if (val == NULL) {
		g_prefix_error(error, "failed to read user id of caller: ");
		return FALSE;
	}
	g_variant_get(val, "(u)", &value);
	if (value != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_PERMISSION_DENIED,
			    "permission denied: UID %u != 0",
			    value);
		return FALSE;
	}
	return TRUE;
}

static void
passim_server_method_call(GDBusConnection *connection,
			  const gchar *sender,
			  const gchar *object_path,
			  const gchar *interface_name,
			  const gchar *method_name,
			  GVariant *parameters,
			  GDBusMethodInvocation *invocation,
			  gpointer user_data)
{
	PassimServer *self = (PassimServer *)user_data;
	GVariant *val = NULL;

	if (g_strcmp0(method_name, "GetItems") == 0) {
		GVariantBuilder builder;
		g_autoptr(GList) items = g_hash_table_get_values(self->items);

		g_debug("Called %s()", method_name);
		g_variant_builder_init(&builder, G_VARIANT_TYPE("aa{sv}"));
		for (GList *l = items; l != NULL; l = l->next) {
			PassimItem *item = PASSIM_ITEM(l->data);
			g_variant_builder_add_value(&builder, passim_item_to_variant(item));
		}
		val = g_variant_builder_end(&builder);
		g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&val, 1));
		return;
	}
	if (g_strcmp0(method_name, "Publish") == 0) {
		GDBusMessage *message;
		GUnixFDList *fd_list;
		gint fd = 0;
		g_autofree gchar *cmdline = NULL;
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GDateTime) dt_now = g_date_time_new_now_utc();
		g_autoptr(GError) error = NULL;
		g_autoptr(GInputStream) istream = NULL;
		g_autoptr(PassimItem) item = NULL;
		g_autoptr(GVariant) variant = NULL;

		g_variant_get(parameters, "(h@a{sv})", &fd, &variant);
		item = passim_item_from_variant(variant);
		g_debug("Called %s(%i, %s, 0x%x, %u, %u)",
			method_name,
			fd,
			passim_item_get_basename(item),
			(guint)passim_item_get_flags(item),
			passim_item_get_max_age(item),
			passim_item_get_share_limit(item));

		/* only callable by root */
		if (!passim_server_sender_check_uid(self, sender, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		/* record the binary that is publishing the file */
		cmdline = passim_server_sender_get_cmdline(self, sender, &error);
		if (cmdline == NULL) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		passim_item_set_cmdline(item, cmdline);

		/* sanity check this does not contain a path */
		if (g_strstr_len(passim_item_get_basename(item), -1, "/") != NULL) {
			g_dbus_method_invocation_return_error_literal(invocation,
								      G_DBUS_ERROR,
								      G_DBUS_ERROR_INVALID_ARGS,
								      "invalid basename");
			return;
		}

		/* sanity check share values */
		if (passim_item_get_share_count(item) >= passim_item_get_share_limit(item)) {
			g_dbus_method_invocation_return_error(invocation,
							      G_DBUS_ERROR,
							      G_DBUS_ERROR_INVALID_ARGS,
							      "share count %u >= share-limit %u",
							      passim_item_get_share_count(item),
							      passim_item_get_share_limit(item));
			return;
		}

		/* read from the file descriptor */
		message = g_dbus_method_invocation_get_message(invocation);
		fd_list = g_dbus_message_get_unix_fd_list(message);
		if (fd_list == NULL || g_unix_fd_list_get_length(fd_list) != 1) {
			g_dbus_method_invocation_return_error_literal(invocation,
								      G_DBUS_ERROR,
								      G_DBUS_ERROR_INVALID_ARGS,
								      "invalid handle");
			return;
		}
		fd = g_unix_fd_list_get(fd_list, 0, &error);
		if (fd < 0) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		/* read file */
		istream = g_unix_input_stream_new(fd, TRUE);
		blob = passim_load_input_stream(istream,
						passim_config_get_max_item_size(self->kf),
						&error);
		if (blob == NULL) {
			if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE)) {
				g_autofree gchar *size =
				    g_format_size(passim_config_get_max_item_size(self->kf));
				g_dbus_method_invocation_return_error(
				    invocation,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "Failed to load file, size limit is %s",
				    size);
				return;
			}
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		/* only set by daemon */
		passim_item_set_ctime(item, dt_now);

		/* publish the new file */
		if (!passim_server_publish_file(self, blob, item, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}
	if (g_strcmp0(method_name, "Unpublish") == 0) {
		const gchar *hash = NULL;
		PassimItem *item;
		g_autoptr(GError) error = NULL;

		/* only callable by root */
		if (!passim_server_sender_check_uid(self, sender, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

		g_variant_get(parameters, "(&s)", &hash);
		item = g_hash_table_lookup(self->items, hash);
		if (item == NULL) {
			g_dbus_method_invocation_return_error(invocation,
							      G_IO_ERROR,
							      G_IO_ERROR_NOT_FOUND,
							      "%s not found",
							      hash);
			return;
		}
		if (!passim_server_delete_item(self, item, &error)) {
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}
	g_dbus_method_invocation_return_error(invocation,
					      G_DBUS_ERROR,
					      G_DBUS_ERROR_UNKNOWN_METHOD,
					      "no such method %s",
					      method_name);
}

static GVariant *
passim_server_get_property(GDBusConnection *connection_,
			   const gchar *sender,
			   const gchar *object_path,
			   const gchar *interface_name,
			   const gchar *property_name,
			   GError **error,
			   gpointer user_data)
{
	PassimServer *self = (PassimServer *)user_data;

	if (g_strcmp0(property_name, "DaemonVersion") == 0)
		return g_variant_new_string(SOURCE_VERSION);
	if (g_strcmp0(property_name, "Status") == 0)
		return g_variant_new_uint32(self->status);

	/* return an error */
	g_set_error(error,
		    G_DBUS_ERROR,
		    G_DBUS_ERROR_UNKNOWN_PROPERTY,
		    "failed to get daemon property %s",
		    property_name);
	return NULL;
}

static void
passim_server_register_object(PassimServer *self)
{
	guint registration_id;
	static const GDBusInterfaceVTable interface_vtable = {
	    .method_call = passim_server_method_call,
	    .get_property = passim_server_get_property,
	    NULL};
	registration_id =
	    g_dbus_connection_register_object(self->connection,
					      PASSIM_DBUS_PATH,
					      self->introspection_daemon->interfaces[0],
					      &interface_vtable,
					      self,  /* user_data */
					      NULL,  /* user_data_free_func */
					      NULL); /* GError** */
	g_assert(registration_id > 0);
}

static void
passim_server_dbus_bus_acquired_cb(GDBusConnection *connection,
				   const gchar *name,
				   gpointer user_data)
{
	PassimServer *self = (PassimServer *)user_data;
	g_autoptr(GError) error = NULL;

	self->connection = g_object_ref(connection);
	passim_server_register_object(self);

	/* connect to D-Bus directly */
	self->proxy_uid = g_dbus_proxy_new_sync(self->connection,
						G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
						    G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
						NULL,
						"org.freedesktop.DBus",
						"/org/freedesktop/DBus",
						"org.freedesktop.DBus",
						NULL,
						&error);
	if (self->proxy_uid == NULL) {
		g_warning("cannot connect to DBus: %s", error->message);
		return;
	}
}

static void
passim_server_dbus_name_acquired_cb(GDBusConnection *connection,
				    const gchar *name,
				    gpointer user_data)
{
	g_debug("acquired name: %s", name);
}

static void
passim_server_dbus_name_lost_cb(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	PassimServer *self = (PassimServer *)user_data;
	g_warning("another service has claimed the dbus name %s", name);
	g_main_loop_quit(self->loop);
}

static gboolean
passim_server_start_dbus(PassimServer *self, GError **error)
{
	g_autofree gchar *introspection_fn = NULL;
	g_autofree gchar *introspection_xml = NULL;

	/* load introspection from file */
	introspection_fn = g_build_filename(PACKAGE_DATADIR,
					    "dbus-1",
					    "interfaces",
					    PASSIM_DBUS_INTERFACE ".xml",
					    NULL);
	if (!g_file_get_contents(introspection_fn, &introspection_xml, NULL, error)) {
		g_prefix_error(error, "failed to read introspection: ");
		return FALSE;
	}
	self->introspection_daemon = g_dbus_node_info_new_for_xml(introspection_xml, error);
	if (self->introspection_daemon == NULL) {
		g_prefix_error(error, "failed to load introspection: ");
		return FALSE;
	}

	/* start D-Bus server */
	self->owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
					PASSIM_DBUS_SERVICE,
					G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
					    G_BUS_NAME_OWNER_FLAGS_REPLACE,
					passim_server_dbus_bus_acquired_cb,
					passim_server_dbus_name_acquired_cb,
					passim_server_dbus_name_lost_cb,
					self,
					NULL);

	/* success */
	return TRUE;
}

static GTlsCertificate *
passim_server_load_tls_certificate(GError **error)
{
	g_autofree gchar *cert_fn = NULL;
	g_autofree gchar *secret_fn = NULL;
	g_autoptr(GBytes) secret_blob = NULL;

	/* create secret key */
	secret_fn =
	    g_build_filename(PACKAGE_LOCALSTATEDIR, "lib", PACKAGE_NAME, "secret.key", NULL);
	if (!g_file_test(secret_fn, G_FILE_TEST_EXISTS)) {
		secret_blob = passim_gnutls_create_private_key(error);
		if (secret_blob == NULL)
			return NULL;
		if (!passim_mkdir_parent(secret_fn, error))
			return NULL;
		if (!passim_file_set_contents(secret_fn, secret_blob, error))
			return NULL;
	}

	/* create TLS cert */
	cert_fn = g_build_filename(PACKAGE_LOCALSTATEDIR, "lib", PACKAGE_NAME, "cert.pem", NULL);
	if (!g_file_test(cert_fn, G_FILE_TEST_EXISTS)) {
		g_autoptr(GBytes) cert_blob = NULL;
		g_auto(gnutls_privkey_t) privkey = NULL;

		if (secret_blob == NULL) {
			secret_blob = passim_file_get_contents(secret_fn, error);
			if (secret_blob == NULL)
				return NULL;
		}
		privkey = passim_gnutls_load_privkey_from_blob(secret_blob, error);
		if (privkey == NULL)
			return NULL;
		cert_blob = passim_gnutls_create_certificate(privkey, error);
		if (cert_blob == NULL)
			return NULL;
		if (!passim_file_set_contents(cert_fn, cert_blob, error))
			return NULL;
	}

	/* load cert */
	g_info("using secret key %s and certificate %s", secret_fn, cert_fn);
	return g_tls_certificate_new_from_files(cert_fn, secret_fn, error);
}

static gboolean
passim_server_sigint_cb(gpointer user_data)
{
	PassimServer *self = (PassimServer *)user_data;
	g_debug("Handling SIGINT");
	g_main_loop_quit(self->loop);
	return FALSE;
}

static void
passim_server_network_monitor_metered_changed_cb(GNetworkMonitor *network_monitor,
						 GParamSpec *pspec,
						 gpointer user_data)
{
	PassimServer *self = (PassimServer *)user_data;
	g_autoptr(GError) error_local = NULL;
	if (!passim_server_avahi_register(self, &error_local))
		g_warning("failed to register: %s", error_local->message);
}

int
main(int argc, char *argv[])
{
	gboolean version = FALSE;
	gboolean timed_exit = FALSE;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = g_option_context_new(NULL);
	g_autoptr(GSource) unix_signal_source = g_unix_signal_source_new(SIGINT);
	g_autoptr(GTlsCertificate) cert = NULL;
	g_autoptr(PassimServer) self = g_new0(PassimServer, 1);
	g_autoptr(SoupServer) soup_server = NULL;
	g_autoslist(GUri) uris = NULL;
	const GOptionEntry options[] = {
	    {"version", '\0', 0, G_OPTION_ARG_NONE, &version, "Show project version", NULL},
	    {"timed-exit", '\0', 0, G_OPTION_ARG_NONE, &timed_exit, "Exit after a delay", NULL},
	    {NULL}};

	(void)g_setenv("G_MESSAGES_DEBUG", "all", FALSE);
	(void)g_setenv("G_DEBUG", "fatal-criticals", FALSE);

	g_option_context_add_main_entries(context, options, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("Failed to parse arguments: %s", error->message);
		return EXIT_FAILURE;
	}

	/* just show versions and exit */
	if (version) {
		g_print("%s\n", VERSION);
		return EXIT_SUCCESS;
	}

	self->status = PASSIM_STATUS_STARTING;
	self->loop = g_main_loop_new(NULL, FALSE);
	self->kf = passim_config_load(&error);
	if (self->kf == NULL) {
		g_printerr("failed to load config: %s\n", error->message);
		return 1;
	}
	self->poll_item_age_id =
	    g_timeout_add_seconds(60 * 60, passim_server_check_item_age_cb, self);
	if (timed_exit)
		self->timed_exit_id = g_timeout_add_seconds(10, passim_server_timed_exit_cb, self);
	self->avahi = passim_avahi_new(self->kf);
	self->port = passim_config_get_port(self->kf);
	self->root = passim_config_get_path(self->kf);
	self->items =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_object_unref);
	self->network_monitor = g_network_monitor_get_default();

	g_signal_connect(G_NETWORK_MONITOR(self->network_monitor),
			 "notify::network-metered",
			 G_CALLBACK(passim_server_network_monitor_metered_changed_cb),
			 self);
	if (!passim_server_start_dbus(self, &error)) {
		g_warning("failed to register D-Bus: %s", error->message);
		return 1;
	}
	if (!passim_server_libdir_scan(self, &error)) {
		g_printerr("failed to scan directory: %s\n", error->message);
		return 1;
	}
	if (!passim_server_sysconfpkgdir_watch(self, &error)) {
		g_printerr("failed to watch sysconfpkg directory: %s\n", error->message);
		return 1;
	}
	if (!passim_server_sysconfpkgdir_scan(self, &error)) {
		g_printerr("failed to scan sysconfpkg directory: %s\n", error->message);
		return 1;
	}
	if (!passim_avahi_connect(self->avahi, &error)) {
		g_warning("failed to contact daemon: %s", error->message);
		return 1;
	}
	passim_server_check_item_age(self);

	/* set up the webserver */
	cert = passim_server_load_tls_certificate(&error);
	if (cert == NULL) {
		g_warning("failed to load TLS cert: %s", error->message);
		return 1;
	}
	soup_server = soup_server_new("server-header", "passim ", "tls-certificate", cert, NULL);
	if (!soup_server_listen_all(soup_server, self->port, SOUP_SERVER_LISTEN_HTTPS, &error)) {
		g_printerr("%s: %s\n", argv[0], error->message);
		return 1;
	}
	soup_server_add_handler(soup_server, NULL, passim_server_handler_cb, self, NULL);
	uris = soup_server_get_uris(soup_server);
	for (GSList *u = uris; u; u = u->next) {
		g_autofree gchar *str = g_uri_to_string(u->data);
		g_info("listening on %s", str);
	}
	self->status = PASSIM_STATUS_LOADING;

	/* register objects with Avahi */
	if (!passim_server_avahi_register(self, &error)) {
		g_warning("failed to register: %s", error->message);
		return 1;
	}

	/* do stuff on ctrl+c */
	g_source_set_callback(unix_signal_source, passim_server_sigint_cb, self, NULL);
	g_source_attach(unix_signal_source, NULL);

	g_main_loop_run(self->loop);
	return 0;
}
