/* IKEv2 DELETE Exchange
 *
 * Copyright (C) 2020 Andrew Cagney
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

#include "defs.h"
#include "state.h"
#include "ikev2.h"
#include "ikev2_delete.h"
#include "ikev2_message.h"
#include "ikev2_send.h"
#include "log.h"
#include "demux.h"
#include "connections.h"

/*
 * Send an Informational Exchange announcing a deletion.
 *
 * CURRENTLY SUPPRESSED:
 * If we fail to send the deletion, we just go ahead with deleting the state.
 * The code in delete_state would break if we actually did this.
 *
 * Deleting an IKE SA is a bigger deal than deleting an IPsec SA.
 */

bool record_v2_delete(struct ike_sa *ike, struct state *st)
{
	/* make sure HDR is at start of a clean buffer */
	uint8_t buf[MIN_OUTPUT_UDP_SIZE];
	struct pbs_out packet = open_pbs_out("informational exchange delete request",
					     buf, sizeof(buf), st->st_logger);
	struct pbs_out rbody = open_v2_message(&packet, ike,
					       NULL /* request */,
					       ISAKMP_v2_INFORMATIONAL);
	if (!pbs_ok(&packet)) {
		return false;
	}

	struct v2SK_payload sk = open_v2SK_payload(st->st_logger, &rbody, ike);
	if (!pbs_ok(&sk.pbs)) {
		return false;
	}

	{
		pb_stream del_pbs;
		struct ikev2_delete v2del_tmp;
		if (IS_CHILD_SA(st)) {
			v2del_tmp = (struct ikev2_delete) {
				.isad_protoid = PROTO_IPSEC_ESP,
				.isad_spisize = sizeof(ipsec_spi_t),
				.isad_nrspi = 1,
			};
		} else {
			v2del_tmp = (struct ikev2_delete) {
				.isad_protoid = PROTO_ISAKMP,
				.isad_spisize = 0,
				.isad_nrspi = 0,
			};
		}

		/* Emit delete payload header out */
		if (!out_struct(&v2del_tmp, &ikev2_delete_desc,
				&sk.pbs, &del_pbs))
			return false;

		/* Emit values of spi to be sent to the peer */
		if (IS_CHILD_SA(st)) {
			diag_t d = pbs_out_raw(&del_pbs, &st->st_esp.our_spi,
					       sizeof(ipsec_spi_t), "local spis");
			if (d != NULL) {
				llog_diag(RC_LOG_SERIOUS, st->st_logger, &d, "%s", "");
				return false;
			}
		}

		close_output_pbs(&del_pbs);
	}

	if (!close_v2SK_payload(&sk)) {
		return false;;
	}
	close_output_pbs(&rbody);
	close_output_pbs(&packet);

	stf_status ret = encrypt_v2SK_payload(&sk);
	if (ret != STF_OK) {
		log_state(RC_LOG, st,"error encrypting notify message");
		return false;
	}

	record_v2_message(ike, &packet, "packet for ikev2 delete informational",
			  MESSAGE_REQUEST);
	return true;
}

static stf_status send_v2_delete_ike_request(struct ike_sa *ike,
					     struct child_sa *unused_child UNUSED,
					     struct msg_digest *md)
{
	pexpect(md == NULL);
	if (!record_v2_delete(ike, &ike->sa)) {
		return STF_INTERNAL_ERROR;
	}
	return STF_OK;
}

static stf_status send_v2_delete_child_request(struct ike_sa *ike,
					       struct child_sa *child,
					       struct msg_digest *md)
{
	pexpect(md == NULL);
	if (!record_v2_delete(ike, &child->sa)) {
		return STF_INTERNAL_ERROR;
	}
	return STF_OK;
}

/*
 * XXX: where to put this?
 */

static const struct state_v2_microcode v2_delete_ike = {
	.story = "delete IKE SA",
	.state = STATE_V2_ESTABLISHED_IKE_SA,
	.next_state = STATE_IKESA_DEL,
	.send = MESSAGE_REQUEST,
	.processor = send_v2_delete_ike_request,
	.timeout_event =  EVENT_RETAIN,
};

static const struct state_v2_microcode v2_delete_child = {
	.story = "delete CHILD SA",
	.state = STATE_V2_ESTABLISHED_CHILD_SA,
	.next_state = STATE_CHILDSA_DEL,
	.send = MESSAGE_REQUEST,
	.processor = send_v2_delete_child_request,
	.timeout_event =  EVENT_RETAIN,
};

static const struct state_v2_microcode *transitions[SA_TYPE_ROOF] = {
	[IKE_SA] = &v2_delete_ike,
	[IPSEC_SA] = &v2_delete_child,
};

void initiate_v2_delete(struct ike_sa *ike, struct state *st)
{
	const struct state_v2_microcode *transition = transitions[st->st_establishing_sa];
	if (st->st_state->kind != transition->state) {
		log_state(RC_LOG, st, "in state %s but need state %s to initiate delete",
			  st->st_state->short_name,
			  finite_states[transition->state]->short_name);
		return;
	}
	v2_msgid_queue_initiator(ike, st, ISAKMP_v2_INFORMATIONAL,
				 transition, NULL);
}

static void delete_or_replace_child(struct ike_sa *ike, struct child_sa *child)
{
	/* the CHILD's connection; not IKE's */
	struct connection *c = child->sa.st_connection;

	if (child->sa.st_event == NULL) {
		/*
		 * ??? should this be an assert/expect?
		 */
		log_state(RC_LOG_SERIOUS, &ike->sa,
			  "received Delete SA payload: delete CHILD SA #%lu. st_event == NULL",
			  child->sa.st_serialno);
		delete_state(&child->sa);
	} else if (child->sa.st_event->ev_type == EVENT_SA_EXPIRE) {
		/*
		 * this state was going to EXPIRE: hurry it along
		 *
		 * ??? why is this treated specially?  Can we not
		 * delete_state()?
		 */
		log_state(RC_LOG_SERIOUS, &ike->sa,
			  "received Delete SA payload: expire CHILD SA #%lu now",
			  child->sa.st_serialno);
		event_force(EVENT_SA_EXPIRE, &child->sa);
	} else if (c->newest_ipsec_sa == child->sa.st_serialno &&
		   (c->policy & POLICY_UP)) {
		/*
		 * CHILD SA for a permanent connection that we have
		 * initiated.  Replace it now.  Useful if the other
		 * peer is rebooting.
		 */
		log_state(RC_LOG_SERIOUS, &ike->sa,
			  "received Delete SA payload: replace CHILD SA #%lu now",
			  child->sa.st_serialno);
		child->sa.st_replace_margin = deltatime(0);
		event_force(EVENT_SA_REPLACE, &child->sa);
	} else {
		log_state(RC_LOG_SERIOUS, &ike->sa,
			  "received Delete SA payload: delete CHILD SA #%lu now",
			  child->sa.st_serialno);
		delete_state(&child->sa);
	}
}

bool process_v2D_requests(bool *del_ike, struct ike_sa *ike, struct msg_digest *md,
			  struct v2SK_payload *sk)
{
	/*
	 * Pass 1 over Delete Payloads:
	 *
	 * - Count number of IPsec SA Delete Payloads
	 * - notice any IKE SA Delete Payload
	 * - sanity checking
	 */
	unsigned ndp = 0;		/* nr child deletes */

	for (struct payload_digest *p = md->chain[ISAKMP_NEXT_v2D];
	     p != NULL; p = p->next) {
		struct ikev2_delete *v2del = &p->payload.v2delete;

		switch (v2del->isad_protoid) {
		case PROTO_ISAKMP:
			if (*del_ike) {
				log_state(RC_LOG, &ike->sa,
					  "Error: INFORMATIONAL Exchange with more than one Delete Payload for the IKE SA");
				return false;
			}

			if (v2del->isad_nrspi != 0 || v2del->isad_spisize != 0) {
				log_state(RC_LOG, &ike->sa,
					  "IKE SA Delete has non-zero SPI size or number of SPIs");
				return false;
			}

			*del_ike = true;
			break;

		case PROTO_IPSEC_AH:
		case PROTO_IPSEC_ESP:
			if (v2del->isad_spisize != sizeof(ipsec_spi_t)) {
				log_state(RC_LOG, &ike->sa,
					  "IPsec Delete Notification has invalid SPI size %u",
					  v2del->isad_spisize);
				return false;
			}

			if (v2del->isad_nrspi * v2del->isad_spisize != pbs_left(&p->pbs)) {
				log_state(RC_LOG, &ike->sa,
					  "IPsec Delete Notification payload size is %zu but %u is required",
					  pbs_left(&p->pbs),
					  v2del->isad_nrspi * v2del->isad_spisize);
				return false;
			}

			ndp++;
			break;

		default:
			log_state(RC_LOG, &ike->sa,
				  "Ignored bogus delete protoid '%d'", v2del->isad_protoid);
		}
	}

	if (*del_ike && ndp != 0) {
		log_state(RC_LOG, &ike->sa,
			  "Odd: INFORMATIONAL Exchange deletes IKE SA and yet also deletes some IPsec SA");
	}

	/*
	 * IKE delete gets an empty response.
	 */
	if (*del_ike) {
		return true;
	}

	/*
	 * Pass 2 over the Delete Payloads:
	 * Actual IPsec SA deletion.
	 * If responding, build response Delete Payloads.
	 * If there is no payload, this loop is a no-op.
	 */

	for (struct payload_digest *p = md->chain[ISAKMP_NEXT_v2D];
	     p != NULL; p = p->next) {
		struct ikev2_delete *v2del = &p->payload.v2delete;

		switch (v2del->isad_protoid) {
		case PROTO_ISAKMP:
			passert_fail(ike->sa.st_logger, HERE, "unexpected IKE delete");

		case PROTO_IPSEC_AH: /* Child SAs */
		case PROTO_IPSEC_ESP: /* Child SAs */
		{
			/* stuff for responding */
			ipsec_spi_t spi_buf[128];
			uint16_t j = 0;	/* number of SPIs in spi_buf */
			uint16_t i;

			for (i = 0; i < v2del->isad_nrspi; i++) {
				ipsec_spi_t spi;

				diag_t d = pbs_in_raw( &p->pbs, &spi, sizeof(spi),"SPI");
				if (d != NULL) {
					llog_diag(RC_LOG, ike->sa.st_logger, &d, "%s", "");
					return false;
				}

				esb_buf b;
				dbg("delete %s SA(0x%08" PRIx32 ")",
				    enum_show(&ikev2_delete_protocol_id_names,
					      v2del->isad_protoid, &b),
				    ntohl((uint32_t) spi));

				/*
				 * From 3.11.  Delete Payload: [the
				 * delete payload will] contain the
				 * IPsec protocol ID of that protocol
				 * (2 for AH, 3 for ESP), and the SPI
				 * is the SPI the sending endpoint
				 * would expect in inbound ESP or AH
				 * packets.
				 *
				 * From our POV, that's the outbound
				 * SPI.
				 */
				struct child_sa *child = find_v2_child_sa_by_outbound_spi(ike,
											  v2del->isad_protoid,
											  spi);

				if (child == NULL) {
					esb_buf b;
					log_state(RC_LOG, &ike->sa,
						  "received delete request for %s SA(0x%08" PRIx32 ") but corresponding state not found",
						  enum_show(&ikev2_delete_protocol_id_names,
							    v2del->isad_protoid, &b),
						  ntohl((uint32_t)spi));
				} else {
					esb_buf b;
					dbg("our side SPI that needs to be deleted: %s SA(0x%08" PRIx32 ")",
					    enum_show(&ikev2_delete_protocol_id_names,
						      v2del->isad_protoid, &b),
					    ntohl((uint32_t)spi));

					/* we just received a delete, don't send another delete */
					child->sa.st_dont_send_delete = true;
					passert(ike->sa.st_serialno == child->sa.st_clonedfrom);
					struct ipsec_proto_info *pr =
						(v2del->isad_protoid == PROTO_IPSEC_AH
						 ? &child->sa.st_ah
						 : &child->sa.st_esp);
					if (j < elemsof(spi_buf)) {
						spi_buf[j] = pr->our_spi;
						j++;
					} else {
						log_state(RC_LOG, &ike->sa,
							  "too many SPIs in Delete Notification payload; ignoring 0x%08" PRIx32,
							  ntohl(spi));
					}
					delete_or_replace_child(ike, child);
				}
			} /* for each spi */

			/* build output Delete Payload */
			struct ikev2_delete v2del_tmp = {
				.isad_protoid = v2del->isad_protoid,
				.isad_spisize = v2del->isad_spisize,
				.isad_nrspi = j,
			};

			/* Emit delete payload header and SPI values */
			struct pbs_out del_pbs;	/* output stream */
			if (!out_struct(&v2del_tmp,
					&ikev2_delete_desc,
					&sk->pbs,
					&del_pbs))
				return false;
			diag_t d = pbs_out_raw(&del_pbs,
					       spi_buf,
					       j * sizeof(spi_buf[0]),
					       "local SPIs");
			if (d != NULL) {
				llog_diag(RC_LOG_SERIOUS, sk->logger, &d, "%s", "");
				return false;
			}

			close_output_pbs(&del_pbs);
			break;
		}

		default:
			/* ignore unrecognized protocol */
			break;
		}
	}

	return true;
}

bool process_v2D_responses(struct ike_sa *ike, struct msg_digest *md)
{
	for (struct payload_digest *p = md->chain[ISAKMP_NEXT_v2D];
	     p != NULL; p = p->next) {
		struct ikev2_delete *v2del = &p->payload.v2delete;

		switch (v2del->isad_protoid) {
		case PROTO_ISAKMP:
			passert_fail(ike->sa.st_logger, HERE, "unexpected IKE delete");

		case PROTO_IPSEC_AH: /* Child SAs */
		case PROTO_IPSEC_ESP: /* Child SAs */
		{
			uint16_t i;

			if (v2del->isad_spisize != sizeof(ipsec_spi_t)) {
				log_state(RC_LOG, &ike->sa,
					  "IPsec Delete Notification has invalid SPI size %u",
					  v2del->isad_spisize);
				return false;
			}

			if (v2del->isad_nrspi * v2del->isad_spisize != pbs_left(&p->pbs)) {
				log_state(RC_LOG, &ike->sa,
					  "IPsec Delete Notification payload size is %zu but %u is required",
					  pbs_left(&p->pbs),
					  v2del->isad_nrspi * v2del->isad_spisize);
				return false;
			}

			for (i = 0; i < v2del->isad_nrspi; i++) {
				ipsec_spi_t spi;

				diag_t d = pbs_in_raw( &p->pbs, &spi, sizeof(spi),"SPI");
				if (d != NULL) {
					llog_diag(RC_LOG, ike->sa.st_logger, &d, "%s", "");
					return false;
				}

				esb_buf b;
				dbg("delete %s SA(0x%08" PRIx32 ")",
				    enum_show(&ikev2_delete_protocol_id_names,
					      v2del->isad_protoid, &b),
				    ntohl((uint32_t) spi));

				/*
				 * From 3.11.  Delete Payload: [the
				 * delete payload will] contain the
				 * IPsec protocol ID of that protocol
				 * (2 for AH, 3 for ESP), and the SPI
				 * is the SPI the sending endpoint
				 * would expect in inbound ESP or AH
				 * packets.
				 *
				 * From our POV, that's the outbound
				 * SPI.
				 */
				struct child_sa *dst = find_v2_child_sa_by_outbound_spi(ike,
											v2del->isad_protoid,
											spi);

				if (dst == NULL) {
					esb_buf b;
					log_state(RC_LOG, &ike->sa,
						  "received delete request for %s SA(0x%08" PRIx32 ") but corresponding state not found",
						  enum_show(&ikev2_delete_protocol_id_names,
							    v2del->isad_protoid, &b),
						  ntohl((uint32_t)spi));
				} else {
					esb_buf b;
					dbg("our side SPI that needs to be deleted: %s SA(0x%08" PRIx32 ")",
					    enum_show(&ikev2_delete_protocol_id_names,
						      v2del->isad_protoid, &b),
					    ntohl((uint32_t)spi));

					/* we just received a delete, don't send another delete */
					dst->sa.st_dont_send_delete = true;
					/* st is a parent */
					passert(&ike->sa != &dst->sa);
					passert(ike->sa.st_serialno == dst->sa.st_clonedfrom);
					delete_or_replace_child(ike, dst);
				}
			} /* for each spi */
			break;
		}

		default:
			/* ignore unrecognized protocol */
			break;
		}
	}  /* for each Delete Payload */
	return true;
}
