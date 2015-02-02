#!/bin/bash
thisfile=$(readlink -ne "${BASH_SOURCE[0]}")

run_test() {
  local test_case=$1
  shift
  local client=$1
  shift 
  local server=$1
  if grpc_interop_test $test_case grpc-docker-testclients $client grpc-docker-server $server
  then
    echo "$test_case $client $server passed" >> /tmp/interop_result.txt
  else
    echo "$test_case $client $server failed" >> /tmp/interop_result.txt
  fi
}

time_out() {
  local test_case=$1
  shift
  local client=$1
  shift
  local server=$1
  if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    if ! timeout 20s bash -l -c "source $thisfile && run_test $test_case $client $server"
    then
      echo "$test_case $client $server timed out" >> /tmp/interop_result.txt
    fi
  fi
}

main() {
  source grpc_docker.sh
  test_cases=(large_unary empty_unary ping_pong client_streaming server_streaming)
  clients=(cxx java go ruby)
  servers=(cxx java go ruby)
  for test_case in "${test_cases[@]}"
  do
    for client in "${clients[@]}"
    do
      for server in "${servers[@]}"
      do
        time_out $test_case $client $server
      done
    done
  done
  if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    gsutil cp /tmp/interop_result.txt gs://stoked-keyword-656-output/interop_result.txt
    rm /tmp/interop_result.txt
  fi
}

set -x
main "$@"
