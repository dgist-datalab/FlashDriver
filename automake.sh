#!/bin/bash

echo "##### automake.sh start! #####"

echo "### [1st] make start! ###"
make clean
make -j SH_LFILE=WEBPROXY_LOAD_16 SH_RFILE=WEBPROXY_RUN_16 SH_LCYCLE=1 SH_RCYCLE=1 SH_FTLTYPE=seq_hashftl
./driver
echo "### [1st] ./driver end! ###"
sleep 5

echo "### [2nd] make start! ###"
make clean
make -j SH_LFILE=WEBPROXY_LOAD_16 SH_RFILE=WEBPROXY_RUN_16 SH_LCYCLE=1 SH_RCYCLE=1 SH_FTLTYPE=seq_hashftl
./driver
echo "### [2nd] ./driver end! ###"
