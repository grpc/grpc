#!/bin/sh

# performs a single qps run with one client and one server

set -ex

cd $(dirname $0)/../../..

killall qps_worker || true

config=opt

NUMCPUS=`python2.7 -c 'import multiprocessing; print multiprocessing.cpu_count()'`

make CONFIG=$config qps_worker qps_driver -j$NUMCPUS

bins/$config/qps_worker -driver_port 10000 -server_port 10001 &
PID1=$!
bins/$config/qps_worker -driver_port 10010 -server_port 10011 &
PID2=$!

export QPS_WORKERS="localhost:10000,localhost:10010"

bins/$config/qps_driver $*

kill -2 $PID1 $PID2
wait

