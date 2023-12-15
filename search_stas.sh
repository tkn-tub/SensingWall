#!/bin/bash

sta_mac_addr=("48:27:e2:3b:32:3c" "34:85:18:b9:1a:94" "34:85:18:b9:1b:8c" "34:85:18:b9:1b:dc" "48:27:e2:3b:32:b4" "48:27:e2:3b:31:64" "34:85:18:b9:1a:64" "48:27:e2:3b:33:74" "34:85:18:b9:1c:10")

echo "" > stas_tty.dat

for dev in /dev/ttyACM*; do
	#echo "checking $dev"
	dev_mac_addr=$(udevadm info -a -p  $(udevadm info -q path -n $dev) | grep ATTRS{serial} | head -n1 | awk -F "==" '{ print $2}' | sed -s "s/\"//g")
	#echo "$dev -> $dev_mac_addr"

	if [[ ${sta_mac_addr[@]} =~ ${dev_mac_addr,,} ]]
	then
  		echo "STA found: $dev -> $dev_mac_addr"
		echo "${dev/\/dev\/ttyACM/}" >> stas_tty.dat
	fi
done
