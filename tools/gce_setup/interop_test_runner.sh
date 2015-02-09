#!/bin/bash
thisfile=$(readlink -ne "${BASH_SOURCE[0]}")
current_time=$(date "+%Y-%m-%d-%H-%M-%S")
result_file_name=interop_result.$current_time.html
echo $result_file_name

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
          echo "          ['$test_case', '$client', '$server', true]," >> /tmp/interop_result.txt
        else
          echo "          ['$test_case', '$client', '$server', false]," >> /tmp/interop_result.txt
        fi
      done
    done
  done
  if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    cat pre.html /tmp/interop_result.txt post.html > /tmp/interop_result.html
    gsutil cp /tmp/interop_result.txt gs://stoked-keyword-656-output/interop_result.txt
    gsutil cp /tmp/interop_result.html gs://stoked-keyword-656-output/interop_result.html
    gsutil cp /tmp/interop_result.html gs://stoked-keyword-656-output/result_history/$result_file_name
    rm /tmp/interop_result.txt
    rm /tmp/interop_result.html
  fi
}

set -x
main "$@"
