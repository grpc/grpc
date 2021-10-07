#!/usr/bin/env bash
# Copyright 2021 The gRPC Authors
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
set -ex

rm netperf_stats.txt

#CLIENT_POD_NUM=0
#SERVER_POD_NUM=$2

for CLIENT_POD_NUM in 0 #1 #2 3 #4 5 6 7
do
    for SERVER_POD_NUM in 1 # 2 3 #4 5 6 7
    do
    #CLIENT_POD_NUM=0
    #SERVER_POD_NUM=num
    POD_NAME_PREFIX="netperf-test-pod-"
    CLIENT_POD="${POD_NAME_PREFIX}${CLIENT_POD_NUM}"
    CLIENT_NODE="$(kubectl get pods ${CLIENT_POD} -o custom-columns=NODENAME:.spec.nodeName --no-headers)"
    SERVER_POD="${POD_NAME_PREFIX}${SERVER_POD_NUM}"
    SERVER_NODE="$(kubectl get pods ${SERVER_POD} -o custom-columns=NODENAME:.spec.nodeName --no-headers)"
    SERVER_POD_IP=$(kubectl get pods ${SERVER_POD} -o custom-columns=IP:.status.podIP --no-headers)
    
    for iter in 1 2 3
    do
        # same command as here:
        # https://github.com/grpc/grpc/blob/fd3bd70939fb4239639fbd26143ec416366e4157/tools/run_tests/performance/run_netperf.sh#L20
        NETPERF_RESULT=$(kubectl exec -it ${CLIENT_POD} -- netperf -P 0 -t TCP_RR -H "${SERVER_POD_IP}" -- -r 1,1 -o P50_LATENCY,P90_LATENCY,P99_LATENCY)
        echo $NETPERF_RESULT

        echo "$CLIENT_POD_NUM,$SERVER_POD_NUM,$CLIENT_NODE,$SERVER_NODE,${NETPERF_RESULT}" >> netperf_stats.txt
    done
    done
done
