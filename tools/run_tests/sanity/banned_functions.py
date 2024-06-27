#!/usr/bin/env python3

# Copyright 2024 gRPC authors.
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


# Explicitly ban select functions from being used in gRPC.
#
# Any new instance of a deprecated function being used in the code will be
# flagged by the script. If there is a new instance of a deprecated function in
# a Pull Request, then the Sanity tests will fail for the Pull Request.
# We are currently working on clearing out the usage of deprecated functions in
# the entire gRPC code base.
# While our cleaning is in progress we have a temporary allow list. The allow
# list has a list of files where clean up of deprecated functions is pending.
# As we clean up the deprecated function from files, we will remove them from
# the allow list.
# It would be wise to do the file clean up and the altering of the allow list
# in the same PR. This will make sure that any roll back of a clean up PR will
# also alter the allow list and avoid build failures.

import os
import sys

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), "../../.."))

#  Map of deprecated functions to allowlist files
DEPRECATED_FUNCTION_TEMP_ALLOW_LIST = {
    "gpr_log_severity": [
        "./include/grpc/support/log.h",
        "./src/core/util/android/log.cc",
        "./src/core/util/linux/log.cc",
        "./src/core/util/log.cc",
        "./src/core/util/posix/log.cc",
        "./src/core/util/windows/log.cc",
        "./src/ruby/ext/grpc/rb_grpc_imports.generated.c",
        "./src/ruby/ext/grpc/rb_grpc_imports.generated.h",
        "./test/core/end2end/tests/no_logging.cc",
    ],
    "gpr_log_severity_string": [],
    "gpr_log(": [
        "./include/grpc/support/log.h",
        "./src/core/ext/filters/backend_metrics/backend_metric_filter.cc",
        "./src/core/ext/filters/channel_idle/legacy_channel_idle_filter.cc",
        "./src/core/ext/filters/fault_injection/fault_injection_filter.cc",
        "./src/core/ext/filters/http/message_compress/compression_filter.cc",
        "./src/core/ext/filters/http/server/http_server_filter.cc",
        "./src/core/ext/filters/load_reporting/server_load_reporting_filter.cc",
        "./src/core/ext/filters/logging/logging_filter.cc",
        "./src/core/ext/filters/message_size/message_size_filter.cc",
        "./src/core/ext/transport/binder/client/binder_connector.cc",
        "./src/core/ext/transport/binder/client/channel_create.cc",
        "./src/core/ext/transport/binder/client/jni_utils.cc",
        "./src/core/ext/transport/binder/security_policy/binder_security_policy.cc",
        "./src/core/ext/transport/binder/server/binder_server.cc",
        "./src/core/ext/transport/binder/transport/binder_transport.cc",
        "./src/core/ext/transport/binder/utils/ndk_binder.cc",
        "./src/core/ext/transport/binder/wire_format/binder_android.cc",
        "./src/core/ext/transport/binder/wire_format/wire_reader_impl.cc",
        "./src/core/ext/transport/binder/wire_format/wire_writer.cc",
        "./src/core/ext/transport/chaotic_good/chaotic_good_transport.h",
        "./src/core/ext/transport/chaotic_good/client/chaotic_good_connector.cc",
        "./src/core/ext/transport/chaotic_good/client_transport.cc",
        "./src/core/ext/transport/chaotic_good/server/chaotic_good_server.cc",
        "./src/core/ext/transport/chaotic_good/server_transport.cc",
        "./src/core/ext/transport/chttp2/client/chttp2_connector.cc",
        "./src/core/ext/transport/chttp2/transport/bin_decoder.cc",
        "./src/core/ext/transport/chttp2/transport/chttp2_transport.cc",
        "./src/core/ext/transport/chttp2/transport/flow_control.cc",
        "./src/core/ext/transport/chttp2/transport/frame_ping.cc",
        "./src/core/ext/transport/chttp2/transport/frame_rst_stream.cc",
        "./src/core/ext/transport/chttp2/transport/frame_settings.cc",
        "./src/core/ext/transport/chttp2/transport/hpack_encoder.cc",
        "./src/core/ext/transport/chttp2/transport/hpack_encoder.h",
        "./src/core/ext/transport/chttp2/transport/hpack_parser.cc",
        "./src/core/ext/transport/chttp2/transport/parsing.cc",
        "./src/core/ext/transport/chttp2/transport/stream_lists.cc",
        "./src/core/ext/transport/chttp2/transport/writing.cc",
        "./src/core/ext/transport/cronet/client/secure/cronet_channel_create.cc",
        "./src/core/ext/transport/cronet/transport/cronet_transport.cc",
        "./src/core/ext/transport/inproc/inproc_transport.cc",
        "./src/core/ext/transport/inproc/legacy_inproc_transport.cc",
        "./src/core/handshaker/handshaker.cc",
        "./src/core/handshaker/http_connect/http_connect_handshaker.cc",
        "./src/core/handshaker/http_connect/http_proxy_mapper.cc",
        "./src/core/handshaker/security/secure_endpoint.cc",
        "./src/core/lib/event_engine/ares_resolver.h",
        "./src/core/lib/event_engine/cf_engine/cf_engine.cc",
        "./src/core/lib/event_engine/posix_engine/posix_engine.cc",
        "./src/core/lib/event_engine/posix_engine/timer_manager.cc",
        "./src/core/lib/event_engine/windows/windows_endpoint.cc",
        "./src/core/lib/event_engine/windows/windows_engine.cc",
        "./src/core/lib/gprpp/time.h",
        "./src/core/lib/gprpp/work_serializer.cc",
        "./src/core/lib/iomgr/call_combiner.cc",
        "./src/core/lib/iomgr/call_combiner.h",
        "./src/core/lib/iomgr/cfstream_handle.cc",
        "./src/core/lib/iomgr/closure.h",
        "./src/core/lib/iomgr/combiner.cc",
        "./src/core/lib/iomgr/endpoint_cfstream.cc",
        "./src/core/lib/iomgr/error.cc",
        "./src/core/lib/iomgr/ev_apple.cc",
        "./src/core/lib/iomgr/ev_epoll1_linux.cc",
        "./src/core/lib/iomgr/ev_poll_posix.cc",
        "./src/core/lib/iomgr/ev_posix.cc",
        "./src/core/lib/iomgr/ev_posix.h",
        "./src/core/lib/iomgr/event_engine_shims/closure.cc",
        "./src/core/lib/iomgr/event_engine_shims/endpoint.cc",
        "./src/core/lib/iomgr/exec_ctx.cc",
        "./src/core/lib/iomgr/executor.cc",
        "./src/core/lib/iomgr/lockfree_event.cc",
        "./src/core/lib/iomgr/socket_utils_common_posix.cc",
        "./src/core/lib/iomgr/tcp_client_cfstream.cc",
        "./src/core/lib/iomgr/tcp_client_posix.cc",
        "./src/core/lib/iomgr/tcp_posix.cc",
        "./src/core/lib/iomgr/tcp_server_posix.cc",
        "./src/core/lib/iomgr/tcp_windows.cc",
        "./src/core/lib/iomgr/timer_generic.cc",
        "./src/core/lib/iomgr/timer_manager.cc",
        "./src/core/lib/promise/for_each.h",
        "./src/core/lib/promise/inter_activity_latch.h",
        "./src/core/lib/promise/interceptor_list.h",
        "./src/core/lib/promise/latch.h",
        "./src/core/lib/promise/party.cc",
        "./src/core/lib/promise/party.h",
        "./src/core/lib/promise/pipe.h",
        "./src/core/lib/resource_quota/memory_quota.cc",
        "./src/core/lib/resource_quota/memory_quota.h",
        "./src/core/lib/security/authorization/grpc_authorization_policy_provider.cc",
        "./src/core/lib/security/authorization/grpc_server_authz_filter.cc",
        "./src/core/lib/security/context/security_context.cc",
        "./src/core/lib/security/credentials/jwt/jwt_credentials.cc",
        "./src/core/lib/security/credentials/oauth2/oauth2_credentials.cc",
        "./src/core/lib/security/credentials/plugin/plugin_credentials.cc",
        "./src/core/lib/security/security_connector/security_connector.cc",
        "./src/core/lib/security/transport/server_auth_filter.cc",
        "./src/core/lib/slice/slice_refcount.h",
        "./src/core/lib/surface/api_trace.h",
        "./src/core/lib/surface/call.cc",
        "./src/core/lib/surface/call_utils.cc",
        "./src/core/lib/surface/channel_init.cc",
        "./src/core/lib/surface/completion_queue.cc",
        "./src/core/lib/surface/legacy_channel.cc",
        "./src/core/lib/transport/bdp_estimator.cc",
        "./src/core/lib/transport/bdp_estimator.h",
        "./src/core/lib/transport/call_filters.cc",
        "./src/core/lib/transport/connectivity_state.cc",
        "./src/core/lib/transport/transport.h",
        "./src/core/resolver/dns/c_ares/grpc_ares_wrapper.cc",
        "./src/core/resolver/dns/c_ares/grpc_ares_wrapper.h",
        "./src/core/resolver/dns/event_engine/event_engine_client_channel_resolver.cc",
        "./src/core/resolver/dns/native/dns_resolver.cc",
        "./src/core/resolver/xds/xds_dependency_manager.cc",
        "./src/core/resolver/xds/xds_resolver.cc",
        "./src/core/server/server.cc",
        "./src/core/server/xds_server_config_fetcher.cc",
        "./src/core/tsi/alts/frame_protector/frame_handler.cc",
        "./src/core/tsi/alts/handshaker/alts_handshaker_client.cc",
        "./src/core/tsi/alts/handshaker/alts_tsi_handshaker.cc",
        "./src/core/tsi/alts/handshaker/transport_security_common_api.cc",
        "./src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.cc",
        "./src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.cc",
        "./src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.cc",
        "./src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.cc",
        "./src/core/tsi/fake_transport_security.cc",
        "./src/core/tsi/ssl/key_logging/ssl_key_logging.cc",
        "./src/core/tsi/ssl_transport_security.cc",
        "./src/core/tsi/ssl_transport_security_utils.cc",
        "./src/core/util/android/log.cc",
        "./src/core/util/linux/log.cc",
        "./src/core/util/posix/log.cc",
        "./src/core/util/windows/log.cc",
        "./src/php/ext/grpc/call_credentials.c",
        "./src/php/ext/grpc/channel.c",
        "./src/ruby/ext/grpc/rb_call.c",
        "./src/ruby/ext/grpc/rb_call_credentials.c",
        "./src/ruby/ext/grpc/rb_channel.c",
        "./src/ruby/ext/grpc/rb_event_thread.c",
        "./src/ruby/ext/grpc/rb_grpc.c",
        "./src/ruby/ext/grpc/rb_server.c",
    ],
    "gpr_should_log(": [
        "./include/grpc/support/log.h",
        "./src/core/util/android/log.cc",
        "./src/core/util/linux/log.cc",
        "./src/core/util/log.cc",
        "./src/core/util/posix/log.cc",
        "./src/core/util/windows/log.cc",
        "./src/ruby/ext/grpc/rb_call_credentials.c",
        "./test/core/end2end/tests/no_logging.cc",
    ],
    "gpr_log_message(": [
        "./include/grpc/support/log.h",
        "./src/core/util/android/log.cc",
        "./src/core/util/linux/log.cc",
        "./src/core/util/log.cc",
        "./src/core/util/posix/log.cc",
        "./src/core/util/windows/log.cc",
    ],
    "gpr_set_log_verbosity(": [
        "./include/grpc/support/log.h",
        "./src/core/util/android/log.cc",
        "./src/core/util/linux/log.cc",
        "./src/core/util/log.cc",
        "./src/core/util/posix/log.cc",
        "./src/core/util/windows/log.cc",
        "./test/core/end2end/tests/no_logging.cc",
    ],
    "gpr_log_func_args": [
        "./include/grpc/support/log.h",
        "./src/core/util/log.cc",
        "./test/core/end2end/tests/no_logging.cc",
    ],
    "gpr_set_log_function(": [
        "./include/grpc/support/log.h",
        "./src/core/util/log.cc",
        "./test/core/end2end/tests/no_logging.cc",
    ],
    "gpr_assertion_failed": [],
    "GPR_ASSERT": [],
    "GPR_DEBUG_ASSERT": [],
}

errors = 0
num_files = 0
for root, dirs, files in os.walk("."):
    if root.startswith(
        "./tools/distrib/python/grpcio_tools"
    ) or root.startswith("./src/python"):
        continue
    for filename in files:
        num_files += 1
        path = os.path.join(root, filename)
        if os.path.splitext(path)[1] not in (".h", ".cc", ".c"):
            continue
        with open(path) as f:
            text = f.read()
        for deprecated, allowlist in list(
            DEPRECATED_FUNCTION_TEMP_ALLOW_LIST.items()
        ):
            if path in allowlist:
                continue
            if deprecated in text:
                print(
                    (
                        'Illegal use of "%s" in %s . Use absl functions instead.'
                        % (deprecated, path)
                    )
                )
                errors += 1

assert errors == 0
if errors > 0:
    print(("Number of errors : %d " % (errors)))

# This check comes about from this issue:
# https://github.com/grpc/grpc/issues/15381
# Basically, a change rendered this script useless and we did not realize it.
# This check ensures that this type of issue doesn't occur again.
assert num_files > 18000  # we have more files
# print(('Number of files checked : %d ' % (num_files)))
