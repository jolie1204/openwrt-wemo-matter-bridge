#!/bin/sh

unload_net_modules() {
    ifconfig apcli0 down
    ifconfig ra0 down

    rmmod mt7628
    rmmod myadd
}

bootstate=$(fw_printenv | grep bootstate)
if [ "${bootstate}" == "bootstate=1" ]; then
    fw_setenv bootstate 2
fi

if [ "${bootstate}" == "bootstate=3" ]; then
    fw_setenv bootstate 0
fi

rm -f /tmp/*.starting

sleep 5

/etc/init.d/wemohk stop
/etc/init.d/wemo stop

unload_net_modules

/sbin/manual_load_monitor.sh &
