/* ip subnet, for libreswan
 *
 * Copyright (C) 2012-2017 Paul Wouters <pwouters@redhat.com>
 * Copyright (C) 2012 Avesh Agarwal <avagarwa@redhat.com>
 * Copyright (C) 1998-2002,2015  D. Hugh Redelmeier.
 * Copyright (C) 2016-2019 Andrew Cagney <cagney@gnu.org>
 * Copyright (C) 2017 Vukasin Karadzic <vukasin.karadzic@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "jambuf.h"
#include "ip_subnet.h"
#include "passert.h"
#include "lswlog.h"	/* for pexpect() */
#include "ip_info.h"

const ip_subnet unset_subnet; /* all zeros */

static ip_subnet subnet2(const ip_address *address, int maskbits)
{
	ip_subnet s = {
		.addr = strip_endpoint(address, HERE),
		.maskbits = maskbits,
		.is_subnet = true,
	};
	psubnet(&s);
	return s;
}

ip_subnet subnet_from_address(const ip_address *address)
{
	const struct ip_info *afi = address_type(address);
	if (!pexpect(afi != NULL)) {
		return unset_subnet;
	}
	return subnet2(address, afi->mask_cnt);
}

ip_address subnet_prefix(const ip_subnet *src)
{
	return address_blit(endpoint_address(&src->addr),
			    /*routing-prefix*/&keep_bits,
			    /*host-id*/&clear_bits,
			    src->maskbits);
}

const struct ip_info *subnet_type(const ip_subnet *src)
{
	return endpoint_type(&src->addr);
}

#ifndef SUBNET_TYPE
int subnet_hport(const ip_subnet *s)
{
	return endpoint_hport(&s->addr);
}
#endif

bool subnet_is_unset(const ip_subnet *s)
{
	return subnet_type(s) == NULL;
}

bool subnet_is_specified(const ip_subnet *s)
{
	return endpoint_is_specified(&s->addr);
}

bool subnet_contains_all_addresses(const ip_subnet *s)
{
	const struct ip_info *afi = subnet_type(s);
	if (afi == NULL) {
		return false;
	}
	if (s->addr.hport != 0) {
		return false;
	}
	if (s->maskbits != 0) {
		return false;
	}
	ip_address network = subnet_prefix(s);
	return address_is_any(&network);
}

bool subnet_contains_no_addresses(const ip_subnet *s)
{
	const struct ip_info *afi = subnet_type(s);
	if (afi == NULL) {
		return false;
	}
	if (s->maskbits != afi->mask_cnt) {
		return false;
	}
	if (s->addr.hport != 0) {
		return false; /* weird one */
	}
	ip_address network = subnet_prefix(s);
	return address_is_any(&network);
}

bool subnet_contains_one_address(const ip_subnet *s)
{
	/* Unlike subnetishost() this rejects 0.0.0.0/32. */
	const struct ip_info *afi = subnet_type(s);
	if (afi == NULL) {
		return false;
	}
	if (s->addr.hport != 0) {
		return false;
	}
	if (s->maskbits != afi->mask_cnt) {
		return false;
	}
	/* ignore port */
	ip_address network = subnet_prefix(s);
	return !address_is_any(&network); /* i.e., non-zero */
}

/*
 * subnet mask - get the mask of a subnet, as an address
 *
 * For instance 1.2.3.4/24 -> 255.255.255.0.
 */

ip_address subnet_mask(const ip_subnet *src)
{
	return address_blit(endpoint_address(&src->addr),
			    /*network-prefix*/ &set_bits,
			    /*host-id*/ &clear_bits,
			    src->maskbits);
}

size_t jam_subnet(jambuf_t *buf, const ip_subnet *subnet)
{
	size_t s = 0;
	s += jam_address(buf, &subnet->addr); /* sensitive? */
	s += jam(buf, "/%u", subnet->maskbits);
	return s;
}

const char *str_subnet(const ip_subnet *subnet, subnet_buf *out)
{
	jambuf_t buf = ARRAY_AS_JAMBUF(out->buf);
	jam_subnet(&buf, subnet);
	return out->buf;
}
