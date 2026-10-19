#!/bin/sh
# GnuBox PC side, classic variant (sdptool + rfcomm + pppd), for systems where the
# BlueZ Profile API script (gnubox-host.py) can't be used.
# sdptool/rfcomm are deprecated in BlueZ 5: install the "bluez-deprecated" (Fedora,
# Arch: bluez-utils-compat) package if they are missing, and run bluetoothd with
# --compat (-C), e.g.  systemctl edit bluetooth  ->  ExecStart=/usr/lib/bluetooth/bluetoothd -C
#
# usage: sudo ./gnubox-host.sh [channel] [uplink-interface]
# GPL v2 or later
set -e
CH=${1:-1}
IF=${2:-$(ip -4 route show default | sed -n 's/.* dev \([^ ]*\).*/\1/p' | head -1)}
LOCAL=10.66.0.1
REMOTE=10.66.0.2
DNS=$(awk '/^nameserver/ && $2 !~ /^127\./ && $2 ~ /\./ {print $2; exit}' /etc/resolv.conf)
[ -n "$DNS" ] || DNS=$(resolvectl dns 2>/dev/null | grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+\.[0-9]\+' | head -1)
[ -n "$DNS" ] || DNS=1.1.1.1

[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }
for t in sdptool rfcomm pppd; do command -v $t >/dev/null || { echo "missing $t"; exit 1; }; done

echo "[gnubox] Serial Port service on RFCOMM channel $CH, NAT via $IF, DNS $DNS"
sdptool add --channel="$CH" SP
echo 1 > /proc/sys/net/ipv4/ip_forward
if command -v nft >/dev/null; then
  nft delete table ip gnubox 2>/dev/null || true
  nft -f - <<NFT
table ip gnubox {
  chain post { type nat hook postrouting priority 100; ip saddr $REMOTE oifname "$IF" masquerade; }
  chain fwd { type filter hook forward priority -10; ip saddr $REMOTE accept; ip daddr $REMOTE ct state established,related accept; }
}
NFT
  trap 'nft delete table ip gnubox 2>/dev/null' EXIT INT TERM
else
  iptables -t nat -A POSTROUTING -s $REMOTE -o "$IF" -j MASQUERADE
  iptables -I FORWARD -s $REMOTE -j ACCEPT
  iptables -I FORWARD -d $REMOTE -m state --state ESTABLISHED,RELATED -j ACCEPT
  trap 'iptables -t nat -D POSTROUTING -s $REMOTE -o "$IF" -j MASQUERADE; iptables -D FORWARD -s $REMOTE -j ACCEPT; iptables -D FORWARD -d $REMOTE -m state --state ESTABLISHED,RELATED -j ACCEPT' EXIT INT TERM
fi

echo "[gnubox] waiting for the phone (GnuBox -> Test); Ctrl+C to stop"
# rfcomm watch re-listens after every connection; {} is the /dev/rfcommN device
rfcomm watch rfcomm0 "$CH" /usr/sbin/pppd {} 115200 local noauth nodetach nocrtscts \
  nodefaultroute $LOCAL:$REMOTE ms-dns "$DNS" lcp-echo-interval 30 lcp-echo-failure 4
