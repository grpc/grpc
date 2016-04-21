# Copyright 2016, Google Inc.
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

# performance scenario configuration for various languages

SINGLE_MACHINE_CORES=8
WARMUP_SECONDS=5
JAVA_WARMUP_SECONDS=15  # Java needs more warmup time for JIT to kick in.
BENCHMARK_SECONDS=30

HISTOGRAM_PARAMS = {
  'resolution': 0.01,
  'max_possible': 60e9,
}

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


class CXXLanguage:

  def __init__(self):
    self.safename = 'cxx'

  def worker_cmdline(self):
    return ['bins/opt/qps_worker']

  def worker_port_offset(self):
    return 0

  def scenarios(self):
    # TODO(ctiller): add 70% load latency test
    for secure in [True, False]:
      if secure:
        secstr = 'secure'
        secargs = {'use_test_ca': True,
                   'server_host_override': 'foo.test.google.fr'}
      else:
        secstr = 'insecure'
        secargs = None

      yield {
          'name': 'cpp_generic_async_streaming_ping_pong_%s'
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
            'histogram_params': HISTOGRAM_PARAMS,
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
      yield {
          'name': 'cpp_generic_async_streaming_qps_unconstrained_%s'
                  % secstr,
          'num_servers': 1,
          'num_clients': 0,
          'client_config': {
            'client_type': 'ASYNC_CLIENT',
            'security_params': secargs,
            'outstanding_rpcs_per_channel': DEEP,
            'client_channels': WIDE,
            'async_client_threads': 0,
            'rpc_type': 'STREAMING',
            'load_params': {
              'closed_loop': {}
            },
            'payload_config': EMPTY_GENERIC_PAYLOAD,
            'histogram_params': HISTOGRAM_PARAMS,
          },
          'server_config': {
            'server_type': 'ASYNC_GENERIC_SERVER',
            'security_params': secargs,
            'core_limit': SINGLE_MACHINE_CORES/2,
            'async_server_threads': 0,
            'payload_config': EMPTY_GENERIC_PAYLOAD,
          },
          'warmup_seconds': WARMUP_SECONDS,
          'benchmark_seconds': BENCHMARK_SECONDS
      }
      yield {
          'name': 'cpp_generic_async_streaming_qps_one_server_core_%s'
                  % secstr,
          'num_servers': 1,
          'num_clients': 0,
          'client_config': {
            'client_type': 'ASYNC_CLIENT',
            'security_params': secargs,
            'outstanding_rpcs_per_channel': DEEP,
            'client_channels': WIDE,
            'async_client_threads': 0,
            'rpc_type': 'STREAMING',
            'load_params': {
              'closed_loop': {}
            },
            'payload_config': EMPTY_GENERIC_PAYLOAD,
            'histogram_params': HISTOGRAM_PARAMS,
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
      yield {
          'name': 'cpp_protobuf_async_streaming_qps_unconstrained_%s'
                  % secstr,
          'num_servers': 1,
          'num_clients': 0,
          'client_config': {
            'client_type': 'ASYNC_CLIENT',
            'security_params': secargs,
            'outstanding_rpcs_per_channel': DEEP,
            'client_channels': WIDE,
            'async_client_threads': 0,
            'rpc_type': 'STREAMING',
            'load_params': {
              'closed_loop': {}
            },
            'payload_config': EMPTY_PROTO_PAYLOAD,
            'histogram_params': HISTOGRAM_PARAMS,
          },
          'server_config': {
            'server_type': 'ASYNC_SERVER',
            'security_params': secargs,
            'core_limit': SINGLE_MACHINE_CORES/2,
            'async_server_threads': 0,
          },
          'warmup_seconds': WARMUP_SECONDS,
          'benchmark_seconds': BENCHMARK_SECONDS
      }
      yield {
          'name': 'cpp_single_channel_throughput_%s'
                  % secstr,
          'num_servers': 1,
          'num_clients': 1,
          'client_config': {
            'client_type': 'ASYNC_CLIENT',
            'security_params': secargs,
            'outstanding_rpcs_per_channel': DEEP,
            'client_channels': 1,
            'async_client_threads': 0,
            'rpc_type': 'STREAMING',
            'load_params': {
              'closed_loop': {}
            },
            'payload_config': BIG_GENERIC_PAYLOAD,
            'histogram_params': HISTOGRAM_PARAMS,
          },
          'server_config': {
            'server_type': 'ASYNC_GENERIC_SERVER',
            'security_params': secargs,
            'core_limit': SINGLE_MACHINE_CORES/2,
            'async_server_threads': 0,
            'payload_config': BIG_GENERIC_PAYLOAD,
          },
          'warmup_seconds': WARMUP_SECONDS,
          'benchmark_seconds': BENCHMARK_SECONDS
      }
      yield {
          'name': 'cpp_protobuf_async_streaming_ping_pong_%s'
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
            'histogram_params': HISTOGRAM_PARAMS,
          },
          'server_config': {
            'server_type': 'ASYNC_SERVER',
            'security_params': secargs,
            'core_limit': 1,
            'async_server_threads': 1,
          },
          'warmup_seconds': WARMUP_SECONDS,
          'benchmark_seconds': BENCHMARK_SECONDS
      }
      yield {
          'name': 'cpp_protobuf_sync_unary_ping_pong_%s'
                  % secstr,
          'num_servers': 1,
          'num_clients': 1,
          'client_config': {
            'client_type': 'SYNC_CLIENT',
            'security_params': secargs,
            'outstanding_rpcs_per_channel': 1,
            'client_channels': 1,
            'async_client_threads': 0,
            'rpc_type': 'UNARY',
            'load_params': {
              'closed_loop': {}
            },
            'payload_config': EMPTY_PROTO_PAYLOAD,
            'histogram_params': HISTOGRAM_PARAMS,
          },
          'server_config': {
            'server_type': 'SYNC_SERVER',
            'security_params': secargs,
            'core_limit': 1,
            'async_server_threads': 0,
          },
          'warmup_seconds': WARMUP_SECONDS,
          'benchmark_seconds': BENCHMARK_SECONDS
      }
      yield {
          'name': 'cpp_protobuf_async_unary_ping_pong_%s'
                  % secstr,
          'num_servers': 1,
          'num_clients': 1,
          'client_config': {
            'client_type': 'ASYNC_CLIENT',
            'security_params': secargs,
            'outstanding_rpcs_per_channel': 1,
            'client_channels': 1,
            'async_client_threads': 1,
            'rpc_type': 'UNARY',
            'load_params': {
              'closed_loop': {}
            },
            'payload_config': EMPTY_PROTO_PAYLOAD,
            'histogram_params': HISTOGRAM_PARAMS,
          },
          'server_config': {
            'server_type': 'ASYNC_SERVER',
            'security_params': secargs,
            'core_limit': 1,
            'async_server_threads': 1,
          },
          'warmup_seconds': WARMUP_SECONDS,
          'benchmark_seconds': BENCHMARK_SECONDS
      }

  def __str__(self):
    return 'c++'


class CSharpLanguage:

  def __init__(self):
    self.safename = str(self)

  def worker_cmdline(self):
    return ['tools/run_tests/performance/run_worker_csharp.sh']

  def worker_port_offset(self):
    return 100

  def scenarios(self):
    # TODO(jtattermusch): add more scenarios
    secargs = None
    yield {
        'name': 'csharp_protobuf_async_streaming_qps_unconstrained',
        'num_servers': 1,
        'num_clients': 0,
        'client_config': {
          'client_type': 'ASYNC_CLIENT',
          'security_params': secargs,
          'outstanding_rpcs_per_channel': DEEP,
          'client_channels': WIDE,
          'async_client_threads': 0,
          'rpc_type': 'STREAMING',
          'load_params': {
            'closed_loop': {}
          },
          'payload_config': EMPTY_PROTO_PAYLOAD,
          'histogram_params': HISTOGRAM_PARAMS,
        },
        'server_config': {
          'server_type': 'ASYNC_SERVER',
          'security_params': secargs,
          'core_limit': 0,
          'async_server_threads': 0,
        },
        'warmup_seconds': WARMUP_SECONDS,
        'benchmark_seconds': BENCHMARK_SECONDS
    }
    yield {
        'name': 'csharp_generic_async_streaming_ping_pong',
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
          'histogram_params': HISTOGRAM_PARAMS,
        },
        'server_config': {
          'server_type': 'ASYNC_GENERIC_SERVER',
          'security_params': secargs,
          'core_limit': 0,
          'async_server_threads': 0,
          'payload_config': EMPTY_GENERIC_PAYLOAD,
        },
        'warmup_seconds': WARMUP_SECONDS,
        'benchmark_seconds': BENCHMARK_SECONDS
    }
    yield {
        'name': 'csharp_protobuf_async_unary_ping_pong',
        'num_servers': 1,
        'num_clients': 1,
        'client_config': {
          'client_type': 'ASYNC_CLIENT',
          'security_params': secargs,
          'outstanding_rpcs_per_channel': 1,
          'client_channels': 1,
          'async_client_threads': 1,
          'rpc_type': 'UNARY',
          'load_params': {
            'closed_loop': {}
          },
          'payload_config': EMPTY_PROTO_PAYLOAD,
          'histogram_params': HISTOGRAM_PARAMS,
        },
        'server_config': {
          'server_type': 'ASYNC_SERVER',
          'security_params': secargs,
          'core_limit': 0,
          'async_server_threads': 0,
        },
        'warmup_seconds': WARMUP_SECONDS,
        'benchmark_seconds': BENCHMARK_SECONDS
    }
    yield {
        'name': 'csharp_protobuf_sync_to_async_unary_ping_pong',
        'num_servers': 1,
        'num_clients': 1,
        'client_config': {
          'client_type': 'SYNC_CLIENT',
          'security_params': secargs,
          'outstanding_rpcs_per_channel': 1,
          'client_channels': 1,
          'async_client_threads': 1,
          'rpc_type': 'UNARY',
          'load_params': {
            'closed_loop': {}
          },
          'payload_config': EMPTY_PROTO_PAYLOAD,
          'histogram_params': HISTOGRAM_PARAMS,
        },
        'server_config': {
          'server_type': 'ASYNC_SERVER',
          'security_params': secargs,
          'core_limit': 0,
          'async_server_threads': 0,
        },
        'warmup_seconds': WARMUP_SECONDS,
        'benchmark_seconds': BENCHMARK_SECONDS
    }
    yield {
        'name': 'csharp_to_cpp_protobuf_sync_unary_ping_pong',
        'num_servers': 1,
        'num_clients': 1,
        'client_config': {
          'client_type': 'SYNC_CLIENT',
          'security_params': secargs,
          'outstanding_rpcs_per_channel': 1,
          'client_channels': 1,
          'async_client_threads': 1,
          'rpc_type': 'UNARY',
          'load_params': {
            'closed_loop': {}
          },
          'payload_config': EMPTY_PROTO_PAYLOAD,
          'histogram_params': HISTOGRAM_PARAMS,
        },
        'server_config': {
          'server_type': 'SYNC_SERVER',
          'security_params': secargs,
          'core_limit': 0,
          'async_server_threads': 0,
        },
        'warmup_seconds': WARMUP_SECONDS,
        'benchmark_seconds': BENCHMARK_SECONDS,
        'SERVER_LANGUAGE': 'c++'  # recognized by run_performance_tests.py
    }

  def __str__(self):
    return 'csharp'


class NodeLanguage:

  def __init__(self):
    pass
    self.safename = str(self)

  def worker_cmdline(self):
    return ['tools/run_tests/performance/run_worker_node.sh']

  def worker_port_offset(self):
    return 200

  def scenarios(self):
    # TODO(jtattermusch): add more scenarios
    secargs = None
    yield {
        'name': 'node_protobuf_unary_ping_pong',
        'num_servers': 1,
        'num_clients': 1,
        'client_config': {
          'client_type': 'ASYNC_CLIENT',
          'security_params': secargs,
          'outstanding_rpcs_per_channel': 1,
          'client_channels': 1,
          'async_client_threads': 1,
          'rpc_type': 'UNARY',
          'load_params': {
            'closed_loop': {}
          },
          'payload_config': EMPTY_PROTO_PAYLOAD,
          'histogram_params': HISTOGRAM_PARAMS,
        },
        'server_config': {
          'server_type': 'ASYNC_SERVER',
          'security_params': secargs,
          'core_limit': 0,
          'async_server_threads': 1,
        },
        'warmup_seconds': WARMUP_SECONDS,
        'benchmark_seconds': BENCHMARK_SECONDS
    }

  def __str__(self):
    return 'node'


class RubyLanguage:

  def __init__(self):
    pass
    self.safename = str(self)

  def worker_cmdline(self):
    return ['tools/run_tests/performance/run_worker_ruby.sh']

  def worker_port_offset(self):
    return 300

  def scenarios(self):
    # TODO(jtattermusch): add more scenarios
    secargs = None
    yield {
        'name': 'ruby_protobuf_unary_ping_pong',
        'num_servers': 1,
        'num_clients': 1,
        'client_config': {
          'client_type': 'SYNC_CLIENT',
          'security_params': secargs,
          'outstanding_rpcs_per_channel': 1,
          'client_channels': 1,
          'async_client_threads': 1,
          'rpc_type': 'UNARY',
          'load_params': {
            'closed_loop': {}
          },
          'payload_config': EMPTY_PROTO_PAYLOAD,
          'histogram_params': HISTOGRAM_PARAMS,
        },
        'server_config': {
          'server_type': 'SYNC_SERVER',
          'security_params': secargs,
          'core_limit': 0,
          'async_server_threads': 1,
        },
        'warmup_seconds': WARMUP_SECONDS,
        'benchmark_seconds': BENCHMARK_SECONDS
    }

  def __str__(self):
    return 'ruby'


class JavaLanguage:

  def __init__(self):
    pass
    self.safename = str(self)

  def worker_cmdline(self):
    return ['tools/run_tests/performance/run_worker_java.sh']

  def worker_port_offset(self):
    return 400

  def scenarios(self):
    # TODO(jtattermusch): add more scenarios
    secargs = None
    yield {
        'name': 'java_protobuf_unary_ping_pong',
        'num_servers': 1,
        'num_clients': 1,
        'client_config': {
          'client_type': 'SYNC_CLIENT',
          'security_params': secargs,
          'outstanding_rpcs_per_channel': 1,
          'client_channels': 1,
          'async_client_threads': 1,
          'rpc_type': 'UNARY',
          'load_params': {
            'closed_loop': {}
          },
          'payload_config': EMPTY_PROTO_PAYLOAD,
          'histogram_params': HISTOGRAM_PARAMS,
        },
        'server_config': {
          'server_type': 'SYNC_SERVER',
          'security_params': secargs,
          'core_limit': 0,
          'async_server_threads': 1,
        },
        'warmup_seconds': JAVA_WARMUP_SECONDS,
        'benchmark_seconds': BENCHMARK_SECONDS
    }

  def __str__(self):
    return 'java'


LANGUAGES = {
    'c++' : CXXLanguage(),
    'csharp' : CSharpLanguage(),
    'node' : NodeLanguage(),
    'ruby' : RubyLanguage(),
    'java' : JavaLanguage(),
}
