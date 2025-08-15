#!/bin/sh

red=17
green=16
relay=19
button=13
ntc=18

value=-1

gpio_export() {
    if [ ! -d /sys/class/gpio/gpio$1 ]; then
        echo $1 > /sys/class/gpio/export
    fi
}

gpio_unexport() {
    if [ -d /sys/class/gpio/gpio$1 ]; then
        echo $1 > /sys/class/gpio/unexport
    fi
}

gpio_direction() {
    if [ ! -d /sys/class/gpio/gpio$1 ];then
        gpio_export $1
    fi
    echo $2 > /sys/class/gpio/gpio$1/direction
}

gpio_read() {
    if [ ! -d /sys/class/gpio/gpio$1 ]; then
        gpio_export $1
    fi
    value=`cat /sys/class/gpio/gpio$1/value`
}

gpio_write() {
    if [ ! -d /sys/class/gpio/gpio$1 ]; then
        gpio_export $1
    fi
    echo $2 > /sys/class/gpio/gpio$1/value
}

case "$1" in
    out)
        if [ $# -ne 2 ]; then
            echo "$0 out 0/1"
            exit
        fi
        if [ $2 -eq 1 ] || [ $2 -eq 0 ]; then
            gpio_export $red
            gpio_direction $red out
            gpio_export $green
            gpio_direction $green out
            gpio_export $relay
            gpio_direction $relay out

            gpio_write $red $2
            gpio_write $green $2
            gpio_write $relay $2
        else
            echo "$0 out 0/1"
        fi
        ;;
    in)
        gpio_export $button
        gpio_export $ntc
        gpio_read $button
        button_value=$value
        gpio_read $ntc
        ntc_value=$value
    
        echo "button value is $button_value"
        echo "NTC value is $ntc_value"
        ;;
    *)
        echo "$0 in/out"
        ;;
esac
