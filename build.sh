#!/bin/bash

WORKSPACE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

# Clean/compile/link:
cd $WORKSPACE_DIR
rm -f hid.o glowusb_hid.o glowusb_x64 pugixml.o
cc -Wall -g -fpic -c -I$WORKSPACE_DIR `pkg-config libusb-1.0 --cflags` hid.c -o hid.o
g++ -Wall -g -fpic -c -I$WORKSPACE_DIR pugixml.cpp -o pugixml.o
g++ -Wall -g -fpic -c -I$WORKSPACE_DIR `pkg-config libusb-1.0 --cflags` glowusb_hid.cpp -o glowusb_hid.o
g++ -Wall -g hid.o pugixml.o glowusb_hid.o `pkg-config libusb-1.0 --libs` -lrt -lpthread -o ./output/glowusb_x64
