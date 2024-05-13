#!/bin/bash

# Flash all STAs connected via USB

echo "start flashing all STA nodes ... ${pwd}"

# fetch all stas
./search_stas.sh

while read dev;
do
	if [[ $dev ]]
	then
		PORT="/dev/ttyACM${dev}"
		echo "make sta_flash $PORT"
		make sta_flash PORT=${PORT}
	fi
done <stas_tty.dat

echo "... done"
