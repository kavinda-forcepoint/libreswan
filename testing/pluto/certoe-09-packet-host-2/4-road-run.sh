# one packet, which gets eaten by XFRM, so east does not initiate
../../guestbin/ping-once.sh --forget -I 192.1.3.209 192.1.2.23
# wait on OE to establish; should show established tunnel and no bare shunts
../../guestbin/wait-for.sh --match private-or-clear -- ipsec whack --trafficstatus
../../guestbin/ping-once.sh --up -I 192.1.3.209 192.1.2.23
ipsec whack --trafficstatus
echo done
