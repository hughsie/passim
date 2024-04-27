/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "passim-avahi-service.h"

void
passim_avahi_service_print(PassimAvahiService *service)
{
	g_debug("Service { iface:%i, proto:%i, name:%s, type:%s, domain:%s, flags:%u }",
		service->interface,
		service->protocol,
		service->name,
		service->type,
		service->domain,
		service->flags);
}

void
passim_avahi_service_free(PassimAvahiService *service)
{
	g_free(service->name);
	g_free(service->type);
	g_free(service->domain);
	g_free(service);
}
