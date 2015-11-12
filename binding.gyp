# GRPC Node gyp file
# This currently builds the Node extension and dependencies
# This file has been automatically generated from a template file.
# Please look at the templates directory instead.
# This file can be regenerated from the template by running
# tools/buildgen/generate_projects.sh

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

# Some of this file is built with the help of
# https://n8.io/converting-a-c-library-to-gyp/
{
  'variables': {
    'config': '<!(echo $CONFIG)'
  },
  # TODO: Finish windows support
  'target_defaults': {
      # Empirically, Node only exports ALPN symbols if its major version is >0.
      # io.js always reports versions >0 and always exports ALPN symbols.
      # Therefore, Node's major version will be truthy if and only if it
      # supports ALPN. The output of "node -v" is v[major].[minor].[patch],
      # like "v4.1.1" in a recent version. We use cut to split by period and
      # take the first field (resulting in "v[major]"), then use cut again
      # to take all but the first character, removing the "v".
    'defines': [
      'TSI_OPENSSL_ALPN_SUPPORT=<!(node --version | cut -d. -f1 | cut -c2-)'
    ],
    'include_dirs': [
      '.',
      'include',
      '<(node_root_dir)/deps/openssl/openssl/include',
      '<(node_root_dir)/deps/zlib'
    ],
    'conditions': [
      ['OS != "win"', {
        'conditions': [
          ['config=="gcov"', {
            'cflags': [
              '-ftest-coverage',
              '-fprofile-arcs',
              '-O0'
            ],
            'ldflags': [
              '-ftest-coverage',
              '-fprofile-arcs'
            ]
          }
         ]
        ]
      }],
      ["target_arch=='ia32'", {
          "include_dirs": [ "<(node_root_dir)/deps/openssl/config/piii" ]
      }],
      ["target_arch=='x64'", {
          "include_dirs": [ "<(node_root_dir)/deps/openssl/config/k8" ]
      }],
      ["target_arch=='arm'", {
          "include_dirs": [ "<(node_root_dir)/deps/openssl/config/arm" ]
      }]
    ]
  },
  'targets': [
    {
      'target_name': 'gpr',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
      ],
      'sources': [
        'src/core/profiling/basic_timers.c',
        'src/core/profiling/stap_timers.c',
        'src/core/support/alloc.c',
        'src/core/support/cmdline.c',
        'src/core/support/cpu_iphone.c',
        'src/core/support/cpu_linux.c',
        'src/core/support/cpu_posix.c',
        'src/core/support/cpu_windows.c',
        'src/core/support/env_linux.c',
        'src/core/support/env_posix.c',
        'src/core/support/env_win32.c',
        'src/core/support/file.c',
        'src/core/support/file_posix.c',
        'src/core/support/file_win32.c',
        'src/core/support/histogram.c',
        'src/core/support/host_port.c',
        'src/core/support/log.c',
        'src/core/support/log_android.c',
        'src/core/support/log_linux.c',
        'src/core/support/log_posix.c',
        'src/core/support/log_win32.c',
        'src/core/support/murmur_hash.c',
        'src/core/support/slice.c',
        'src/core/support/slice_buffer.c',
        'src/core/support/stack_lockfree.c',
        'src/core/support/string.c',
        'src/core/support/string_posix.c',
        'src/core/support/string_win32.c',
        'src/core/support/subprocess_posix.c',
        'src/core/support/sync.c',
        'src/core/support/sync_posix.c',
        'src/core/support/sync_win32.c',
        'src/core/support/thd.c',
        'src/core/support/thd_posix.c',
        'src/core/support/thd_win32.c',
        'src/core/support/time.c',
        'src/core/support/time_posix.c',
        'src/core/support/time_precise.c',
        'src/core/support/time_win32.c',
        'src/core/support/tls_pthread.c',
      ],
      "conditions": [
        ['OS == "mac"', {
          'xcode_settings': {
            'MACOSX_DEPLOYMENT_TARGET': '10.9'
          }
        }]
      ],
    },
    {
      'target_name': 'grpc',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'gpr',
      ],
      'sources': [
        'src/core/httpcli/httpcli_security_connector.c',
        'src/core/security/base64.c',
        'src/core/security/client_auth_filter.c',
        'src/core/security/credentials.c',
        'src/core/security/credentials_metadata.c',
        'src/core/security/credentials_posix.c',
        'src/core/security/credentials_win32.c',
        'src/core/security/google_default_credentials.c',
        'src/core/security/handshake.c',
        'src/core/security/json_token.c',
        'src/core/security/jwt_verifier.c',
        'src/core/security/secure_endpoint.c',
        'src/core/security/security_connector.c',
        'src/core/security/security_context.c',
        'src/core/security/server_auth_filter.c',
        'src/core/security/server_secure_chttp2.c',
        'src/core/surface/init_secure.c',
        'src/core/surface/secure_channel_create.c',
        'src/core/tsi/fake_transport_security.c',
        'src/core/tsi/ssl_transport_security.c',
        'src/core/tsi/transport_security.c',
        'src/core/census/grpc_context.c',
        'src/core/census/grpc_filter.c',
        'src/core/channel/channel_args.c',
        'src/core/channel/channel_stack.c',
        'src/core/channel/client_channel.c',
        'src/core/channel/client_uchannel.c',
        'src/core/channel/compress_filter.c',
        'src/core/channel/connected_channel.c',
        'src/core/channel/http_client_filter.c',
        'src/core/channel/http_server_filter.c',
        'src/core/channel/noop_filter.c',
        'src/core/client_config/client_config.c',
        'src/core/client_config/connector.c',
        'src/core/client_config/lb_policies/load_balancer_api.c',
        'src/core/client_config/lb_policies/pick_first.c',
        'src/core/client_config/lb_policies/round_robin.c',
        'src/core/client_config/lb_policy.c',
        'src/core/client_config/lb_policy_factory.c',
        'src/core/client_config/lb_policy_registry.c',
        'src/core/client_config/resolver.c',
        'src/core/client_config/resolver_factory.c',
        'src/core/client_config/resolver_registry.c',
        'src/core/client_config/resolvers/dns_resolver.c',
        'src/core/client_config/resolvers/sockaddr_resolver.c',
        'src/core/client_config/subchannel.c',
        'src/core/client_config/subchannel_factory.c',
        'src/core/client_config/subchannel_factory_decorators/add_channel_arg.c',
        'src/core/client_config/subchannel_factory_decorators/merge_channel_args.c',
        'src/core/client_config/uri_parser.c',
        'src/core/compression/algorithm.c',
        'src/core/compression/message_compress.c',
        'src/core/debug/trace.c',
        'src/core/httpcli/format_request.c',
        'src/core/httpcli/httpcli.c',
        'src/core/httpcli/parser.c',
        'src/core/iomgr/closure.c',
        'src/core/iomgr/endpoint.c',
        'src/core/iomgr/endpoint_pair_posix.c',
        'src/core/iomgr/endpoint_pair_windows.c',
        'src/core/iomgr/exec_ctx.c',
        'src/core/iomgr/executor.c',
        'src/core/iomgr/fd_posix.c',
        'src/core/iomgr/iocp_windows.c',
        'src/core/iomgr/iomgr.c',
        'src/core/iomgr/iomgr_posix.c',
        'src/core/iomgr/iomgr_windows.c',
        'src/core/iomgr/pollset_multipoller_with_epoll.c',
        'src/core/iomgr/pollset_multipoller_with_poll_posix.c',
        'src/core/iomgr/pollset_posix.c',
        'src/core/iomgr/pollset_set_posix.c',
        'src/core/iomgr/pollset_set_windows.c',
        'src/core/iomgr/pollset_windows.c',
        'src/core/iomgr/resolve_address_posix.c',
        'src/core/iomgr/resolve_address_windows.c',
        'src/core/iomgr/sockaddr_utils.c',
        'src/core/iomgr/socket_utils_common_posix.c',
        'src/core/iomgr/socket_utils_linux.c',
        'src/core/iomgr/socket_utils_posix.c',
        'src/core/iomgr/socket_windows.c',
        'src/core/iomgr/tcp_client_posix.c',
        'src/core/iomgr/tcp_client_windows.c',
        'src/core/iomgr/tcp_posix.c',
        'src/core/iomgr/tcp_server_posix.c',
        'src/core/iomgr/tcp_server_windows.c',
        'src/core/iomgr/tcp_windows.c',
        'src/core/iomgr/time_averaged_stats.c',
        'src/core/iomgr/timer.c',
        'src/core/iomgr/timer_heap.c',
        'src/core/iomgr/udp_server.c',
        'src/core/iomgr/wakeup_fd_eventfd.c',
        'src/core/iomgr/wakeup_fd_nospecial.c',
        'src/core/iomgr/wakeup_fd_pipe.c',
        'src/core/iomgr/wakeup_fd_posix.c',
        'src/core/iomgr/workqueue_posix.c',
        'src/core/iomgr/workqueue_windows.c',
        'src/core/json/json.c',
        'src/core/json/json_reader.c',
        'src/core/json/json_string.c',
        'src/core/json/json_writer.c',
        'src/core/proto/load_balancer.pb.c',
        'src/core/surface/api_trace.c',
        'src/core/surface/byte_buffer.c',
        'src/core/surface/byte_buffer_queue.c',
        'src/core/surface/byte_buffer_reader.c',
        'src/core/surface/call.c',
        'src/core/surface/call_details.c',
        'src/core/surface/call_log_batch.c',
        'src/core/surface/channel.c',
        'src/core/surface/channel_connectivity.c',
        'src/core/surface/channel_create.c',
        'src/core/surface/completion_queue.c',
        'src/core/surface/event_string.c',
        'src/core/surface/init.c',
        'src/core/surface/lame_client.c',
        'src/core/surface/metadata_array.c',
        'src/core/surface/server.c',
        'src/core/surface/server_chttp2.c',
        'src/core/surface/server_create.c',
        'src/core/surface/version.c',
        'src/core/transport/chttp2/alpn.c',
        'src/core/transport/chttp2/bin_encoder.c',
        'src/core/transport/chttp2/frame_data.c',
        'src/core/transport/chttp2/frame_goaway.c',
        'src/core/transport/chttp2/frame_ping.c',
        'src/core/transport/chttp2/frame_rst_stream.c',
        'src/core/transport/chttp2/frame_settings.c',
        'src/core/transport/chttp2/frame_window_update.c',
        'src/core/transport/chttp2/hpack_parser.c',
        'src/core/transport/chttp2/hpack_table.c',
        'src/core/transport/chttp2/huffsyms.c',
        'src/core/transport/chttp2/incoming_metadata.c',
        'src/core/transport/chttp2/parsing.c',
        'src/core/transport/chttp2/status_conversion.c',
        'src/core/transport/chttp2/stream_encoder.c',
        'src/core/transport/chttp2/stream_lists.c',
        'src/core/transport/chttp2/stream_map.c',
        'src/core/transport/chttp2/timeout_encoding.c',
        'src/core/transport/chttp2/varint.c',
        'src/core/transport/chttp2/writing.c',
        'src/core/transport/chttp2_transport.c',
        'src/core/transport/connectivity_state.c',
        'src/core/transport/metadata.c',
        'src/core/transport/stream_op.c',
        'src/core/transport/transport.c',
        'src/core/transport/transport_op_string.c',
        'third_party/nanopb/pb_common.c',
        'third_party/nanopb/pb_decode.c',
        'third_party/nanopb/pb_encode.c',
        'src/core/census/context.c',
        'src/core/census/initialize.c',
        'src/core/census/operation.c',
        'src/core/census/tracing.c',
      ],
      "conditions": [
        ['OS == "mac"', {
          'xcode_settings': {
            'MACOSX_DEPLOYMENT_TARGET': '10.9'
          }
        }]
      ],
    },
    {
      'include_dirs': [
        "<!(node -e \"require('nan')\")"
      ],
      'cflags': [
        '-std=c++0x',
        '-Wall',
        '-pthread',
        '-g',
        '-zdefs',
        '-Werror',
        '-Wno-error=deprecated-declarations'
      ],
      'ldflags': [
        '-g'
      ],
      "conditions": [
        ['OS == "mac"', {
          'xcode_settings': {
            'MACOSX_DEPLOYMENT_TARGET': '10.9',
            'OTHER_CFLAGS': [
              '-std=c++11',
              '-stdlib=libc++'
            ]
          }
        }]
      ],
      "target_name": "grpc_node",
      "sources": [
        "src/node/ext/byte_buffer.cc",
        "src/node/ext/call.cc",
        "src/node/ext/call_credentials.cc",
        "src/node/ext/channel.cc",
        "src/node/ext/channel_credentials.cc",
        "src/node/ext/completion_queue_async_worker.cc",
        "src/node/ext/node_grpc.cc",
        "src/node/ext/server.cc",
        "src/node/ext/server_credentials.cc",
        "src/node/ext/timeval.cc"
      ],
      "dependencies": [
        "grpc"
      ]
    }
  ]
}
