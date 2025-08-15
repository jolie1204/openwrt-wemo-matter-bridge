#!/bin/sh

while true; do
    if [ "`nvram get nWay`" == "3" ] ; then
        /sbin/api_test meter_info_gen &
        echo -n "Waiting for /tmp/meter_info_load to be available" > /dev/console
        while [ ! -f /tmp/meter_info_load ]; do
            echo -n "." > /dev/console
            sleep 1
        done
        echo " " > /dev/console

        dummy_mode=$(nvram get dummy_mode)
        if [ "${dummy_mode}x"  == "1x" ]; then 
            sh /sbin/go_dummy.sh &
            /etc/init.d/wemo stop
            exit
        fi
    fi

    /sbin/wemoApp -webdir /tmp/Belkin_settings/ &>/dev/console
    if [ -e /tmp/rebooting ]; then
	exit
    fi
    killall wemoApp
    killall api_test
    nvram commit
    killall nvramd
    if [ "$(nvram get SAVE_MULTIPLE_LOGS)" = "{ NULL String }" -o "$(nvram get SAVE_MULTIPLE_LOGS)" = "" ]; then
	    rm -f /tmp/messages-*
    fi
    cp /var/log/messages /tmp/messages-$(date +%Y-%m-%d-%H:%M)
    sleep 2
done
