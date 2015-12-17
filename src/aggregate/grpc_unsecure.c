
/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef GRPC_UNSECURE_C_
#define GRPC_UNSECURE_C_

#ifdef __cplusplus
extern "C" {
#endif

#include "src/aggregate/gpr.c"

#include "src/core/surface/init_unsecure.c"
#include "src/core/census/grpc_context.c"
#include "src/core/census/grpc_filter.c"
#include "src/core/channel/channel_args.c"
#include "src/core/channel/channel_stack.c"
#include "src/core/channel/client_channel.c"
#include "src/core/channel/client_uchannel.c"
#include "src/core/channel/compress_filter.c"
#include "src/core/channel/connected_channel.c"
#include "src/core/channel/http_client_filter.c"
#include "src/core/channel/http_server_filter.c"
#include "src/core/channel/subchannel_call_holder.c"
#include "src/core/client_config/client_config.c"
#include "src/core/client_config/connector.c"
#include "src/core/client_config/default_initial_connect_string.c"
#include "src/core/client_config/initial_connect_string.c"
#include "src/core/client_config/lb_policies/pick_first.c"
#include "src/core/client_config/lb_policies/round_robin.c"
#include "src/core/client_config/lb_policy.c"
#include "src/core/client_config/lb_policy_factory.c"
#include "src/core/client_config/lb_policy_registry.c"
#include "src/core/client_config/resolver.c"
#include "src/core/client_config/resolver_factory.c"
#include "src/core/client_config/resolver_registry.c"
#include "src/core/client_config/resolvers/dns_resolver.c"
#include "src/core/client_config/resolvers/sockaddr_resolver.c"
#include "src/core/client_config/subchannel.c"
#include "src/core/client_config/subchannel_factory.c"
#include "src/core/client_config/uri_parser.c"
#include "src/core/compression/algorithm.c"
#include "src/core/compression/message_compress.c"
#include "src/core/debug/trace.c"
#include "src/core/httpcli/format_request.c"
#include "src/core/httpcli/httpcli.c"
#include "src/core/httpcli/parser.c"
#include "src/core/iomgr/closure.c"
#include "src/core/iomgr/endpoint.c"
#include "src/core/iomgr/endpoint_pair_posix.c"
#include "src/core/iomgr/endpoint_pair_windows.c"
#include "src/core/iomgr/exec_ctx.c"
#include "src/core/iomgr/executor.c"
#include "src/core/iomgr/fd_posix.c"
#include "src/core/iomgr/iocp_windows.c"
#include "src/core/iomgr/iomgr.c"
#include "src/core/iomgr/iomgr_posix.c"
#include "src/core/iomgr/iomgr_windows.c"
#include "src/core/iomgr/pollset_multipoller_with_epoll.c"
#include "src/core/iomgr/pollset_multipoller_with_poll_posix.c"
#include "src/core/iomgr/pollset_posix.c"
#include "src/core/iomgr/pollset_set_posix.c"
#include "src/core/iomgr/pollset_set_windows.c"
#include "src/core/iomgr/pollset_windows.c"
#include "src/core/iomgr/resolve_address_posix.c"
#include "src/core/iomgr/resolve_address_windows.c"
#include "src/core/iomgr/sockaddr_utils.c"
#include "src/core/iomgr/socket_utils_common_posix.c"
#include "src/core/iomgr/socket_utils_linux.c"
#include "src/core/iomgr/socket_utils_posix.c"
#include "src/core/iomgr/socket_windows.c"
#include "src/core/iomgr/tcp_client_posix.c"
#include "src/core/iomgr/tcp_client_windows.c"
#include "src/core/iomgr/tcp_posix.c"
#include "src/core/iomgr/tcp_server_posix.c"
#include "src/core/iomgr/tcp_server_windows.c"
#include "src/core/iomgr/tcp_windows.c"
#include "src/core/iomgr/time_averaged_stats.c"
#include "src/core/iomgr/timer.c"
#include "src/core/iomgr/timer_heap.c"
#include "src/core/iomgr/udp_server.c"
#include "src/core/iomgr/wakeup_fd_eventfd.c"
#include "src/core/iomgr/wakeup_fd_nospecial.c"
#include "src/core/iomgr/wakeup_fd_pipe.c"
#include "src/core/iomgr/wakeup_fd_posix.c"
#include "src/core/iomgr/workqueue_posix.c"
#include "src/core/iomgr/workqueue_windows.c"
#include "src/core/json/json.c"
#include "src/core/json/json_reader.c"
#include "src/core/json/json_string.c"
#include "src/core/json/json_writer.c"
#include "src/core/surface/api_trace.c"
#include "src/core/surface/byte_buffer.c"
#include "src/core/surface/byte_buffer_reader.c"
#include "src/core/surface/call.c"
#include "src/core/surface/call_details.c"
#include "src/core/surface/call_log_batch.c"
#include "src/core/surface/channel.c"
#include "src/core/surface/channel_connectivity.c"
#include "src/core/surface/channel_create.c"
#include "src/core/surface/channel_ping.c"
#include "src/core/surface/completion_queue.c"
#include "src/core/surface/event_string.c"
#include "src/core/surface/init.c"
#include "src/core/surface/lame_client.c"
#include "src/core/surface/metadata_array.c"
#include "src/core/surface/server.c"
#include "src/core/surface/server_chttp2.c"
#include "src/core/surface/server_create.c"
#include "src/core/surface/version.c"
#include "src/core/transport/byte_stream.c"
#include "src/core/transport/chttp2/alpn.c"
#include "src/core/transport/chttp2/bin_encoder.c"
#include "src/core/transport/chttp2/frame_data.c"
#include "src/core/transport/chttp2/frame_goaway.c"
#include "src/core/transport/chttp2/frame_ping.c"
#include "src/core/transport/chttp2/frame_rst_stream.c"
#include "src/core/transport/chttp2/frame_settings.c"
#include "src/core/transport/chttp2/frame_window_update.c"
#include "src/core/transport/chttp2/hpack_encoder.c"
#include "src/core/transport/chttp2/hpack_parser.c"
#include "src/core/transport/chttp2/hpack_table.c"
#include "src/core/transport/chttp2/huffsyms.c"
#include "src/core/transport/chttp2/incoming_metadata.c"
#include "src/core/transport/chttp2/parsing.c"
#include "src/core/transport/chttp2/status_conversion.c"
#include "src/core/transport/chttp2/stream_lists.c"
#include "src/core/transport/chttp2/stream_map.c"
#include "src/core/transport/chttp2/timeout_encoding.c"
#include "src/core/transport/chttp2/varint.c"
#include "src/core/transport/chttp2/writing.c"
#include "src/core/transport/chttp2_transport.c"
#include "src/core/transport/connectivity_state.c"
#include "src/core/transport/metadata.c"
#include "src/core/transport/metadata_batch.c"
#include "src/core/transport/static_metadata.c"
#include "src/core/transport/transport.c"
#include "src/core/transport/transport_op_string.c"
#include "src/core/census/context.c"
#include "src/core/census/initialize.c"
#include "src/core/census/operation.c"
#include "src/core/census/tracing.c"

#ifdef __cplusplus
}
#endif

#endif /* GRPC_UNSECURE_C_ */
