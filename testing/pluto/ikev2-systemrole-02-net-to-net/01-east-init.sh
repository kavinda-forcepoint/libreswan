/testing/guestbin/swan-prep
# System Role deployment on nic will push configurations to our machine
# into /etc/ipsec.d/
rm -rf OUTPUT/east/ipsec.d
mkdir -p OUTPUT/east/ipsec.d
chmod 777 OUTPUT/east
mount -o bind,rw OUTPUT/east/ipsec.d /etc/ipsec.d
# initnss normally happens in the initsystem - but not for namespace testing
../../guestbin/if-namespace.sh ipsec initnss
# test config for syntax errors
ipsec addconn --checkconfig --config /etc/ipsec.conf
# start for test
ipsec start
../../guestbin/wait-until-pluto-started
# test secrets reading for early warning of syntax errors
ipsec secrets
../../guestbin/if-namespace.sh /usr/sbin/sshd -o PidFile=/var/run/pluto/sshd.pid
# ready for System Role to drop file(s) into /etc/ipsec.d/
echo "initdone"
