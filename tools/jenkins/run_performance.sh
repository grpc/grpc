#!/usr/bin/env bash
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# This script is invoked by Jenkins and runs performance smoke test.
set -ex

# Enter the gRPC repo root
cd $(dirname $0)/../..

[[ $* =~ '--latency_profile' ]] \
	&& tools/profiling/latency_profile/run_latency_profile.sh \
	|| true

config=opt

make CONFIG=$config qps_worker qps_json_driver -j8

bins/$config/qps_worker -driver_port 10000 &
PID1=$!
bins/$config/qps_worker -driver_port 10010 &
PID2=$!

#
# Put a timeout on these tests
#
((sleep 900; kill $$ && killall qps_worker && rm -f /tmp/qps-test.$$ )&)

export QPS_WORKERS="localhost:10000,localhost:10010"

# big is the size in bytes of large messages (0 is the size otherwise)
big=65536

# wide is the number of client channels in multi-channel tests (1 otherwise)
wide=64

# deep is the number of RPCs outstanding on a channel in non-ping-pong tests
# (the value used is 1 otherwise)
deep=100

#
# Get total core count
cores=`grep -c ^processor /proc/cpuinfo || sysctl -n hw.ncpu`
halfcores=`expr $cores / 2`

for secure in true false; do
  # Scenario 1: generic async streaming ping-pong (contentionless latency)
  bins/$config/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=1 \
    --client_channels=1 --bbuf_req_size=0 --bbuf_resp_size=0 \
    --async_client_threads=1 --async_server_threads=1 --secure_test=$secure \
    --num_servers=1 --num_clients=1 \
    --server_core_limit=$halfcores --client_core_limit=0

  # Scenario 2: generic async streaming "unconstrained" (QPS)
  bins/$config/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=$deep \
    --client_channels=$wide --bbuf_req_size=0 --bbuf_resp_size=0 \
    --async_client_threads=0 --async_server_threads=0 --secure_test=$secure \
    --num_servers=1 --num_clients=0 \
    --server_core_limit=$halfcores --client_core_limit=0 2>&1 | \
      tee /tmp/qps-test.$$

  # Scenario 2b: QPS with a single server core
  bins/$config/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=$deep \
    --client_channels=$wide --bbuf_req_size=0 --bbuf_resp_size=0 \
    --async_client_threads=0 --async_server_threads=0 --secure_test=$secure \
    --num_servers=1 --num_clients=0 --server_core_limit=1 --client_core_limit=0

  # Scenario 2c: protobuf-based QPS
  bins/$config/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_SERVER --outstanding_rpcs_per_channel=$deep \
    --client_channels=$wide --simple_req_size=0 --simple_resp_size=0 \
    --async_client_threads=0 --async_server_threads=0 --secure_test=$secure \
    --num_servers=1 --num_clients=0 \
    --server_core_limit=$halfcores --client_core_limit=0

  # Scenario 3: Latency at sub-peak load (all clients equally loaded)
  for loadfactor in 0.7; do
    bins/$config/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
      --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=$deep \
      --client_channels=$wide --bbuf_req_size=0 --bbuf_resp_size=0 \
      --async_client_threads=0 --async_server_threads=0 --secure_test=$secure \
      --num_servers=1 --num_clients=0 --poisson_load=`awk -v lf=$loadfactor \
      '$5 == "QPS:" {print int(lf * $6); exit}' /tmp/qps-test.$$` \
      --server_core_limit=$halfcores --client_core_limit=0
  done

  rm /tmp/qps-test.$$

  # Scenario 4: Single-channel bidirectional throughput test (like TCP_STREAM).
  bins/$config/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=$deep \
    --client_channels=1 --bbuf_req_size=$big --bbuf_resp_size=$big \
    --async_client_threads=1 --async_server_threads=1 --secure_test=$secure \
    --num_servers=1 --num_clients=1 \
    --server_core_limit=$halfcores --client_core_limit=0

  # Scenario 5: Sync unary ping-pong with protobufs
  bins/$config/qps_driver --rpc_type=UNARY --client_type=SYNC_CLIENT \
    --server_type=SYNC_SERVER --outstanding_rpcs_per_channel=1 \
    --client_channels=1 --simple_req_size=0 --simple_resp_size=0 \
    --secure_test=$secure --num_servers=1 --num_clients=1 \
    --server_core_limit=$halfcores --client_core_limit=0

done

bins/$config/qps_json_driver --quit=true

wait
