#!/bin/bash

ap_mac_addr="48:27:e2:3b:33:2d"

# store the TTY ID in file
echo "" > ap_tty.dat

for dev in /dev/ttyACM*; do
	dev_mac_addr=$(udevadm info -a -p  $(udevadm info -q path -n $dev) | grep ATTRS{serial} | head -n1 | awk -F "==" '{ print $2}' | sed -s "s/\"//g")

	if [[ ${ap_mac_addr} =~ ${dev_mac_addr,,} ]]
	then
  		echo "AP found: $dev -> $dev_mac_addr"
  		echo "${dev/\/dev\/ttyACM/}" >> ap_tty.dat
	fi
done
