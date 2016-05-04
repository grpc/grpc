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

SECURE_SECARGS = {'use_test_ca': True,
                  'server_host_override': 'foo.test.google.fr'}

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


def _get_secargs(is_secure):
  if is_secure:
    return SECURE_SECARGS
  else:
    return None


def _ping_pong_scenario(name, rpc_type,
                        client_type, server_type,
                        secure=True,
                        use_generic_payload=False,
                        use_unconstrained_client=False,
                        server_language=None,
                        server_core_limit=0,
                        async_server_threads=0,
                        warmup_seconds=WARMUP_SECONDS):
  """Creates a basic ping pong scenario."""
  scenario = {
    'name': name,
    'num_servers': 1,
    'num_clients': 1,
    'client_config': {
      'client_type': client_type,
      'security_params': _get_secargs(secure),
      'outstanding_rpcs_per_channel': 1,
      'client_channels': 1,
      'async_client_threads': 1,
      'rpc_type': rpc_type,
      'load_params': {
        'closed_loop': {}
      },
      'histogram_params': HISTOGRAM_PARAMS,
    },
    'server_config': {
      'server_type': server_type,
      'security_params': _get_secargs(secure),
      'core_limit': server_core_limit,
      'async_server_threads': async_server_threads,
    },
    'warmup_seconds': warmup_seconds,
    'benchmark_seconds': BENCHMARK_SECONDS
  }
  if use_generic_payload:
    if server_type != 'ASYNC_GENERIC_SERVER':
      raise Exception('Use ASYNC_GENERIC_SERVER for generic payload.')
    scenario['client_config']['payload_config'] = EMPTY_GENERIC_PAYLOAD
    scenario['server_config']['payload_config'] = EMPTY_GENERIC_PAYLOAD
  else:
    # For proto payload, only the client should get the config.
    scenario['client_config']['payload_config'] = EMPTY_PROTO_PAYLOAD

  if use_unconstrained_client:
    scenario['num_clients'] = 0  # use as many client as available.
    # TODO(jtattermusch): for SYNC_CLIENT, this will create 100*64 threads
    # and that's probably too much (at least for wrapped languages).
    scenario['client_config']['outstanding_rpcs_per_channel'] = DEEP
    scenario['client_config']['client_channels'] = WIDE
    scenario['client_config']['async_client_threads'] = 0
  else:
    scenario['client_config']['outstanding_rpcs_per_channel'] = 1
    scenario['client_config']['client_channels'] = 1
    scenario['client_config']['async_client_threads'] = 1

  if server_language:
    # the SERVER_LANGUAGE field is recognized by run_performance_tests.py
    scenario['SERVER_LANGUAGE'] = server_language
  return scenario


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
      secstr = 'secure' if secure else 'insecure'

      yield _ping_pong_scenario(
          'cpp_generic_async_streaming_ping_pong_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
          use_generic_payload=True, server_core_limit=1, async_server_threads=1,
          secure=secure)

      yield _ping_pong_scenario(
          'cpp_protobuf_async_streaming_ping_pong_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
          server_core_limit=1, async_server_threads=1,
          secure=secure)

      yield _ping_pong_scenario(
          'cpp_protobuf_async_unary_ping_pong_%s' % secstr, rpc_type='UNARY',
          client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
          server_core_limit=1, async_server_threads=1,
          secure=secure)

      yield _ping_pong_scenario(
          'cpp_protobuf_sync_unary_ping_pong_%s' % secstr, rpc_type='UNARY',
          client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
          server_core_limit=1, async_server_threads=1,
          secure=secure)

      yield _ping_pong_scenario(
          'cpp_protobuf_async_unary_qps_unconstrained_%s' % secstr, rpc_type='UNARY',
          client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
          server_core_limit=SINGLE_MACHINE_CORES/2,
          use_unconstrained_client=True,
          secure=secure)

      yield _ping_pong_scenario(
          'cpp_protobuf_async_streaming_qps_unconstrained_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
          server_core_limit=SINGLE_MACHINE_CORES/2,
          use_unconstrained_client=True,
          secure=secure)

      yield _ping_pong_scenario(
          'cpp_generic_async_streaming_qps_unconstrained_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
          use_unconstrained_client=True, use_generic_payload=True,
          server_core_limit=SINGLE_MACHINE_CORES/2,
          secure=secure)

      yield _ping_pong_scenario(
          'cpp_generic_async_streaming_qps_one_server_core_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
          use_unconstrained_client=True, use_generic_payload=True,
          server_core_limit=1, async_server_threads=1,
          secure=secure)

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
    yield _ping_pong_scenario(
        'csharp_generic_async_streaming_ping_pong', rpc_type='STREAMING',
        client_type='ASYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
        use_generic_payload=True)

    yield _ping_pong_scenario(
        'csharp_protobuf_async_streaming_ping_pong', rpc_type='STREAMING',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER')

    yield _ping_pong_scenario(
        'csharp_protobuf_async_unary_ping_pong', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER')

    yield _ping_pong_scenario(
        'csharp_protobuf_sync_to_async_unary_ping_pong', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='ASYNC_SERVER')

    yield _ping_pong_scenario(
        'csharp_protobuf_async_unary_qps_unconstrained', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        use_unconstrained_client=True)

    yield _ping_pong_scenario(
        'csharp_protobuf_async_streaming_qps_unconstrained', rpc_type='STREAMING',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        use_unconstrained_client=True)

    yield _ping_pong_scenario(
        'csharp_to_cpp_protobuf_sync_unary_ping_pong', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
        server_language='c++', server_core_limit=1, async_server_threads=1)

    yield _ping_pong_scenario(
        'csharp_to_cpp_protobuf_async_streaming_ping_pong', rpc_type='STREAMING',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        server_language='c++', server_core_limit=1, async_server_threads=1)

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
    # TODO(jtattermusch): make this scenario work
    #yield _ping_pong_scenario(
    #    'node_generic_async_streaming_ping_pong', rpc_type='STREAMING',
    #    client_type='ASYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
    #    use_generic_payload=True)

    # TODO(jtattermusch): make this scenario work
    #yield _ping_pong_scenario(
    #    'node_protobuf_async_streaming_ping_pong', rpc_type='STREAMING',
    #    client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER')

    yield _ping_pong_scenario(
        'node_protobuf_unary_ping_pong', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER')

    yield _ping_pong_scenario(
        'node_protobuf_async_unary_qps_unconstrained', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        use_unconstrained_client=True)

    # TODO(jtattermusch): make this scenario work
    #yield _ping_pong_scenario(
    #    'node_protobuf_async_streaming_qps_unconstrained', rpc_type='STREAMING',
    #    client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
    #    use_unconstrained_client=True)

    # TODO(jtattermusch): make this scenario work
    #yield _ping_pong_scenario(
    #    'node_to_cpp_protobuf_async_unary_ping_pong', rpc_type='UNARY',
    #    client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
    #    server_language='c++', server_core_limit=1, async_server_threads=1)

    # TODO(jtattermusch): make this scenario work
    #yield _ping_pong_scenario(
    #    'node_to_cpp_protobuf_async_streaming_ping_pong', rpc_type='STREAMING',
    #    client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
    #    server_language='c++', server_core_limit=1, async_server_threads=1)

  def __str__(self):
    return 'node'

class PythonLanguage:

  def __init__(self):
    self.safename = 'python'

  def worker_cmdline(self):
    return ['tools/run_tests/performance/run_worker_python.sh']

  def worker_port_offset(self):
    return 500

  def scenarios(self):
    # TODO(jtattermusch): this scenario reports QPS 0.0
    yield _ping_pong_scenario(
        'python_generic_async_streaming_ping_pong', rpc_type='STREAMING',
        client_type='ASYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
        use_generic_payload=True)

    # TODO(jtattermusch): make this scenario work
    #yield _ping_pong_scenario(
    #    'python_protobuf_async_streaming_ping_pong', rpc_type='STREAMING',
    #    client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER')

    # TODO(jtattermusch): make this scenario work
    #yield _ping_pong_scenario(
    #    'python_protobuf_async_unary_ping_pong', rpc_type='UNARY',
    #    client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER')

    yield _ping_pong_scenario(
        'python_protobuf_sync_unary_ping_pong', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER')

    # TODO(jtattermusch): make this scenario work
    #yield _ping_pong_scenario(
    #    'python_protobuf_sync_unary_qps_unconstrained', rpc_type='UNARY',
    #    client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
    #    use_unconstrained_client=True)

    # TODO(jtattermusch): make this scenario work
    #yield _ping_pong_scenario(
    #    'python_protobuf_async_streaming_qps_unconstrained', rpc_type='STREAMING',
    #    client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
    #    use_unconstrained_client=True)

    yield _ping_pong_scenario(
        'python_to_cpp_protobuf_sync_unary_ping_pong', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
        server_language='c++', server_core_limit=1, async_server_threads=1)

    # TODO(jtattermusch): make this scenario work
    #yield _ping_pong_scenario(
    #    'python_to_cpp_protobuf_sync_streaming_ping_pong', rpc_type='STREAMING',
    #    client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
    #    server_language='c++', server_core_limit=1, async_server_threads=1)

  def __str__(self):
    return 'python'

class RubyLanguage:

  def __init__(self):
    pass
    self.safename = str(self)

  def worker_cmdline(self):
    return ['tools/run_tests/performance/run_worker_ruby.sh']

  def worker_port_offset(self):
    return 300

  def scenarios(self):
    yield _ping_pong_scenario(
        'ruby_protobuf_sync_streaming_ping_pong', rpc_type='STREAMING',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER')

    yield _ping_pong_scenario(
        'ruby_protobuf_unary_ping_pong', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER')

    # TODO: scenario reports QPS of 0.0
    #yield _ping_pong_scenario(
    #    'ruby_protobuf_sync_unary_qps_unconstrained', rpc_type='UNARY',
    #    client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
    #    use_unconstrained_client=True)

    # TODO: scenario reports QPS of 0.0
    #yield _ping_pong_scenario(
    #    'ruby_protobuf_sync_streaming_qps_unconstrained', rpc_type='STREAMING',
    #    client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
    #    use_unconstrained_client=True)

    yield _ping_pong_scenario(
        'ruby_to_cpp_protobuf_sync_unary_ping_pong', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
        server_language='c++', server_core_limit=1, async_server_threads=1)

    yield _ping_pong_scenario(
        'ruby_to_cpp_protobuf_sync_streaming_ping_pong', rpc_type='STREAMING',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
        server_language='c++', server_core_limit=1, async_server_threads=1)

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
    for secure in [True, False]:
      secstr = 'secure' if secure else 'insecure'

      yield _ping_pong_scenario(
          'java_generic_async_streaming_ping_pong_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
          use_generic_payload=True, async_server_threads=1,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS)

      yield _ping_pong_scenario(
          'java_protobuf_async_streaming_ping_pong_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
          async_server_threads=1,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS)

      yield _ping_pong_scenario(
          'java_protobuf_async_unary_ping_pong_%s' % secstr, rpc_type='UNARY',
          client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
          async_server_threads=1,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS)

      yield _ping_pong_scenario(
          'java_protobuf_unary_ping_pong_%s' % secstr, rpc_type='UNARY',
          client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
          async_server_threads=1,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS)

      yield _ping_pong_scenario(
          'java_protobuf_async_unary_qps_unconstrained_%s' % secstr, rpc_type='UNARY',
          client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
          use_unconstrained_client=True,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS)

      yield _ping_pong_scenario(
          'java_protobuf_async_streaming_qps_unconstrained_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
          use_unconstrained_client=True,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS)

      yield _ping_pong_scenario(
          'java_generic_async_streaming_qps_unconstrained_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
          use_unconstrained_client=True, use_generic_payload=True,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS)

      yield _ping_pong_scenario(
          'java_generic_async_streaming_qps_one_server_core_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
          use_unconstrained_client=True, use_generic_payload=True,
          async_server_threads=1,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS)

      # TODO(jtattermusch): add scenarios java vs C++ 

  def __str__(self):
    return 'java'


LANGUAGES = {
    'c++' : CXXLanguage(),
    'csharp' : CSharpLanguage(),
    'node' : NodeLanguage(),
    'ruby' : RubyLanguage(),
    'java' : JavaLanguage(),
    'python' : PythonLanguage(),
}
