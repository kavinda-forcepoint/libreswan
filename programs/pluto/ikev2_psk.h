/* pre-shared-key authentication, for libreswan
 *
 * Copyright (C) 2021  Andrew Cagney
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
 *
 */

#ifndef IKEV2_PSK_H
#define IKEV2_PSK_H

#include <stdbool.h>

enum keyword_authby;
struct ike_sa;
struct crypt_mac;
struct pbs_in;

bool v2_authsig_and_log_using_psk(enum keyword_authby authby,
				  const struct ike_sa *ike,
				  const struct crypt_mac *idhash,
				  struct pbs_in *sig_pbs);

#endif
