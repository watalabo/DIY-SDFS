#!/bin/bash

CMDNAME=`basename $0`
if [ $# -ne 1 ]; then
  echo "Usage: $CMDNAME file" 1>&2
  exit 1
fi

#gcc -Wall $1.c `pkg-config fuse3 --cflags --libs` -o $1
g++ -Wall -g -DHAVE_UTIMENSAT -pthread $1.cpp `pkg-config fuse3 --cflags --libs` -o $1
