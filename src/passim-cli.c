/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <passim.h>

#include "passim-common.h"

typedef struct {
	PassimClient *client;
	gboolean next_reboot;
} PassimCli;

typedef gboolean (*PassimCliCmdFunc)(PassimCli *util, gchar **values, GError **error);
typedef struct {
	gchar *name;
	gchar *arguments;
	gchar *description;
	PassimCliCmdFunc callback;
} PassimCliCmd;

static void
passim_cli_cmd_free(PassimCliCmd *item)
{
	g_free(item->name);
	g_free(item->arguments);
	g_free(item->description);
	g_free(item);
}

static void
passim_cli_private_free(PassimCli *self)
{
	if (self->client != NULL)
		g_object_unref(self->client);
	g_free(self);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PassimCli, passim_cli_private_free)
#pragma clang diagnostic pop

static GPtrArray *
passim_cli_cmd_array_new(void)
{
	return g_ptr_array_new_with_free_func((GDestroyNotify)passim_cli_cmd_free);
}

static gint
passim_cli_cmd_sort_cb(PassimCliCmd **item1, PassimCliCmd **item2)
{
	return g_strcmp0((*item1)->name, (*item2)->name);
}

static void
passim_cli_cmd_array_sort(GPtrArray *array)
{
	g_ptr_array_sort(array, (GCompareFunc)passim_cli_cmd_sort_cb);
}

static void
passim_cli_cmd_array_add(GPtrArray *array,
			 const gchar *name,
			 const gchar *arguments,
			 const gchar *description,
			 PassimCliCmdFunc callback)
{
	g_auto(GStrv) names = NULL;

	g_return_if_fail(name != NULL);
	g_return_if_fail(description != NULL);
	g_return_if_fail(callback != NULL);

	/* add each one */
	names = g_strsplit(name, ",", -1);
	for (guint i = 0; names[i] != NULL; i++) {
		PassimCliCmd *item = g_new0(PassimCliCmd, 1);
		item->name = g_strdup(names[i]);
		if (i == 0) {
			item->description = g_strdup(description);
		} else {
			item->description = g_strdup_printf("Alias to %s", names[0]);
		}
		item->arguments = g_strdup(arguments);
		item->callback = callback;
		g_ptr_array_add(array, item);
	}
}

static gboolean
passim_cli_cmd_array_run(GPtrArray *array,
			 PassimCli *self,
			 const gchar *command,
			 gchar **values,
			 GError **error)
{
	g_auto(GStrv) values_copy = g_new0(gchar *, g_strv_length(values) + 1);

	/* clear out bash completion sentinel */
	for (guint i = 0; values[i] != NULL; i++) {
		if (g_strcmp0(values[i], "{") == 0)
			break;
		values_copy[i] = g_strdup(values[i]);
	}

	/* find command */
	for (guint i = 0; i < array->len; i++) {
		PassimCliCmd *item = g_ptr_array_index(array, i);
		if (g_strcmp0(item->name, command) == 0)
			return item->callback(self, values_copy, error);
	}

	/* not found */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Command not found");
	return FALSE;
}

static gchar *
passim_cli_cmd_array_to_string(GPtrArray *array)
{
	gsize len;
	const gsize max_len = 35;
	GString *string;

	/* print each command */
	string = g_string_new("");
	for (guint i = 0; i < array->len; i++) {
		PassimCliCmd *item = g_ptr_array_index(array, i);
		g_string_append(string, "  ");
		g_string_append(string, item->name);
		len = strlen(item->name) + 2;
		if (item->arguments != NULL) {
			g_string_append(string, " ");
			g_string_append(string, item->arguments);
			len += strlen(item->arguments) + 1;
		}
		if (len < max_len) {
			for (gsize j = len; j < max_len + 1; j++)
				g_string_append_c(string, ' ');
			g_string_append(string, item->description);
			g_string_append_c(string, '\n');
		} else {
			g_string_append_c(string, '\n');
			for (gsize j = 0; j < max_len + 1; j++)
				g_string_append_c(string, ' ');
			g_string_append(string, item->description);
			g_string_append_c(string, '\n');
		}
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size(string, string->len - 1);

	return g_string_free(string, FALSE);
}

static gboolean
passim_cli_dump(PassimCli *self, gchar **values, GError **error)
{
	g_autoptr(GPtrArray) items = NULL;

	items = passim_client_get_items(self->client, error);
	if (items == NULL)
		return FALSE;
	for (guint i = 0; i < items->len; i++) {
		PassimItem *item = g_ptr_array_index(items, i);
		g_autofree gchar *str = passim_item_to_string(item);
		g_print("%s\n", str);
	}

	/* success */
	return TRUE;
}

static gboolean
passim_cli_publish(PassimCli *self, gchar **values, GError **error)
{
	g_autofree gchar *str = NULL;
	g_autoptr(PassimItem) item = passim_item_new();

	/* parse args */
	if (g_strv_length(values) < 1) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_ARGUMENT,
				    "Invalid arguments");
		return FALSE;
	}
	if (!passim_item_load_filename(item, values[0], error))
		return FALSE;
	if (g_strv_length(values) > 1)
		passim_item_set_max_age(item, g_ascii_strtoull(values[1], NULL, 10));
	if (g_strv_length(values) > 2)
		passim_item_set_share_count(item, g_ascii_strtoull(values[2], NULL, 10));
	if (self->next_reboot)
		passim_item_add_flag(item, PASSIM_ITEM_FLAG_NEXT_REBOOT);
	if (!passim_client_publish(self->client, item, error))
		return FALSE;

	/* success */
	str = passim_item_to_string(item);
	g_print("Published: %s\n", str);
	return TRUE;
}

static gboolean
passim_cli_unpublish(PassimCli *self, gchar **values, GError **error)
{
	g_autofree gchar *str = NULL;

	/* parse args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_ARGUMENT,
				    "Invalid arguments");
		return FALSE;
	}
	if (!passim_client_unpublish(self->client, values[0], error))
		return FALSE;

	/* success */
	g_print("Unpublished: %s\n", values[0]);
	return TRUE;
}

int
main(int argc, char *argv[])
{
	gboolean version = FALSE;
	g_autofree gchar *cmd_descriptions = NULL;
	g_autoptr(PassimCli) self = g_new0(PassimCli, 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = g_option_context_new(NULL);
	g_autoptr(GPtrArray) cmd_array = passim_cli_cmd_array_new();
	const GOptionEntry options[] = {
	    {"version", '\0', 0, G_OPTION_ARG_NONE, &version, "Show project version", NULL},
	    {"next-reboot", '\0', 0, G_OPTION_ARG_NONE, &self->next_reboot, "Next reboot", NULL},
	    {NULL}};
	passim_cli_cmd_array_add(cmd_array, "dump", NULL, "Dump files shared", passim_cli_dump);
	passim_cli_cmd_array_add(cmd_array,
				 "publish",
				 "FILENAME [MAX-AGE] [MAX-SHARE]",
				 "Publish an additional file",
				 passim_cli_publish);
	passim_cli_cmd_array_add(cmd_array,
				 "unpublish",
				 "HASH",
				 "Unpublish an existing file",
				 passim_cli_unpublish);
	passim_cli_cmd_array_sort(cmd_array);

	cmd_descriptions = passim_cli_cmd_array_to_string(cmd_array);
	g_option_context_set_summary(context, cmd_descriptions);
	g_option_context_set_description(context, "Interacting with the local passimd process.");
	g_set_application_name("Passim CLI");
	g_option_context_add_main_entries(context, options, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("Failed to parse arguments: %s", error->message);
		return EXIT_FAILURE;
	}

	/* connect to daemon */
	self->client = passim_client_new();
	if (!passim_client_load(self->client, &error)) {
		g_printerr("Failed to connect to daemon: %s", error->message);
		return EXIT_FAILURE;
	}

	/* just show versions and exit */
	if (version) {
		g_print("client version: %s\n", VERSION);
		g_print("daemon version: %s\n", passim_client_get_version(self->client));
		return EXIT_SUCCESS;
	}

	/* run command */
	if (!passim_cli_cmd_array_run(cmd_array, self, argv[1], (gchar **)&argv[2], &error)) {
		g_dbus_error_strip_remote_error(error);
		g_printerr("%s\n", error->message);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
