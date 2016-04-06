#!/bin/sh

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

if [ x"$QPS_WORKERS" == x ]; then
  echo Error: Must set QPS_WORKERS variable in form \
    "host:port,host:port,..." 1>&2
  exit 1
fi

bins=`find . .. ../.. ../../.. -name bins | head -1`

# Print out each command that gets executed
set -x

#
# Specify parameters used in some of the tests
#

# big is the size in bytes of large messages (0 is the size otherwise)
big=65536

# wide is the number of client channels in multi-channel tests (1 otherwise)
wide=64

# deep is the number of RPCs outstanding on a channel in non-ping-pong tests
# (the value used is 1 otherwise)
deep=100

# half is half the count of worker processes, used in the crossbar scenario
# that uses equal clients and servers. The other scenarios use only 1 server
# and either 1 client or N-1 clients as appropriate
half=`echo $QPS_WORKERS | awk -F, '{print int(NF/2)}'`

for secure in true false; do
  # Scenario 1: generic async streaming ping-pong (contentionless latency)
  "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=1 \
    --client_channels=1 --bbuf_req_size=0 --bbuf_resp_size=0 \
    --async_client_threads=1 --async_server_threads=1 --secure_test=$secure \
    --num_servers=1 --num_clients=1

  # Scenario 2: generic async streaming "unconstrained" (QPS)
  "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=$deep \
    --client_channels=$wide --bbuf_req_size=0 --bbuf_resp_size=0 \
    --async_client_threads=0 --async_server_threads=0 --secure_test=$secure \
    --num_servers=1 --num_clients=0 2>&1 | tee /tmp/qps-test.$$

  # Scenario 2b: QPS with a single server core
  "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=$deep \
    --client_channels=$wide --bbuf_req_size=0 --bbuf_resp_size=0 \
    --async_client_threads=0 --async_server_threads=0 --secure_test=$secure \
    --num_servers=1 --num_clients=0 --server_core_limit=1

  # Scenario 2c: protobuf-based QPS
  "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_SERVER --outstanding_rpcs_per_channel=$deep \
    --client_channels=$wide --simple_req_size=0 --simple_resp_size=0 \
    --async_client_threads=0 --async_server_threads=0 --secure_test=$secure \
    --num_servers=1 --num_clients=0

  # Scenario 3: Latency at sub-peak load (all clients equally loaded)
  for loadfactor in 0.2 0.5 0.7; do
    "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
      --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=$deep \
      --client_channels=$wide --bbuf_req_size=0 --bbuf_resp_size=0 \
      --async_client_threads=0 --async_server_threads=0 --secure_test=$secure \
      --num_servers=1 --num_clients=0 --poisson_load=`awk -v lf=$loadfactor \
      '$5 == "QPS:" {print int(lf * $6); exit}' /tmp/qps-test.$$`
  done

  rm /tmp/qps-test.$$

  # Scenario 4: Single-channel bidirectional throughput test (like TCP_STREAM).
  "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=$deep \
    --client_channels=1 --bbuf_req_size=$big --bbuf_resp_size=$big \
    --async_client_threads=1 --async_server_threads=1 --secure_test=$secure \
    --num_servers=1 --num_clients=1

  # Scenario 5: Sync unary ping-pong with protobufs
  "$bins"/opt/qps_driver --rpc_type=UNARY --client_type=SYNC_CLIENT \
    --server_type=SYNC_SERVER --outstanding_rpcs_per_channel=1 \
    --client_channels=1 --simple_req_size=0 --simple_resp_size=0 \
    --secure_test=$secure --num_servers=1 --num_clients=1

  # Scenario 6: Sync streaming ping-pong with protobufs
  "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=SYNC_CLIENT \
    --server_type=SYNC_SERVER --outstanding_rpcs_per_channel=1 \
    --client_channels=1 --simple_req_size=0 --simple_resp_size=0 \
    --secure_test=$secure --num_servers=1 --num_clients=1

  # Scenario 7: Async unary ping-pong with protobufs
  "$bins"/opt/qps_driver --rpc_type=UNARY --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_SERVER --outstanding_rpcs_per_channel=1 \
    --client_channels=1 --simple_req_size=0 --simple_resp_size=0 \
    --async_client_threads=1 --async_server_threads=1 --secure_test=$secure \
    --num_servers=1 --num_clients=1

  # Scenario 8: Async streaming ping-pong with protobufs
  "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_SERVER --outstanding_rpcs_per_channel=1 \
    --client_channels=1 --simple_req_size=0 --simple_resp_size=0 \
    --async_client_threads=1 --async_server_threads=1 --secure_test=$secure \
    --num_servers=1 --num_clients=1

  # Scenario 9: Crossbar QPS test
  "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=$deep \
    --client_channels=$wide --bbuf_req_size=0 --bbuf_resp_size=0 \
    --async_client_threads=0 --async_server_threads=0 --secure_test=$secure \
    --num_servers=$half --num_clients=0

  # Scenario 10: Multi-channel bidir throughput test
  "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=1 \
    --client_channels=$wide --bbuf_req_size=$big --bbuf_resp_size=$big \
    --async_client_threads=0 --async_server_threads=0 --secure_test=$secure \
    --num_servers=1 --num_clients=1

  # Scenario 11: Single-channel request throughput test
  "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=$deep \
    --client_channels=1 --bbuf_req_size=$big --bbuf_resp_size=0 \
    --async_client_threads=1 --async_server_threads=1 --secure_test=$secure \
    --num_servers=1 --num_clients=1

  # Scenario 12: Single-channel response throughput test
  "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_GENERIC_SERVER --outstanding_rpcs_per_channel=$deep \
    --client_channels=1 --bbuf_req_size=0 --bbuf_resp_size=$big \
    --async_client_threads=1 --async_server_threads=1 --secure_test=$secure \
    --num_servers=1 --num_clients=1

  # Scenario 13: Single-channel bidirectional protobuf throughput test
  "$bins"/opt/qps_driver --rpc_type=STREAMING --client_type=ASYNC_CLIENT \
    --server_type=ASYNC_SERVER --outstanding_rpcs_per_channel=$deep \
    --client_channels=1 --simple_req_size=$big --simple_resp_size=$big \
    --async_client_threads=1 --async_server_threads=1 --secure_test=$secure \
    --num_servers=1 --num_clients=1
done
