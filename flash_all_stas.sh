#!/bin/bash

# Flash the following STAs connected via USB

echo "start flashing all STA nodes ..."

devs='0 2 4 6 8 10 12 14'
for dev in $devs
do
	make sta_flash PORT=/dev/ttyACM$dev
done

echo "... done"
