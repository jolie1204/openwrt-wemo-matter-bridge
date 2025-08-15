#!/bin/sh
echo -n "Serial Number to use : "
read serial
echo -n "Router SSID to use : "
read ssid
echo -n "Router MAC address to use : "
read mac
echo "Please verify the inputs"
echo "\tSerial Number = $serial"
echo "\tRouter SSID = $ssid"
echo "\tRouter MAC address = $mac"
echo -n "Hit any key to commit, or ctrl-c to exit"
read answer
echo commiting

nvram_set RouterMac=$mac
nvram_set RouterSsid=$ssid
nvram_set SerialNumber=$serial
nvram_set cwf_serial_number=$serial
nvram_set ClientSSID=$ssid
