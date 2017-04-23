#!/bin/bash -e
SERIALFILE=test.microflo
OPTIONS="--port 3334 --baudrate 115200 --serial $SERIALFILE"

# Make sure we clean up
trap 'kill $(jobs -p)' EXIT

./build/linux/firmware $SERIALFILE &
./microflo.js runtime $OPTIONS --componentmap build/linux/componentlib-map.json | grep 'listening'
