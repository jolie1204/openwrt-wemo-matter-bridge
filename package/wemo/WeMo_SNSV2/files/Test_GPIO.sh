#!/bin/sh

red=17
green=16
relay=19
button=23
ntc=18

value=-1
saved_value=-1

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

pin=0

case "$1" in
    out)
        for pin in $red $green $relay $button $ntc;
        do
            gpio_export $pin
            gpio_direction $pin $1
            gpio_write $pin $2

            pin=$(($pin + 1))
        done
            ;;
    in)
        for pin in $red $green $relay $button $ntc;
        do
            gpio_direction $pin $1
            gpio_read $pin
            if [ $saved_value -eq -1 ]; then
                saved_value=$value
            elif [ $saved_value -ne $value ]; then
                echo gpio$pin FAIL
                exit
            fi
            pin=$(($pin + 1))
        done
        echo ALL Value is $value and PASS
        ;;
    *)
        echo "$0 out 0/1 or $0 in"
        ;;
esac
