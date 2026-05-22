#!/bin/sh

[ -t 1 ] || return 0
[ -x /usr/sbin/wemo-matter-bridge ] || return 0

cat <<'EOF'

WeMo Matter Bridge
  Status: wemo-matter-bridge status
  Pairing: wemo-matter-bridge qr

Scan the QR code in Apple Home or Google Home to add the bridge.
EOF
