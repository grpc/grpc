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

import math

WARMUP_SECONDS=5
JAVA_WARMUP_SECONDS=15  # Java needs more warmup time for JIT to kick in.
BENCHMARK_SECONDS=30

SMOKETEST='smoketest'
SCALABLE='scalable'
SWEEP='sweep'
DEFAULT_CATEGORIES=[SCALABLE, SMOKETEST]

SECURE_SECARGS = {'use_test_ca': True,
                  'server_host_override': 'foo.test.google.fr'}

HISTOGRAM_PARAMS = {
  'resolution': 0.01,
  'max_possible': 60e9,
}

# target number of RPCs outstanding on across all client channels in
# non-ping-pong tests (since we can only specify per-channel numbers, the
# actual target will be slightly higher)
OUTSTANDING_REQUESTS={
    'async': 6400,
    'sync': 1000
}

# wide is the number of client channels in multi-channel tests (1 otherwise)
WIDE=64


def _get_secargs(is_secure):
  if is_secure:
    return SECURE_SECARGS
  else:
    return None


def remove_nonproto_fields(scenario):
  """Remove special-purpose that contains some extra info about the scenario
  but don't belong to the ScenarioConfig protobuf message"""
  scenario.pop('CATEGORIES', None)
  scenario.pop('CLIENT_LANGUAGE', None)
  scenario.pop('SERVER_LANGUAGE', None)
  scenario.pop('EXCLUDED_POLL_ENGINES', None)
  return scenario


def geometric_progression(start, stop, step):
  n = start
  while n < stop:
    yield int(round(n))
    n *= step


def _payload_type(use_generic_payload, req_size, resp_size):
    r = {}
    sizes = {
      'req_size': req_size,
      'resp_size': resp_size,
    }
    if use_generic_payload:
        r['bytebuf_params'] = sizes
    else:
        r['simple_params'] = sizes
    return r


def _ping_pong_scenario(name, rpc_type,
                        client_type, server_type,
                        secure=True,
                        use_generic_payload=False,
                        req_size=0,
                        resp_size=0,
                        unconstrained_client=None,
                        client_language=None,
                        server_language=None,
                        async_server_threads=0,
                        warmup_seconds=WARMUP_SECONDS,
                        categories=DEFAULT_CATEGORIES,
                        channels=None,
                        outstanding=None,
                        resource_quota_size=None,
                        messages_per_stream=None,
                        excluded_poll_engines=[]):
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
      'async_server_threads': async_server_threads,
    },
    'warmup_seconds': warmup_seconds,
    'benchmark_seconds': BENCHMARK_SECONDS
  }
  if resource_quota_size:
    scenario['server_config']['resource_quota_size'] = resource_quota_size
  if use_generic_payload:
    if server_type != 'ASYNC_GENERIC_SERVER':
      raise Exception('Use ASYNC_GENERIC_SERVER for generic payload.')
    scenario['server_config']['payload_config'] = _payload_type(use_generic_payload, req_size, resp_size)

  scenario['client_config']['payload_config'] = _payload_type(use_generic_payload, req_size, resp_size)

  if unconstrained_client:
    outstanding_calls = outstanding if outstanding is not None else OUTSTANDING_REQUESTS[unconstrained_client]
    # clamp buffer usage to something reasonable (16 gig for now)
    MAX_MEMORY_USE = 16 * 1024 * 1024 * 1024
    if outstanding_calls * max(req_size, resp_size) > MAX_MEMORY_USE:
        outstanding_calls = max(1, MAX_MEMORY_USE / max(req_size, resp_size))
    wide = channels if channels is not None else WIDE
    deep = int(math.ceil(1.0 * outstanding_calls / wide))

    scenario['num_clients'] = 0  # use as many client as available.
    scenario['client_config']['outstanding_rpcs_per_channel'] = deep
    scenario['client_config']['client_channels'] = wide
    scenario['client_config']['async_client_threads'] = 0
  else:
    scenario['client_config']['outstanding_rpcs_per_channel'] = 1
    scenario['client_config']['client_channels'] = 1
    scenario['client_config']['async_client_threads'] = 1

  if messages_per_stream:
    scenario['client_config']['messages_per_stream'] = messages_per_stream
  if client_language:
    # the CLIENT_LANGUAGE field is recognized by run_performance_tests.py
    scenario['CLIENT_LANGUAGE'] = client_language
  if server_language:
    # the SERVER_LANGUAGE field is recognized by run_performance_tests.py
    scenario['SERVER_LANGUAGE'] = server_language
  if categories:
    scenario['CATEGORIES'] = categories
  if len(excluded_poll_engines):
    # The polling engines for which this scenario is excluded
    scenario['EXCLUDED_POLL_ENGINES'] = excluded_poll_engines
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
      smoketest_categories = ([SMOKETEST] if secure else []) + [SCALABLE]

      yield _ping_pong_scenario(
          'cpp_generic_async_streaming_ping_pong_%s' % secstr,
          rpc_type='STREAMING',
          client_type='ASYNC_CLIENT',
          server_type='ASYNC_GENERIC_SERVER',
          use_generic_payload=True, async_server_threads=1,
          secure=secure,
          categories=smoketest_categories)

      yield _ping_pong_scenario(
          'cpp_generic_async_streaming_qps_unconstrained_%s' % secstr,
          rpc_type='STREAMING',
          client_type='ASYNC_CLIENT',
          server_type='ASYNC_GENERIC_SERVER',
          unconstrained_client='async', use_generic_payload=True,
          secure=secure,
          categories=smoketest_categories+[SCALABLE])

      for mps in geometric_progression(1, 20, 10):
        yield _ping_pong_scenario(
            'cpp_generic_async_streaming_qps_unconstrained_%smps_%s' % (mps, secstr),
            rpc_type='STREAMING',
            client_type='ASYNC_CLIENT',
            server_type='ASYNC_GENERIC_SERVER',
            unconstrained_client='async', use_generic_payload=True,
            secure=secure, messages_per_stream=mps,
            categories=smoketest_categories+[SCALABLE])

      for mps in geometric_progression(1, 200, math.sqrt(10)):
        yield _ping_pong_scenario(
            'cpp_generic_async_streaming_qps_unconstrained_%smps_%s' % (mps, secstr),
            rpc_type='STREAMING',
            client_type='ASYNC_CLIENT',
            server_type='ASYNC_GENERIC_SERVER',
            unconstrained_client='async', use_generic_payload=True,
            secure=secure, messages_per_stream=mps,
            categories=[SWEEP])

      yield _ping_pong_scenario(
          'cpp_generic_async_streaming_qps_1channel_1MBmsg_%s' % secstr,
          rpc_type='STREAMING',
          req_size=1024*1024,
          resp_size=1024*1024,
          client_type='ASYNC_CLIENT',
          server_type='ASYNC_GENERIC_SERVER',
          unconstrained_client='async', use_generic_payload=True,
          secure=secure,
          categories=smoketest_categories+[SCALABLE],
          channels=1, outstanding=100)

      yield _ping_pong_scenario(
          'cpp_generic_async_streaming_qps_unconstrained_64KBmsg_%s' % secstr,
          rpc_type='STREAMING',
          req_size=64*1024,
          resp_size=64*1024,
          client_type='ASYNC_CLIENT',
          server_type='ASYNC_GENERIC_SERVER',
          unconstrained_client='async', use_generic_payload=True,
          secure=secure,
          categories=smoketest_categories+[SCALABLE])

      yield _ping_pong_scenario(
          'cpp_generic_async_streaming_qps_one_server_core_%s' % secstr,
          rpc_type='STREAMING',
          client_type='ASYNC_CLIENT',
          server_type='ASYNC_GENERIC_SERVER',
          unconstrained_client='async', use_generic_payload=True,
          async_server_threads=1,
          secure=secure)

      yield _ping_pong_scenario(
          'cpp_protobuf_async_client_sync_server_unary_qps_unconstrained_%s' %
          (secstr),
          rpc_type='UNARY',
          client_type='ASYNC_CLIENT',
          server_type='SYNC_SERVER',
          unconstrained_client='async',
          secure=secure,
          categories=smoketest_categories + [SCALABLE],
          excluded_poll_engines = ['poll-cv'])

      yield _ping_pong_scenario(
          'cpp_protobuf_async_client_unary_1channel_64wide_128Breq_8MBresp_%s' %
          (secstr),
          rpc_type='UNARY',
          client_type='ASYNC_CLIENT',
          server_type='ASYNC_SERVER',
          channels=1,
          outstanding=64,
          req_size=128,
          resp_size=8*1024*1024,
          secure=secure,
          categories=smoketest_categories + [SCALABLE])

      yield _ping_pong_scenario(
          'cpp_protobuf_async_client_sync_server_streaming_qps_unconstrained_%s' % secstr,
          rpc_type='STREAMING',
          client_type='ASYNC_CLIENT',
          server_type='SYNC_SERVER',
          unconstrained_client='async',
          secure=secure,
          categories=smoketest_categories+[SCALABLE],
          excluded_poll_engines = ['poll-cv'])

      yield _ping_pong_scenario(
        'cpp_protobuf_async_unary_ping_pong_%s_1MB' % secstr, rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        req_size=1024*1024, resp_size=1024*1024,
        secure=secure,
        categories=smoketest_categories + [SCALABLE])

      for rpc_type in ['unary', 'streaming']:
        for synchronicity in ['sync', 'async']:
          yield _ping_pong_scenario(
              'cpp_protobuf_%s_%s_ping_pong_%s' % (synchronicity, rpc_type, secstr),
              rpc_type=rpc_type.upper(),
              client_type='%s_CLIENT' % synchronicity.upper(),
              server_type='%s_SERVER' % synchronicity.upper(),
              async_server_threads=1,
              secure=secure)

          for size in geometric_progression(1, 1024*1024*1024+1, 8):
              yield _ping_pong_scenario(
                  'cpp_protobuf_%s_%s_qps_unconstrained_%s_%db' % (synchronicity, rpc_type, secstr, size),
                  rpc_type=rpc_type.upper(),
                  req_size=size,
                  resp_size=size,
                  client_type='%s_CLIENT' % synchronicity.upper(),
                  server_type='%s_SERVER' % synchronicity.upper(),
                  unconstrained_client=synchronicity,
                  secure=secure,
                  categories=[SWEEP])

          yield _ping_pong_scenario(
              'cpp_protobuf_%s_%s_qps_unconstrained_%s' % (synchronicity, rpc_type, secstr),
              rpc_type=rpc_type.upper(),
              client_type='%s_CLIENT' % synchronicity.upper(),
              server_type='%s_SERVER' % synchronicity.upper(),
              unconstrained_client=synchronicity,
              secure=secure,
              categories=smoketest_categories+[SCALABLE])

          # TODO(vjpai): Re-enable this test. It has a lot of timeouts
          # and hasn't yet been conclusively identified as a test failure
          # or race in the library
          # yield _ping_pong_scenario(
          #     'cpp_protobuf_%s_%s_qps_unconstrained_%s_500kib_resource_quota' % (synchronicity, rpc_type, secstr),
          #     rpc_type=rpc_type.upper(),
          #     client_type='%s_CLIENT' % synchronicity.upper(),
          #     server_type='%s_SERVER' % synchronicity.upper(),
          #     unconstrained_client=synchronicity,
          #     secure=secure,
          #     categories=smoketest_categories+[SCALABLE],
          #     resource_quota_size=500*1024)

          if rpc_type == 'streaming':
            for mps in geometric_progression(1, 20, 10):
              yield _ping_pong_scenario(
                  'cpp_protobuf_%s_%s_qps_unconstrained_%smps_%s' % (synchronicity, rpc_type, mps, secstr),
                  rpc_type=rpc_type.upper(),
                  client_type='%s_CLIENT' % synchronicity.upper(),
                  server_type='%s_SERVER' % synchronicity.upper(),
                  unconstrained_client=synchronicity,
                  secure=secure, messages_per_stream=mps,
                  categories=smoketest_categories+[SCALABLE])

            for mps in geometric_progression(1, 200, math.sqrt(10)):
              yield _ping_pong_scenario(
                  'cpp_protobuf_%s_%s_qps_unconstrained_%smps_%s' % (synchronicity, rpc_type, mps, secstr),
                  rpc_type=rpc_type.upper(),
                  client_type='%s_CLIENT' % synchronicity.upper(),
                  server_type='%s_SERVER' % synchronicity.upper(),
                  unconstrained_client=synchronicity,
                  secure=secure, messages_per_stream=mps,
                  categories=[SWEEP])

          for channels in geometric_progression(1, 20000, math.sqrt(10)):
            for outstanding in geometric_progression(1, 200000, math.sqrt(10)):
                if synchronicity == 'sync' and outstanding > 1200: continue
                if outstanding < channels: continue
                yield _ping_pong_scenario(
                    'cpp_protobuf_%s_%s_qps_unconstrained_%s_%d_channels_%d_outstanding' % (synchronicity, rpc_type, secstr, channels, outstanding),
                    rpc_type=rpc_type.upper(),
                    client_type='%s_CLIENT' % synchronicity.upper(),
                    server_type='%s_SERVER' % synchronicity.upper(),
                    unconstrained_client=synchronicity, secure=secure,
                    categories=[SWEEP], channels=channels, outstanding=outstanding)

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
        use_generic_payload=True,
        categories=[SMOKETEST, SCALABLE])

    yield _ping_pong_scenario(
        'csharp_protobuf_async_streaming_ping_pong', rpc_type='STREAMING',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER')

    yield _ping_pong_scenario(
        'csharp_protobuf_async_unary_ping_pong', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        categories=[SMOKETEST, SCALABLE])

    yield _ping_pong_scenario(
        'csharp_protobuf_sync_to_async_unary_ping_pong', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='ASYNC_SERVER')

    yield _ping_pong_scenario(
        'csharp_protobuf_async_unary_qps_unconstrained', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        unconstrained_client='async',
        categories=[SMOKETEST,SCALABLE])

    yield _ping_pong_scenario(
        'csharp_protobuf_async_streaming_qps_unconstrained', rpc_type='STREAMING',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        unconstrained_client='async',
        categories=[SCALABLE])

    yield _ping_pong_scenario(
        'csharp_to_cpp_protobuf_sync_unary_ping_pong', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
        server_language='c++', async_server_threads=1,
        categories=[SMOKETEST, SCALABLE])

    yield _ping_pong_scenario(
        'csharp_to_cpp_protobuf_async_streaming_ping_pong', rpc_type='STREAMING',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        server_language='c++', async_server_threads=1)

    yield _ping_pong_scenario(
        'csharp_to_cpp_protobuf_async_unary_qps_unconstrained', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        unconstrained_client='async', server_language='c++',
        categories=[SCALABLE])

    yield _ping_pong_scenario(
        'csharp_to_cpp_protobuf_sync_to_async_unary_qps_unconstrained', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='ASYNC_SERVER',
        unconstrained_client='sync', server_language='c++',
        categories=[SCALABLE])

    yield _ping_pong_scenario(
        'cpp_to_csharp_protobuf_async_unary_qps_unconstrained', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        unconstrained_client='async', client_language='c++',
        categories=[SCALABLE])

    yield _ping_pong_scenario(
        'csharp_protobuf_async_unary_ping_pong_1MB', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        req_size=1024*1024, resp_size=1024*1024,
        categories=[SMOKETEST, SCALABLE])


  def __str__(self):
    return 'csharp'


class NodeLanguage:

  def __init__(self):
    pass
    self.safename = str(self)

  def worker_cmdline(self):
    return ['tools/run_tests/performance/run_worker_node.sh',
            '--benchmark_impl=grpc']

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
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        categories=[SCALABLE, SMOKETEST])

    yield _ping_pong_scenario(
        'cpp_to_node_unary_ping_pong', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        client_language='c++')

    yield _ping_pong_scenario(
        'node_protobuf_unary_ping_pong_1MB', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        req_size=1024*1024, resp_size=1024*1024,
        categories=[SCALABLE])

    sizes = [('1B', 1), ('1KB', 1024), ('10KB', 10 * 1024),
             ('1MB', 1024 * 1024), ('10MB', 10 * 1024 * 1024),
             ('100MB', 100 * 1024 * 1024)]

    for size_name, size in sizes:
      for secure in (True, False):
        yield _ping_pong_scenario(
            'node_protobuf_unary_ping_pong_%s_resp_%s' %
            (size_name, 'secure' if secure else 'insecure'),
            rpc_type='UNARY',
            client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
            req_size=0, resp_size=size,
            secure=secure,
            categories=[SCALABLE])

    # TODO(murgatroid99): fix bugs with this scenario and re-enable it
    # yield _ping_pong_scenario(
    #     'node_protobuf_async_unary_qps_unconstrained', rpc_type='UNARY',
    #     client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
    #     unconstrained_client='async',
    #     categories=[SCALABLE, SMOKETEST])

    # TODO(jtattermusch): make this scenario work
    #yield _ping_pong_scenario(
    #    'node_protobuf_async_streaming_qps_unconstrained', rpc_type='STREAMING',
    #    client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
    #    unconstrained_client='async')

    yield _ping_pong_scenario(
        'node_to_cpp_protobuf_async_unary_ping_pong', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        server_language='c++', async_server_threads=1)

    # TODO(jtattermusch): make this scenario work
    #yield _ping_pong_scenario(
    #    'node_to_cpp_protobuf_async_streaming_ping_pong', rpc_type='STREAMING',
    #    client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
    #    server_language='c++', async_server_threads=1)

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
    yield _ping_pong_scenario(
        'python_generic_sync_streaming_ping_pong', rpc_type='STREAMING',
        client_type='SYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
        use_generic_payload=True,
        categories=[SMOKETEST, SCALABLE])

    yield _ping_pong_scenario(
        'python_protobuf_sync_streaming_ping_pong', rpc_type='STREAMING',
        client_type='SYNC_CLIENT', server_type='ASYNC_SERVER')

    yield _ping_pong_scenario(
        'python_protobuf_async_unary_ping_pong', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER')

    yield _ping_pong_scenario(
        'python_protobuf_sync_unary_ping_pong', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='ASYNC_SERVER',
        categories=[SMOKETEST, SCALABLE])

    yield _ping_pong_scenario(
        'python_protobuf_sync_unary_qps_unconstrained', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='ASYNC_SERVER',
        unconstrained_client='sync')

    yield _ping_pong_scenario(
        'python_protobuf_sync_streaming_qps_unconstrained', rpc_type='STREAMING',
        client_type='SYNC_CLIENT', server_type='ASYNC_SERVER',
        unconstrained_client='sync')

    yield _ping_pong_scenario(
        'python_to_cpp_protobuf_sync_unary_ping_pong', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='ASYNC_SERVER',
        server_language='c++', async_server_threads=1,
        categories=[SMOKETEST, SCALABLE])

    yield _ping_pong_scenario(
        'python_to_cpp_protobuf_sync_streaming_ping_pong', rpc_type='STREAMING',
        client_type='SYNC_CLIENT', server_type='ASYNC_SERVER',
        server_language='c++', async_server_threads=1)

    yield _ping_pong_scenario(
        'python_protobuf_sync_unary_ping_pong_1MB', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='ASYNC_SERVER',
        req_size=1024*1024, resp_size=1024*1024,
        categories=[SMOKETEST, SCALABLE])

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
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
        categories=[SMOKETEST, SCALABLE])

    yield _ping_pong_scenario(
        'ruby_protobuf_unary_ping_pong', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
        categories=[SMOKETEST, SCALABLE])

    yield _ping_pong_scenario(
        'ruby_protobuf_sync_unary_qps_unconstrained', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
        unconstrained_client='sync')

    yield _ping_pong_scenario(
        'ruby_protobuf_sync_streaming_qps_unconstrained', rpc_type='STREAMING',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
        unconstrained_client='sync')

    yield _ping_pong_scenario(
        'ruby_to_cpp_protobuf_sync_unary_ping_pong', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
        server_language='c++', async_server_threads=1)

    yield _ping_pong_scenario(
        'ruby_to_cpp_protobuf_sync_streaming_ping_pong', rpc_type='STREAMING',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
        server_language='c++', async_server_threads=1)

    yield _ping_pong_scenario(
        'ruby_protobuf_unary_ping_pong_1MB', rpc_type='UNARY',
        client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
        req_size=1024*1024, resp_size=1024*1024,
        categories=[SMOKETEST, SCALABLE])

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
      smoketest_categories = ([SMOKETEST] if secure else []) + [SCALABLE]

      yield _ping_pong_scenario(
          'java_generic_async_streaming_ping_pong_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
          use_generic_payload=True, async_server_threads=1,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS,
          categories=smoketest_categories)

      yield _ping_pong_scenario(
          'java_protobuf_async_streaming_ping_pong_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
          async_server_threads=1,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS)

      yield _ping_pong_scenario(
          'java_protobuf_async_unary_ping_pong_%s' % secstr, rpc_type='UNARY',
          client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
          async_server_threads=1,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS,
          categories=smoketest_categories)

      yield _ping_pong_scenario(
          'java_protobuf_unary_ping_pong_%s' % secstr, rpc_type='UNARY',
          client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
          async_server_threads=1,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS)

      yield _ping_pong_scenario(
          'java_protobuf_async_unary_qps_unconstrained_%s' % secstr, rpc_type='UNARY',
          client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
          unconstrained_client='async',
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS,
          categories=smoketest_categories+[SCALABLE])

      yield _ping_pong_scenario(
          'java_protobuf_async_streaming_qps_unconstrained_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
          unconstrained_client='async',
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS,
          categories=[SCALABLE])

      yield _ping_pong_scenario(
          'java_generic_async_streaming_qps_unconstrained_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
          unconstrained_client='async', use_generic_payload=True,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS,
          categories=[SCALABLE])

      yield _ping_pong_scenario(
          'java_generic_async_streaming_qps_one_server_core_%s' % secstr, rpc_type='STREAMING',
          client_type='ASYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
          unconstrained_client='async', use_generic_payload=True,
          async_server_threads=1,
          secure=secure, warmup_seconds=JAVA_WARMUP_SECONDS)

      # TODO(jtattermusch): add scenarios java vs C++

  def __str__(self):
    return 'java'


class GoLanguage:

  def __init__(self):
    pass
    self.safename = str(self)

  def worker_cmdline(self):
    return ['tools/run_tests/performance/run_worker_go.sh']

  def worker_port_offset(self):
    return 600

  def scenarios(self):
    for secure in [True, False]:
      secstr = 'secure' if secure else 'insecure'
      smoketest_categories = ([SMOKETEST] if secure else []) + [SCALABLE]

      # ASYNC_GENERIC_SERVER for Go actually uses a sync streaming server,
      # but that's mostly because of lack of better name of the enum value.
      yield _ping_pong_scenario(
          'go_generic_sync_streaming_ping_pong_%s' % secstr, rpc_type='STREAMING',
          client_type='SYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
          use_generic_payload=True, async_server_threads=1,
          secure=secure,
          categories=smoketest_categories)

      yield _ping_pong_scenario(
          'go_protobuf_sync_streaming_ping_pong_%s' % secstr, rpc_type='STREAMING',
          client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
          async_server_threads=1,
          secure=secure)

      yield _ping_pong_scenario(
          'go_protobuf_sync_unary_ping_pong_%s' % secstr, rpc_type='UNARY',
          client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
          async_server_threads=1,
          secure=secure,
          categories=smoketest_categories)

      # unconstrained_client='async' is intended (client uses goroutines)
      yield _ping_pong_scenario(
          'go_protobuf_sync_unary_qps_unconstrained_%s' % secstr, rpc_type='UNARY',
          client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
          unconstrained_client='async',
          secure=secure,
          categories=smoketest_categories+[SCALABLE])

      # unconstrained_client='async' is intended (client uses goroutines)
      yield _ping_pong_scenario(
          'go_protobuf_sync_streaming_qps_unconstrained_%s' % secstr, rpc_type='STREAMING',
          client_type='SYNC_CLIENT', server_type='SYNC_SERVER',
          unconstrained_client='async',
          secure=secure,
          categories=[SCALABLE])

      # unconstrained_client='async' is intended (client uses goroutines)
      # ASYNC_GENERIC_SERVER for Go actually uses a sync streaming server,
      # but that's mostly because of lack of better name of the enum value.
      yield _ping_pong_scenario(
          'go_generic_sync_streaming_qps_unconstrained_%s' % secstr, rpc_type='STREAMING',
          client_type='SYNC_CLIENT', server_type='ASYNC_GENERIC_SERVER',
          unconstrained_client='async', use_generic_payload=True,
          secure=secure,
          categories=[SCALABLE])

      # TODO(jtattermusch): add scenarios go vs C++

  def __str__(self):
    return 'go'

class NodeExpressLanguage:

  def __init__(self):
    pass
    self.safename = str(self)

  def worker_cmdline(self):
    return ['tools/run_tests/performance/run_worker_node.sh',
            '--benchmark_impl=express']

  def worker_port_offset(self):
    return 700

  def scenarios(self):
    yield _ping_pong_scenario(
        'node_express_json_unary_ping_pong', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        categories=[SCALABLE, SMOKETEST])

    yield _ping_pong_scenario(
        'node_express_json_async_unary_qps_unconstrained', rpc_type='UNARY',
        client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
        unconstrained_client='async',
        categories=[SCALABLE, SMOKETEST])

    sizes = [('1B', 1), ('1KB', 1024), ('10KB', 10 * 1024),
             ('1MB', 1024 * 1024), ('10MB', 10 * 1024 * 1024),
             ('100MB', 100 * 1024 * 1024)]

    for size_name, size in sizes:
      for secure in (True, False):
        yield _ping_pong_scenario(
            'node_express_json_unary_ping_pong_%s_resp_%s' %
            (size_name, 'secure' if secure else 'insecure'),
            rpc_type='UNARY',
            client_type='ASYNC_CLIENT', server_type='ASYNC_SERVER',
            req_size=0, resp_size=size,
            secure=secure,
            categories=[SCALABLE])

  def __str__(self):
    return 'node_express'


LANGUAGES = {
    'c++' : CXXLanguage(),
    'csharp' : CSharpLanguage(),
    'node' : NodeLanguage(),
    'node_express': NodeExpressLanguage(),
    'ruby' : RubyLanguage(),
    'java' : JavaLanguage(),
    'python' : PythonLanguage(),
    'go' : GoLanguage(),
}
