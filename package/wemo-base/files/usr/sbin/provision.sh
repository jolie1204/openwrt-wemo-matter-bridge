#!/bin/sh
SSID="$1"; PSK="$2"; ENC="${3:-psk2}"

uci set wireless.sta_radio0.ssid="$SSID"
uci set wireless.sta_radio0.encryption="$ENC"
[ -n "$PSK" ] && uci set wireless.sta_radio0.key="$PSK"
uci set wireless.sta_radio0.disabled='0'
uci commit wireless

wifi reload

# Wait up to 60s for WWAN link
for i in $(seq 1 30); do
  ubus call network.interface.wwan status 2>/dev/null | grep -q '"up": true' && OK=1 && break
  sleep 2
done

# If connected, disable setup AP
if [ "$OK" = "1" ]; then
  uci set wireless.ap_setup.disabled='1'
  uci commit wireless
  wifi reload
  exit 0
fi
exit 1
