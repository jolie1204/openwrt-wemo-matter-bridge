#!/bin/sh

# Check if another instance of this shell is running.
for pid in $(pidof identify_hw.sh); do
    if [ $pid != $$ ]; then
        echo "identify_hw.sh Process is already running"
        exit 1
    fi
done

identify=3

period=/sys/class/pwm/pwmchip0/pwm1/period
period_saved=$(cat $period)

cleanup() {
    echo $period_saved > $period
    exit
}

echo $identify > $period

sleep 4.2

cleanup
