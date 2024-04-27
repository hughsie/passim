/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "passim-version.h"

/**
 * passim_version_string:
 *
 * Gets the libpassim installed runtime version.
 *
 * This may be different to the *build-time* version if the daemon and library
 * objects somehow get out of sync.
 *
 * Returns: version string
 *
 * Since: 0.1.0
 **/
const gchar *
passim_version_string(void)
{
	return G_STRINGIFY(PASSIM_MAJOR_VERSION) "." G_STRINGIFY(
	    PASSIM_MINOR_VERSION) "." G_STRINGIFY(PASSIM_MICRO_VERSION);
}
