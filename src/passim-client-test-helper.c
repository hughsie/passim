/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>

#include "passim-client.h"

static const gchar *bytes_content = "passim-client-test-bytes-data";
static const gchar *file_content = "passim-client-test-file-data";

int
main(int argc, char *argv[])
{
	g_autoptr(PassimClient) client = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) items = NULL;
	g_autoptr(GPtrArray) items2 = NULL;
	g_autoptr(PassimItem) item_bytes = NULL;
	g_autoptr(PassimItem) item_file = NULL;
	g_autoptr(PassimItem) item_nodata = NULL;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GFile) gfile = NULL;
	g_autoptr(GBytes) file_bytes = NULL;
	g_autofree gchar *tmpfile = NULL;
	g_autofree gchar *hash_bytes_str = NULL;
	g_autofree gchar *hash_file_str = NULL;
	const gchar *version;
	const gchar *name;
	const gchar *uri;

	client = passim_client_new();
	g_assert_nonnull(client);

	if (!passim_client_load(client, &error)) {
		g_printerr("Failed to load: %s\n", error->message);
		return 1;
	}

	version = passim_client_get_version(client);
	g_assert_nonnull(version);
	name = passim_client_get_name(client);
	g_assert_nonnull(name);
	uri = passim_client_get_uri(client);
	g_assert_nonnull(uri);
	g_assert_cmpint(passim_client_get_status(client), ==, PASSIM_STATUS_RUNNING);

	items = passim_client_get_items(client, &error);
	if (items == NULL) {
		g_printerr("Failed to get items: %s\n", error->message);
		return 1;
	}
	g_assert_cmpuint(items->len, >, 0);

	/* clean up items from any previous failed run */
	hash_bytes_str = g_compute_checksum_for_string(G_CHECKSUM_SHA256, bytes_content, -1);
	hash_file_str = g_compute_checksum_for_string(G_CHECKSUM_SHA256, file_content, -1);
	passim_client_unpublish(client, hash_bytes_str, NULL);
	passim_client_unpublish(client, hash_file_str, NULL);

	/* publish via bytes */
	item_bytes = passim_item_new();
	bytes = g_bytes_new_static(bytes_content, strlen(bytes_content));
	passim_item_set_basename(item_bytes, "client-test-bytes.bin");
	passim_item_set_bytes(item_bytes, bytes);
	passim_item_set_max_age(item_bytes, 3600);
	passim_item_set_share_limit(item_bytes, 5);
	if (!passim_client_publish(client, item_bytes, &error)) {
		g_printerr("Failed to publish bytes: %s\n", error->message);
		return 1;
	}

	/* publish via file */
	tmpfile = g_build_filename(g_get_tmp_dir(), "passim-client-test.bin", NULL);
	if (!g_file_set_contents(tmpfile, file_content, -1, &error)) {
		g_printerr("Failed to write tmpfile: %s\n", error->message);
		return 1;
	}
	item_file = passim_item_new();
	gfile = g_file_new_for_path(tmpfile);
	file_bytes = g_bytes_new_static(file_content, strlen(file_content));
	passim_item_set_file(item_file, gfile);
	passim_item_set_bytes(item_file, file_bytes);
	passim_item_set_basename(item_file, "client-test-file.bin");
	passim_item_set_max_age(item_file, 3600);
	passim_item_set_share_limit(item_file, 5);
	if (!passim_client_publish(client, item_file, &error)) {
		g_printerr("Failed to publish file: %s\n", error->message);
		return 1;
	}
	g_unlink(tmpfile);

	/* verify items appeared */
	items2 = passim_client_get_items(client, &error);
	if (items2 == NULL) {
		g_printerr("Failed to get items after publish: %s\n", error->message);
		return 1;
	}
	g_assert_cmpuint(items2->len, >, items->len);

	/* unpublish both */
	if (!passim_client_unpublish(client, hash_bytes_str, &error)) {
		g_printerr("Failed to unpublish bytes item: %s\n", error->message);
		return 1;
	}

	if (!passim_client_unpublish(client, hash_file_str, &error)) {
		g_printerr("Failed to unpublish file item: %s\n", error->message);
		return 1;
	}

	/* publish with no data should fail */
	item_nodata = passim_item_new();
	passim_item_set_basename(item_nodata, "no-data.bin");
	g_assert_false(passim_client_publish(client, item_nodata, &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

	return 0;
}
