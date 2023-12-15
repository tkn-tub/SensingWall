#!/bin/bash

ap_mac_addr="48:27:e2:3b:33:2d"

for dev in /dev/ttyACM*; do
	#echo "checking $dev"
	dev_mac_addr=$(udevadm info -a -p  $(udevadm info -q path -n $dev) | grep ATTRS{serial} | head -n1 | awk -F "==" '{ print $2}' | sed -s "s/\"//g")
	#echo "$dev -> $dev_mac_addr"

	if [[ ${ap_mac_addr} =~ ${dev_mac_addr,,} ]]
	then
  		echo "AP found: $dev -> $dev_mac_addr"
	fi
done
