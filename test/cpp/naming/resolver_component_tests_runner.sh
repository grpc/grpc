#!/bin/bash
# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# This file is auto-generated

set -ex

# all command args required in this set order
FLAGS_test_bin_path=`echo "$1" | grep '\--test_bin_path=' | cut -d "=" -f 2`
FLAGS_dns_server_bin_path=`echo "$2" | grep '\--dns_server_bin_path=' | cut -d "=" -f 2`
FLAGS_records_config_path=`echo "$3" | grep '\--records_config_path=' | cut -d "=" -f 2`
FLAGS_test_dns_server_port=`echo "$4" | grep '\--test_dns_server_port=' | cut -d "=" -f 2`

for cmd_arg in "$FLAGS_test_bin_path" "$FLAGS_dns_server_bin_path" "$FLAGS_records_config_path" "$FLAGS_test_dns_server_port"; do
  if [[ "$cmd_arg" == "" ]]; then
    echo "Missing a CMD arg" && exit 1
  fi
done

if [[ "$GRPC_DNS_RESOLVER" != "" && "$GRPC_DNS_RESOLVER" != ares ]]; then
  echo "This test only works under GRPC_DNS_RESOLVER=ares. Have GRPC_DNS_RESOLVER=$GRPC_DNS_RESOLVER" && exit 1
fi
export GRPC_DNS_RESOLVER=ares

"$FLAGS_dns_server_bin_path" --records_config_path="$FLAGS_records_config_path" --port="$FLAGS_test_dns_server_port" 2>&1 > /dev/null &
DNS_SERVER_PID=$!
echo "Local DNS server started. PID: $DNS_SERVER_PID"

# Health check local DNS server TCP and UDP ports
for ((i=0;i<30;i++));
do
  echo "Retry health-check DNS query to local DNS server over tcp and udp"
  RETRY=0
  dig A health-check-local-dns-server-is-alive.resolver-tests.grpctestingexp. @localhost -p "$FLAGS_test_dns_server_port" +tries=1 +timeout=1 | grep '123.123.123.123' || RETRY=1
  dig A health-check-local-dns-server-is-alive.resolver-tests.grpctestingexp. @localhost -p "$FLAGS_test_dns_server_port" +tries=1 +timeout=1 +tcp | grep '123.123.123.123' || RETRY=1
  if [[ "$RETRY" == 0 ]]; then
    break
  fi;
  sleep 0.1
done

if [[ $RETRY == 1 ]]; then
  echo "FAILED TO START LOCAL DNS SERVER"
  kill -SIGTERM $DNS_SERVER_PID
  wait
  exit 1
fi

function terminate_all {
  echo "Received signal. Terminating $! and $DNS_SERVER_PID"
  kill -SIGTERM $! || true
  kill -SIGTERM $DNS_SERVER_PID || true
  wait
  exit 1
}

trap terminate_all SIGTERM SIGINT

EXIT_CODE=0
# TODO: this test should check for GCE residency and skip tests using _grpclb._tcp.* SRV records once GCE residency checks are made
# in the resolver.

$FLAGS_test_bin_path \
  --target_name='srv-ipv4-single-target.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:1234,True' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

$FLAGS_test_bin_path \
  --target_name='srv-ipv4-multi-target.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.5:1234,True;1.2.3.6:1234,True;1.2.3.7:1234,True' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

$FLAGS_test_bin_path \
  --target_name='srv-ipv6-single-target.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='[2607:f8b0:400a:801::1001]:1234,True' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

$FLAGS_test_bin_path \
  --target_name='srv-ipv6-multi-target.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='[2607:f8b0:400a:801::1002]:1234,True;[2607:f8b0:400a:801::1003]:1234,True;[2607:f8b0:400a:801::1004]:1234,True' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

$FLAGS_test_bin_path \
  --target_name='srv-ipv4-simple-service-config.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:1234,True' \
  --expected_chosen_service_config='{"loadBalancingPolicy":"round_robin","methodConfig":[{"name":[{"method":"Foo","service":"SimpleService","waitForReady":true}]}]}' \
  --expected_lb_policy='round_robin' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

$FLAGS_test_bin_path \
  --target_name='ipv4-no-srv-simple-service-config.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:443,False' \
  --expected_chosen_service_config='{"loadBalancingPolicy":"round_robin","methodConfig":[{"name":[{"method":"Foo","service":"NoSrvSimpleService","waitForReady":true}]}]}' \
  --expected_lb_policy='round_robin' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

$FLAGS_test_bin_path \
  --target_name='ipv4-no-config-for-cpp.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:443,False' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

$FLAGS_test_bin_path \
  --target_name='ipv4-cpp-config-has-zero-percentage.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:443,False' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

$FLAGS_test_bin_path \
  --target_name='ipv4-second-language-is-cpp.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:443,False' \
  --expected_chosen_service_config='{"loadBalancingPolicy":"round_robin","methodConfig":[{"name":[{"method":"Foo","service":"CppService","waitForReady":true}]}]}' \
  --expected_lb_policy='round_robin' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

$FLAGS_test_bin_path \
  --target_name='ipv4-config-with-percentages.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:443,False' \
  --expected_chosen_service_config='{"loadBalancingPolicy":"round_robin","methodConfig":[{"name":[{"method":"Foo","service":"AlwaysPickedService","waitForReady":true}]}]}' \
  --expected_lb_policy='round_robin' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

$FLAGS_test_bin_path \
  --target_name='srv-ipv4-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:1234,True;1.2.3.4:443,False' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

$FLAGS_test_bin_path \
  --target_name='srv-ipv6-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='[2607:f8b0:400a:801::1002]:1234,True;[2607:f8b0:400a:801::1002]:443,False' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

$FLAGS_test_bin_path \
  --target_name='ipv4-config-causing-fallback-to-tcp.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:443,False' \
  --expected_chosen_service_config='{"loadBalancingPolicy":"round_robin","methodConfig":[{"name":[{"method":"Foo","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooTwo","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooThree","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooFour","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooFive","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooSix","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooSeven","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooEight","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooNine","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooTen","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooEleven","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooTwelve","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooTwelve","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooTwelve","service":"SimpleService","waitForReady":true}]},{"name":[{"method":"FooTwelve","service":"SimpleService","waitForReady":true}]}]}' \
  --expected_lb_policy='' \
  --local_dns_server_address=127.0.0.1:$FLAGS_test_dns_server_port &
wait $! || EXIT_CODE=1

kill -SIGTERM $DNS_SERVER_PID || true
wait
exit $EXIT_CODE
