#!/bin/sh

set_led_state() {
    power_on=$(cat /tmp/notify_change)
    if [ "${power_on}x" == "1x" ]; then
        echo 5 > /sys/class/pwm/pwmchip0/pwm1/period
    else
        echo 6 > /sys/class/pwm/pwmchip0/pwm1/period
    fi
}

monitor_change() {
    while inotifywait -e modify /tmp/notify_change; do
        set_led_state
    done
}

# set initial LED state
set_led_state

# if any reason it fails from above loop
while true; do
    monitor_change
done
