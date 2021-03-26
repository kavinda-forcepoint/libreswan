/* ip_address tests, for libreswan
 *
 * Copyright (C) 2000  Henry Spencer.
 * Copyright (C) 2012 Paul Wouters <paul@libreswan.org>
 * Copyright (C) 2018 Andrew Cagney
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

#include <stdio.h>
#include <string.h>

#include "lswcdefs.h"		/* for elemsof() */
#include "constants.h"		/* for streq() */
#include "ip_address.h"
#include "ipcheck.h"

static void check_shunk_to_address(void)
{
	static const struct test {
		int line;
		int family;
		const char *in;
		const char *cooked;
		bool requires_dns;
	} tests[] = {

		/* any/unspec */
		{ LN, 4, "0.0.0.0", "0.0.0.0", false, },
		{ LN, 6, "::", "::", false, },
		{ LN, 6, "0:0:0:0:0:0:0:0", "::", false, },

		/* local */
		{ LN, 4, "127.0.0.1", "127.0.0.1", false, },
		{ LN, 6, "::1", "::1", false, },
		{ LN, 6, "0:0:0:0:0:0:0:1", "::1", false, },

		/* mask - and buffer overflow */
		{ LN, 4, "255.255.255.255", "255.255.255.255", false, },
		{ LN, 6, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", false, },

		/* all bytes and '/' */
		{ LN, 4, "1.2.3.4", "1.2.3.4", false, },
		{ LN, 6, "1:2:3:4:5:6:7:8", "1:2:3:4:5:6:7:8", false, },

		/* suppress leading zeros - 01 vs 1 */
		{ LN, 6, "0001:0012:0003:0014:0005:0016:0007:0018", "1:12:3:14:5:16:7:18", false, },
		/* drop leading 0:0: */
		{ LN, 6, "0:0:3:4:5:6:7:8", "::3:4:5:6:7:8", false, },
		/* drop middle 0:...:0 */
		{ LN, 6, "1:2:0:0:0:0:7:8", "1:2::7:8", false, },
		/* drop trailing :0..:0 */
		{ LN, 6, "1:2:3:4:5:0:0:0", "1:2:3:4:5::", false, },
		/* drop first 0:..:0 */
		{ LN, 6, "1:2:0:0:5:6:0:0", "1:2::5:6:0:0", false, },
		/* drop logest 0:..:0 */
		{ LN, 6, "0:0:3:0:0:0:7:8", "0:0:3::7:8", false, },
		/* need two 0 */
		{ LN, 6, "0:2:0:4:0:6:0:8", "0:2:0:4:0:6:0:8", false, },

		{ LN, 4, "www.libreswan.org", "188.127.201.229", .requires_dns = true, },
	};

	err_t err;

	for (size_t ti = 0; ti < elemsof(tests); ti++) {
		const struct test *t = &tests[ti];
		PRINT("%s '%s' -> cooked: %s dns: %s", pri_family(t->family), t->in,
		      t->cooked == NULL ? "ERROR" : t->cooked,
		      bool_str(t->requires_dns));

		const struct ip_info *type;
		ip_address tmp, *address = &tmp;

		/* NUMERIC/NULL */

		type = NULL;
		err = ttoaddress_num(shunk1(t->in), type, address);
		if (err != NULL) {
			if (t->cooked != NULL && !t->requires_dns) {
				FAIL("numeric_to_address(%s,NULL) unexpecedly failed: %s",
				     t->in, err);
			}
		} else if (t->requires_dns) {
			FAIL(" numeric_to_address(%s, NULL) unexpecedly parsed a DNS address",
			     t->in);
		} else if (t->cooked == NULL) {
			FAIL(" numeric_to_address(%s, NULL) unexpecedly succeeded", t->in);
		} else {
			CHECK_TYPE(address);
		}

		/* NUMERIC/TYPE */

		type = IP_TYPE(t->family);
		err = ttoaddress_num(shunk1(t->in), type, address);
		if (err != NULL) {
			if (!t->requires_dns) {
				FAIL(" numeric_to_address(%s, %s) unexpecedly failed: %s",
				     t->in, pri_family(t->family), err);
			}
		} else if (t->requires_dns) {
			FAIL(" numeric_to_address(%s, %s) unexpecedly parsed a DNS address",
			     t->in, pri_family(t->family));
		} else if (t->cooked == NULL) {
			FAIL(" numeric_to_address(%s, %s) unexpecedly succeeded",
			     t->in, pri_family(t->family));
		} else {
			CHECK_TYPE(address);
		}

		if (t->requires_dns && !use_dns) {
			PRINT("skipping str_address() tests as no DNS");
			continue;
		}

		/* DNS/TYPE */

		if (t->requires_dns && !use_dns) {
			PRINT("skipping dns_hunk_to_address(type) -- no DNS");
		} else {
			type = IP_TYPE(t->family);
			err = ttoaddress_dns(shunk1(t->in), type, address);
			if (err != NULL) {
				if (t->cooked != NULL) {
					FAIL("dns_hunk_to_address(type) unexpecedly failed: %s", err);
				}
			} else if (t->cooked == NULL) {
				FAIL(" dns_hunk_to_address(type) unexpecedly succeeded");
			} else {
				CHECK_TYPE(address);
			}
		}

		/* now convert it back cooked */
		CHECK_STR(address_buf, address, t->cooked, address);

	}
}

static void check_str_address_sensitive(void)
{
	static const struct test {
		int line;
		int family;
		const char *in;
		const char *out;
	} tests[] = {
		{ LN, 4, "1.2.3.4",			"<address>" },
		{ LN, 6, "1:12:3:14:5:16:7:18",	"<address>" },
	};

	for (size_t ti = 0; ti < elemsof(tests); ti++) {
		const struct test *t = &tests[ti];
		PRINT("%s '%s' -> '%s'", pri_family(t->family), t->in, t->out);

		/* convert it *to* internal format */
		const struct ip_info *type = NULL;
		ip_address tmp, *address = &tmp;
		err_t err = ttoaddress_num(shunk1(t->in), type, address);
		if (err != NULL) {
			FAIL("ttoaddress_num() failed: %s", err);
			continue;
		}
		CHECK_TYPE(address);
		CHECK_STR(address_buf, address_sensitive, t->out, address);
	}
}

static void check_str_address_reversed(void)
{
	static const struct test {
		int line;
		int family;
		const char *in;
		const char *out;                   /* NULL means error expected */
	} tests[] = {
		{ LN, 4, "1.2.3.4", "4.3.2.1.IN-ADDR.ARPA." },
		/* 0 1 2 3 4 5 6 7 8 9 a b c d e f 0 1 2 3 4 5 6 7 8 9 a b c d e f */
		{ LN, 6, "0123:4567:89ab:cdef:1234:5678:9abc:def0",
		  "0.f.e.d.c.b.a.9.8.7.6.5.4.3.2.1.f.e.d.c.b.a.9.8.7.6.5.4.3.2.1.0.IP6.ARPA.", }
	};

	for (size_t ti = 0; ti < elemsof(tests); ti++) {
		const struct test *t = &tests[ti];
		PRINT("%s '%s' -> '%s", pri_family(t->family), t->in, t->out);

		/* convert it *to* internal format */
		const struct ip_info *type = NULL;
		ip_address tmp, *address = &tmp;
		err_t err = ttoaddress_num(shunk1(t->in), type, address);
		if (err != NULL) {
			FAIL("ttoaddress_num() returned: %s", err);
			continue;
		}
		CHECK_TYPE(address);
		CHECK_STR(address_reversed_buf, address_reversed, t->out, address);
	}
}

static void check_in_addr(void)
{
	static const struct test {
		int line;
		const int family;
		const char *in;
		uint8_t addr[16];
	} tests[] = {
		{ LN, 4, "1.2.3.4", { 1, 2, 3, 4, }, },
		{ LN, 6, "102:304:506:708:90a:b0c:d0e:f10", { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, }, },
	};

	for (size_t ti = 0; ti < elemsof(tests); ti++) {
		const struct test *t = &tests[ti];
		PRINT("%s '%s' -> '%s'", pri_family(t->family), t->in, t->in);

		ip_address a;
		switch (t->family) {
		case 4:
		{
			struct in_addr in;
			memcpy(&in, t->addr, sizeof(in));
			a = address_from_in_addr(&in);
			break;
		}
		case 6:
		{
			struct in6_addr in6;
			memcpy(&in6, t->addr, sizeof(in6));
			a = address_from_in6_addr(&in6);
			break;
		}
		}

		/* as a string */
		address_buf buf;
		const char *out = str_address(&a, &buf);
		if (out == NULL) {
			FAIL("str_address() returned NULL");
		} else if (!strcaseeq(out, t->in)) {
			FAIL("str_address() returned '%s', expecting '%s'",
				out, t->in);
		}

		switch (t->family) {
		case 4:
		{
			uint32_t h = ntohl_address(&a);
			uint32_t n = htonl(h);
			if (!memeq(&n, t->addr, sizeof(n))) {
				FAIL("ntohl_address() returned %08"PRIx32", expecting something else", h);
			}
			break;
		}
		}
	}
}

static void check_address_is(void)
{
	static const struct test {
		int line;
		int family;
		const char *in;
		bool is_unset;
		bool is_any;
		bool is_specified;
		bool is_loopback;
	} tests[] = {
		{ LN, 0, "<invalid>",		.is_unset = true, },
		{ LN, 4, "0.0.0.0",			.is_any = true, },
		{ LN, 6, "::",			.is_any = true, },
		{ LN, 4, "1.2.3.4",			.is_specified = true, },
		{ LN, 6, "1:12:3:14:5:16:7:18",	.is_specified = true, },
		{ LN, 4, "127.0.0.1",		.is_specified = true, .is_loopback = true, },
		{ LN, 6, "::1",			.is_specified = true, .is_loopback = true, },
	};

	for (size_t ti = 0; ti < elemsof(tests); ti++) {
		const struct test *t = &tests[ti];
		PRINT("%s '%s'-> unset: %s, any: %s, specified: %s", pri_family(t->family), t->in,
		      bool_str(t->is_unset), bool_str(t->is_any), bool_str(t->is_specified));

		/* convert it *to* internal format */
		ip_address tmp, *address = &tmp;
		if (t->family == 0) {
			tmp = unset_address;
		} else {
			const struct ip_info *type = NULL;
			err_t err = ttoaddress_num(shunk1(t->in), type, &tmp);
			if (err != NULL) {
				FAIL("ttoaddress_num() failed: %s", err);
			}
		}

		CHECK_COND(address, is_unset);
		CHECK_COND2(address, is_any);
		CHECK_COND2(address, is_specified);
		CHECK_COND2(address, is_loopback);
	}
}

static void check_addresses_to(void)
{
	static const struct test {
		int line;
		int family;
		const char *lo;
		const char *hi;
		const char *subnet;	/* NULL means error expected */
		const char *range;	/* NULL means use subnet */
	} tests[] = {

		/* zero-zero; zero-one */
		{ LN, 4, "0.0.0.0", "0.0.0.0", NULL, NULL, },
		{ LN, 6, "::",      "::",      NULL, NULL, },
		{ LN, 4, "0.0.0.0", "0.0.0.1", "0.0.0.0/31", NULL, },
		{ LN, 6, "::",      "::1",     "::/127", NULL, },

		/* single address */
		{ LN, 4, "1.2.3.0",    "1.2.3.0",   "1.2.3.0/32", NULL, },
		{ LN, 6, "::1",        "::1",       "::1/128", NULL, },
		{ LN, 4, "1.2.3.4",    "1.2.3.4",   "1.2.3.4/32", NULL, },

		/* subnet */
		{ LN, 4, "1.2.3.8",    "1.2.3.15", "1.2.3.8/29", NULL, },
		{ LN, 4, "1.2.3.240",  "1.2.3.255", "1.2.3.240/28", NULL, },
		{ LN, 4, "0.0.0.0",    "255.255.255.255", "0.0.0.0/0", NULL, },
		{ LN, 4, "1.2.0.0",    "1.2.255.255", "1.2.0.0/16", NULL, },
		{ LN, 4, "1.2.0.0",    "1.2.0.255", "1.2.0.0/24", NULL, },
		{ LN, 4, "1.2.255.0",  "1.2.255.255", "1.2.255.0/24", NULL, },
		{ LN, 6, "1:2:3:4:5:6:7:0",   "1:2:3:4:5:6:7:ffff", "1:2:3:4:5:6:7:0/112", NULL, },
		{ LN, 6, "1:2:3:4:5:6:7:0",   "1:2:3:4:5:6:7:fff", "1:2:3:4:5:6:7:0/116", NULL, },
		{ LN, 6, "1:2:3:4:5:6:7:f0",  "1:2:3:4:5:6:7:ff", "1:2:3:4:5:6:7:f0/124", NULL, },

		/* range only */
		{ LN, 4, "1.2.3.0",    "1.2.3.254", NULL, "1.2.3.0-1.2.3.254", },
		{ LN, 4, "1.2.3.0",    "1.2.3.126", NULL, "1.2.3.0-1.2.3.126", },
		{ LN, 4, "1.2.3.0",    "1.2.3.125", NULL, "1.2.3.0-1.2.3.125", },
		{ LN, 4, "1.2.255.1",  "1.2.255.255", NULL, "1.2.255.1-1.2.255.255", },
		{ LN, 4, "1.2.0.1",    "1.2.255.255", NULL, "1.2.0.1-1.2.255.255", },

		/* all */
		{ LN, 4, "0.0.0.0", IPv4_MAX, "0.0.0.0/0", NULL, },
		{ LN, 6, "::",      IPv6_MAX, "::/0",      NULL, },

		/* wrong order */
		{ LN, 4, "1.2.255.0",  "1.2.254.255", NULL, NULL, },
	};

	const char *oops;

	for (size_t ti = 0; ti < elemsof(tests); ti++) {
		const struct test *t = &tests[ti];
		/* range falls back to subnet */
		PRINT("%s-%s -> %s -> %s",
		      t->lo, t->hi,
		      t->range == NULL ? "<bad-range>" : t->range,
		      t->subnet == NULL ? "<bad-subnet>" : t->subnet);

		const struct ip_info *type = IP_TYPE(t->family);

		ip_address lo;
		oops = ttoaddress_num(shunk1(t->lo), type, &lo);
		if (oops != NULL) {
			FAIL("ttoaddress_num(lo=%s) failed: %s", t->lo, oops);
			continue;
		}

		ip_address hi;
		oops = ttoaddress_num(shunk1(t->hi), type, &hi);
		if (oops != NULL) {
			FAIL("ttoaddress_num(hi=%s) failed: %s", t->hi, oops);
			continue;
		}

		ip_subnet s;
		oops = addresses_to_nonzero_subnet(lo, hi, &s);
		subnet_buf sb;
		str_subnet(&s, &sb);

		if (oops != NULL) {
			if (t->subnet != NULL) {
				FAIL("addresses_to_subnet(%s,%s) failed: %s", t->lo, t->hi, oops);
			}
		} else if (t->subnet == NULL) {
			FAIL("addresses_to_subnet(%s,%s) returned %s unexpectedly",
			     t->lo, t->hi, sb.buf);
		} else if (!streq(t->subnet, sb.buf)) {
			FAIL("addresses_to_subnet(%s,%s) returned `%s', expected `%s'",
			     t->lo, t->hi, sb.buf, t->subnet);
		}

		ip_range r;
		range_buf rb;
		oops = addresses_to_nonzero_range(lo, hi, &r);
		r.is_subnet = true; /* maybe */
		str_range(&r, &rb);

		if (oops != NULL) {
			if (t->range != NULL || t->subnet != NULL) {
				FAIL("addresses_to_range(%s,%s) unexpectedly failed: %s",
				     t->lo, t->hi, oops);
			}
		} else if (t->range != NULL) {
			if (!streq(t->range, rb.buf)) {
				FAIL("addresses_to_range(%s,%s) returned `%s', expected `%s'",
				     t->lo, t->hi, rb.buf, t->range);
			}
		} else if (t->subnet != NULL) {
			if (!streq(t->subnet, rb.buf)) {
				FAIL("addresses_to_range(%s,%s) returned `%s', expected `%s'",
				     t->lo, t->hi, rb.buf, t->subnet);
			}
		} else {
			FAIL("addresses_to_range(%s,%s) returned %s unexpectedly",
			     t->lo, t->hi, rb.buf);
		}

		if (t->range == NULL) {
			continue;
		}

		oops = range_to_subnet(r, &s);
		str_subnet(&s, &sb);

		if (oops != NULL && t->subnet == NULL) {
			/* okay, error expected */
		} else if (oops != NULL) {
			FAIL("range_to_subnet(%s=>%s) failed: %s",
			     rb.buf, sb.buf, oops);
		} else if (t->subnet == NULL) {
			FAIL("range_to_subnet(%s) returned %s unexpectedly",
			     rb.buf, sb.buf);
		} else {
			if (!streq(t->subnet, sb.buf)) {
				FAIL("range_to_subnet(%s) returned `%s', expected `%s'",
				     rb.buf, sb.buf, t->subnet);
			}
		}

	}
}

void ip_address_check(void)
{
	check_shunk_to_address();
	check_str_address_sensitive();
	check_str_address_reversed();
	check_address_is();
	check_in_addr();
	check_addresses_to();
}
