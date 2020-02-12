#!/bin/bash

if [[ "$WORKER_KIND" == "server" ]];
then
  benchmarks/build/install/grpc-benchmarks/bin/benchmark_worker --driver_port=$DRIVER_PORT --server_port=$SERVER_PORT
else
  benchmarks/build/install/grpc-benchmarks/bin/benchmark_worker --driver_port=$DRIVER_PORT
fi

