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
"""Generates the appropriate build.json data for all the end2end tests."""

from __future__ import print_function

import yaml
import collections
import hashlib

FixtureOptions = collections.namedtuple(
    'FixtureOptions',
    'fullstack includes_proxy dns_resolver name_resolution secure platforms ci_mac tracing exclude_configs exclude_iomgrs large_writes enables_compression supports_compression is_inproc is_http2 supports_proxy_auth supports_write_buffering client_channel'
)
default_unsecure_fixture_options = FixtureOptions(
    True, False, True, True, False, ['windows', 'linux', 'mac', 'posix'], True,
    False, [], [], True, False, True, False, True, False, True, True)
socketpair_unsecure_fixture_options = default_unsecure_fixture_options._replace(
    fullstack=False, dns_resolver=False, client_channel=False)
default_secure_fixture_options = default_unsecure_fixture_options._replace(
    secure=True)
uds_fixture_options = default_unsecure_fixture_options._replace(
    dns_resolver=False,
    platforms=['linux', 'mac', 'posix'],
    exclude_iomgrs=['uv'])
local_fixture_options = default_secure_fixture_options._replace(
    dns_resolver=False,
    platforms=['linux', 'mac', 'posix'],
    exclude_iomgrs=['uv'])
fd_unsecure_fixture_options = default_unsecure_fixture_options._replace(
    dns_resolver=False,
    fullstack=False,
    platforms=['linux', 'mac', 'posix'],
    exclude_iomgrs=['uv'],
    client_channel=False)
inproc_fixture_options = default_secure_fixture_options._replace(
    dns_resolver=False,
    fullstack=False,
    name_resolution=False,
    supports_compression=False,
    is_inproc=True,
    is_http2=False,
    supports_write_buffering=False,
    client_channel=False)

# maps fixture name to whether it requires the security library
END2END_FIXTURES = {
    'h2_compress':
        default_unsecure_fixture_options._replace(enables_compression=True),
    'h2_census':
        default_unsecure_fixture_options,
    # This cmake target is disabled for now because it depends on OpenCensus,
    # which is Bazel-only.
    # 'h2_load_reporting': default_unsecure_fixture_options,
    'h2_fakesec':
        default_secure_fixture_options._replace(ci_mac=False),
    'h2_fd':
        fd_unsecure_fixture_options,
    'h2_full':
        default_unsecure_fixture_options,
    'h2_full+pipe':
        default_unsecure_fixture_options._replace(platforms=['linux'],
                                                  exclude_iomgrs=['uv']),
    'h2_full+trace':
        default_unsecure_fixture_options._replace(tracing=True),
    'h2_full+workarounds':
        default_unsecure_fixture_options,
    'h2_http_proxy':
        default_unsecure_fixture_options._replace(ci_mac=False,
                                                  exclude_iomgrs=['uv'],
                                                  supports_proxy_auth=True),
    'h2_oauth2':
        default_secure_fixture_options._replace(ci_mac=False,
                                                exclude_iomgrs=['uv']),
    'h2_proxy':
        default_unsecure_fixture_options._replace(includes_proxy=True,
                                                  ci_mac=False,
                                                  exclude_iomgrs=['uv']),
    'h2_sockpair_1byte':
        socketpair_unsecure_fixture_options._replace(ci_mac=False,
                                                     exclude_configs=['msan'],
                                                     large_writes=False,
                                                     exclude_iomgrs=['uv']),
    'h2_sockpair':
        socketpair_unsecure_fixture_options._replace(ci_mac=False,
                                                     exclude_iomgrs=['uv']),
    'h2_sockpair+trace':
        socketpair_unsecure_fixture_options._replace(ci_mac=False,
                                                     tracing=True,
                                                     large_writes=False,
                                                     exclude_iomgrs=['uv']),
    'h2_ssl':
        default_secure_fixture_options,
    'h2_ssl_cred_reload':
        default_secure_fixture_options,
    'h2_tls':
        default_secure_fixture_options,
    'h2_local_uds':
        local_fixture_options,
    'h2_local_ipv4':
        local_fixture_options,
    'h2_local_ipv6':
        local_fixture_options,
    'h2_ssl_proxy':
        default_secure_fixture_options._replace(includes_proxy=True,
                                                ci_mac=False,
                                                exclude_iomgrs=['uv']),
    'h2_uds':
        uds_fixture_options,
    'inproc':
        inproc_fixture_options
}

TestOptions = collections.namedtuple(
    'TestOptions',
    'needs_fullstack needs_dns needs_names proxyable secure traceable cpu_cost exclude_iomgrs large_writes flaky allows_compression needs_compression exclude_inproc needs_http2 needs_proxy_auth needs_write_buffering needs_client_channel'
)
default_test_options = TestOptions(False, False, False, True, False, True, 1.0,
                                   [], False, False, True, False, False, False,
                                   False, False, False)
connectivity_test_options = default_test_options._replace(needs_fullstack=True)

LOWCPU = 0.1

# maps test names to options
END2END_TESTS = {
    'authority_not_supported':
        default_test_options,
    'bad_hostname':
        default_test_options._replace(needs_names=True),
    'bad_ping':
        connectivity_test_options._replace(proxyable=False),
    'binary_metadata':
        default_test_options._replace(cpu_cost=LOWCPU),
    'resource_quota_server':
        default_test_options._replace(large_writes=True,
                                      proxyable=False,
                                      allows_compression=False),
    'call_creds':
        default_test_options._replace(secure=True),
    'cancel_after_accept':
        default_test_options._replace(cpu_cost=LOWCPU),
    'cancel_after_client_done':
        default_test_options._replace(cpu_cost=LOWCPU),
    'cancel_after_invoke':
        default_test_options._replace(cpu_cost=LOWCPU),
    'cancel_after_round_trip':
        default_test_options._replace(cpu_cost=LOWCPU),
    'cancel_before_invoke':
        default_test_options._replace(cpu_cost=LOWCPU),
    'cancel_in_a_vacuum':
        default_test_options._replace(cpu_cost=LOWCPU),
    'cancel_with_status':
        default_test_options._replace(cpu_cost=LOWCPU),
    'compressed_payload':
        default_test_options._replace(proxyable=False, needs_compression=True),
    'connectivity':
        connectivity_test_options._replace(needs_names=True,
                                           proxyable=False,
                                           cpu_cost=LOWCPU,
                                           exclude_iomgrs=['uv']),
    'channelz':
        default_test_options,
    'default_host':
        default_test_options._replace(needs_fullstack=True,
                                      needs_dns=True,
                                      needs_names=True),
    'call_host_override':
        default_test_options._replace(needs_fullstack=True,
                                      needs_dns=True,
                                      needs_names=True),
    'disappearing_server':
        connectivity_test_options._replace(flaky=True, needs_names=True),
    'empty_batch':
        default_test_options._replace(cpu_cost=LOWCPU),
    'filter_causes_close':
        default_test_options._replace(cpu_cost=LOWCPU),
    'filter_call_init_fails':
        default_test_options,
    'filter_context':
        default_test_options,
    'filter_latency':
        default_test_options._replace(cpu_cost=LOWCPU),
    'filter_status_code':
        default_test_options._replace(cpu_cost=LOWCPU),
    'graceful_server_shutdown':
        default_test_options._replace(cpu_cost=LOWCPU, exclude_inproc=True),
    'hpack_size':
        default_test_options._replace(proxyable=False,
                                      traceable=False,
                                      cpu_cost=LOWCPU),
    'high_initial_seqno':
        default_test_options._replace(cpu_cost=LOWCPU),
    'idempotent_request':
        default_test_options,
    'invoke_large_request':
        default_test_options,
    'keepalive_timeout':
        default_test_options._replace(proxyable=False,
                                      cpu_cost=LOWCPU,
                                      needs_http2=True),
    'large_metadata':
        default_test_options,
    'max_concurrent_streams':
        default_test_options._replace(proxyable=False,
                                      cpu_cost=LOWCPU,
                                      exclude_inproc=True),
    'max_connection_age':
        default_test_options._replace(cpu_cost=LOWCPU, exclude_inproc=True),
    'max_connection_idle':
        connectivity_test_options._replace(proxyable=False,
                                           exclude_iomgrs=['uv'],
                                           cpu_cost=LOWCPU),
    'max_message_length':
        default_test_options._replace(cpu_cost=LOWCPU),
    'negative_deadline':
        default_test_options,
    'no_error_on_hotpath':
        default_test_options._replace(proxyable=False),
    'no_logging':
        default_test_options._replace(traceable=False),
    'no_op':
        default_test_options,
    'payload':
        default_test_options,
    # This cmake target is disabled for now because it depends on OpenCensus,
    # which is Bazel-only.
    # 'load_reporting_hook': default_test_options,
    'ping_pong_streaming':
        default_test_options._replace(cpu_cost=LOWCPU),
    'ping':
        connectivity_test_options._replace(proxyable=False, cpu_cost=LOWCPU),
    'proxy_auth':
        default_test_options._replace(needs_proxy_auth=True),
    'registered_call':
        default_test_options,
    'request_with_flags':
        default_test_options._replace(proxyable=False, cpu_cost=LOWCPU),
    'request_with_payload':
        default_test_options._replace(cpu_cost=LOWCPU),
    # TODO(roth): Remove proxyable=False for all retry tests once we
    # have a way for the proxy to propagate the fact that trailing
    # metadata is available when initial metadata is returned.
    # See https://github.com/grpc/grpc/issues/14467 for context.
    'retry':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_cancellation':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_disabled':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_exceeds_buffer_size_in_initial_batch':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_exceeds_buffer_size_in_subsequent_batch':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_non_retriable_status':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_non_retriable_status_before_recv_trailing_metadata_started':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_recv_initial_metadata':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_recv_message':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_server_pushback_delay':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_server_pushback_disabled':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_streaming':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_streaming_after_commit':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_streaming_succeeds_before_replay_finished':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_throttled':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'retry_too_many_attempts':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_client_channel=True,
                                      proxyable=False),
    'server_finishes_request':
        default_test_options._replace(cpu_cost=LOWCPU),
    'shutdown_finishes_calls':
        default_test_options._replace(cpu_cost=LOWCPU),
    'shutdown_finishes_tags':
        default_test_options._replace(cpu_cost=LOWCPU),
    'simple_cacheable_request':
        default_test_options._replace(cpu_cost=LOWCPU),
    'stream_compression_compressed_payload':
        default_test_options._replace(proxyable=False, exclude_inproc=True),
    'stream_compression_payload':
        default_test_options._replace(exclude_inproc=True),
    'stream_compression_ping_pong_streaming':
        default_test_options._replace(exclude_inproc=True),
    'simple_delayed_request':
        connectivity_test_options,
    'simple_metadata':
        default_test_options,
    'simple_request':
        default_test_options,
    'streaming_error_response':
        default_test_options._replace(cpu_cost=LOWCPU),
    'trailing_metadata':
        default_test_options,
    'workaround_cronet_compression':
        default_test_options,
    'write_buffering':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_write_buffering=True),
    'write_buffering_at_end':
        default_test_options._replace(cpu_cost=LOWCPU,
                                      needs_write_buffering=True),
}


def compatible(f, t):
    if END2END_TESTS[t].needs_fullstack:
        if not END2END_FIXTURES[f].fullstack:
            return False
    if END2END_TESTS[t].needs_dns:
        if not END2END_FIXTURES[f].dns_resolver:
            return False
    if END2END_TESTS[t].needs_names:
        if not END2END_FIXTURES[f].name_resolution:
            return False
    if not END2END_TESTS[t].proxyable:
        if END2END_FIXTURES[f].includes_proxy:
            return False
    if not END2END_TESTS[t].traceable:
        if END2END_FIXTURES[f].tracing:
            return False
    if END2END_TESTS[t].large_writes:
        if not END2END_FIXTURES[f].large_writes:
            return False
    if not END2END_TESTS[t].allows_compression:
        if END2END_FIXTURES[f].enables_compression:
            return False
    if END2END_TESTS[t].needs_compression:
        if not END2END_FIXTURES[f].supports_compression:
            return False
    if END2END_TESTS[t].exclude_inproc:
        if END2END_FIXTURES[f].is_inproc:
            return False
    if END2END_TESTS[t].needs_http2:
        if not END2END_FIXTURES[f].is_http2:
            return False
    if END2END_TESTS[t].needs_proxy_auth:
        if not END2END_FIXTURES[f].supports_proxy_auth:
            return False
    if END2END_TESTS[t].needs_write_buffering:
        if not END2END_FIXTURES[f].supports_write_buffering:
            return False
    if END2END_TESTS[t].needs_client_channel:
        if not END2END_FIXTURES[f].client_channel:
            return False
    return True


def without(l, e):
    l = l[:]
    l.remove(e)
    return l


# Originally, this method was used to generate end2end test cases for build.yaml,
# but since the test cases are now extracted from bazel BUILD file,
# this is not used for generating run_tests.py test cases anymore.
# Nevertheless, subset of the output is still used by end2end_tests.cc.template
# and end2end_nosec_tests.cc.template
# TODO(jtattermusch): cleanup this file, so that it only generates the data we need.
# Right now there's some duplication between generate_tests.bzl and this file.
def main():
    json = {
        # needed by end2end_tests.cc.template and end2end_nosec_tests.cc.template
        'core_end2end_tests':
            dict((t, END2END_TESTS[t].secure) for t in END2END_TESTS.keys())
    }
    print(yaml.dump(json))


if __name__ == '__main__':
    main()
