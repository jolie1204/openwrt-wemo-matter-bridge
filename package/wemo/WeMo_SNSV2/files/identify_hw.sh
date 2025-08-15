#!/bin/sh

# Check if another instance of this shell is running.
for pid in $(pidof identify_hw.sh); do
    if [ $pid != $$ ]; then
        echo "identify_hw.sh Process is already running"
        exit 1
    fi
done

amber=/sys/class/leds/amber/trigger
white=/sys/class/leds/white/trigger
white_delay_on=/sys/class/leds/white/delay_on
white_delay_off=/sys/class/leds/white/delay_off
amber_delay_on=/sys/class/leds/amber/delay_on
amber_delay_off=/sys/class/leds/amber/delay_off

amber_saved=$(cat $amber | cut -d'[' -f2 | cut -d']' -f1)
white_saved=$(cat $white | cut -d'[' -f2 | cut -d']' -f1)

if [ $amber_saved == "timer" ]; then
    amber_delay_on_saved=$(cat $amber_delay_on)
    amber_delay_off_saved=$(cat $amber_delay_off)
fi

if [ $white_saved == "timer" ]; then
    white_delay_on_saved=$(cat $white_delay_on)
    white_delay_off_saved=$(cat $white_delay_off)
fi

cleanup() {
    echo $amber_saved > $amber

    if [ $amber_saved == "timer" ]; then
        echo $amber_delay_on_saved > $amber_delay_on
        echo $amber_delay_off_saved > $amber_delay_off
    fi
    if [ $white_saved == "timer" ]; then
        sleep 0.69
    	echo $white_saved > $white
        echo $white_delay_on_saved > $white_delay_on
        echo $white_delay_off_saved > $white_delay_off
    else
    	echo $white_saved > $white
    fi
    exit
}

echo none > $amber
echo none > $white
echo timer > $white

echo 260 > $white_delay_off
echo 400 > $white_delay_on

sleep 3.8

cleanup
