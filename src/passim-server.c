/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gunixinputstream.h>
#include <passim.h>

#include "passim-avahi.h"
#include "passim-common.h"

typedef struct {
	GDBusConnection *connection;
	GDBusNodeInfo *introspection_daemon;
	GDBusProxy *proxy_uid;
	GHashTable *items; /* utf-8:PassimItem */
	GKeyFile *kf;
	GMainLoop *loop;
	PassimAvahi *avahi;
	gchar *root;
	guint16 port;
	guint owner_id;
	guint poll_item_age_id;
	guint timed_exit_id;
	gboolean http_active;
} PassimServer;

static void
passim_server_free(PassimServer *self)
{
	if (self->poll_item_age_id != 0)
		g_source_remove(self->poll_item_age_id);
	if (self->timed_exit_id != 0)
		g_source_remove(self->timed_exit_id);
	if (self->loop != NULL)
		g_main_loop_unref(self->loop);
	if (self->avahi != NULL)
		g_object_unref(self->avahi);
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

static gboolean
passim_server_avahi_register(PassimServer *self, GError **error)
{
	guint keyidx = 0;
	g_autofree const gchar **keys = NULL;
	g_autoptr(GList) items = NULL;

	/* sanity check */
	if (!self->http_active) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_MOUNTED,
			    "http server has not yet started");
		return FALSE;
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
	return passim_avahi_register(self->avahi, (gchar **)keys, error);
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
	if (!passim_item_load_filename(item, filename, error))
		return FALSE;

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
	cmdline = passim_xattr_get_string(filename, "user.boot_time", NULL);
	if (cmdline != NULL) {
		g_autofree gchar *boot_time_now = passim_get_boot_time();
		if (g_strcmp0(boot_time_now, boot_time) == 0) {
			passim_item_add_flag(item, PASSIM_ITEM_FLAG_NEXT_REBOOT);
			passim_item_add_flag(item, PASSIM_ITEM_FLAG_DISABLED);
		}
	}

	g_debug("added http://localhost:%u/%s?sha256=%s",
		self->port,
		passim_item_get_basename(item),
		passim_item_get_hash(item));
	g_hash_table_insert(self->items, g_strdup(passim_item_get_hash(item)), g_object_ref(item));
	return TRUE;
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

typedef struct {
	PassimServer *self;
	GSocketConnection *connection;
	gchar *hash;
	gchar *basename;
	gboolean addr_loopback;
} PassimServerContext;

static void
passim_server_context_free(PassimServerContext *ctx)
{
	if (ctx->connection != NULL)
		g_object_unref(ctx->connection);
	g_free(ctx->hash);
	g_free(ctx->basename);
	g_free(ctx);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(PassimServerContext, passim_server_context_free)

static void
passim_server_context_send(PassimServerContext *ctx,
			   guint error_code,
			   GPtrArray *headers,
			   const gchar *html)
{
	GOutputStream *out = g_io_stream_get_output_stream(G_IO_STREAM(ctx->connection));
	const gchar *http_reason = passim_http_code_to_string(error_code);
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	g_string_append_printf(str, "HTTP/1.0 %u %s\r\n", error_code, http_reason);
	if (html != NULL) {
		g_string_append_printf(str,
				       "Content-Length: %" G_GSIZE_FORMAT "\r\n",
				       strlen(html) + 2);
	}
	if (headers != NULL) {
		for (guint i = 0; i < headers->len; i++) {
			const gchar *header = g_ptr_array_index(headers, i);
			g_string_append_printf(str, "%s\r\n", header);
		}
	}
	g_string_append(str, "\r\n");
	if (html != NULL)
		g_string_append_printf(str, "%s\r\n", html);
	g_output_stream_write_all(out, str->str, str->len, NULL, NULL, NULL);
	if (!g_io_stream_close(G_IO_STREAM(ctx->connection), NULL, &error))
		g_warning("failed to close connection: %s", error->message);
}

static void
passim_server_context_send_error(PassimServerContext *ctx, guint error_code, const gchar *reason)
{
	g_autofree gchar *html = NULL;
	const gchar *reason_fallback = reason ? reason : passim_http_code_to_string(error_code);
	html = g_strdup_printf("<html><head><title>%u %s</title></head>"
			       "<body>%s</body></html>",
			       error_code,
			       passim_http_code_to_string(error_code),
			       reason_fallback);
	passim_server_context_send(ctx, error_code, NULL, html);
}

static void
passim_server_context_send_redirect(PassimServerContext *ctx, const gchar *location)
{
	g_autoptr(GPtrArray) headers = g_ptr_array_new_with_free_func(g_free);
	g_autofree gchar *uri =
	    g_strdup_printf("http://%s/%s?sha256=%s", location, ctx->basename, ctx->hash);
	g_autofree gchar *html =
	    g_strdup_printf("<html><body><a href=\"%s\">Redirecting</a>...</body></html>", uri);
	g_ptr_array_add(headers, g_strdup_printf("Location: %s", uri));
	passim_server_context_send(ctx, 303, headers, html);
}

static void
passim_server_send_index(PassimServerContext *ctx)
{
	PassimServer *self = ctx->self;
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
	    "<p>A <a href=\"https://github.com/hughsie/%s\">local caching server</a>.</p>\n",
	    PACKAGE_NAME);
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
		g_string_append(html, "<th>Flags</th>\n");
		g_string_append(html, "</tr>\n");
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *hash = l->data;
			PassimItem *item = g_hash_table_lookup(self->items, hash);
			g_autofree gchar *flags = passim_item_get_flags_as_string(item);
			g_autofree gchar *url = g_strdup_printf("http://localhost:%u/%s?sha256=%s",
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
			g_string_append_printf(html,
					       "<td><code>%s</code></td>\n",
					       passim_item_get_cmdline(item));
			g_string_append_printf(html,
					       "<td>%u/%uh</td>\n",
					       passim_item_get_age(item) / 3600u,
					       passim_item_get_max_age(item) / 3600u);
			g_string_append_printf(html,
					       "<td>%u / %u</td>\n",
					       passim_item_get_share_count(item),
					       passim_item_get_share_limit(item));
			g_string_append_printf(html, "<td><code>%s</code></td>\n", flags);
			g_string_append(html, "</tr>");
		}
		g_string_append(html, "</table>\n");
	}
	g_string_append(html, "</body>\n");
	g_string_append(html, "</html>\n");
	passim_server_context_send(ctx, 200, NULL, html->str);
}

static void
passim_server_context_send_file(PassimServerContext *ctx, GFile *file, GPtrArray *headers)
{
	GOutputStream *out = g_io_stream_get_output_stream(G_IO_STREAM(ctx->connection));
	g_autoptr(GError) error = NULL;
	g_autoptr(GFileInfo) info = NULL;
	g_autoptr(GFileInputStream) file_in = NULL;
	g_autoptr(GString) str = NULL;

	file_in = g_file_read(file, NULL, &error);
	if (file_in == NULL) {
		passim_server_context_send_error(ctx, 404, error->message);
		return;
	}

	str = g_string_new("HTTP/1.0 200 OK\r\n");
	info = g_file_input_stream_query_info(file_in,
					      G_FILE_ATTRIBUTE_STANDARD_SIZE
					      "," G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
					      NULL,
					      NULL);
	if (headers != NULL) {
		for (guint i = 0; i < headers->len; i++) {
			const gchar *header = g_ptr_array_index(headers, i);
			g_string_append_printf(str, "%s\r\n", header);
		}
	}
	if (info != NULL) {
		const gchar *content_type;
		if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_SIZE)) {
			g_string_append_printf(str,
					       "Content-Length: %" G_GINT64_FORMAT "\r\n",
					       g_file_info_get_size(info));
		}
		content_type = g_file_info_get_content_type(info);
		if (content_type != NULL) {
			g_autofree gchar *mime_type = g_content_type_get_mime_type(content_type);
			if (mime_type != NULL)
				g_string_append_printf(str, "Content-Type: %s\r\n", mime_type);
		}
	}
	g_string_append(str, "\r\n");
	if (g_output_stream_write_all(out, str->str, str->len, NULL, NULL, NULL))
		g_output_stream_splice(out, G_INPUT_STREAM(file_in), 0, NULL, NULL);
	g_input_stream_close(G_INPUT_STREAM(file_in), NULL, NULL);
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
passim_server_context_send_item(PassimServerContext *ctx, PassimItem *item)
{
	PassimServer *self = ctx->self;
	g_autoptr(GPtrArray) headers = g_ptr_array_new_with_free_func(g_free);

	g_ptr_array_add(headers,
			g_strdup_printf("Content-Disposition: attachment; filename=\"%s\"",
					passim_item_get_basename(item)));
	passim_server_context_send_file(ctx, passim_item_get_file(item), headers);
	passim_item_set_share_count(item, passim_item_get_share_count(item) + 1);

	/* we've shared this enough now */
	if (passim_item_get_share_count(item) >= passim_item_get_share_limit(item)) {
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
		passim_server_context_send_error(ctx, 404, error->message);
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

static gboolean
passim_server_handler_cb(GSocketService *service,
			 GSocketConnection *connection,
			 GObject *source_object,
			 gpointer user_data)
{
	PassimServer *self = (PassimServer *)user_data;
	GInputStream *in = g_io_stream_get_input_stream(G_IO_STREAM(connection));
	GInetAddress *inet_addr;
	PassimItem *item;
	g_autofree gchar *line = NULL;
	g_autofree gchar *unescaped = NULL;
	g_autofree gchar *inet_addrstr = NULL;
	g_autofree gchar *hash = NULL;
	g_auto(GStrv) request = NULL;
	g_auto(GStrv) sections = NULL;
	g_autoptr(GDataInputStream) data = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GSocketAddress) socket_addr = NULL;
	g_autoptr(PassimServerContext) ctx = g_new0(PassimServerContext, 1);

	/* create context */
	ctx->self = self;
	ctx->connection = g_object_ref(connection);

	/* who is connecting */
	socket_addr = g_socket_connection_get_remote_address(connection, &error);
	if (socket_addr == NULL) {
		passim_server_context_send_error(ctx,
						 400,
						 "failed to get client connection address");
		return TRUE;
	}
	inet_addr = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(socket_addr));
	inet_addrstr = g_inet_address_to_string(inet_addr);
	ctx->addr_loopback = passim_server_is_loopback(inet_addrstr);
	g_info("accepting connection from %s:%u (%s)",
	       inet_addrstr,
	       g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(socket_addr)),
	       ctx->addr_loopback ? "loopback" : "remote");

	/* be tolerant of input */
	data = g_data_input_stream_new(in);
	g_data_input_stream_set_newline_type(data, G_DATA_STREAM_NEWLINE_TYPE_ANY);
	line = g_data_input_stream_read_line(data, NULL, NULL, NULL);
	if (line == NULL) {
		passim_server_context_send_error(ctx, 400, NULL);
		return TRUE;
	}
	sections = g_strsplit(line, " ", -1);
	if (g_strv_length(sections) != 3) {
		passim_server_context_send_error(ctx, 400, NULL);
		return TRUE;
	}
	if (g_strcmp0(sections[0], "GET") != 0) {
		passim_server_context_send_error(ctx, 501, NULL);
		return TRUE;
	}
	if (!g_str_has_prefix(sections[1], "/")) {
		passim_server_context_send_error(ctx, 400, NULL);
		return TRUE;
	}
	if (!g_str_has_prefix(sections[2], "HTTP/1.")) {
		passim_server_context_send_error(ctx, 505, NULL);
		return TRUE;
	}
	unescaped = g_uri_unescape_string(sections[1], NULL);
	g_info("handle URI: %s", unescaped);

	/* just return the index */
	if (g_strcmp0(unescaped, "/") == 0) {
		if (!ctx->addr_loopback) {
			passim_server_context_send_error(ctx, 403, NULL);
			return TRUE;
		}
		passim_server_send_index(ctx);
		return TRUE;
	}
	if (g_strcmp0(unescaped, "/favicon.ico") == 0 || g_strcmp0(unescaped, "/style.css") == 0) {
		g_autofree gchar *fn =
		    g_build_filename(PACKAGE_DATADIR, PACKAGE_NAME, unescaped, NULL);
		g_autoptr(GFile) file = g_file_new_for_path(fn);
		if (!ctx->addr_loopback) {
			passim_server_context_send_error(ctx, 403, NULL);
			return TRUE;
		}
		passim_server_context_send_file(ctx, file, NULL);
		return TRUE;
	}

	/* find the request hash argument */
	request = g_strsplit_set(unescaped + 1, "?&", -1);
	for (guint i = 1; request[i] != NULL; i++) {
		g_auto(GStrv) kv = g_strsplit(request[i], "=", -1);
		if (g_strv_length(kv) != 2)
			continue;
		if (g_strcmp0(kv[0], "sha256") == 0) {
			hash = g_strdup(kv[1]);
			break;
		}
	}
	if (hash == NULL) {
		passim_server_context_send_error(ctx, 400, "sha256= argument required");
		return TRUE;
	}
	if (!g_str_is_ascii(hash) || strlen(hash) != 64) {
		passim_server_context_send_error(ctx, 505, "sha256 hash is malformed");
		return TRUE;
	}

	/* already exists locally */
	item = g_hash_table_lookup(self->items, hash);
	if (item != NULL) {
		if (passim_item_has_flag(item, PASSIM_ITEM_FLAG_DISABLED)) {
			passim_server_context_send_error(ctx, 423, NULL);
			return TRUE;
		}
		passim_server_context_send_item(ctx, item);
		return TRUE;
	}

	/* only localhost is allowed to scan for hashes */
	if (!ctx->addr_loopback) {
		passim_server_context_send_error(ctx, 403, NULL);
		return TRUE;
	}

	/* look for remote servers with this hash */
	g_info("searching for %s", hash);
	ctx->hash = g_strdup(hash);
	ctx->basename = g_strdup(request[0]);
	passim_avahi_find_async(self->avahi,
				hash,
				NULL,
				passim_server_avahi_find_cb,
				g_steal_pointer(&ctx));
	return TRUE;
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
				(guint)age,
				passim_item_get_max_age(item));
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
		const gchar *basename = NULL;
		gint fd = 0;
		guint32 max_age = 0;
		guint32 share_limit = 0;
		guint64 flags = 0;
		g_autofree gchar *cmdline = NULL;
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GError) error = NULL;
		g_autoptr(GInputStream) istream = NULL;
		g_autoptr(PassimItem) item = passim_item_new();

		g_variant_get(parameters,
			      "(h&stuu)",
			      &fd,
			      &basename,
			      &flags,
			      &max_age,
			      &share_limit);
		g_debug("Called %s(%i, %s, 0x%x, %u, %u)",
			method_name,
			fd,
			basename,
			(guint)flags,
			max_age,
			share_limit);
		passim_item_set_basename(item, basename);
		passim_item_set_flags(item, flags);
		passim_item_set_max_age(item, max_age);
		passim_item_set_share_limit(item, share_limit);

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
		if (g_strstr_len(basename, -1, "/") != NULL) {
			g_dbus_method_invocation_return_error_literal(invocation,
								      G_DBUS_ERROR,
								      G_DBUS_ERROR_INVALID_ARGS,
								      "invalid basename");
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
			g_dbus_method_invocation_return_gerror(invocation, error);
			return;
		}

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
	if (g_strcmp0(property_name, "DaemonVersion") == 0)
		return g_variant_new_string(SOURCE_VERSION);

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

int
main(int argc, char *argv[])
{
	gboolean version = FALSE;
	gboolean timed_exit = FALSE;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = g_option_context_new(NULL);
	g_autoptr(GSocketService) service = g_socket_service_new();
	g_autoptr(PassimServer) self = g_new0(PassimServer, 1);
	const GOptionEntry options[] = {
	    {"version", '\0', 0, G_OPTION_ARG_NONE, &version, "Show project version", NULL},
	    {"timed-exit", '\0', 0, G_OPTION_ARG_NONE, &timed_exit, "Exit after a delay", NULL},
	    {NULL}};

	g_setenv("G_MESSAGES_DEBUG", "all", FALSE);
	g_setenv("G_DEBUG", "fatal-criticals", FALSE);

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
	if (!passim_server_start_dbus(self, &error)) {
		g_warning("failed to register D-Bus: %s", error->message);
		return 1;
	}
	if (!passim_server_libdir_scan(self, &error)) {
		g_printerr("failed to scan directory: %s\n", error->message);
		return 1;
	}
	if (!passim_avahi_connect(self->avahi, &error)) {
		g_warning("failed to contact daemon: %s", error->message);
		return 1;
	}
	passim_server_check_item_age(self);

	/* set up the webserver */
	if (!g_socket_listener_add_inet_port(G_SOCKET_LISTENER(service),
					     self->port,
					     NULL,
					     &error)) {
		g_printerr("%s: %s\n", argv[0], error->message);
		return 1;
	}
	g_info("HTTP server listening on port %d", self->port);
	g_signal_connect(service, "incoming", G_CALLBACK(passim_server_handler_cb), self);
	self->http_active = TRUE;

	/* register objects with Avahi */
	if (!passim_server_avahi_register(self, &error)) {
		g_warning("failed to register: %s", error->message);
		return 1;
	}

	g_main_loop_run(self->loop);
	return 0;
}
