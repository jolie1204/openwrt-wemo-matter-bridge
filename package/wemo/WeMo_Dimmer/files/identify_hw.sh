#!/bin/sh

rm -f /tmp/api_*.log
waspd -gc129
animation_old=$(cat /tmp/api_* | grep Val: | head -n1 | awk '{print $3}')
if [ $animation_old -eq 5 ] || [ $animation_old -eq 6 ] || [ $animation_old -eq 7]; then
    animation_old=0
fi

cleanup() {
    rm -f /tmp/api_*.log
    waspd -s129:12:$animation_old
    rm -f /tmp/api_*.log
    exit
}

waspd -s129:12:0
waspd -s130:12:6;waspd -s131:12:255;waspd -s130:12:10;waspd -s131:12:255;waspd -s130:12;14;waspd -s131:12:255
sleep 0.45
waspd -s130:12:6;waspd -s131:12:0;waspd -s130:12:10;waspd -s131:12:0;waspd -s130:12;14;waspd -s131:12:0
sleep 0.2
waspd -s130:12:6;waspd -s131:12:255;waspd -s130:12:10;waspd -s131:12:255;waspd -s130:12;14;waspd -s131:12:255
sleep 0.45
waspd -s130:12:6;waspd -s131:12:0;waspd -s130:12:10;waspd -s131:12:0;waspd -s130:12;14;waspd -s131:12:0
sleep 0.2
waspd -s130:12:6;waspd -s131:12:255;waspd -s130:12:10;waspd -s131:12:255;waspd -s130:12;14;waspd -s131:12:255
sleep 0.45
waspd -s130:12:6;waspd -s131:12:0;waspd -s130:12:10;waspd -s131:12:0;waspd -s130:12;14;waspd -s131:12:0
sleep 0.2
waspd -s130:12:6;waspd -s131:12:255;waspd -s130:12:10;waspd -s131:12:255;waspd -s130:12;14;waspd -s131:12:255
sleep 0.45
waspd -s130:12:6;waspd -s131:12:0;waspd -s130:12:10;waspd -s131:12:0;waspd -s130:12;14;waspd -s131:12:0

cleanup
