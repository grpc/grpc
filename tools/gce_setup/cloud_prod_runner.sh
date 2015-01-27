#!/bin/bash

main() {
  source grpc_docker.sh
  test_cases=(large_unary empty_unary client_streaming server_streaming)
  clients=(cxx java go ruby)
  for test_case in "${test_cases[@]}"
  do
    for client in "${clients[@]}"
    do
      if grpc_cloud_prod_test $test_case grpc-docker-testclients $client
      then
        echo "$test_case $client $server passed" >> /tmp/cloud_prod_result.txt
      else
        echo "$test_case $client $server failed" >> /tmp/cloud_prod_result.txt
      fi
    done
  done
  gsutil cp /tmp/cloud_prod_result.txt gs://stoked-keyword-656-output/cloud_prod_result.txt
  rm /tmp/cloud_prod_result.txt
}

set -x
main "$@"
