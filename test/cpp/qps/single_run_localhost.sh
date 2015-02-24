#!/bin/sh

# performs a single qps run with one client and one server

set -ex

cd $(dirname $0)/../../..

NUMCPUS=`python2.7 -c 'import multiprocessing; print multiprocessing.cpu_count()'`

make CONFIG=opt qps_client qps_server qps_driver -j$NUMCPUS

bins/opt/qps_server -driver_port 10000 -port 10002 &
SERVER_PID=$!
bins/opt/qps_client -driver_port 10001 &
CLIENT_PID=$!

# wait for startup
sleep 2

QPS_SERVERS=localhost:10000
QPS_CLIENTS=localhost:10001

bins/opt/qps_driver $*

kill -2 $CLIENT_PID
kill -2 $SERVER_PID
wait

