#!/usr/bin/env python2.7
# Copyright 2015-2016, Google Inc.
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

"""Generates the appropriate build.yaml data for performance tests."""

import yaml

SINGLE_MACHINE_CORES=8
WARMUP_SECONDS=5
BENCHMARK_SECONDS=30

EMPTY_GENERIC_PAYLOAD = {
  'bytebuf_params': {
    'req_size': 0,
    'resp_size': 0,
  }
}
EMPTY_PROTO_PAYLOAD = {
  'simple_params': {
    'req_size': 0,
    'resp_size': 0,
  }
}
BIG_GENERIC_PAYLOAD = {
  'bytebuf_params': {
    'req_size': 65536,
    'resp_size': 65536,
  }
}

# deep is the number of RPCs outstanding on a channel in non-ping-pong tests
# (the value used is 1 otherwise)
DEEP=100

# wide is the number of client channels in multi-channel tests (1 otherwise)
WIDE=64

scenarios = []
for secure in [True, False]:
  if secure:
    secstr = 'secure'
    secargs = {'use_test_ca': True,
               'server_host_override': 'foo.test.google.fr'}
  else:
    secstr = 'insecure'
    secargs = None

  scenarios.append({
    'single_machine': True,
    'config_protobuf': {
      'name': 'generic async streaming ping-pong (contentionless latency) (%s)'
              % secstr,
      'num_servers': 1,
      'num_clients': 1,
      'client_config': {
        'client_type': 'ASYNC_CLIENT',
        'security_params': secargs,
        'outstanding_rpcs_per_channel': 1,
        'client_channels': 1,
        'async_client_threads': 1,
        'rpc_type': 'STREAMING',
        'load_params': {
          'closed_loop': {}
        },
        'payload_config': EMPTY_GENERIC_PAYLOAD,
      },
      'server_config': {
        'server_type': 'ASYNC_GENERIC_SERVER',
        'security_params': secargs,
        'core_limit': SINGLE_MACHINE_CORES/2,
        'async_server_threads': 1,
        'payload_config': EMPTY_GENERIC_PAYLOAD,
      },
      'warmup_seconds': WARMUP_SECONDS,
      'benchmark_seconds': BENCHMARK_SECONDS
    }
  })
  scenarios.append({
    'single_machine': True,
    'config_protobuf': {
      'name': 'generic async streaming "unconstrained" (QPS) (%s)'
              % secstr,
      'num_servers': 1,
      'num_clients': 0,
      'client_config': {
        'client_type': 'ASYNC_CLIENT',
        'security_params': secargs,
        'outstanding_rpcs_per_channel': DEEP,
        'client_channels': WIDE,
        'async_client_threads': 1,
        'rpc_type': 'STREAMING',
        'load_params': {
          'closed_loop': {}
        },
        'payload_config': EMPTY_GENERIC_PAYLOAD,
      },
      'server_config': {
        'server_type': 'ASYNC_GENERIC_SERVER',
        'security_params': secargs,
        'core_limit': SINGLE_MACHINE_CORES/2,
        'async_server_threads': 1,
        'payload_config': EMPTY_GENERIC_PAYLOAD,
      },
      'warmup_seconds': WARMUP_SECONDS,
      'benchmark_seconds': BENCHMARK_SECONDS
    }
  })
  scenarios.append({
    'single_machine': True,
    'config_protobuf': {
      'name': 'QPS with a single server core (%s)'
              % secstr,
      'num_servers': 1,
      'num_clients': 0,
      'client_config': {
        'client_type': 'ASYNC_CLIENT',
        'security_params': secargs,
        'outstanding_rpcs_per_channel': DEEP,
        'client_channels': WIDE,
        'async_client_threads': 1,
        'rpc_type': 'STREAMING',
        'load_params': {
          'closed_loop': {}
        },
        'payload_config': EMPTY_GENERIC_PAYLOAD,
      },
      'server_config': {
        'server_type': 'ASYNC_GENERIC_SERVER',
        'security_params': secargs,
        'core_limit': 1,
        'async_server_threads': 1,
        'payload_config': EMPTY_GENERIC_PAYLOAD,
      },
      'warmup_seconds': WARMUP_SECONDS,
      'benchmark_seconds': BENCHMARK_SECONDS
    }
  })
  scenarios.append({
    'single_machine': True,
    'config_protobuf': {
      'name': 'protobuf-based QPS (%s)'
              % secstr,
      'num_servers': 1,
      'num_clients': 0,
      'client_config': {
        'client_type': 'ASYNC_CLIENT',
        'security_params': secargs,
        'outstanding_rpcs_per_channel': DEEP,
        'client_channels': WIDE,
        'async_client_threads': 1,
        'rpc_type': 'STREAMING',
        'load_params': {
          'closed_loop': {}
        },
        'payload_config': EMPTY_GENERIC_PAYLOAD,
      },
      'server_config': {
        'server_type': 'ASYNC_GENERIC_SERVER',
        'security_params': secargs,
        'core_limit': SINGLE_MACHINE_CORES/2,
        'async_server_threads': 1,
        'payload_config': EMPTY_GENERIC_PAYLOAD,
      },
      'warmup_seconds': WARMUP_SECONDS,
      'benchmark_seconds': BENCHMARK_SECONDS
    }
  })
  scenarios.append({
    'single_machine': True,
    'config_protobuf': {
      'name': 'Single-channel bidirectional throughput test (like TCP_STREAM) (%s)'
              % secstr,
      'num_servers': 1,
      'num_clients': 1,
      'client_config': {
        'client_type': 'ASYNC_CLIENT',
        'security_params': secargs,
        'outstanding_rpcs_per_channel': 1,
        'client_channels': 1,
        'async_client_threads': 1,
        'rpc_type': 'STREAMING',
        'load_params': {
          'closed_loop': {}
        },
        'payload_config': BIG_GENERIC_PAYLOAD,
      },
      'server_config': {
        'server_type': 'ASYNC_GENERIC_SERVER',
        'security_params': secargs,
        'core_limit': SINGLE_MACHINE_CORES/2,
        'async_server_threads': 1,
        'payload_config': BIG_GENERIC_PAYLOAD,
      },
      'warmup_seconds': WARMUP_SECONDS,
      'benchmark_seconds': BENCHMARK_SECONDS
    }
  })
  scenarios.append({
    'single_machine': True,
    'config_protobuf': {
      'name': 'Sync unary ping-pong with protobufs (%s)'
              % secstr,
      'num_servers': 1,
      'num_clients': 1,
      'client_config': {
        'client_type': 'ASYNC_CLIENT',
        'security_params': secargs,
        'outstanding_rpcs_per_channel': 1,
        'client_channels': 1,
        'async_client_threads': 1,
        'rpc_type': 'STREAMING',
        'load_params': {
          'closed_loop': {}
        },
        'payload_config': EMPTY_PROTO_PAYLOAD,
      },
      'server_config': {
        'server_type': 'ASYNC_GENERIC_SERVER',
        'security_params': secargs,
        'core_limit': SINGLE_MACHINE_CORES/2,
        'async_server_threads': 1,
        'payload_config': EMPTY_PROTO_PAYLOAD,
      },
      'warmup_seconds': WARMUP_SECONDS,
      'benchmark_seconds': BENCHMARK_SECONDS
    }
  })

print yaml.dump({'performance_scenarios': scenarios})
