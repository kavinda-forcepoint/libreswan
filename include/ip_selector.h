/* ip traffic selector, for libreswan
 *
 * Copyright (C) 2020-2021  Andrew Cagney
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Library General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/lgpl-2.1.txt>.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
 * License for more details.
 *
 */

#ifndef IP_SELECTOR_H
#define IP_SELECTOR_H

#include "ip_address.h"
#include "ip_endpoint.h"
#include "ip_subnet.h"
#include "ip_protoport.h"
#include "ip_protocol.h"
#include "ip_range.h"

struct jambuf;

/*
 * IKEv2 style traffic selectors can describe.
 *
 *    LO_ADDRESS..HI_ADDRESS : LO_PORT..HI_PORT
 *
 * However, this is currently implemented using a subnet which,
 * bizarrely, can describe:
 *
 *    NETWORK_PREFIX | 0 / MASK : PORT
 *
 * where PORT==0 imples 0..65535, and (presumably) port can only be
 * non-zero when the NETWORK_PREFIX/MASK is for a single address.
 */

/*
 * Define SELECTOR_TYPE to enable an ip_selector type separate to ip_subnet; expect
 * everything to break.
 */

#ifdef SELECTOR_TYPE

typedef struct {
	bool is_selector;
	/* (routing)prefix|host(id):port */
	ip_endpoint addr;
	/* (routing prefix) bits */
	unsigned maskbits;
	/* to-be-deleted - selector */
	bool is_subnet;
} ip_selector;

#else

#include "ip_subnet.h"
typedef ip_subnet ip_selector;

#endif

#define PRI_SELECTOR "%s"
#define pri_selector(S,B) str_selector(S, B)

void pexpect_selector(const ip_selector *s, const char *t, where_t where);
#define pselector(S) pexpect_selector(S, #S, HERE)

ip_selector selector_from_address(const ip_address *address);
ip_selector selector_from_address_protocol(const ip_address *address,
					   const ip_protocol *protocol);
ip_selector selector_from_address_protocol_port(const ip_address *address,
						const ip_protocol *protocol,
						ip_port port);

ip_selector selector_from_endpoint(const ip_endpoint *address);

ip_selector selector_from_subnet(const ip_subnet *subnet);
ip_selector selector_from_subnet_protocol_port(const ip_subnet *subnet,
					       const ip_protocol *protocol,
					       ip_port port);

ip_selector selector_from_address_protoport(const ip_address *address,
					    const ip_protoport *protoport);
ip_selector selector_from_subnet_protoport(const ip_subnet *subnet,
					   const ip_protoport *protoport);

err_t numeric_to_selector(shunk_t src, const struct ip_info *afi, ip_selector *dst);

/*
 * Magic values.
 *
 * XXX: While the headers call the all-zero address "ANY" (INADDR_ANY,
 * IN6ADDR_ANY_INIT), the headers also refer to the IPv6 value as
 * unspecified (for instance IN6_IS_ADDR_UNSPECIFIED()) leaving the
 * term "unspecified" underspecified.
 *
 * Consequently an AF_UNSPEC address (i.e., uninitialized or unset),
 * is identified by *_unset().
 */

extern const ip_selector unset_selector;
bool selector_is_unset(const ip_selector *selector);

/* attributes */

const struct ip_info *selector_type(const ip_selector *selector);

const ip_protocol *selector_protocol(const ip_selector *selector);
ip_range selector_range(const ip_selector *selector);
ip_port selector_port(const ip_selector *selector);
/* ip_port_range selector_port_range(const ip_selector *selector); */

/* hacks */
int selector_hport(const ip_selector *selector);
#define update_selector_hport(SELECTOR, HPORT) { (SELECTOR)->addr.hport = (HPORT); }
#define update_selector_ipproto(SELECTOR, IPPROTO) { (SELECTOR)->addr.ipproto = (IPPROTO); }

/* assuming a subnet like XFRM does */
ip_address selector_prefix(const ip_selector *selector);
ip_address selector_prefix_mask(const ip_selector *selector);
unsigned selector_prefix_bits(const ip_selector *selector);

bool selector_contains_all_addresses(const ip_selector *selector);
bool selector_contains_no_addresses(const ip_selector *selector);

bool selector_in_selector(const ip_selector *l, const ip_selector *r);
bool endpoint_in_selector(const ip_endpoint *l, const ip_selector *r);
bool address_in_selector(const ip_address *l, const ip_selector *r);

bool selector_eq(const ip_selector *l, const ip_selector *r);
bool selector_in(const ip_selector *l, const ip_selector *r);

bool selector_is_one_address(const ip_selector *selector);
bool selector_is_address(const ip_selector *selector, const ip_address *address);


/* printing */

typedef struct {
	char buf[sizeof(address_buf) + 4/*"/NNN"*/ + 6/*:65535*/];
} selector_buf;

const char *str_selector(const ip_selector *selector, selector_buf *out);
size_t jam_selector(struct jambuf *buf, const ip_selector *selector);

const char *str_selector_sensitive(const ip_selector *selector, selector_buf *out);
size_t jam_selector_sensitive(struct jambuf *buf, const ip_selector *selector);

typedef struct {
	char buf[sizeof(selector_buf) + sizeof("=UNKNOWN=UNKNOWN=>") + sizeof(selector_buf)];
} selectors_buf;

const char *str_selectors(const ip_selector *src, const ip_selector *dst, selectors_buf *out);
size_t jam_selectors(struct jambuf *buf, const ip_selector *src, const ip_selector *dst);

const char *str_selectors_sensitive(const ip_selector *src, const ip_selector *dst, selectors_buf *out);
size_t jam_selectors_sensitive(struct jambuf *buf, const ip_selector *src, const ip_selector *dst);

/*
 * XXX: two hacks to get around .client not containing a proper
 * selector and instead needing to compare just the client's subnet.
 *
 * These are needed by code manipulating end.client because it is
 * serving double time.  It holding either:
 *
 * - the configured client subnet
 * - a connection's shunt
 */

bool selector_subnet_eq(const ip_selector *lhs, const ip_selector *rhs);
bool selector_subnet_in(const ip_selector *lhs, const ip_selector *rhs);
const char *str_selector_subnet(const ip_selector *selector, subnet_buf *buf);
size_t jam_selector_subnet(struct jambuf *buf, const ip_selector *selector);
bool selector_subnet_is_address(const ip_selector *selector, const ip_address *address);

#endif
