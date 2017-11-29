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

if [[ "$GRPC_DNS_RESOLVER" == "" ]]; then
  export GRPC_DNS_RESOLVER=ares
elif [[ "$GRPC_DNS_RESOLVER" != ares ]]; then
  echo "Unexpected: GRPC_DNS_RESOLVER=$GRPC_DNS_RESOLVER. This test only works with c-ares resolver"
  exit 1
fi

cd $(dirname $0)/../../..

if [[ "$CONFIG" == "" ]]; then
  export CONFIG=opt
fi
make resolver_component_test
echo "Sanity check DNS records are resolveable with dig:"
EXIT_CODE=0

ONE_FAILED=0
dig SRV _grpclb._tcp.srv-ipv4-single-target.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig SRV _grpclb._tcp.srv-ipv4-single-target.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig A ipv4-single-target.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig A ipv4-single-target.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig SRV _grpclb._tcp.srv-ipv4-multi-target.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig SRV _grpclb._tcp.srv-ipv4-multi-target.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig A ipv4-multi-target.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig A ipv4-multi-target.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig SRV _grpclb._tcp.srv-ipv6-single-target.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig SRV _grpclb._tcp.srv-ipv6-single-target.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig AAAA ipv6-single-target.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig AAAA ipv6-single-target.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig SRV _grpclb._tcp.srv-ipv6-multi-target.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig SRV _grpclb._tcp.srv-ipv6-multi-target.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig AAAA ipv6-multi-target.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig AAAA ipv6-multi-target.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig TXT _grpc_config.srv-ipv4-simple-service-config.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig TXT _grpc_config.srv-ipv4-simple-service-config.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig SRV _grpclb._tcp.srv-ipv4-simple-service-config.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig SRV _grpclb._tcp.srv-ipv4-simple-service-config.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig A ipv4-simple-service-config.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig A ipv4-simple-service-config.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig A ipv4-no-srv-simple-service-config.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig A ipv4-no-srv-simple-service-config.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig TXT _grpc_config.ipv4-no-srv-simple-service-config.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig TXT _grpc_config.ipv4-no-srv-simple-service-config.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig TXT _grpc_config.ipv4-no-config-for-cpp.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig TXT _grpc_config.ipv4-no-config-for-cpp.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig A ipv4-no-config-for-cpp.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig A ipv4-no-config-for-cpp.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig A ipv4-cpp-config-has-zero-percentage.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig A ipv4-cpp-config-has-zero-percentage.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig TXT _grpc_config.ipv4-cpp-config-has-zero-percentage.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig TXT _grpc_config.ipv4-cpp-config-has-zero-percentage.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig TXT _grpc_config.ipv4-second-language-is-cpp.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig TXT _grpc_config.ipv4-second-language-is-cpp.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig A ipv4-second-language-is-cpp.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig A ipv4-second-language-is-cpp.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig A ipv4-config-with-percentages.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig A ipv4-config-with-percentages.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig TXT _grpc_config.ipv4-config-with-percentages.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig TXT _grpc_config.ipv4-config-with-percentages.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig SRV _grpclb._tcp.srv-ipv4-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig SRV _grpclb._tcp.srv-ipv4-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig A balancer-for-ipv4-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig A balancer-for-ipv4-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig A srv-ipv4-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig A srv-ipv4-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig SRV _grpclb._tcp.srv-ipv6-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig SRV _grpclb._tcp.srv-ipv6-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig AAAA balancer-for-ipv6-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig AAAA balancer-for-ipv6-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

ONE_FAILED=0
dig AAAA srv-ipv6-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. | grep 'ANSWER SECTION' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Sanity check: dig AAAA srv-ipv6-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. FAILED"
  exit 1
fi

echo "Sanity check PASSED. Run resolver tests:"

ONE_FAILED=0
bins/$CONFIG/resolver_component_test \
  --target_name='srv-ipv4-single-target.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:1234,True' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Test based on target record: srv-ipv4-single-target.resolver-tests-version-4.grpctestingexp. FAILED"
  EXIT_CODE=1
fi

ONE_FAILED=0
bins/$CONFIG/resolver_component_test \
  --target_name='srv-ipv4-multi-target.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.5:1234,True;1.2.3.6:1234,True;1.2.3.7:1234,True' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Test based on target record: srv-ipv4-multi-target.resolver-tests-version-4.grpctestingexp. FAILED"
  EXIT_CODE=1
fi

ONE_FAILED=0
bins/$CONFIG/resolver_component_test \
  --target_name='srv-ipv6-single-target.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='[2607:f8b0:400a:801::1001]:1234,True' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Test based on target record: srv-ipv6-single-target.resolver-tests-version-4.grpctestingexp. FAILED"
  EXIT_CODE=1
fi

ONE_FAILED=0
bins/$CONFIG/resolver_component_test \
  --target_name='srv-ipv6-multi-target.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='[2607:f8b0:400a:801::1002]:1234,True;[2607:f8b0:400a:801::1003]:1234,True;[2607:f8b0:400a:801::1004]:1234,True' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Test based on target record: srv-ipv6-multi-target.resolver-tests-version-4.grpctestingexp. FAILED"
  EXIT_CODE=1
fi

ONE_FAILED=0
bins/$CONFIG/resolver_component_test \
  --target_name='srv-ipv4-simple-service-config.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:1234,True' \
  --expected_chosen_service_config='{"loadBalancingPolicy":"round_robin","methodConfig":[{"name":[{"method":"Foo","service":"SimpleService","waitForReady":true}]}]}' \
  --expected_lb_policy='round_robin' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Test based on target record: srv-ipv4-simple-service-config.resolver-tests-version-4.grpctestingexp. FAILED"
  EXIT_CODE=1
fi

ONE_FAILED=0
bins/$CONFIG/resolver_component_test \
  --target_name='ipv4-no-srv-simple-service-config.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:443,False' \
  --expected_chosen_service_config='{"loadBalancingPolicy":"round_robin","methodConfig":[{"name":[{"method":"Foo","service":"NoSrvSimpleService","waitForReady":true}]}]}' \
  --expected_lb_policy='round_robin' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Test based on target record: ipv4-no-srv-simple-service-config.resolver-tests-version-4.grpctestingexp. FAILED"
  EXIT_CODE=1
fi

ONE_FAILED=0
bins/$CONFIG/resolver_component_test \
  --target_name='ipv4-no-config-for-cpp.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:443,False' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Test based on target record: ipv4-no-config-for-cpp.resolver-tests-version-4.grpctestingexp. FAILED"
  EXIT_CODE=1
fi

ONE_FAILED=0
bins/$CONFIG/resolver_component_test \
  --target_name='ipv4-cpp-config-has-zero-percentage.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:443,False' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Test based on target record: ipv4-cpp-config-has-zero-percentage.resolver-tests-version-4.grpctestingexp. FAILED"
  EXIT_CODE=1
fi

ONE_FAILED=0
bins/$CONFIG/resolver_component_test \
  --target_name='ipv4-second-language-is-cpp.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:443,False' \
  --expected_chosen_service_config='{"loadBalancingPolicy":"round_robin","methodConfig":[{"name":[{"method":"Foo","service":"CppService","waitForReady":true}]}]}' \
  --expected_lb_policy='round_robin' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Test based on target record: ipv4-second-language-is-cpp.resolver-tests-version-4.grpctestingexp. FAILED"
  EXIT_CODE=1
fi

ONE_FAILED=0
bins/$CONFIG/resolver_component_test \
  --target_name='ipv4-config-with-percentages.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:443,False' \
  --expected_chosen_service_config='{"loadBalancingPolicy":"round_robin","methodConfig":[{"name":[{"method":"Foo","service":"AlwaysPickedService","waitForReady":true}]}]}' \
  --expected_lb_policy='round_robin' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Test based on target record: ipv4-config-with-percentages.resolver-tests-version-4.grpctestingexp. FAILED"
  EXIT_CODE=1
fi

ONE_FAILED=0
bins/$CONFIG/resolver_component_test \
  --target_name='srv-ipv4-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='1.2.3.4:1234,True;1.2.3.4:443,False' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Test based on target record: srv-ipv4-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. FAILED"
  EXIT_CODE=1
fi

ONE_FAILED=0
bins/$CONFIG/resolver_component_test \
  --target_name='srv-ipv6-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp.' \
  --expected_addrs='[2607:f8b0:400a:801::1002]:1234,True;[2607:f8b0:400a:801::1002]:443,False' \
  --expected_chosen_service_config='' \
  --expected_lb_policy='' || ONE_FAILED=1
if [[ "$ONE_FAILED" != 0 ]]; then
  echo "Test based on target record: srv-ipv6-target-has-backend-and-balancer.resolver-tests-version-4.grpctestingexp. FAILED"
  EXIT_CODE=1
fi

exit $EXIT_CODE
