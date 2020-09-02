#!/bin/bash

source ./config.sh
mountpoint -q ${MNT_DIR} && ./umount.sh

## single thread
./diy-sdfs -s -o auto_unmount,allow_other,logfile=${LOG_FILE},configfile=${CONFIG_FILE} ${MNT_DIR}
####./gdtnfs -s -d -o auto_unmount,allow_other,logfile=${LOG_FILE},configfile=${CONFIG_FILE} ${MNT_DIR}
#./gdtnfs -s -f -o auto_unmount,allow_other,logfile=${LOG_FILE},configfile=${CONFIG_FILE} ${MNT_DIR}

## multi thread
#./gdtnfs -o auto_unmount,allow_other,logfile=${LOG_FILE},configfile=${CONFIG_FILE} ${MNT_DIR}

## single thread, print_info
#./gdtnfs -s -f -o auto_unmount,print_info,allow_other,logfile=${LOG_FILE},configfile=${CONFIG_FILE} ${MNT_DIR}
