/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>
#include <gnutls/abstract.h>
#include <gnutls/crypto.h>
#include <gnutls/pkcs7.h>

typedef guchar gnutls_data_t;

static void
_gnutls_datum_deinit(gnutls_datum_t *d)
{
	gnutls_free(d->data);
	gnutls_free(d);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_pkcs7_t, gnutls_pkcs7_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_privkey_t, gnutls_privkey_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_pubkey_t, gnutls_pubkey_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_crt_t, gnutls_x509_crt_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_dn_t, gnutls_x509_dn_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_privkey_t, gnutls_x509_privkey_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_spki_t, gnutls_x509_spki_deinit, NULL)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gnutls_data_t, gnutls_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gnutls_pkcs7_signature_info_st, gnutls_pkcs7_signature_info_deinit)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gnutls_datum_t, _gnutls_datum_deinit)
#pragma clang diagnostic pop

gchar *
passim_gnutls_datum_to_dn_str(const gnutls_datum_t *raw);
gnutls_x509_crt_t
passim_gnutls_load_crt_from_blob(GBytes *blob, gnutls_x509_crt_fmt_t format, GError **error);
gnutls_privkey_t
passim_gnutls_load_privkey_from_blob(GBytes *blob, GError **error);
gnutls_pubkey_t
passim_gnutls_load_pubkey_from_privkey(gnutls_privkey_t privkey, GError **error);

GBytes *
passim_gnutls_create_private_key(GError **error);
GBytes *
passim_gnutls_create_certificate(gnutls_privkey_t privkey, GError **error);
