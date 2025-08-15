#!/bin/sh

###################################################################
#load necessary driver and daemon in failsafe mode for factory test
###################################################################

if [ "$1" == "" ];then
  ssid="WeMo.Unknow"
else
  ssid=$1
fi

if [ "$2" == "" ];then
  channel_no=1
else
  channel_no=$2 
fi

if [ "$3" == "" ];then
  ip_addr=192.168.1.1
else
  ip_addr=$3 
fi

echo ssid=$ssid
echo channel_no=$channel_no
echo ip_addr=$ip_addr

if [ ! -f /etc/wireless/mt7628/mt7628.dat ]; then
export DAT_FILE=/tmp/mt7628.dat
echo "#The word of "Default" must not be removed
Default" > $DAT_FILE
echo "CountryRegion=0
CountryRegionABand=7
CountryCode=US
Channel=$channel_no" >> $DAT_FILE
echo "SSID1=$ssid" >> $DAT_FILE
cat /etc/wireless/mt7628/mt7628.dat.base >> $DAT_FILE
fi

insmod /lib/modules/3.18.27/myadd.ko

insmod /lib/modules/3.18.27/mt7628.ko

ifconfig lo up

ifconfig ra0 up $ip_addr

waspd -D /dev/ttyS1 &
