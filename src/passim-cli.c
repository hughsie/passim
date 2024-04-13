/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <libintl.h>
#include <locale.h>
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

typedef struct {
	const gchar *key;
	gchar *value;
} PassimItemAttr;

static gchar *
passim_cli_item_flag_to_string(PassimItemFlags flags)
{
	const gchar *strv[4] = {NULL};
	guint i = 0;

	if (flags & PASSIM_ITEM_FLAG_DISABLED) {
		/* TRANSLATORS: the item is not enabled */
		strv[i++] = _("Disabled");
	}
	if (flags & PASSIM_ITEM_FLAG_NEXT_REBOOT) {
		/* TRANSLATORS: only begin sharing the item after the next restart */
		strv[i++] = _("Next Reboot");
	}
	return g_strjoinv(", ", (gchar **)strv);
}

static GPtrArray *
passim_cli_item_to_attrs(PassimItem *item)
{
	GPtrArray *array = g_ptr_array_new_with_free_func(g_free);

	if (passim_item_get_basename(item) != NULL) {
		PassimItemAttr *attr = g_new0(PassimItemAttr, 1);
		/* TRANSLATORS: item file basename */
		attr->key = _("Filename");
		attr->value = g_strdup(passim_item_get_basename(item));
		g_ptr_array_add(array, attr);
	}
	if (passim_item_get_flags(item) != PASSIM_ITEM_FLAG_NONE) {
		PassimItemAttr *attr = g_new0(PassimItemAttr, 1);
		/* TRANSLATORS: item flags */
		attr->key = _("Flags");
		attr->value = passim_cli_item_flag_to_string(passim_item_get_flags(item));
		g_ptr_array_add(array, attr);
	}
	if (passim_item_get_cmdline(item) != NULL) {
		PassimItemAttr *attr = g_new0(PassimItemAttr, 1);
		/* TRANSLATORS: basename of the thing that published the item */
		attr->key = _("Command Line");
		attr->value = g_strdup(passim_item_get_cmdline(item));
		g_ptr_array_add(array, attr);
	}
	if (passim_item_get_max_age(item) != G_MAXUINT32) {
		PassimItemAttr *attr = g_new0(PassimItemAttr, 1);
		/* TRANSLATORS: age of the published item */
		attr->key = _("Age");
		attr->value = g_strdup_printf("%u/%u",
					      passim_item_get_age(item),
					      passim_item_get_max_age(item));
		g_ptr_array_add(array, attr);
	}
	if (passim_item_get_share_limit(item) != G_MAXUINT32) {
		PassimItemAttr *attr = g_new0(PassimItemAttr, 1);
		/* TRANSLATORS: number of times we can share the item */
		attr->key = _("Share Limit");
		attr->value = g_strdup_printf("%u/%u",
					      passim_item_get_share_count(item),
					      passim_item_get_share_limit(item));
		g_ptr_array_add(array, attr);
	}
	if (passim_item_get_size(item) != 0) {
		PassimItemAttr *attr = g_new0(PassimItemAttr, 1);
		/* TRANSLATORS: size of the published item */
		attr->key = _("Size");
		attr->value = g_format_size(passim_item_get_size(item));
		g_ptr_array_add(array, attr);
	}
	return array;
}

static gchar *
passim_cli_align_indent(const gchar *key, const gchar *value, guint indent)
{
	GString *str = g_string_new(key);
	g_string_append_c(str, ':');
	for (guint i = str->len; i < indent - 1; i++)
		g_string_append_c(str, ' ');
	g_string_append_c(str, ' ');
	g_string_append(str, value);
	return g_string_free(str, FALSE);
}

#define PASSIM_CLI_VALIGN 20

static gboolean
passim_cli_status(PassimCli *self, gchar **values, GError **error)
{
	PassimStatus status = passim_client_get_status(self->client);
	const gchar *status_value;
	gdouble download_saving = passim_client_get_download_saving(self->client);
	gdouble carbon_saving = passim_client_get_carbon_saving(self->client);
	g_autofree gchar *status_str = NULL;
	g_autoptr(GPtrArray) items = NULL;

	/* global status */
	if (status == PASSIM_STATUS_STARTING || status == PASSIM_STATUS_LOADING) {
		/* TRANSLATORS: daemon is starting up */
		status_value = _("Loading…");
	} else if (status == PASSIM_STATUS_DISABLED_METERED) {
		/* TRANSLATORS: daemon is scared to publish files */
		status_value = _("Disabled (metered network)");
	} else if (status == PASSIM_STATUS_RUNNING) {
		/* TRANSLATORS: daemon is offering files like normal */
		status_value = _("Running");
	} else {
		status_value = passim_status_to_string(status);
	}
	status_str = passim_cli_align_indent(_("Status"), status_value, PASSIM_CLI_VALIGN);
	g_print("%s\n", status_str);

	/* this is important enough to show */
	if (download_saving > 0) {
		g_autofree gchar *download_value = g_format_size(download_saving);
		g_autofree gchar *download_str =
		    /* TRANSLATORS: how many bytes we did not download from the internet */
		    passim_cli_align_indent(_("Network Saving"), download_value, PASSIM_CLI_VALIGN);
		g_print("%s\n", download_str);
	}
	if (carbon_saving > 0.001) {
		g_autofree gchar *carbon_value = g_strdup_printf("%.02lf kg CO₂e", carbon_saving);
		g_autofree gchar *carbon_str =
		    /* TRANSLATORS: how much carbon we did not *burn* by using local data */
		    passim_cli_align_indent(_("Carbon Saving"), carbon_value, PASSIM_CLI_VALIGN);
		g_print("%s\n", carbon_str);
	}

	/* show location of the web console */
	if (passim_client_get_uri(self->client) != NULL) {
		g_autofree gchar *uri_str =
		    /* TRANSLATORS: full https://whatever of the daemon */
		    passim_cli_align_indent(_("URI"),
					    passim_client_get_uri(self->client),
					    PASSIM_CLI_VALIGN);
		g_print("%s\n", uri_str);
	}

	/* all items */
	items = passim_client_get_items(self->client, error);
	if (items == NULL)
		return FALSE;
	for (guint i = 0; i < items->len; i++) {
		PassimItem *item = g_ptr_array_index(items, i);
		g_autoptr(GPtrArray) attrs = passim_cli_item_to_attrs(item);

		g_print("\n%s\n", passim_item_get_hash(item));
		for (guint j = 0; j < attrs->len; j++) {
			PassimItemAttr *attr = g_ptr_array_index(attrs, j);
			g_autofree gchar *str =
			    passim_cli_align_indent(attr->key, attr->value, PASSIM_CLI_VALIGN - 2);
			g_print("%s %s\n", j < attrs->len - 1 ? "├" : "└", str);
		}
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
				    /* TRANSLATORS: user mistyped the command */
				    _("Invalid arguments"));
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
	/* TRANSLATORS: now sharing to the world */
	g_print("%s: %s\n", _("Published"), str);
	return TRUE;
}

static gboolean
passim_cli_unpublish(PassimCli *self, gchar **values, GError **error)
{
	/* parse args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_ARGUMENT,
				    /* TRANSLATORS: user mistyped the command */
				    _("Invalid arguments"));
		return FALSE;
	}
	if (!passim_client_unpublish(self->client, values[0], error))
		return FALSE;

	/* TRANSLATORS: no longer sharing with the world */
	g_print("%s: %s\n", _("Unpublished"), values[0]);
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
	    /* TRANSLATORS: --version */
	    {"version", '\0', 0, G_OPTION_ARG_NONE, &version, N_("Show project version"), NULL},
	    /* TRANSLATORS: only begin sharing the item after the next restart */
	    {"next-reboot",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &self->next_reboot,
	     N_("Next reboot"),
	     NULL},
	    {NULL},
	};

	setlocale(LC_ALL, "");

	bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	passim_cli_cmd_array_add(cmd_array,
				 "status,dump",
				 NULL,
				 /* TRANSLATORS: CLI action description */
				 _("Show daemon status"),
				 passim_cli_status);
	passim_cli_cmd_array_add(cmd_array,
				 "publish",
				 /* TRANSLATORS: CLI option example */
				 _("FILENAME [MAX-AGE] [MAX-SHARE]"),
				 /* TRANSLATORS: CLI action description */
				 _("Publish an additional file"),
				 passim_cli_publish);
	passim_cli_cmd_array_add(cmd_array,
				 "unpublish",
				 /* TRANSLATORS: CLI option example */
				 _("HASH"),
				 /* TRANSLATORS: CLI action description */
				 _("Unpublish an existing file"),
				 passim_cli_unpublish);
	passim_cli_cmd_array_sort(cmd_array);

	cmd_descriptions = passim_cli_cmd_array_to_string(cmd_array);
	g_option_context_set_summary(context, cmd_descriptions);
	/* TRANSLATORS: CLI tool description */
	g_option_context_set_description(context, _("Interact with the local passimd process."));
	/* TRANSLATORS: CLI tool name */
	g_set_application_name(_("Passim CLI"));
	g_option_context_add_main_entries(context, options, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		/* TRANSLATORS: we don't know what to do */
		g_printerr("%s: %s", _("Failed to parse arguments"), error->message);
		return EXIT_FAILURE;
	}

	/* connect to daemon */
	self->client = passim_client_new();
	if (!passim_client_load(self->client, &error)) {
		/* TRANSLATORS: daemon failed to start */
		g_printerr("%s: %s", _("Failed to connect to daemon"), error->message);
		return EXIT_FAILURE;
	}

	/* just show versions and exit */
	if (version) {
		/* TRANSLATORS: CLI tool */
		g_print("%s: %s\n", _("client version"), VERSION);
		/* TRANSLATORS: server */
		g_print("%s: %s\n", _("daemon version"), passim_client_get_version(self->client));
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
