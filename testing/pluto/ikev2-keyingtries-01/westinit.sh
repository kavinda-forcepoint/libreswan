/testing/guestbin/swan-prep
ipsec start
../../guestbin/wait-until-pluto-started
ipsec auto --add westnet-eastnet
ipsec auto --add westnet-eastnet-k1
ipsec auto --add westnet-eastnet-k3
echo "initdone"
