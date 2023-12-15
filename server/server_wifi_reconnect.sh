#!/bin/bash

# Set your wireless interface, SSID, and MAC address
INTERFACE="wlp101s0"
TARGET_SSID="sensing-wall"
TARGET_MAC="48:27:E2:3B:33:2D"

# Function to check if connected to the target network
is_connected() {
    connected_ssid=$(iwgetid -r)
    connected_mac=$(iwgetid -a | awk '{print $4}')

    if [[ "$connected_ssid" == "$TARGET_SSID" ]] && [[ "$connected_mac" == "$TARGET_MAC" ]]; then
        return 0  # Connected to the target network
    else
        return 1  # Not connected to the target network
    fi
}

# Function to reconnect to the target network
reconnect() {
    echo "Reconnecting to $TARGET_SSID..."

    iwconfig $INTERFACE essid $TARGET_SSID ap $TARGET_MAC
}

# Main loop
while true; do
    if is_connected; then
        echo "Connected to $TARGET_SSID."
    else
        reconnect
    fi

    # Adjust the sleep duration according to your needs
    sleep 1 # Sleep for 1 second before checking again
done
