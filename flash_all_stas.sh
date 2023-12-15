#!/bin/bash

# Flash all STAs connected via USB

echo "start flashing all STA nodes ..."

# fetch all stas
./search_stas.sh

while read dev;
do
	if [[ $dev ]]
	then
		make sta_flash PORT=/dev/ttyACM$dev
	fi
done <stas_tty.dat

echo "... done"
