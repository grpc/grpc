#!/bin/bash

if [[ "$WORKER_KIND" == "server" ]];
then
  bins/opt/qps_worker --driver_port=$DRIVER_PORT --server_port=$SERVER_PORT
else
  bins/opt/qps_worker --driver_port=$DRIVER_PORT
fi

