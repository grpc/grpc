#!/bin/sh

# performs a single qps run with one client and one server

set -ex

cd $(dirname $0)/../../..

killall qps_server qps_client || true

config=opt

NUMCPUS=`python2.7 -c 'import multiprocessing; print multiprocessing.cpu_count()'`

make CONFIG=$config qps_client qps_server qps_driver -j$NUMCPUS

bins/$config/qps_server -driver_port 10000 -port 10002 &
SERVER_PID=$!
bins/$config/qps_client -driver_port 10001 &
CLIENT_PID=$!

export QPS_SERVERS=localhost:10000
export QPS_CLIENTS=localhost:10001

bins/$config/qps_driver $*

kill -2 $CLIENT_PID
kill -2 $SERVER_PID
wait

