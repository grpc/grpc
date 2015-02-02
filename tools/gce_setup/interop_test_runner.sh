#!/bin/bash

main() {
  source grpc_docker.sh
  test_cases=(large_unary empty_unary ping_pong client_streaming server_streaming)
  clients=(cxx java go ruby node)
  servers=(cxx java go ruby node)
  for test_case in "${test_cases[@]}"
  do
    for client in "${clients[@]}"
    do
      for server in "${servers[@]}"
      do
        if grpc_interop_test $test_case grpc-docker-testclients $client grpc-docker-server $server
        then
          echo "$test_case $client $server passed" >> /tmp/interop_result.txt
        else
          echo "$test_case $client $server failed" >> /tmp/interop_result.txt
        fi
      done
    done
  done
  gsutil cp /tmp/interop_result.txt gs://stoked-keyword-656-output/interop_result.txt
  rm /tmp/interop_result.txt
}

set -x
main "$@"
