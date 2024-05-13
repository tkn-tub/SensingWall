#!/bin/bash

# Flash the AP connected via USB

echo "start flashing AP node ..."

# fetch AP
./search_ap.sh

while read dev;
do
	if [[ $dev ]]
	then
		make sta_flash PORT=/dev/ttyACM$dev
	fi
done <ap_tty.dat

echo "... done"
