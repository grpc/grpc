#!/bin/sh

if [ x"$QPS_WORKERS" == x ]; then
  echo Error: Must set QPS_WORKERS variable in form \
    "host:port,host:port,..." 1>&2
  exit 1
fi

bins=`find . .. ../.. ../../.. -name bins | head -1`

for channels in 1 2 4 8
do
  for client in SYNCHRONOUS_CLIENT ASYNC_CLIENT
  do
    for server in SYNCHRONOUS_SERVER ASYNC_SERVER
    do
      for rpc in UNARY STREAMING
      do
        echo "Test $rpc $client $server , $channels channels"
        "$bins"/opt/qps_driver --rpc_type=$rpc \
          --client_type=$client --server_type=$server
      done
    done
  done
done
