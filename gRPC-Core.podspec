# This file has been automatically generated from a template file.
# Please make modifications to `templates/gRPC-Core.podspec.template`
# instead. This file can be regenerated from the template by running
# `tools/buildgen/generate_projects.sh`.

# gRPC Core CocoaPods podspec
#
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


Pod::Spec.new do |s|
  s.name     = 'gRPC-Core'
  version = '1.4.0-dev'
  s.version  = version
  s.summary  = 'Core cross-platform gRPC library, written in C'
  s.homepage = 'http://www.grpc.io'
  s.license  = 'New BSD'
  s.authors  = { 'The gRPC contributors' => 'grpc-packages@google.com' }

  s.source = {
    :git => 'https://github.com/grpc/grpc.git',
    :tag => "v#{version}",
    # TODO(jcanizales): Depend explicitly on the nanopb pod, and disable submodules.
    :submodules => true,
  }

  s.ios.deployment_target = '7.0'
  s.osx.deployment_target = '10.9'
  s.requires_arc = false

  name = 'grpc'

  # When creating a dynamic framework, name it grpc.framework instead of gRPC-Core.framework.
  # This lets users write their includes like `#include <grpc/grpc.h>` as opposed to `#include
  # <gRPC-Core/grpc.h>`.
  s.module_name = name

  # When creating a dynamic framework, copy the headers under `include/grpc/` into the root of
  # the `Headers/` directory of the framework (i.e., not under `Headers/include/grpc`).
  #
  # TODO(jcanizales): Debug why this doesn't work on macOS.
  s.header_mappings_dir = 'include/grpc'

  # The above has an undesired effect when creating a static library: It forces users to write
  # includes like `#include <gRPC-Core/grpc.h>`. `s.header_dir` adds a path prefix to that, and
  # because Cocoapods lets omit the pod name when including headers of static libraries, the
  # following lets users write `#include <grpc/grpc.h>`.
  s.header_dir = name

  # The module map created automatically by Cocoapods doesn't work for C libraries like gRPC-Core.
  s.module_map = 'include/grpc/module.modulemap'

  # To compile the library, we need the user headers search path (quoted includes) to point to the
  # root of the repo, and the system headers search path (angled includes) to point to `include/`.
  # Cocoapods effectively clones the repo under `<Podfile dir>/Pods/gRPC-Core/`, and sets a build
  # variable called `$(PODS_ROOT)` to `<Podfile dir>/Pods/`, so we use that.
  #
  # Relying on the file structure under $(PODS_ROOT) isn't officially supported in Cocoapods, as it
  # is taken as an implementation detail. We've asked for an alternative, and have been told that
  # what we're doing should keep working: https://github.com/CocoaPods/CocoaPods/issues/4386
  #
  # The `src_root` value of `$(PODS_ROOT)/gRPC-Core` assumes Cocoapods is installing this pod from
  # its remote repo. For local development of this library, enabled by using `:path` in the Podfile,
  # that assumption is wrong. In such case, the following settings need to be reset with the
  # appropriate value of `src_root`. This can be accomplished in the `pre_install` hook of the
  # Podfile; see `src/objective-c/tests/Podfile` for an example.
  src_root = '$(PODS_ROOT)/gRPC-Core'
  s.pod_target_xcconfig = {
    'GRPC_SRC_ROOT' => src_root,
    'HEADER_SEARCH_PATHS' => '"$(inherited)" "$(GRPC_SRC_ROOT)/include"',
    'USER_HEADER_SEARCH_PATHS' => '"$(GRPC_SRC_ROOT)"',
    # If we don't set these two settings, `include/grpc/support/time.h` and
    # `src/core/lib/support/string.h` shadow the system `<time.h>` and `<string.h>`, breaking the
    # build.
    'USE_HEADERMAP' => 'NO',
    'ALWAYS_SEARCH_USER_PATHS' => 'NO',
  }

  s.default_subspecs = 'Interface', 'Implementation'
  s.compiler_flags = '-DGRPC_ARES=0'

  # Like many other C libraries, gRPC-Core has its public headers under `include/<libname>/` and its
  # sources and private headers in other directories outside `include/`. Cocoapods' linter doesn't
  # allow any header to be listed outside the `header_mappings_dir` (even though doing so works in
  # practice). Because we need our `header_mappings_dir` to be `include/grpc/` for the reason
  # mentioned above, we work around the linter limitation by dividing the pod into two subspecs, one
  # for public headers and the other for implementation. Each gets its own `header_mappings_dir`,
  # making the linter happy.
  #
  # The list of source files is generated by a template: `templates/gRPC-Core.podspec.template`. It
  # can be regenerated from the template by running `tools/buildgen/generate_projects.sh`.
  s.subspec 'Interface' do |ss|
    ss.header_mappings_dir = 'include/grpc'

    ss.source_files = 'include/grpc/support/alloc.h',
                      'include/grpc/support/atm.h',
                      'include/grpc/support/atm_gcc_atomic.h',
                      'include/grpc/support/atm_gcc_sync.h',
                      'include/grpc/support/atm_windows.h',
                      'include/grpc/support/avl.h',
                      'include/grpc/support/cmdline.h',
                      'include/grpc/support/cpu.h',
                      'include/grpc/support/histogram.h',
                      'include/grpc/support/host_port.h',
                      'include/grpc/support/log.h',
                      'include/grpc/support/log_windows.h',
                      'include/grpc/support/port_platform.h',
                      'include/grpc/support/string_util.h',
                      'include/grpc/support/subprocess.h',
                      'include/grpc/support/sync.h',
                      'include/grpc/support/sync_generic.h',
                      'include/grpc/support/sync_posix.h',
                      'include/grpc/support/sync_windows.h',
                      'include/grpc/support/thd.h',
                      'include/grpc/support/time.h',
                      'include/grpc/support/tls.h',
                      'include/grpc/support/tls_gcc.h',
                      'include/grpc/support/tls_msvc.h',
                      'include/grpc/support/tls_pthread.h',
                      'include/grpc/support/useful.h',
                      'include/grpc/impl/codegen/atm.h',
                      'include/grpc/impl/codegen/atm_gcc_atomic.h',
                      'include/grpc/impl/codegen/atm_gcc_sync.h',
                      'include/grpc/impl/codegen/atm_windows.h',
                      'include/grpc/impl/codegen/gpr_slice.h',
                      'include/grpc/impl/codegen/gpr_types.h',
                      'include/grpc/impl/codegen/port_platform.h',
                      'include/grpc/impl/codegen/sync.h',
                      'include/grpc/impl/codegen/sync_generic.h',
                      'include/grpc/impl/codegen/sync_posix.h',
                      'include/grpc/impl/codegen/sync_windows.h',
                      'include/grpc/grpc_security.h',
                      'include/grpc/census.h'
  end
  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = '.'
    ss.libraries = 'z'
    ss.dependency "#{s.name}/Interface", version
    ss.dependency 'BoringSSL', '~> 8.0'

    # To save you from scrolling, this is the last part of the podspec.
    ss.source_files = 'src/core/lib/profiling/timers.h',
                      'src/core/lib/support/arena.h',
                      'src/core/lib/support/atomic.h',
                      'src/core/lib/support/atomic_with_atm.h',
                      'src/core/lib/support/atomic_with_std.h',
                      'src/core/lib/support/backoff.h',
                      'src/core/lib/support/block_annotate.h',
                      'src/core/lib/support/env.h',
                      'src/core/lib/support/memory.h',
                      'src/core/lib/support/mpscq.h',
                      'src/core/lib/support/murmur_hash.h',
                      'src/core/lib/support/spinlock.h',
                      'src/core/lib/support/stack_lockfree.h',
                      'src/core/lib/support/string.h',
                      'src/core/lib/support/string_windows.h',
                      'src/core/lib/support/thd_internal.h',
                      'src/core/lib/support/time_precise.h',
                      'src/core/lib/support/tmpfile.h',
                      'src/core/lib/profiling/basic_timers.c',
                      'src/core/lib/profiling/stap_timers.c',
                      'src/core/lib/support/alloc.c',
                      'src/core/lib/support/arena.c',
                      'src/core/lib/support/atm.c',
                      'src/core/lib/support/avl.c',
                      'src/core/lib/support/backoff.c',
                      'src/core/lib/support/cmdline.c',
                      'src/core/lib/support/cpu_iphone.c',
                      'src/core/lib/support/cpu_linux.c',
                      'src/core/lib/support/cpu_posix.c',
                      'src/core/lib/support/cpu_windows.c',
                      'src/core/lib/support/env_linux.c',
                      'src/core/lib/support/env_posix.c',
                      'src/core/lib/support/env_windows.c',
                      'src/core/lib/support/histogram.c',
                      'src/core/lib/support/host_port.c',
                      'src/core/lib/support/log.c',
                      'src/core/lib/support/log_android.c',
                      'src/core/lib/support/log_linux.c',
                      'src/core/lib/support/log_posix.c',
                      'src/core/lib/support/log_windows.c',
                      'src/core/lib/support/mpscq.c',
                      'src/core/lib/support/murmur_hash.c',
                      'src/core/lib/support/stack_lockfree.c',
                      'src/core/lib/support/string.c',
                      'src/core/lib/support/string_posix.c',
                      'src/core/lib/support/string_util_windows.c',
                      'src/core/lib/support/string_windows.c',
                      'src/core/lib/support/subprocess_posix.c',
                      'src/core/lib/support/subprocess_windows.c',
                      'src/core/lib/support/sync.c',
                      'src/core/lib/support/sync_posix.c',
                      'src/core/lib/support/sync_windows.c',
                      'src/core/lib/support/thd.c',
                      'src/core/lib/support/thd_posix.c',
                      'src/core/lib/support/thd_windows.c',
                      'src/core/lib/support/time.c',
                      'src/core/lib/support/time_posix.c',
                      'src/core/lib/support/time_precise.c',
                      'src/core/lib/support/time_windows.c',
                      'src/core/lib/support/tls_pthread.c',
                      'src/core/lib/support/tmpfile_msys.c',
                      'src/core/lib/support/tmpfile_posix.c',
                      'src/core/lib/support/tmpfile_windows.c',
                      'src/core/lib/support/wrap_memcpy.c',
                      'src/core/ext/transport/chttp2/transport/bin_decoder.h',
                      'src/core/ext/transport/chttp2/transport/bin_encoder.h',
                      'src/core/ext/transport/chttp2/transport/chttp2_transport.h',
                      'src/core/ext/transport/chttp2/transport/frame.h',
                      'src/core/ext/transport/chttp2/transport/frame_data.h',
                      'src/core/ext/transport/chttp2/transport/frame_goaway.h',
                      'src/core/ext/transport/chttp2/transport/frame_ping.h',
                      'src/core/ext/transport/chttp2/transport/frame_rst_stream.h',
                      'src/core/ext/transport/chttp2/transport/frame_settings.h',
                      'src/core/ext/transport/chttp2/transport/frame_window_update.h',
                      'src/core/ext/transport/chttp2/transport/hpack_encoder.h',
                      'src/core/ext/transport/chttp2/transport/hpack_parser.h',
                      'src/core/ext/transport/chttp2/transport/hpack_table.h',
                      'src/core/ext/transport/chttp2/transport/http2_settings.h',
                      'src/core/ext/transport/chttp2/transport/huffsyms.h',
                      'src/core/ext/transport/chttp2/transport/incoming_metadata.h',
                      'src/core/ext/transport/chttp2/transport/internal.h',
                      'src/core/ext/transport/chttp2/transport/stream_map.h',
                      'src/core/ext/transport/chttp2/transport/varint.h',
                      'src/core/ext/transport/chttp2/alpn/alpn.h',
                      'src/core/ext/filters/http/client/http_client_filter.h',
                      'src/core/ext/filters/http/message_compress/message_compress_filter.h',
                      'src/core/ext/filters/http/server/http_server_filter.h',
                      'src/core/lib/security/context/security_context.h',
                      'src/core/lib/security/credentials/composite/composite_credentials.h',
                      'src/core/lib/security/credentials/credentials.h',
                      'src/core/lib/security/credentials/fake/fake_credentials.h',
                      'src/core/lib/security/credentials/google_default/google_default_credentials.h',
                      'src/core/lib/security/credentials/iam/iam_credentials.h',
                      'src/core/lib/security/credentials/jwt/json_token.h',
                      'src/core/lib/security/credentials/jwt/jwt_credentials.h',
                      'src/core/lib/security/credentials/jwt/jwt_verifier.h',
                      'src/core/lib/security/credentials/oauth2/oauth2_credentials.h',
                      'src/core/lib/security/credentials/plugin/plugin_credentials.h',
                      'src/core/lib/security/credentials/ssl/ssl_credentials.h',
                      'src/core/lib/security/transport/auth_filters.h',
                      'src/core/lib/security/transport/lb_targets_info.h',
                      'src/core/lib/security/transport/secure_endpoint.h',
                      'src/core/lib/security/transport/security_connector.h',
                      'src/core/lib/security/transport/security_handshaker.h',
                      'src/core/lib/security/transport/tsi_error.h',
                      'src/core/lib/security/util/json_util.h',
                      'src/core/tsi/fake_transport_security.h',
                      'src/core/tsi/ssl_transport_security.h',
                      'src/core/tsi/ssl_types.h',
                      'src/core/tsi/transport_security.h',
                      'src/core/tsi/transport_security_adapter.h',
                      'src/core/tsi/transport_security_interface.h',
                      'src/core/lib/debug/trace.h',
                      'src/core/ext/transport/chttp2/server/chttp2_server.h',
                      'src/core/ext/filters/client_channel/client_channel.h',
                      'src/core/ext/filters/client_channel/client_channel_factory.h',
                      'src/core/ext/filters/client_channel/connector.h',
                      'src/core/ext/filters/client_channel/http_connect_handshaker.h',
                      'src/core/ext/filters/client_channel/http_proxy.h',
                      'src/core/ext/filters/client_channel/lb_policy.h',
                      'src/core/ext/filters/client_channel/lb_policy_factory.h',
                      'src/core/ext/filters/client_channel/lb_policy_registry.h',
                      'src/core/ext/filters/client_channel/parse_address.h',
                      'src/core/ext/filters/client_channel/proxy_mapper.h',
                      'src/core/ext/filters/client_channel/proxy_mapper_registry.h',
                      'src/core/ext/filters/client_channel/resolver.h',
                      'src/core/ext/filters/client_channel/resolver_factory.h',
                      'src/core/ext/filters/client_channel/resolver_registry.h',
                      'src/core/ext/filters/client_channel/retry_throttle.h',
                      'src/core/ext/filters/client_channel/subchannel.h',
                      'src/core/ext/filters/client_channel/subchannel_index.h',
                      'src/core/ext/filters/client_channel/uri_parser.h',
                      'src/core/ext/filters/deadline/deadline_filter.h',
                      'src/core/ext/transport/chttp2/client/chttp2_connector.h',
                      'src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h',
                      'src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h',
                      'src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h',
                      'src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h',
                      'src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h',
                      'src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.h',
                      'third_party/nanopb/pb.h',
                      'third_party/nanopb/pb_common.h',
                      'third_party/nanopb/pb_decode.h',
                      'third_party/nanopb/pb_encode.h',
                      'src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h',
                      'src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h',
                      'src/core/ext/filters/load_reporting/load_reporting.h',
                      'src/core/ext/filters/load_reporting/load_reporting_filter.h',
                      'src/core/ext/census/aggregation.h',
                      'src/core/ext/census/base_resources.h',
                      'src/core/ext/census/census_interface.h',
                      'src/core/ext/census/census_rpc_stats.h',
                      'src/core/ext/census/gen/census.pb.h',
                      'src/core/ext/census/gen/trace_context.pb.h',
                      'src/core/ext/census/grpc_filter.h',
                      'src/core/ext/census/mlog.h',
                      'src/core/ext/census/resource.h',
                      'src/core/ext/census/rpc_metric_id.h',
                      'src/core/ext/census/trace_context.h',
                      'src/core/ext/census/trace_label.h',
                      'src/core/ext/census/trace_propagation.h',
                      'src/core/ext/census/trace_status.h',
                      'src/core/ext/census/trace_string.h',
                      'src/core/ext/census/tracing.h',
                      'src/core/ext/filters/max_age/max_age_filter.h',
                      'src/core/ext/filters/message_size/message_size_filter.h',
                      'src/core/lib/surface/init.c',
                      'src/core/ext/transport/chttp2/server/secure/server_secure_chttp2.c',
                      'src/core/ext/transport/chttp2/transport/bin_decoder.c',
                      'src/core/ext/transport/chttp2/transport/bin_encoder.c',
                      'src/core/ext/transport/chttp2/transport/chttp2_plugin.c',
                      'src/core/ext/transport/chttp2/transport/chttp2_transport.c',
                      'src/core/ext/transport/chttp2/transport/frame_data.c',
                      'src/core/ext/transport/chttp2/transport/frame_goaway.c',
                      'src/core/ext/transport/chttp2/transport/frame_ping.c',
                      'src/core/ext/transport/chttp2/transport/frame_rst_stream.c',
                      'src/core/ext/transport/chttp2/transport/frame_settings.c',
                      'src/core/ext/transport/chttp2/transport/frame_window_update.c',
                      'src/core/ext/transport/chttp2/transport/hpack_encoder.c',
                      'src/core/ext/transport/chttp2/transport/hpack_parser.c',
                      'src/core/ext/transport/chttp2/transport/hpack_table.c',
                      'src/core/ext/transport/chttp2/transport/http2_settings.c',
                      'src/core/ext/transport/chttp2/transport/huffsyms.c',
                      'src/core/ext/transport/chttp2/transport/incoming_metadata.c',
                      'src/core/ext/transport/chttp2/transport/parsing.c',
                      'src/core/ext/transport/chttp2/transport/stream_lists.c',
                      'src/core/ext/transport/chttp2/transport/stream_map.c',
                      'src/core/ext/transport/chttp2/transport/varint.c',
                      'src/core/ext/transport/chttp2/transport/writing.c',
                      'src/core/ext/transport/chttp2/alpn/alpn.c',
                      'src/core/ext/filters/http/client/http_client_filter.c',
                      'src/core/ext/filters/http/http_filters_plugin.c',
                      'src/core/ext/filters/http/message_compress/message_compress_filter.c',
                      'src/core/ext/filters/http/server/http_server_filter.c',
                      'src/core/lib/http/httpcli_security_connector.c',
                      'src/core/lib/security/context/security_context.c',
                      'src/core/lib/security/credentials/composite/composite_credentials.c',
                      'src/core/lib/security/credentials/credentials.c',
                      'src/core/lib/security/credentials/credentials_metadata.c',
                      'src/core/lib/security/credentials/fake/fake_credentials.c',
                      'src/core/lib/security/credentials/google_default/credentials_generic.c',
                      'src/core/lib/security/credentials/google_default/google_default_credentials.c',
                      'src/core/lib/security/credentials/iam/iam_credentials.c',
                      'src/core/lib/security/credentials/jwt/json_token.c',
                      'src/core/lib/security/credentials/jwt/jwt_credentials.c',
                      'src/core/lib/security/credentials/jwt/jwt_verifier.c',
                      'src/core/lib/security/credentials/oauth2/oauth2_credentials.c',
                      'src/core/lib/security/credentials/plugin/plugin_credentials.c',
                      'src/core/lib/security/credentials/ssl/ssl_credentials.c',
                      'src/core/lib/security/transport/client_auth_filter.c',
                      'src/core/lib/security/transport/lb_targets_info.c',
                      'src/core/lib/security/transport/secure_endpoint.c',
                      'src/core/lib/security/transport/security_connector.c',
                      'src/core/lib/security/transport/security_handshaker.c',
                      'src/core/lib/security/transport/server_auth_filter.c',
                      'src/core/lib/security/transport/tsi_error.c',
                      'src/core/lib/security/util/json_util.c',
                      'src/core/lib/surface/init_secure.c',
                      'src/core/tsi/fake_transport_security.c',
                      'src/core/tsi/ssl_transport_security.c',
                      'src/core/tsi/transport_security.c',
                      'src/core/tsi/transport_security_adapter.c',
                      'src/core/lib/debug/trace.c',
                      'src/core/ext/transport/chttp2/server/chttp2_server.c',
                      'src/core/ext/transport/chttp2/client/secure/secure_channel_create.c',
                      'src/core/ext/filters/client_channel/channel_connectivity.c',
                      'src/core/ext/filters/client_channel/client_channel.c',
                      'src/core/ext/filters/client_channel/client_channel_factory.c',
                      'src/core/ext/filters/client_channel/client_channel_plugin.c',
                      'src/core/ext/filters/client_channel/connector.c',
                      'src/core/ext/filters/client_channel/http_connect_handshaker.c',
                      'src/core/ext/filters/client_channel/http_proxy.c',
                      'src/core/ext/filters/client_channel/lb_policy.c',
                      'src/core/ext/filters/client_channel/lb_policy_factory.c',
                      'src/core/ext/filters/client_channel/lb_policy_registry.c',
                      'src/core/ext/filters/client_channel/parse_address.c',
                      'src/core/ext/filters/client_channel/proxy_mapper.c',
                      'src/core/ext/filters/client_channel/proxy_mapper_registry.c',
                      'src/core/ext/filters/client_channel/resolver.c',
                      'src/core/ext/filters/client_channel/resolver_factory.c',
                      'src/core/ext/filters/client_channel/resolver_registry.c',
                      'src/core/ext/filters/client_channel/retry_throttle.c',
                      'src/core/ext/filters/client_channel/subchannel.c',
                      'src/core/ext/filters/client_channel/subchannel_index.c',
                      'src/core/ext/filters/client_channel/uri_parser.c',
                      'src/core/ext/filters/deadline/deadline_filter.c',
                      'src/core/ext/transport/chttp2/client/chttp2_connector.c',
                      'src/core/ext/transport/chttp2/server/insecure/server_chttp2.c',
                      'src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c',
                      'src/core/ext/transport/chttp2/client/insecure/channel_create.c',
                      'src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c',
                      'src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.c',
                      'src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.c',
                      'src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel_secure.c',
                      'src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.c',
                      'src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.c',
                      'src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c',
                      'third_party/nanopb/pb_common.c',
                      'third_party/nanopb/pb_decode.c',
                      'third_party/nanopb/pb_encode.c',
                      'src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.c',
                      'src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.c',
                      'src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.c',
                      'src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.c',
                      'src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.c',
                      'src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.c',
                      'src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.c',
                      'src/core/ext/filters/load_reporting/load_reporting.c',
                      'src/core/ext/filters/load_reporting/load_reporting_filter.c',
                      'src/core/ext/census/base_resources.c',
                      'src/core/ext/census/context.c',
                      'src/core/ext/census/gen/census.pb.c',
                      'src/core/ext/census/gen/trace_context.pb.c',
                      'src/core/ext/census/grpc_context.c',
                      'src/core/ext/census/grpc_filter.c',
                      'src/core/ext/census/grpc_plugin.c',
                      'src/core/ext/census/initialize.c',
                      'src/core/ext/census/mlog.c',
                      'src/core/ext/census/operation.c',
                      'src/core/ext/census/placeholders.c',
                      'src/core/ext/census/resource.c',
                      'src/core/ext/census/trace_context.c',
                      'src/core/ext/census/tracing.c',
                      'src/core/ext/filters/max_age/max_age_filter.c',
                      'src/core/ext/filters/message_size/message_size_filter.c',
                      'src/core/plugin_registry/grpc_plugin_registry.c'

    ss.private_header_files = 'src/core/lib/profiling/timers.h',
                              'src/core/lib/support/arena.h',
                              'src/core/lib/support/atomic.h',
                              'src/core/lib/support/atomic_with_atm.h',
                              'src/core/lib/support/atomic_with_std.h',
                              'src/core/lib/support/backoff.h',
                              'src/core/lib/support/block_annotate.h',
                              'src/core/lib/support/env.h',
                              'src/core/lib/support/memory.h',
                              'src/core/lib/support/mpscq.h',
                              'src/core/lib/support/murmur_hash.h',
                              'src/core/lib/support/spinlock.h',
                              'src/core/lib/support/stack_lockfree.h',
                              'src/core/lib/support/string.h',
                              'src/core/lib/support/string_windows.h',
                              'src/core/lib/support/thd_internal.h',
                              'src/core/lib/support/time_precise.h',
                              'src/core/lib/support/tmpfile.h',
                              'src/core/ext/transport/chttp2/transport/bin_decoder.h',
                              'src/core/ext/transport/chttp2/transport/bin_encoder.h',
                              'src/core/ext/transport/chttp2/transport/chttp2_transport.h',
                              'src/core/ext/transport/chttp2/transport/frame.h',
                              'src/core/ext/transport/chttp2/transport/frame_data.h',
                              'src/core/ext/transport/chttp2/transport/frame_goaway.h',
                              'src/core/ext/transport/chttp2/transport/frame_ping.h',
                              'src/core/ext/transport/chttp2/transport/frame_rst_stream.h',
                              'src/core/ext/transport/chttp2/transport/frame_settings.h',
                              'src/core/ext/transport/chttp2/transport/frame_window_update.h',
                              'src/core/ext/transport/chttp2/transport/hpack_encoder.h',
                              'src/core/ext/transport/chttp2/transport/hpack_parser.h',
                              'src/core/ext/transport/chttp2/transport/hpack_table.h',
                              'src/core/ext/transport/chttp2/transport/http2_settings.h',
                              'src/core/ext/transport/chttp2/transport/huffsyms.h',
                              'src/core/ext/transport/chttp2/transport/incoming_metadata.h',
                              'src/core/ext/transport/chttp2/transport/internal.h',
                              'src/core/ext/transport/chttp2/transport/stream_map.h',
                              'src/core/ext/transport/chttp2/transport/varint.h',
                              'src/core/ext/transport/chttp2/alpn/alpn.h',
                              'src/core/ext/filters/http/client/http_client_filter.h',
                              'src/core/ext/filters/http/message_compress/message_compress_filter.h',
                              'src/core/ext/filters/http/server/http_server_filter.h',
                              'src/core/lib/security/context/security_context.h',
                              'src/core/lib/security/credentials/composite/composite_credentials.h',
                              'src/core/lib/security/credentials/credentials.h',
                              'src/core/lib/security/credentials/fake/fake_credentials.h',
                              'src/core/lib/security/credentials/google_default/google_default_credentials.h',
                              'src/core/lib/security/credentials/iam/iam_credentials.h',
                              'src/core/lib/security/credentials/jwt/json_token.h',
                              'src/core/lib/security/credentials/jwt/jwt_credentials.h',
                              'src/core/lib/security/credentials/jwt/jwt_verifier.h',
                              'src/core/lib/security/credentials/oauth2/oauth2_credentials.h',
                              'src/core/lib/security/credentials/plugin/plugin_credentials.h',
                              'src/core/lib/security/credentials/ssl/ssl_credentials.h',
                              'src/core/lib/security/transport/auth_filters.h',
                              'src/core/lib/security/transport/lb_targets_info.h',
                              'src/core/lib/security/transport/secure_endpoint.h',
                              'src/core/lib/security/transport/security_connector.h',
                              'src/core/lib/security/transport/security_handshaker.h',
                              'src/core/lib/security/transport/tsi_error.h',
                              'src/core/lib/security/util/json_util.h',
                              'src/core/tsi/fake_transport_security.h',
                              'src/core/tsi/ssl_transport_security.h',
                              'src/core/tsi/ssl_types.h',
                              'src/core/tsi/transport_security.h',
                              'src/core/tsi/transport_security_adapter.h',
                              'src/core/tsi/transport_security_interface.h',
                              'src/core/lib/debug/trace.h',
                              'src/core/ext/transport/chttp2/server/chttp2_server.h',
                              'src/core/ext/filters/client_channel/client_channel.h',
                              'src/core/ext/filters/client_channel/client_channel_factory.h',
                              'src/core/ext/filters/client_channel/connector.h',
                              'src/core/ext/filters/client_channel/http_connect_handshaker.h',
                              'src/core/ext/filters/client_channel/http_proxy.h',
                              'src/core/ext/filters/client_channel/lb_policy.h',
                              'src/core/ext/filters/client_channel/lb_policy_factory.h',
                              'src/core/ext/filters/client_channel/lb_policy_registry.h',
                              'src/core/ext/filters/client_channel/parse_address.h',
                              'src/core/ext/filters/client_channel/proxy_mapper.h',
                              'src/core/ext/filters/client_channel/proxy_mapper_registry.h',
                              'src/core/ext/filters/client_channel/resolver.h',
                              'src/core/ext/filters/client_channel/resolver_factory.h',
                              'src/core/ext/filters/client_channel/resolver_registry.h',
                              'src/core/ext/filters/client_channel/retry_throttle.h',
                              'src/core/ext/filters/client_channel/subchannel.h',
                              'src/core/ext/filters/client_channel/subchannel_index.h',
                              'src/core/ext/filters/client_channel/uri_parser.h',
                              'src/core/ext/filters/deadline/deadline_filter.h',
                              'src/core/ext/transport/chttp2/client/chttp2_connector.h',
                              'src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h',
                              'src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h',
                              'src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h',
                              'src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h',
                              'src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h',
                              'src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.h',
                              'third_party/nanopb/pb.h',
                              'third_party/nanopb/pb_common.h',
                              'third_party/nanopb/pb_decode.h',
                              'third_party/nanopb/pb_encode.h',
                              'src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h',
                              'src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h',
                              'src/core/ext/filters/load_reporting/load_reporting.h',
                              'src/core/ext/filters/load_reporting/load_reporting_filter.h',
                              'src/core/ext/census/aggregation.h',
                              'src/core/ext/census/base_resources.h',
                              'src/core/ext/census/census_interface.h',
                              'src/core/ext/census/census_rpc_stats.h',
                              'src/core/ext/census/gen/census.pb.h',
                              'src/core/ext/census/gen/trace_context.pb.h',
                              'src/core/ext/census/grpc_filter.h',
                              'src/core/ext/census/mlog.h',
                              'src/core/ext/census/resource.h',
                              'src/core/ext/census/rpc_metric_id.h',
                              'src/core/ext/census/trace_context.h',
                              'src/core/ext/census/trace_label.h',
                              'src/core/ext/census/trace_propagation.h',
                              'src/core/ext/census/trace_status.h',
                              'src/core/ext/census/trace_string.h',
                              'src/core/ext/census/tracing.h',
                              'src/core/ext/filters/max_age/max_age_filter.h',
                              'src/core/ext/filters/message_size/message_size_filter.h'
  end

  s.subspec 'Cronet-Interface' do |ss|
    ss.header_mappings_dir = 'include/grpc'
    ss.source_files = 'include/grpc/grpc_cronet.h'
  end

  s.subspec 'Cronet-Implementation' do |ss|
    ss.header_mappings_dir = '.'

    ss.dependency "#{s.name}/Interface", version
    ss.dependency "#{s.name}/Implementation", version
    ss.dependency "#{s.name}/Cronet-Interface", version

    ss.source_files = 'src/core/ext/transport/cronet/client/secure/cronet_channel_create.c',
                      'src/core/ext/transport/cronet/transport/cronet_transport.{c,h}',
                      'third_party/objective_c/Cronet/bidirectional_stream_c.h'
  end

  s.subspec 'Tests' do |ss|
    ss.header_mappings_dir = '.'

    ss.dependency "#{s.name}/Interface", version
    ss.dependency "#{s.name}/Implementation", version

    ss.source_files = 'test/core/end2end/cq_verifier.{c,h}',
                      'test/core/end2end/end2end_tests.{c,h}',
                      'test/core/end2end/end2end_test_utils.c',
                      'test/core/end2end/tests/*.{c,h}',
                      'test/core/end2end/data/*.{c,h}',
                      'test/core/util/debugger_macros.{c,h}',
                      'test/core/util/test_config.{c,h}',
                      'test/core/util/port.h',
                      'test/core/util/port.c',
                      'test/core/util/port_server_client.{c,h}'
  end
end
