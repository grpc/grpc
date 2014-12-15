# GRPC global makefile
# This currently builds C and C++ code.




# General settings.
# You may want to change these depending on your system.

prefix ?= /usr/local

PROTOC = protoc
CC = gcc
CXX = g++
LD = gcc
LDXX = g++
AR = ar
STRIP = strip --strip-unneeded
INSTALL = install -D
RM = rm -f

HOST_CC = $(CC)
HOST_CXX = $(CXX)
HOST_LD = $(LD)
HOST_LDXX = $(LDXX)

ifeq ($(DEBUG),)
CPPFLAGS += -O2
DEFINES += NDEBUG
else
CPPFLAGS += -O0
DEFINES += _DEBUG DEBUG
endif

CFLAGS += -std=c89 -pedantic
CXXFLAGS += -std=c++11
CPPFLAGS += -g -fPIC -Wall -Werror -Wno-long-long
LDFLAGS += -g -pthread -fPIC

INCLUDES = . include gens
LIBS = rt m z event event_pthreads pthread
LIBSXX = protobuf
LIBS_SECURE = ssl crypto dl
LIBS_PROTOC = protoc protobuf

ifneq ($(wildcard /usr/src/gtest/src/gtest-all.cc),)
GTEST_LIB = /usr/src/gtest/src/gtest-all.cc -I/usr/src/gtest
else
GTEST_LIB = -lgtest
endif
GTEST_LIB += -lgflags
ifeq ($(V),1)
E = @:
Q =
else
E = @echo
Q = @
endif

VERSION = 0.8.0.0

CPPFLAGS_NO_ARCH += $(addprefix -I, $(INCLUDES)) $(addprefix -D, $(DEFINES))
CPPFLAGS += $(CPPFLAGS_NO_ARCH) $(ARCH_FLAGS)

LDFLAGS += $(ARCH_FLAGS)
LDLIBS += $(addprefix -l, $(LIBS))
LDLIBSXX += $(addprefix -l, $(LIBSXX))
LDLIBS_SECURE += $(addprefix -l, $(LIBS_SECURE))
HOST_LDLIBS_PROTOC += $(addprefix -l, $(LIBS_PROTOC))

HOST_CPPFLAGS = $(CPPFLAGS)
HOST_CFLAGS = $(CFLAGS)
HOST_CXXFLAGS = $(CXXFLAGS)
HOST_LDFLAGS = $(LDFLAGS)
HOST_LDLIBS = $(LDLIBS)


# These are automatically computed variables.
# There shouldn't be any need to change anything from now on.

HOST_SYSTEM = $(shell uname | cut -f 1 -d_)
ifeq ($(SYSTEM),)
SYSTEM = $(HOST_SYSTEM)
endif

ifeq ($(wildcard .git),)
IS_GIT_FOLDER = false
else
IS_GIT_FOLDER = true
endif

EVENT2_CHECK_CMD = $(CC) $(CFLAGS) $(CPPFLAGS) -o /dev/null test/build/event2.c -levent $(LDFLAGS)
OPENSSL_ALPN_CHECK_CMD = $(CC) $(CFLAGS) $(CPPFLAGS) -o /dev/null test/build/openssl-alpn.c -levent $(LDFLAGS) $(LDLIBS_SECURE)
ZLIB_CHECK_CMD = $(CC) $(CFLAGS) $(CPPFLAGS) -o /dev/null test/build/event2.c -levent $(LDFLAGS)

HAS_SYSTEM_EVENT2 = $(shell $(EVENT2_CHECK_CMD) >& /dev/null && echo true || echo false)
HAS_SYSTEM_OPENSSL_ALPN = $(shell $(OPENSSL_ALPN_CHECK_CMD) >& /dev/null && echo true || echo false)
HAS_SYSTEM_ZLIB = $(shell $(ZLIB_CHECK_CMD) >& /dev/null && echo true || echo false)

ifeq ($(wildcard third_party/libevent/include/event2/event.h),)
HAS_EMBEDDED_EVENT2 = false
else
HAS_EMBEDDED_EVENT2 = true
endif

ifeq ($(wildcard third_party/openssl/ssl/ssl.h),)
HAS_EMBEDDED_OPENSSL_ALPN = false
else
HAS_EMBEDDED_OPENSSL_ALPN = true
endif

ifeq ($(wildcard third_party/zlib/zlib.h),)
HAS_EMBEDDED_ZLIB = false
else
HAS_EMBEDDED_ZLIB = true
endif

ifneq ($(SYSTEM),MINGW32)
ifeq ($(HAS_SYSTEM_EVENT2),false)
DEP_MISSING += libevent
endif
endif

ifeq ($(HAS_SYSTEM_ZLIB),false)
ifeq ($(HAS_EMBEDDED_ZLIB),true)
ZLIB_DEP = third_party/zlib/libz.a
CPPFLAGS += -Ithird_party/zlib
LDFLAGS += -Lthird_party/zlib
else
DEP_MISSING += zlib
endif
endif

ifeq ($(HAS_SYSTEM_OPENSSL_ALPN),false)
ifeq ($(HAS_EMBEDDED_OPENSSL_ALPN),true)
OPENSSL_DEP = third_party/openssl/libssl.a
CPPFLAGS += -Ithird_party/openssl/include
LDFLAGS += -Lthird_party/openssl
else
NO_SECURE = true
endif
endif

ifneq ($(DEP_MISSING),)
NO_DEPS = true
endif

ifneq ($(MAKECMDGOALS),clean)
NO_DEPS = true
endif

.SECONDARY = %.pb.h %.pb.cc

ifeq ($(DEP_MISSING),)
all: static shared
dep_error:
	@echo "You shouldn't see this message - all of your dependencies are correct."
else
all: dep_error git_update stop

dep_error:
	@echo
	@echo "DEPENDENCY ERROR"
	@echo
	@echo "You are missing system dependencies that are essential to build grpc,"
	@echo "and the third_party directory doesn't have them:"
	@echo
	@echo "  $(DEP_MISSING)"
	@echo
	@echo "Installing the development packages for your system will solve"
	@echo "this issue. Please consult INSTALL to get more information."
	@echo
	@echo "If you need information about why these tests failed, run:"
	@echo
	@echo "  make run_dep_checks"
	@echo
endif

git_update:
ifeq ($(IS_GIT_FOLDER),true)
	@echo "Additionally, since you are in a git clone, you can download the"
	@echo "missing dependencies in third_party by running the following command:"
	@echo
	@echo "  git submodule --init update"
	@echo
endif

openssl_dep_error: openssl_dep_message git_update stop

openssl_dep_message:
	@echo
	@echo "DEPENDENCY ERROR"
	@echo
	@echo "The target you are trying to run requires OpenSSL with ALPN support."
	@echo "Your system doesn't have it, and neither does the third_party directory."
	@echo
	@echo "Please consult INSTALL to get more information."
	@echo
	@echo "If you need information about why these tests failed, run:"
	@echo
	@echo "  make run_dep_checks"
	@echo

stop:
	@false

run_dep_checks:
	$(EVENT2_CHECK_CMD) || true
	$(OPENSSL_ALPN_CHECK_CMD) || true
	$(ZLIB_CHECK_CMD) || true

third_party/zlib/libz.a:
	(cd third_party/zlib ; CFLAGS="-fPIC -fvisibility=hidden" ./configure --static)
	$(MAKE) -C third_party/zlib

third_party/openssl/libssl.a:
	(cd third_party/openssl ; CC="$(CC) -fPIC -fvisibility=hidden" ./config)
	$(MAKE) -C third_party/openssl build_crypto build_ssl

static: static_c static_cxx

static_c: dep_c libs/libgpr.a libs/libgrpc.a libs/libgrpc_unsecure.a

static_cxx: dep_cxx libs/libgrpc++.a

shared: shared_c shared_cxx

shared_c: dep_c libs/libgpr.so.$(VERSION) libs/libgrpc.so.$(VERSION) libs/libgrpc_unsecure.so.$(VERSION)

shared_cxx: dep_cxx libs/libgrpc++.so.$(VERSION)

privatelibs: privatelibs_c privatelibs_cxx

privatelibs_c: dep_c libs/libgrpc_test_util.a libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_cancel_after_accept.a libs/libend2end_test_cancel_after_accept_and_writes_closed.a libs/libend2end_test_cancel_after_invoke.a libs/libend2end_test_cancel_before_invoke.a libs/libend2end_test_cancel_in_a_vacuum.a libs/libend2end_test_early_server_shutdown_finishes_inflight_calls.a libs/libend2end_test_early_server_shutdown_finishes_tags.a libs/libend2end_test_invoke_large_request.a libs/libend2end_test_max_concurrent_streams.a libs/libend2end_test_no_op.a libs/libend2end_test_ping_pong_streaming.a libs/libend2end_test_request_response_with_binary_metadata_and_payload.a libs/libend2end_test_request_response_with_metadata_and_payload.a libs/libend2end_test_request_response_with_payload.a libs/libend2end_test_simple_delayed_request.a libs/libend2end_test_simple_request.a libs/libend2end_test_thread_stress.a libs/libend2end_test_writes_done_hangs_with_pending_read.a libs/libend2end_certs.a

privatelibs_cxx: dep_cxx libs/libgrpc_test_util.a libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_cancel_after_accept.a libs/libend2end_test_cancel_after_accept_and_writes_closed.a libs/libend2end_test_cancel_after_invoke.a libs/libend2end_test_cancel_before_invoke.a libs/libend2end_test_cancel_in_a_vacuum.a libs/libend2end_test_early_server_shutdown_finishes_inflight_calls.a libs/libend2end_test_early_server_shutdown_finishes_tags.a libs/libend2end_test_invoke_large_request.a libs/libend2end_test_max_concurrent_streams.a libs/libend2end_test_no_op.a libs/libend2end_test_ping_pong_streaming.a libs/libend2end_test_request_response_with_binary_metadata_and_payload.a libs/libend2end_test_request_response_with_metadata_and_payload.a libs/libend2end_test_request_response_with_payload.a libs/libend2end_test_simple_delayed_request.a libs/libend2end_test_simple_request.a libs/libend2end_test_thread_stress.a libs/libend2end_test_writes_done_hangs_with_pending_read.a libs/libend2end_certs.a

buildtests: buildtests_c buildtests_cxx

buildtests_c: bin_dep_c privatelibs_c bins/grpc_byte_buffer_reader_test bins/gpr_cancellable_test bins/gpr_log_test bins/gpr_useful_test bins/gpr_cmdline_test bins/gpr_histogram_test bins/gpr_host_port_test bins/gpr_slice_buffer_test bins/gpr_slice_test bins/gpr_string_test bins/gpr_sync_test bins/gpr_thd_test bins/gpr_time_test bins/murmur_hash_test bins/grpc_stream_op_test bins/alpn_test bins/chttp2_stream_encoder_test bins/hpack_table_test bins/chttp2_stream_map_test bins/hpack_parser_test bins/transport_metadata_test bins/chttp2_status_conversion_test bins/chttp2_transport_end2end_test bins/tcp_posix_test bins/dualstack_socket_test bins/no_server_test bins/resolve_address_test bins/sockaddr_utils_test bins/tcp_server_posix_test bins/tcp_client_posix_test bins/grpc_channel_stack_test bins/metadata_buffer_test bins/grpc_completion_queue_test bins/census_window_stats_test bins/census_statistics_quick_test bins/census_statistics_performance_test bins/census_statistics_multiple_writers_test bins/census_statistics_multiple_writers_circular_buffer_test bins/census_stub_test bins/census_hash_table_test bins/fling_server bins/fling_client bins/fling_test bins/echo_server bins/echo_client bins/echo_test bins/message_compress_test bins/bin_encoder_test bins/secure_endpoint_test bins/httpcli_format_request_test bins/httpcli_parser_test bins/httpcli_test bins/grpc_credentials_test bins/grpc_base64_test bins/grpc_json_token_test bins/timeout_encoding_test bins/fd_posix_test bins/fling_stream_test bins/lame_client_test bins/alarm_test bins/time_test bins/chttp2_fake_security_cancel_after_accept_test bins/chttp2_fake_security_cancel_after_accept_and_writes_closed_test bins/chttp2_fake_security_cancel_after_invoke_test bins/chttp2_fake_security_cancel_before_invoke_test bins/chttp2_fake_security_cancel_in_a_vacuum_test bins/chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test bins/chttp2_fake_security_early_server_shutdown_finishes_tags_test bins/chttp2_fake_security_invoke_large_request_test bins/chttp2_fake_security_max_concurrent_streams_test bins/chttp2_fake_security_no_op_test bins/chttp2_fake_security_ping_pong_streaming_test bins/chttp2_fake_security_request_response_with_binary_metadata_and_payload_test bins/chttp2_fake_security_request_response_with_metadata_and_payload_test bins/chttp2_fake_security_request_response_with_payload_test bins/chttp2_fake_security_simple_delayed_request_test bins/chttp2_fake_security_simple_request_test bins/chttp2_fake_security_thread_stress_test bins/chttp2_fake_security_writes_done_hangs_with_pending_read_test bins/chttp2_fullstack_cancel_after_accept_test bins/chttp2_fullstack_cancel_after_accept_and_writes_closed_test bins/chttp2_fullstack_cancel_after_invoke_test bins/chttp2_fullstack_cancel_before_invoke_test bins/chttp2_fullstack_cancel_in_a_vacuum_test bins/chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test bins/chttp2_fullstack_early_server_shutdown_finishes_tags_test bins/chttp2_fullstack_invoke_large_request_test bins/chttp2_fullstack_max_concurrent_streams_test bins/chttp2_fullstack_no_op_test bins/chttp2_fullstack_ping_pong_streaming_test bins/chttp2_fullstack_request_response_with_binary_metadata_and_payload_test bins/chttp2_fullstack_request_response_with_metadata_and_payload_test bins/chttp2_fullstack_request_response_with_payload_test bins/chttp2_fullstack_simple_delayed_request_test bins/chttp2_fullstack_simple_request_test bins/chttp2_fullstack_thread_stress_test bins/chttp2_fullstack_writes_done_hangs_with_pending_read_test bins/chttp2_simple_ssl_fullstack_cancel_after_accept_test bins/chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test bins/chttp2_simple_ssl_fullstack_cancel_after_invoke_test bins/chttp2_simple_ssl_fullstack_cancel_before_invoke_test bins/chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test bins/chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test bins/chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test bins/chttp2_simple_ssl_fullstack_invoke_large_request_test bins/chttp2_simple_ssl_fullstack_max_concurrent_streams_test bins/chttp2_simple_ssl_fullstack_no_op_test bins/chttp2_simple_ssl_fullstack_ping_pong_streaming_test bins/chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test bins/chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test bins/chttp2_simple_ssl_fullstack_request_response_with_payload_test bins/chttp2_simple_ssl_fullstack_simple_delayed_request_test bins/chttp2_simple_ssl_fullstack_simple_request_test bins/chttp2_simple_ssl_fullstack_thread_stress_test bins/chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test bins/chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test bins/chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test bins/chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test bins/chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test bins/chttp2_simple_ssl_with_oauth2_fullstack_no_op_test bins/chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test bins/chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test bins/chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test bins/chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test bins/chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test bins/chttp2_socket_pair_cancel_after_accept_test bins/chttp2_socket_pair_cancel_after_accept_and_writes_closed_test bins/chttp2_socket_pair_cancel_after_invoke_test bins/chttp2_socket_pair_cancel_before_invoke_test bins/chttp2_socket_pair_cancel_in_a_vacuum_test bins/chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test bins/chttp2_socket_pair_early_server_shutdown_finishes_tags_test bins/chttp2_socket_pair_invoke_large_request_test bins/chttp2_socket_pair_max_concurrent_streams_test bins/chttp2_socket_pair_no_op_test bins/chttp2_socket_pair_ping_pong_streaming_test bins/chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test bins/chttp2_socket_pair_request_response_with_metadata_and_payload_test bins/chttp2_socket_pair_request_response_with_payload_test bins/chttp2_socket_pair_simple_delayed_request_test bins/chttp2_socket_pair_simple_request_test bins/chttp2_socket_pair_thread_stress_test bins/chttp2_socket_pair_writes_done_hangs_with_pending_read_test bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test bins/chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test bins/chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test bins/chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test bins/chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test bins/chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test bins/chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test bins/chttp2_socket_pair_one_byte_at_a_time_no_op_test bins/chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test bins/chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test bins/chttp2_socket_pair_one_byte_at_a_time_simple_request_test bins/chttp2_socket_pair_one_byte_at_a_time_thread_stress_test bins/chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test

buildtests_cxx: bin_dep_cxx privatelibs_cxx bins/thread_pool_test bins/status_test bins/sync_client_async_server_test bins/qps_client bins/qps_server bins/interop_server bins/interop_client bins/end2end_test

test: test_c test_cxx

test_c: buildtests_c
	$(E) "[RUN]     Testing grpc_byte_buffer_reader_test"
	$(Q) ./bins/grpc_byte_buffer_reader_test || ( echo test grpc_byte_buffer_reader_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_cancellable_test"
	$(Q) ./bins/gpr_cancellable_test || ( echo test gpr_cancellable_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_log_test"
	$(Q) ./bins/gpr_log_test || ( echo test gpr_log_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_useful_test"
	$(Q) ./bins/gpr_useful_test || ( echo test gpr_useful_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_cmdline_test"
	$(Q) ./bins/gpr_cmdline_test || ( echo test gpr_cmdline_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_histogram_test"
	$(Q) ./bins/gpr_histogram_test || ( echo test gpr_histogram_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_host_port_test"
	$(Q) ./bins/gpr_host_port_test || ( echo test gpr_host_port_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_slice_buffer_test"
	$(Q) ./bins/gpr_slice_buffer_test || ( echo test gpr_slice_buffer_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_slice_test"
	$(Q) ./bins/gpr_slice_test || ( echo test gpr_slice_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_string_test"
	$(Q) ./bins/gpr_string_test || ( echo test gpr_string_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_sync_test"
	$(Q) ./bins/gpr_sync_test || ( echo test gpr_sync_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_thd_test"
	$(Q) ./bins/gpr_thd_test || ( echo test gpr_thd_test failed ; exit 1 )
	$(E) "[RUN]     Testing gpr_time_test"
	$(Q) ./bins/gpr_time_test || ( echo test gpr_time_test failed ; exit 1 )
	$(E) "[RUN]     Testing murmur_hash_test"
	$(Q) ./bins/murmur_hash_test || ( echo test murmur_hash_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_stream_op_test"
	$(Q) ./bins/grpc_stream_op_test || ( echo test grpc_stream_op_test failed ; exit 1 )
	$(E) "[RUN]     Testing alpn_test"
	$(Q) ./bins/alpn_test || ( echo test alpn_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_stream_encoder_test"
	$(Q) ./bins/chttp2_stream_encoder_test || ( echo test chttp2_stream_encoder_test failed ; exit 1 )
	$(E) "[RUN]     Testing hpack_table_test"
	$(Q) ./bins/hpack_table_test || ( echo test hpack_table_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_stream_map_test"
	$(Q) ./bins/chttp2_stream_map_test || ( echo test chttp2_stream_map_test failed ; exit 1 )
	$(E) "[RUN]     Testing hpack_parser_test"
	$(Q) ./bins/hpack_parser_test || ( echo test hpack_parser_test failed ; exit 1 )
	$(E) "[RUN]     Testing transport_metadata_test"
	$(Q) ./bins/transport_metadata_test || ( echo test transport_metadata_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_status_conversion_test"
	$(Q) ./bins/chttp2_status_conversion_test || ( echo test chttp2_status_conversion_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_transport_end2end_test"
	$(Q) ./bins/chttp2_transport_end2end_test || ( echo test chttp2_transport_end2end_test failed ; exit 1 )
	$(E) "[RUN]     Testing tcp_posix_test"
	$(Q) ./bins/tcp_posix_test || ( echo test tcp_posix_test failed ; exit 1 )
	$(E) "[RUN]     Testing dualstack_socket_test"
	$(Q) ./bins/dualstack_socket_test || ( echo test dualstack_socket_test failed ; exit 1 )
	$(E) "[RUN]     Testing no_server_test"
	$(Q) ./bins/no_server_test || ( echo test no_server_test failed ; exit 1 )
	$(E) "[RUN]     Testing resolve_address_test"
	$(Q) ./bins/resolve_address_test || ( echo test resolve_address_test failed ; exit 1 )
	$(E) "[RUN]     Testing sockaddr_utils_test"
	$(Q) ./bins/sockaddr_utils_test || ( echo test sockaddr_utils_test failed ; exit 1 )
	$(E) "[RUN]     Testing tcp_server_posix_test"
	$(Q) ./bins/tcp_server_posix_test || ( echo test tcp_server_posix_test failed ; exit 1 )
	$(E) "[RUN]     Testing tcp_client_posix_test"
	$(Q) ./bins/tcp_client_posix_test || ( echo test tcp_client_posix_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_channel_stack_test"
	$(Q) ./bins/grpc_channel_stack_test || ( echo test grpc_channel_stack_test failed ; exit 1 )
	$(E) "[RUN]     Testing metadata_buffer_test"
	$(Q) ./bins/metadata_buffer_test || ( echo test metadata_buffer_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_completion_queue_test"
	$(Q) ./bins/grpc_completion_queue_test || ( echo test grpc_completion_queue_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_window_stats_test"
	$(Q) ./bins/census_window_stats_test || ( echo test census_window_stats_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_statistics_quick_test"
	$(Q) ./bins/census_statistics_quick_test || ( echo test census_statistics_quick_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_statistics_performance_test"
	$(Q) ./bins/census_statistics_performance_test || ( echo test census_statistics_performance_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_statistics_multiple_writers_test"
	$(Q) ./bins/census_statistics_multiple_writers_test || ( echo test census_statistics_multiple_writers_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_statistics_multiple_writers_circular_buffer_test"
	$(Q) ./bins/census_statistics_multiple_writers_circular_buffer_test || ( echo test census_statistics_multiple_writers_circular_buffer_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_stub_test"
	$(Q) ./bins/census_stub_test || ( echo test census_stub_test failed ; exit 1 )
	$(E) "[RUN]     Testing census_hash_table_test"
	$(Q) ./bins/census_hash_table_test || ( echo test census_hash_table_test failed ; exit 1 )
	$(E) "[RUN]     Testing fling_test"
	$(Q) ./bins/fling_test || ( echo test fling_test failed ; exit 1 )
	$(E) "[RUN]     Testing echo_test"
	$(Q) ./bins/echo_test || ( echo test echo_test failed ; exit 1 )
	$(E) "[RUN]     Testing message_compress_test"
	$(Q) ./bins/message_compress_test || ( echo test message_compress_test failed ; exit 1 )
	$(E) "[RUN]     Testing bin_encoder_test"
	$(Q) ./bins/bin_encoder_test || ( echo test bin_encoder_test failed ; exit 1 )
	$(E) "[RUN]     Testing secure_endpoint_test"
	$(Q) ./bins/secure_endpoint_test || ( echo test secure_endpoint_test failed ; exit 1 )
	$(E) "[RUN]     Testing httpcli_format_request_test"
	$(Q) ./bins/httpcli_format_request_test || ( echo test httpcli_format_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing httpcli_parser_test"
	$(Q) ./bins/httpcli_parser_test || ( echo test httpcli_parser_test failed ; exit 1 )
	$(E) "[RUN]     Testing httpcli_test"
	$(Q) ./bins/httpcli_test || ( echo test httpcli_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_credentials_test"
	$(Q) ./bins/grpc_credentials_test || ( echo test grpc_credentials_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_base64_test"
	$(Q) ./bins/grpc_base64_test || ( echo test grpc_base64_test failed ; exit 1 )
	$(E) "[RUN]     Testing grpc_json_token_test"
	$(Q) ./bins/grpc_json_token_test || ( echo test grpc_json_token_test failed ; exit 1 )
	$(E) "[RUN]     Testing timeout_encoding_test"
	$(Q) ./bins/timeout_encoding_test || ( echo test timeout_encoding_test failed ; exit 1 )
	$(E) "[RUN]     Testing fd_posix_test"
	$(Q) ./bins/fd_posix_test || ( echo test fd_posix_test failed ; exit 1 )
	$(E) "[RUN]     Testing fling_stream_test"
	$(Q) ./bins/fling_stream_test || ( echo test fling_stream_test failed ; exit 1 )
	$(E) "[RUN]     Testing lame_client_test"
	$(Q) ./bins/lame_client_test || ( echo test lame_client_test failed ; exit 1 )
	$(E) "[RUN]     Testing alarm_test"
	$(Q) ./bins/alarm_test || ( echo test alarm_test failed ; exit 1 )
	$(E) "[RUN]     Testing time_test"
	$(Q) ./bins/time_test || ( echo test time_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_cancel_after_accept_test"
	$(Q) ./bins/chttp2_fake_security_cancel_after_accept_test || ( echo test chttp2_fake_security_cancel_after_accept_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_cancel_after_accept_and_writes_closed_test"
	$(Q) ./bins/chttp2_fake_security_cancel_after_accept_and_writes_closed_test || ( echo test chttp2_fake_security_cancel_after_accept_and_writes_closed_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_cancel_after_invoke_test"
	$(Q) ./bins/chttp2_fake_security_cancel_after_invoke_test || ( echo test chttp2_fake_security_cancel_after_invoke_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_cancel_before_invoke_test"
	$(Q) ./bins/chttp2_fake_security_cancel_before_invoke_test || ( echo test chttp2_fake_security_cancel_before_invoke_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_cancel_in_a_vacuum_test"
	$(Q) ./bins/chttp2_fake_security_cancel_in_a_vacuum_test || ( echo test chttp2_fake_security_cancel_in_a_vacuum_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test"
	$(Q) ./bins/chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test || ( echo test chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_early_server_shutdown_finishes_tags_test"
	$(Q) ./bins/chttp2_fake_security_early_server_shutdown_finishes_tags_test || ( echo test chttp2_fake_security_early_server_shutdown_finishes_tags_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_invoke_large_request_test"
	$(Q) ./bins/chttp2_fake_security_invoke_large_request_test || ( echo test chttp2_fake_security_invoke_large_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_max_concurrent_streams_test"
	$(Q) ./bins/chttp2_fake_security_max_concurrent_streams_test || ( echo test chttp2_fake_security_max_concurrent_streams_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_no_op_test"
	$(Q) ./bins/chttp2_fake_security_no_op_test || ( echo test chttp2_fake_security_no_op_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_ping_pong_streaming_test"
	$(Q) ./bins/chttp2_fake_security_ping_pong_streaming_test || ( echo test chttp2_fake_security_ping_pong_streaming_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_request_response_with_binary_metadata_and_payload_test"
	$(Q) ./bins/chttp2_fake_security_request_response_with_binary_metadata_and_payload_test || ( echo test chttp2_fake_security_request_response_with_binary_metadata_and_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_request_response_with_metadata_and_payload_test"
	$(Q) ./bins/chttp2_fake_security_request_response_with_metadata_and_payload_test || ( echo test chttp2_fake_security_request_response_with_metadata_and_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_request_response_with_payload_test"
	$(Q) ./bins/chttp2_fake_security_request_response_with_payload_test || ( echo test chttp2_fake_security_request_response_with_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_simple_delayed_request_test"
	$(Q) ./bins/chttp2_fake_security_simple_delayed_request_test || ( echo test chttp2_fake_security_simple_delayed_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_simple_request_test"
	$(Q) ./bins/chttp2_fake_security_simple_request_test || ( echo test chttp2_fake_security_simple_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_thread_stress_test"
	$(Q) ./bins/chttp2_fake_security_thread_stress_test || ( echo test chttp2_fake_security_thread_stress_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fake_security_writes_done_hangs_with_pending_read_test"
	$(Q) ./bins/chttp2_fake_security_writes_done_hangs_with_pending_read_test || ( echo test chttp2_fake_security_writes_done_hangs_with_pending_read_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_cancel_after_accept_test"
	$(Q) ./bins/chttp2_fullstack_cancel_after_accept_test || ( echo test chttp2_fullstack_cancel_after_accept_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_cancel_after_accept_and_writes_closed_test"
	$(Q) ./bins/chttp2_fullstack_cancel_after_accept_and_writes_closed_test || ( echo test chttp2_fullstack_cancel_after_accept_and_writes_closed_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_cancel_after_invoke_test"
	$(Q) ./bins/chttp2_fullstack_cancel_after_invoke_test || ( echo test chttp2_fullstack_cancel_after_invoke_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_cancel_before_invoke_test"
	$(Q) ./bins/chttp2_fullstack_cancel_before_invoke_test || ( echo test chttp2_fullstack_cancel_before_invoke_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_cancel_in_a_vacuum_test"
	$(Q) ./bins/chttp2_fullstack_cancel_in_a_vacuum_test || ( echo test chttp2_fullstack_cancel_in_a_vacuum_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test"
	$(Q) ./bins/chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test || ( echo test chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_early_server_shutdown_finishes_tags_test"
	$(Q) ./bins/chttp2_fullstack_early_server_shutdown_finishes_tags_test || ( echo test chttp2_fullstack_early_server_shutdown_finishes_tags_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_invoke_large_request_test"
	$(Q) ./bins/chttp2_fullstack_invoke_large_request_test || ( echo test chttp2_fullstack_invoke_large_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_max_concurrent_streams_test"
	$(Q) ./bins/chttp2_fullstack_max_concurrent_streams_test || ( echo test chttp2_fullstack_max_concurrent_streams_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_no_op_test"
	$(Q) ./bins/chttp2_fullstack_no_op_test || ( echo test chttp2_fullstack_no_op_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_ping_pong_streaming_test"
	$(Q) ./bins/chttp2_fullstack_ping_pong_streaming_test || ( echo test chttp2_fullstack_ping_pong_streaming_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_request_response_with_binary_metadata_and_payload_test"
	$(Q) ./bins/chttp2_fullstack_request_response_with_binary_metadata_and_payload_test || ( echo test chttp2_fullstack_request_response_with_binary_metadata_and_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_request_response_with_metadata_and_payload_test"
	$(Q) ./bins/chttp2_fullstack_request_response_with_metadata_and_payload_test || ( echo test chttp2_fullstack_request_response_with_metadata_and_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_request_response_with_payload_test"
	$(Q) ./bins/chttp2_fullstack_request_response_with_payload_test || ( echo test chttp2_fullstack_request_response_with_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_simple_delayed_request_test"
	$(Q) ./bins/chttp2_fullstack_simple_delayed_request_test || ( echo test chttp2_fullstack_simple_delayed_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_simple_request_test"
	$(Q) ./bins/chttp2_fullstack_simple_request_test || ( echo test chttp2_fullstack_simple_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_thread_stress_test"
	$(Q) ./bins/chttp2_fullstack_thread_stress_test || ( echo test chttp2_fullstack_thread_stress_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_fullstack_writes_done_hangs_with_pending_read_test"
	$(Q) ./bins/chttp2_fullstack_writes_done_hangs_with_pending_read_test || ( echo test chttp2_fullstack_writes_done_hangs_with_pending_read_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_cancel_after_accept_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_cancel_after_accept_test || ( echo test chttp2_simple_ssl_fullstack_cancel_after_accept_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test || ( echo test chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_cancel_after_invoke_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_cancel_after_invoke_test || ( echo test chttp2_simple_ssl_fullstack_cancel_after_invoke_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_cancel_before_invoke_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_cancel_before_invoke_test || ( echo test chttp2_simple_ssl_fullstack_cancel_before_invoke_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test || ( echo test chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test || ( echo test chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test || ( echo test chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_invoke_large_request_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_invoke_large_request_test || ( echo test chttp2_simple_ssl_fullstack_invoke_large_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_max_concurrent_streams_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_max_concurrent_streams_test || ( echo test chttp2_simple_ssl_fullstack_max_concurrent_streams_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_no_op_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_no_op_test || ( echo test chttp2_simple_ssl_fullstack_no_op_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_ping_pong_streaming_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_ping_pong_streaming_test || ( echo test chttp2_simple_ssl_fullstack_ping_pong_streaming_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test || ( echo test chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test || ( echo test chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_request_response_with_payload_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_request_response_with_payload_test || ( echo test chttp2_simple_ssl_fullstack_request_response_with_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_simple_delayed_request_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_simple_delayed_request_test || ( echo test chttp2_simple_ssl_fullstack_simple_delayed_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_simple_request_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_simple_request_test || ( echo test chttp2_simple_ssl_fullstack_simple_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_thread_stress_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_thread_stress_test || ( echo test chttp2_simple_ssl_fullstack_thread_stress_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test"
	$(Q) ./bins/chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test || ( echo test chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_no_op_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_no_op_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_no_op_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test"
	$(Q) ./bins/chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test || ( echo test chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_cancel_after_accept_test"
	$(Q) ./bins/chttp2_socket_pair_cancel_after_accept_test || ( echo test chttp2_socket_pair_cancel_after_accept_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_cancel_after_accept_and_writes_closed_test"
	$(Q) ./bins/chttp2_socket_pair_cancel_after_accept_and_writes_closed_test || ( echo test chttp2_socket_pair_cancel_after_accept_and_writes_closed_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_cancel_after_invoke_test"
	$(Q) ./bins/chttp2_socket_pair_cancel_after_invoke_test || ( echo test chttp2_socket_pair_cancel_after_invoke_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_cancel_before_invoke_test"
	$(Q) ./bins/chttp2_socket_pair_cancel_before_invoke_test || ( echo test chttp2_socket_pair_cancel_before_invoke_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_cancel_in_a_vacuum_test"
	$(Q) ./bins/chttp2_socket_pair_cancel_in_a_vacuum_test || ( echo test chttp2_socket_pair_cancel_in_a_vacuum_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test"
	$(Q) ./bins/chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test || ( echo test chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_early_server_shutdown_finishes_tags_test"
	$(Q) ./bins/chttp2_socket_pair_early_server_shutdown_finishes_tags_test || ( echo test chttp2_socket_pair_early_server_shutdown_finishes_tags_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_invoke_large_request_test"
	$(Q) ./bins/chttp2_socket_pair_invoke_large_request_test || ( echo test chttp2_socket_pair_invoke_large_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_max_concurrent_streams_test"
	$(Q) ./bins/chttp2_socket_pair_max_concurrent_streams_test || ( echo test chttp2_socket_pair_max_concurrent_streams_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_no_op_test"
	$(Q) ./bins/chttp2_socket_pair_no_op_test || ( echo test chttp2_socket_pair_no_op_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_ping_pong_streaming_test"
	$(Q) ./bins/chttp2_socket_pair_ping_pong_streaming_test || ( echo test chttp2_socket_pair_ping_pong_streaming_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test"
	$(Q) ./bins/chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test || ( echo test chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_request_response_with_metadata_and_payload_test"
	$(Q) ./bins/chttp2_socket_pair_request_response_with_metadata_and_payload_test || ( echo test chttp2_socket_pair_request_response_with_metadata_and_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_request_response_with_payload_test"
	$(Q) ./bins/chttp2_socket_pair_request_response_with_payload_test || ( echo test chttp2_socket_pair_request_response_with_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_simple_delayed_request_test"
	$(Q) ./bins/chttp2_socket_pair_simple_delayed_request_test || ( echo test chttp2_socket_pair_simple_delayed_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_simple_request_test"
	$(Q) ./bins/chttp2_socket_pair_simple_request_test || ( echo test chttp2_socket_pair_simple_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_thread_stress_test"
	$(Q) ./bins/chttp2_socket_pair_thread_stress_test || ( echo test chttp2_socket_pair_thread_stress_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_writes_done_hangs_with_pending_read_test"
	$(Q) ./bins/chttp2_socket_pair_writes_done_hangs_with_pending_read_test || ( echo test chttp2_socket_pair_writes_done_hangs_with_pending_read_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_no_op_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_no_op_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_no_op_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_simple_request_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_simple_request_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_simple_request_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_thread_stress_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_thread_stress_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_thread_stress_test failed ; exit 1 )
	$(E) "[RUN]     Testing chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test"
	$(Q) ./bins/chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test || ( echo test chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test failed ; exit 1 )


test_cxx: buildtests_cxx
	$(E) "[RUN]     Testing thread_pool_test"
	$(Q) ./bins/thread_pool_test || ( echo test thread_pool_test failed ; exit 1 )
	$(E) "[RUN]     Testing status_test"
	$(Q) ./bins/status_test || ( echo test status_test failed ; exit 1 )
	$(E) "[RUN]     Testing sync_client_async_server_test"
	$(Q) ./bins/sync_client_async_server_test || ( echo test sync_client_async_server_test failed ; exit 1 )
	$(E) "[RUN]     Testing qps_client"
	$(Q) ./bins/qps_client || ( echo test qps_client failed ; exit 1 )
	$(E) "[RUN]     Testing qps_server"
	$(Q) ./bins/qps_server || ( echo test qps_server failed ; exit 1 )
	$(E) "[RUN]     Testing end2end_test"
	$(Q) ./bins/end2end_test || ( echo test end2end_test failed ; exit 1 )


tools: privatelibs bins/gen_hpack_tables

protoc_plugins: bins/cpp_plugin bins/ruby_plugin

buildbenchmarks: privatelibs bins/grpc_completion_queue_benchmark bins/low_level_ping_pong_benchmark

benchmarks: buildbenchmarks

strip: strip-static strip-shared

strip-static_c: static_c
	$(E) "[STRIP]   Stripping libgpr.a"
	$(Q) $(STRIP) libs/libgpr.a
	$(E) "[STRIP]   Stripping libgrpc.a"
	$(Q) $(STRIP) libs/libgrpc.a
	$(E) "[STRIP]   Stripping libgrpc_unsecure.a"
	$(Q) $(STRIP) libs/libgrpc_unsecure.a

strip-static_cxx: static_cxx
	$(E) "[STRIP]   Stripping libgrpc++.a"
	$(Q) $(STRIP) libs/libgrpc++.a

strip-shared_c: shared_c
	$(E) "[STRIP]   Stripping libgpr.so"
	$(Q) $(STRIP) libs/libgpr.so.$(VERSION)
	$(E) "[STRIP]   Stripping libgrpc.so"
	$(Q) $(STRIP) libs/libgrpc.so.$(VERSION)
	$(E) "[STRIP]   Stripping libgrpc_unsecure.so"
	$(Q) $(STRIP) libs/libgrpc_unsecure.so.$(VERSION)

strip-shared_cxx: shared_cxx
	$(E) "[STRIP]   Stripping libgrpc++.so"
	$(Q) $(STRIP) libs/libgrpc++.so.$(VERSION)

deps/gens/test/cpp/interop/empty.pb.dep:
	$(Q) mkdir -p `dirname $@`
	$(Q) touch $@

gens/test/cpp/interop/empty.pb.cc: test/cpp/interop/empty.proto protoc_plugins
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) --cpp_out=gens --grpc_out=gens --plugin=protoc-gen-grpc=bins/cpp_plugin $<

deps/gens/test/cpp/interop/messages.pb.dep:
	$(Q) mkdir -p `dirname $@`
	$(Q) touch $@

gens/test/cpp/interop/messages.pb.cc: test/cpp/interop/messages.proto protoc_plugins
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) --cpp_out=gens --grpc_out=gens --plugin=protoc-gen-grpc=bins/cpp_plugin $<

deps/gens/test/cpp/interop/test.pb.dep:
	$(Q) mkdir -p `dirname $@`
	$(Q) touch $@

gens/test/cpp/interop/test.pb.cc: test/cpp/interop/test.proto protoc_plugins
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) --cpp_out=gens --grpc_out=gens --plugin=protoc-gen-grpc=bins/cpp_plugin $<

deps/gens/test/cpp/util/echo.pb.dep:
	$(Q) mkdir -p `dirname $@`
	$(Q) touch $@

gens/test/cpp/util/echo.pb.cc: test/cpp/util/echo.proto protoc_plugins
	$(E) "[PROTOC]  Generating protobuf CC file from $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(PROTOC) --cpp_out=gens --grpc_out=gens --plugin=protoc-gen-grpc=bins/cpp_plugin $<


deps/%.dep : %.c
	$(E) "[DEP]     Generating dependencies for $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(CC) $(CFLAGS) $(CPPFLAGS_NO_ARCH) -MG -M $< > $@

deps/%.dep : %.cc
	$(E) "[DEP]     Generating dependencies for $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(CXX) $(CXXFLAGS) $(CPPFLAGS_NO_ARCH) -MG -M $< > $@

objs/%.o : %.c
	$(E) "[C]       Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

objs/%.o : gens/%.pb.cc
	$(E) "[CXX]     Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

objs/src/compiler/%.o : src/compiler/%.cc
	$(E) "[HOSTCXX] Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(HOST_CXX) $(HOST_CXXFLAGS) $(HOST_CPPFLAGS) -c -o $@ $<

objs/%.o : %.cc
	$(E) "[CXX]     Compiling $<"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

dep: dep_c dep_cxx

dep_c: deps_libgpr deps_libgrpc deps_libgrpc_test_util deps_libend2end_fixture_chttp2_fake_security deps_libend2end_fixture_chttp2_fullstack deps_libend2end_fixture_chttp2_simple_ssl_fullstack deps_libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack deps_libend2end_fixture_chttp2_socket_pair deps_libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time deps_libend2end_test_cancel_after_accept deps_libend2end_test_cancel_after_accept_and_writes_closed deps_libend2end_test_cancel_after_invoke deps_libend2end_test_cancel_before_invoke deps_libend2end_test_cancel_in_a_vacuum deps_libend2end_test_early_server_shutdown_finishes_inflight_calls deps_libend2end_test_early_server_shutdown_finishes_tags deps_libend2end_test_invoke_large_request deps_libend2end_test_max_concurrent_streams deps_libend2end_test_no_op deps_libend2end_test_ping_pong_streaming deps_libend2end_test_request_response_with_binary_metadata_and_payload deps_libend2end_test_request_response_with_metadata_and_payload deps_libend2end_test_request_response_with_payload deps_libend2end_test_simple_delayed_request deps_libend2end_test_simple_request deps_libend2end_test_thread_stress deps_libend2end_test_writes_done_hangs_with_pending_read deps_libend2end_certs deps_libgrpc_unsecure

bins_dep_c: deps_gen_hpack_tables deps_grpc_byte_buffer_reader_test deps_gpr_cancellable_test deps_gpr_log_test deps_gpr_useful_test deps_gpr_cmdline_test deps_gpr_histogram_test deps_gpr_host_port_test deps_gpr_slice_buffer_test deps_gpr_slice_test deps_gpr_string_test deps_gpr_sync_test deps_gpr_thd_test deps_gpr_time_test deps_murmur_hash_test deps_grpc_stream_op_test deps_alpn_test deps_chttp2_stream_encoder_test deps_hpack_table_test deps_chttp2_stream_map_test deps_hpack_parser_test deps_transport_metadata_test deps_chttp2_status_conversion_test deps_chttp2_transport_end2end_test deps_tcp_posix_test deps_dualstack_socket_test deps_no_server_test deps_resolve_address_test deps_sockaddr_utils_test deps_tcp_server_posix_test deps_tcp_client_posix_test deps_grpc_channel_stack_test deps_metadata_buffer_test deps_grpc_completion_queue_test deps_grpc_completion_queue_benchmark deps_census_window_stats_test deps_census_statistics_quick_test deps_census_statistics_performance_test deps_census_statistics_multiple_writers_test deps_census_statistics_multiple_writers_circular_buffer_test deps_census_stub_test deps_census_hash_table_test deps_fling_server deps_fling_client deps_fling_test deps_echo_server deps_echo_client deps_echo_test deps_low_level_ping_pong_benchmark deps_message_compress_test deps_bin_encoder_test deps_secure_endpoint_test deps_httpcli_format_request_test deps_httpcli_parser_test deps_httpcli_test deps_grpc_credentials_test deps_grpc_base64_test deps_grpc_json_token_test deps_timeout_encoding_test deps_fd_posix_test deps_fling_stream_test deps_lame_client_test deps_alarm_test deps_time_test deps_chttp2_fake_security_cancel_after_accept_test deps_chttp2_fake_security_cancel_after_accept_and_writes_closed_test deps_chttp2_fake_security_cancel_after_invoke_test deps_chttp2_fake_security_cancel_before_invoke_test deps_chttp2_fake_security_cancel_in_a_vacuum_test deps_chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test deps_chttp2_fake_security_early_server_shutdown_finishes_tags_test deps_chttp2_fake_security_invoke_large_request_test deps_chttp2_fake_security_max_concurrent_streams_test deps_chttp2_fake_security_no_op_test deps_chttp2_fake_security_ping_pong_streaming_test deps_chttp2_fake_security_request_response_with_binary_metadata_and_payload_test deps_chttp2_fake_security_request_response_with_metadata_and_payload_test deps_chttp2_fake_security_request_response_with_payload_test deps_chttp2_fake_security_simple_delayed_request_test deps_chttp2_fake_security_simple_request_test deps_chttp2_fake_security_thread_stress_test deps_chttp2_fake_security_writes_done_hangs_with_pending_read_test deps_chttp2_fullstack_cancel_after_accept_test deps_chttp2_fullstack_cancel_after_accept_and_writes_closed_test deps_chttp2_fullstack_cancel_after_invoke_test deps_chttp2_fullstack_cancel_before_invoke_test deps_chttp2_fullstack_cancel_in_a_vacuum_test deps_chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test deps_chttp2_fullstack_early_server_shutdown_finishes_tags_test deps_chttp2_fullstack_invoke_large_request_test deps_chttp2_fullstack_max_concurrent_streams_test deps_chttp2_fullstack_no_op_test deps_chttp2_fullstack_ping_pong_streaming_test deps_chttp2_fullstack_request_response_with_binary_metadata_and_payload_test deps_chttp2_fullstack_request_response_with_metadata_and_payload_test deps_chttp2_fullstack_request_response_with_payload_test deps_chttp2_fullstack_simple_delayed_request_test deps_chttp2_fullstack_simple_request_test deps_chttp2_fullstack_thread_stress_test deps_chttp2_fullstack_writes_done_hangs_with_pending_read_test deps_chttp2_simple_ssl_fullstack_cancel_after_accept_test deps_chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test deps_chttp2_simple_ssl_fullstack_cancel_after_invoke_test deps_chttp2_simple_ssl_fullstack_cancel_before_invoke_test deps_chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test deps_chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test deps_chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test deps_chttp2_simple_ssl_fullstack_invoke_large_request_test deps_chttp2_simple_ssl_fullstack_max_concurrent_streams_test deps_chttp2_simple_ssl_fullstack_no_op_test deps_chttp2_simple_ssl_fullstack_ping_pong_streaming_test deps_chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test deps_chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test deps_chttp2_simple_ssl_fullstack_request_response_with_payload_test deps_chttp2_simple_ssl_fullstack_simple_delayed_request_test deps_chttp2_simple_ssl_fullstack_simple_request_test deps_chttp2_simple_ssl_fullstack_thread_stress_test deps_chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test deps_chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test deps_chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test deps_chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test deps_chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test deps_chttp2_simple_ssl_with_oauth2_fullstack_no_op_test deps_chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test deps_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test deps_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test deps_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test deps_chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test deps_chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test deps_chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test deps_chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test deps_chttp2_socket_pair_cancel_after_accept_test deps_chttp2_socket_pair_cancel_after_accept_and_writes_closed_test deps_chttp2_socket_pair_cancel_after_invoke_test deps_chttp2_socket_pair_cancel_before_invoke_test deps_chttp2_socket_pair_cancel_in_a_vacuum_test deps_chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test deps_chttp2_socket_pair_early_server_shutdown_finishes_tags_test deps_chttp2_socket_pair_invoke_large_request_test deps_chttp2_socket_pair_max_concurrent_streams_test deps_chttp2_socket_pair_no_op_test deps_chttp2_socket_pair_ping_pong_streaming_test deps_chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test deps_chttp2_socket_pair_request_response_with_metadata_and_payload_test deps_chttp2_socket_pair_request_response_with_payload_test deps_chttp2_socket_pair_simple_delayed_request_test deps_chttp2_socket_pair_simple_request_test deps_chttp2_socket_pair_thread_stress_test deps_chttp2_socket_pair_writes_done_hangs_with_pending_read_test deps_chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test deps_chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test deps_chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test deps_chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test deps_chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test deps_chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test deps_chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test deps_chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test deps_chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test deps_chttp2_socket_pair_one_byte_at_a_time_no_op_test deps_chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test deps_chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test deps_chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test deps_chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test deps_chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test deps_chttp2_socket_pair_one_byte_at_a_time_simple_request_test deps_chttp2_socket_pair_one_byte_at_a_time_thread_stress_test deps_chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test

dep_cxx: deps_libgrpc++ deps_libgrpc++_test_util

bins_dep_cxx: deps_cpp_plugin deps_ruby_plugin deps_thread_pool_test deps_status_test deps_sync_client_async_server_test deps_qps_client deps_qps_server deps_interop_server deps_interop_client deps_end2end_test

install: install_c install_cxx

install_c: install-headers_c install-static_c install-shared_c

install_cxx: install-headers_cxx install-static_cxx install-shared_cxx

install-headers: install-headers_c install-headers_cxx

install-headers_c:
	$(E) "[INSTALL] Installing public C headers"
	$(Q) $(foreach h, $(PUBLIC_HEADERS_C), $(INSTALL) $(h) $(prefix)/$(h) && ) exit 0 || exit 1

install-headers_cxx:
	$(E) "[INSTALL] Installing public C++ headers"
	$(Q) $(foreach h, $(PUBLIC_HEADERS_CXX), $(INSTALL) $(h) $(prefix)/$(h) && ) exit 0 || exit 1

install-static: install-static_c install-static_cxx

install-static_c: static_c strip-static_c
	$(E) "[INSTALL] Installing libgpr.a"
	$(Q) $(INSTALL) libs/libgpr.a $(prefix)/lib/libgpr.a
	$(E) "[INSTALL] Installing libgrpc.a"
	$(Q) $(INSTALL) libs/libgrpc.a $(prefix)/lib/libgrpc.a
	$(E) "[INSTALL] Installing libgrpc_unsecure.a"
	$(Q) $(INSTALL) libs/libgrpc_unsecure.a $(prefix)/lib/libgrpc_unsecure.a

install-static_cxx: static_cxx strip-static_cxx
	$(E) "[INSTALL] Installing libgrpc++.a"
	$(Q) $(INSTALL) libs/libgrpc++.a $(prefix)/lib/libgrpc++.a

install-shared_c: shared_c strip-shared_c
	$(E) "[INSTALL] Installing libgpr.so"
	$(Q) $(INSTALL) libs/libgpr.so.$(VERSION) $(prefix)/lib/libgpr.so.$(VERSION)
	$(E) "[INSTALL] Installing libgrpc.so"
	$(Q) $(INSTALL) libs/libgrpc.so.$(VERSION) $(prefix)/lib/libgrpc.so.$(VERSION)
	$(E) "[INSTALL] Installing libgrpc_unsecure.so"
	$(Q) $(INSTALL) libs/libgrpc_unsecure.so.$(VERSION) $(prefix)/lib/libgrpc_unsecure.so.$(VERSION)

install-shared_cxx: shared_cxx strip-shared_cxx
	$(E) "[INSTALL] Installing libgrpc++.so"
	$(Q) $(INSTALL) libs/libgrpc++.so.$(VERSION) $(prefix)/lib/libgrpc++.so.$(VERSION)

clean: clean_libgpr clean_libgrpc clean_libgrpc_test_util clean_libgrpc++ clean_libgrpc++_test_util clean_libend2end_fixture_chttp2_fake_security clean_libend2end_fixture_chttp2_fullstack clean_libend2end_fixture_chttp2_simple_ssl_fullstack clean_libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack clean_libend2end_fixture_chttp2_socket_pair clean_libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time clean_libend2end_test_cancel_after_accept clean_libend2end_test_cancel_after_accept_and_writes_closed clean_libend2end_test_cancel_after_invoke clean_libend2end_test_cancel_before_invoke clean_libend2end_test_cancel_in_a_vacuum clean_libend2end_test_early_server_shutdown_finishes_inflight_calls clean_libend2end_test_early_server_shutdown_finishes_tags clean_libend2end_test_invoke_large_request clean_libend2end_test_max_concurrent_streams clean_libend2end_test_no_op clean_libend2end_test_ping_pong_streaming clean_libend2end_test_request_response_with_binary_metadata_and_payload clean_libend2end_test_request_response_with_metadata_and_payload clean_libend2end_test_request_response_with_payload clean_libend2end_test_simple_delayed_request clean_libend2end_test_simple_request clean_libend2end_test_thread_stress clean_libend2end_test_writes_done_hangs_with_pending_read clean_libend2end_certs clean_libgrpc_unsecure clean_gen_hpack_tables clean_cpp_plugin clean_ruby_plugin clean_grpc_byte_buffer_reader_test clean_gpr_cancellable_test clean_gpr_log_test clean_gpr_useful_test clean_gpr_cmdline_test clean_gpr_histogram_test clean_gpr_host_port_test clean_gpr_slice_buffer_test clean_gpr_slice_test clean_gpr_string_test clean_gpr_sync_test clean_gpr_thd_test clean_gpr_time_test clean_murmur_hash_test clean_grpc_stream_op_test clean_alpn_test clean_chttp2_stream_encoder_test clean_hpack_table_test clean_chttp2_stream_map_test clean_hpack_parser_test clean_transport_metadata_test clean_chttp2_status_conversion_test clean_chttp2_transport_end2end_test clean_tcp_posix_test clean_dualstack_socket_test clean_no_server_test clean_resolve_address_test clean_sockaddr_utils_test clean_tcp_server_posix_test clean_tcp_client_posix_test clean_grpc_channel_stack_test clean_metadata_buffer_test clean_grpc_completion_queue_test clean_grpc_completion_queue_benchmark clean_census_window_stats_test clean_census_statistics_quick_test clean_census_statistics_performance_test clean_census_statistics_multiple_writers_test clean_census_statistics_multiple_writers_circular_buffer_test clean_census_stub_test clean_census_hash_table_test clean_fling_server clean_fling_client clean_fling_test clean_echo_server clean_echo_client clean_echo_test clean_low_level_ping_pong_benchmark clean_message_compress_test clean_bin_encoder_test clean_secure_endpoint_test clean_httpcli_format_request_test clean_httpcli_parser_test clean_httpcli_test clean_grpc_credentials_test clean_grpc_base64_test clean_grpc_json_token_test clean_timeout_encoding_test clean_fd_posix_test clean_fling_stream_test clean_lame_client_test clean_thread_pool_test clean_status_test clean_sync_client_async_server_test clean_qps_client clean_qps_server clean_interop_server clean_interop_client clean_end2end_test clean_alarm_test clean_time_test clean_chttp2_fake_security_cancel_after_accept_test clean_chttp2_fake_security_cancel_after_accept_and_writes_closed_test clean_chttp2_fake_security_cancel_after_invoke_test clean_chttp2_fake_security_cancel_before_invoke_test clean_chttp2_fake_security_cancel_in_a_vacuum_test clean_chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test clean_chttp2_fake_security_early_server_shutdown_finishes_tags_test clean_chttp2_fake_security_invoke_large_request_test clean_chttp2_fake_security_max_concurrent_streams_test clean_chttp2_fake_security_no_op_test clean_chttp2_fake_security_ping_pong_streaming_test clean_chttp2_fake_security_request_response_with_binary_metadata_and_payload_test clean_chttp2_fake_security_request_response_with_metadata_and_payload_test clean_chttp2_fake_security_request_response_with_payload_test clean_chttp2_fake_security_simple_delayed_request_test clean_chttp2_fake_security_simple_request_test clean_chttp2_fake_security_thread_stress_test clean_chttp2_fake_security_writes_done_hangs_with_pending_read_test clean_chttp2_fullstack_cancel_after_accept_test clean_chttp2_fullstack_cancel_after_accept_and_writes_closed_test clean_chttp2_fullstack_cancel_after_invoke_test clean_chttp2_fullstack_cancel_before_invoke_test clean_chttp2_fullstack_cancel_in_a_vacuum_test clean_chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test clean_chttp2_fullstack_early_server_shutdown_finishes_tags_test clean_chttp2_fullstack_invoke_large_request_test clean_chttp2_fullstack_max_concurrent_streams_test clean_chttp2_fullstack_no_op_test clean_chttp2_fullstack_ping_pong_streaming_test clean_chttp2_fullstack_request_response_with_binary_metadata_and_payload_test clean_chttp2_fullstack_request_response_with_metadata_and_payload_test clean_chttp2_fullstack_request_response_with_payload_test clean_chttp2_fullstack_simple_delayed_request_test clean_chttp2_fullstack_simple_request_test clean_chttp2_fullstack_thread_stress_test clean_chttp2_fullstack_writes_done_hangs_with_pending_read_test clean_chttp2_simple_ssl_fullstack_cancel_after_accept_test clean_chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test clean_chttp2_simple_ssl_fullstack_cancel_after_invoke_test clean_chttp2_simple_ssl_fullstack_cancel_before_invoke_test clean_chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test clean_chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test clean_chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test clean_chttp2_simple_ssl_fullstack_invoke_large_request_test clean_chttp2_simple_ssl_fullstack_max_concurrent_streams_test clean_chttp2_simple_ssl_fullstack_no_op_test clean_chttp2_simple_ssl_fullstack_ping_pong_streaming_test clean_chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test clean_chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test clean_chttp2_simple_ssl_fullstack_request_response_with_payload_test clean_chttp2_simple_ssl_fullstack_simple_delayed_request_test clean_chttp2_simple_ssl_fullstack_simple_request_test clean_chttp2_simple_ssl_fullstack_thread_stress_test clean_chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test clean_chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test clean_chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test clean_chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test clean_chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test clean_chttp2_simple_ssl_with_oauth2_fullstack_no_op_test clean_chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test clean_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test clean_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test clean_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test clean_chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test clean_chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test clean_chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test clean_chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test clean_chttp2_socket_pair_cancel_after_accept_test clean_chttp2_socket_pair_cancel_after_accept_and_writes_closed_test clean_chttp2_socket_pair_cancel_after_invoke_test clean_chttp2_socket_pair_cancel_before_invoke_test clean_chttp2_socket_pair_cancel_in_a_vacuum_test clean_chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test clean_chttp2_socket_pair_early_server_shutdown_finishes_tags_test clean_chttp2_socket_pair_invoke_large_request_test clean_chttp2_socket_pair_max_concurrent_streams_test clean_chttp2_socket_pair_no_op_test clean_chttp2_socket_pair_ping_pong_streaming_test clean_chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test clean_chttp2_socket_pair_request_response_with_metadata_and_payload_test clean_chttp2_socket_pair_request_response_with_payload_test clean_chttp2_socket_pair_simple_delayed_request_test clean_chttp2_socket_pair_simple_request_test clean_chttp2_socket_pair_thread_stress_test clean_chttp2_socket_pair_writes_done_hangs_with_pending_read_test clean_chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test clean_chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test clean_chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test clean_chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test clean_chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test clean_chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test clean_chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test clean_chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test clean_chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test clean_chttp2_socket_pair_one_byte_at_a_time_no_op_test clean_chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test clean_chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test clean_chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test clean_chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test clean_chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test clean_chttp2_socket_pair_one_byte_at_a_time_simple_request_test clean_chttp2_socket_pair_one_byte_at_a_time_thread_stress_test clean_chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test
	$(Q) $(RM) -r deps objs libs bins gens


# The various libraries


LIBGPR_SRC = \
    src/core/support/alloc.c \
    src/core/support/cancellable.c \
    src/core/support/cmdline.c \
    src/core/support/cpu_posix.c \
    src/core/support/histogram.c \
    src/core/support/host_port.c \
    src/core/support/log.c \
    src/core/support/log_posix.c \
    src/core/support/log_linux.c \
    src/core/support/log_android.c \
    src/core/support/log_win32.c \
    src/core/support/murmur_hash.c \
    src/core/support/slice.c \
    src/core/support/slice_buffer.c \
    src/core/support/string.c \
    src/core/support/string_posix.c \
    src/core/support/string_win32.c \
    src/core/support/sync.c \
    src/core/support/sync_posix.c \
    src/core/support/sync_win32.c \
    src/core/support/thd_posix.c \
    src/core/support/thd_win32.c \
    src/core/support/time.c \
    src/core/support/time_posix.c \
    src/core/support/time_win32.c \

PUBLIC_HEADERS_C += \
    include/grpc/support/alloc.h \
    include/grpc/support/atm_gcc_atomic.h \
    include/grpc/support/atm_gcc_sync.h \
    include/grpc/support/atm.h \
    include/grpc/support/atm_win32.h \
    include/grpc/support/cancellable_platform.h \
    include/grpc/support/cmdline.h \
    include/grpc/support/histogram.h \
    include/grpc/support/host_port.h \
    include/grpc/support/log.h \
    include/grpc/support/port_platform.h \
    include/grpc/support/slice_buffer.h \
    include/grpc/support/slice.h \
    include/grpc/support/string.h \
    include/grpc/support/sync_generic.h \
    include/grpc/support/sync.h \
    include/grpc/support/sync_posix.h \
    include/grpc/support/sync_win32.h \
    include/grpc/support/thd.h \
    include/grpc/support/thd_posix.h \
    include/grpc/support/thd_win32.h \
    include/grpc/support/time.h \
    include/grpc/support/time_posix.h \
    include/grpc/support/time_win32.h \
    include/grpc/support/useful.h \

LIBGPR_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBGPR_SRC))))
LIBGPR_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBGPR_SRC))))

libs/libgpr.a: $(LIBGPR_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libgpr.a $(LIBGPR_OBJS)

libs/libgpr.so.$(VERSION): $(LIBGPR_OBJS)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) -shared -Wl,-soname,libgpr.so.0 -o libs/libgpr.so.$(VERSION) $(LIBGPR_OBJS) $(LDLIBS)

deps_libgpr: $(LIBGPR_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBGPR_DEPS)
endif

clean_libgpr:
	$(E) "[CLEAN]   Cleaning libgpr files"
	$(Q) $(RM) $(LIBGPR_OBJS)
	$(Q) $(RM) $(LIBGPR_DEPS)
	$(Q) $(RM) libs/libgpr.a
	$(Q) $(RM) libs/libgpr.so.$(VERSION)


LIBGRPC_SRC = \
    src/core/channel/call_op_string.c \
    src/core/channel/census_filter.c \
    src/core/channel/channel_args.c \
    src/core/channel/channel_stack.c \
    src/core/channel/child_channel.c \
    src/core/channel/client_channel.c \
    src/core/channel/client_setup.c \
    src/core/channel/connected_channel.c \
    src/core/channel/http_client_filter.c \
    src/core/channel/http_filter.c \
    src/core/channel/http_server_filter.c \
    src/core/channel/metadata_buffer.c \
    src/core/channel/noop_filter.c \
    src/core/compression/algorithm.c \
    src/core/compression/message_compress.c \
    src/core/endpoint/endpoint.c \
    src/core/endpoint/secure_endpoint.c \
    src/core/httpcli/format_request.c \
    src/core/httpcli/httpcli.c \
    src/core/httpcli/httpcli_security_context.c \
    src/core/httpcli/parser.c \
    src/core/iomgr/endpoint_pair_posix.c \
    src/core/iomgr/iomgr_libevent.c \
    src/core/iomgr/iomgr_libevent_use_threads.c \
    src/core/iomgr/resolve_address_posix.c \
    src/core/iomgr/sockaddr_utils.c \
    src/core/iomgr/socket_utils_common_posix.c \
    src/core/iomgr/socket_utils_linux.c \
    src/core/iomgr/socket_utils_posix.c \
    src/core/iomgr/tcp_client_posix.c \
    src/core/iomgr/tcp_posix.c \
    src/core/iomgr/tcp_server_posix.c \
    src/core/security/auth.c \
    src/core/security/base64.c \
    src/core/security/credentials.c \
    src/core/security/google_root_certs.c \
    src/core/security/json_token.c \
    src/core/security/secure_transport_setup.c \
    src/core/security/security_context.c \
    src/core/security/server_secure_chttp2.c \
    src/core/statistics/census_init.c \
    src/core/statistics/census_rpc_stats.c \
    src/core/statistics/census_tracing.c \
    src/core/statistics/hash_table.c \
    src/core/statistics/census_log.c \
    src/core/statistics/window_stats.c \
    src/core/surface/byte_buffer.c \
    src/core/surface/byte_buffer_reader.c \
    src/core/surface/call.c \
    src/core/surface/channel.c \
    src/core/surface/channel_create.c \
    src/core/surface/client.c \
    src/core/surface/completion_queue.c \
    src/core/surface/event_string.c \
    src/core/surface/init.c \
    src/core/surface/lame_client.c \
    src/core/surface/secure_channel_create.c \
    src/core/surface/secure_server_create.c \
    src/core/surface/server.c \
    src/core/surface/server_chttp2.c \
    src/core/surface/server_create.c \
    src/core/transport/chttp2/alpn.c \
    src/core/transport/chttp2/bin_encoder.c \
    src/core/transport/chttp2/frame_data.c \
    src/core/transport/chttp2/frame_goaway.c \
    src/core/transport/chttp2/frame_ping.c \
    src/core/transport/chttp2/frame_rst_stream.c \
    src/core/transport/chttp2/frame_settings.c \
    src/core/transport/chttp2/frame_window_update.c \
    src/core/transport/chttp2/hpack_parser.c \
    src/core/transport/chttp2/hpack_table.c \
    src/core/transport/chttp2/huffsyms.c \
    src/core/transport/chttp2/status_conversion.c \
    src/core/transport/chttp2/stream_encoder.c \
    src/core/transport/chttp2/stream_map.c \
    src/core/transport/chttp2/timeout_encoding.c \
    src/core/transport/chttp2_transport.c \
    src/core/transport/chttp2/varint.c \
    src/core/transport/metadata.c \
    src/core/transport/stream_op.c \
    src/core/transport/transport.c \
    src/core/tsi/fake_transport_security.c \
    src/core/tsi/ssl_transport_security.c \
    src/core/tsi/transport_security.c \
    third_party/cJSON/cJSON.c \

PUBLIC_HEADERS_C += \
    include/grpc/byte_buffer.h \
    include/grpc/byte_buffer_reader.h \
    include/grpc/grpc.h \
    include/grpc/grpc_security.h \
    include/grpc/status.h \

LIBGRPC_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBGRPC_SRC))))
LIBGRPC_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBGRPC_SRC))))

LIBGRPC_OBJS += $(OPENSSL_DEP)

ifeq ($(NO_SECURE),true)

libs/libgrpc.a: openssl_dep_error

else

libs/libgrpc.a: $(LIBGRPC_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libgrpc.a $(LIBGRPC_OBJS)

libs/libgrpc.so.$(VERSION): $(LIBGRPC_OBJS)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) -shared -Wl,-soname,libgrpc.so.0 -o libs/libgrpc.so.$(VERSION) $(LIBGRPC_OBJS) $(LDLIBS) $(LDLIBS_SECURE)

endif

deps_libgrpc: $(LIBGRPC_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_DEPS)
endif
endif

clean_libgrpc:
	$(E) "[CLEAN]   Cleaning libgrpc files"
	$(Q) $(RM) $(LIBGRPC_OBJS)
	$(Q) $(RM) $(LIBGRPC_DEPS)
	$(Q) $(RM) libs/libgrpc.a
	$(Q) $(RM) libs/libgrpc.so.$(VERSION)


LIBGRPC_TEST_UTIL_SRC = \
    test/core/util/grpc_profiler.c \
    test/core/util/ipv6_posix.c \
    test/core/util/parse_hexstring.c \
    test/core/util/port_posix.c \
    test/core/util/slice_splitter.c \
    test/core/util/test_config.c \
    test/core/end2end/end2end_tests.c \
    test/core/end2end/data/server1_cert.c \
    test/core/end2end/data/server1_key.c \
    test/core/end2end/data/ca_cert.c \
    test/core/end2end/cq_verifier.c \
    test/core/endpoint/endpoint_tests.c \
    test/core/transport/transport_end2end_tests.c \
    test/core/statistics/census_log_tests.c \


LIBGRPC_TEST_UTIL_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBGRPC_TEST_UTIL_SRC))))
LIBGRPC_TEST_UTIL_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBGRPC_TEST_UTIL_SRC))))

LIBGRPC_TEST_UTIL_OBJS += $(OPENSSL_DEP)

ifeq ($(NO_SECURE),true)

libs/libgrpc_test_util.a: openssl_dep_error

else

libs/libgrpc_test_util.a: $(LIBGRPC_TEST_UTIL_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libgrpc_test_util.a $(LIBGRPC_TEST_UTIL_OBJS)



endif

deps_libgrpc_test_util: $(LIBGRPC_TEST_UTIL_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_TEST_UTIL_DEPS)
endif
endif

clean_libgrpc_test_util:
	$(E) "[CLEAN]   Cleaning libgrpc_test_util files"
	$(Q) $(RM) $(LIBGRPC_TEST_UTIL_OBJS)
	$(Q) $(RM) $(LIBGRPC_TEST_UTIL_DEPS)
	$(Q) $(RM) libs/libgrpc_test_util.a
	$(Q) $(RM) libs/libgrpc_test_util.so.$(VERSION)


LIBGRPC++_SRC = \
    src/cpp/server/server.cc \
    src/cpp/server/server_rpc_handler.cc \
    src/cpp/server/thread_pool.cc \
    src/cpp/server/async_server_context.cc \
    src/cpp/server/async_server.cc \
    src/cpp/server/completion_queue.cc \
    src/cpp/server/server_builder.cc \
    src/cpp/stream/stream_context.cc \
    src/cpp/client/create_channel.cc \
    src/cpp/client/channel.cc \
    src/cpp/client/client_context.cc \
    src/cpp/client/internal_stub.cc \
    src/cpp/util/time.cc \
    src/cpp/util/status.cc \
    src/cpp/proto/proto_utils.cc \
    src/cpp/rpc_method.cc \

PUBLIC_HEADERS_CXX += \
    include/grpc++/channel_interface.h \
    include/grpc++/async_server.h \
    include/grpc++/create_channel.h \
    include/grpc++/server_builder.h \
    include/grpc++/thread_pool_interface.h \
    include/grpc++/stream_context_interface.h \
    include/grpc++/status.h \
    include/grpc++/config.h \
    include/grpc++/completion_queue.h \
    include/grpc++/stream.h \
    include/grpc++/async_server_context.h \
    include/grpc++/server.h \
    include/grpc++/client_context.h \

LIBGRPC++_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBGRPC++_SRC))))
LIBGRPC++_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBGRPC++_SRC))))

LIBGRPC++_OBJS += $(OPENSSL_DEP)

ifeq ($(NO_SECURE),true)

libs/libgrpc++.a: openssl_dep_error

else

libs/libgrpc++.a: $(LIBGRPC++_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libgrpc++.a $(LIBGRPC++_OBJS)

libs/libgrpc++.so.$(VERSION): $(LIBGRPC++_OBJS)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) -shared -Wl,-soname,libgrpc++.so.0 -o libs/libgrpc++.so.$(VERSION) $(LIBGRPC++_OBJS) $(LDLIBS) $(LDLIBS_SECURE)

endif

deps_libgrpc++: $(LIBGRPC++_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC++_DEPS)
endif
endif

clean_libgrpc++:
	$(E) "[CLEAN]   Cleaning libgrpc++ files"
	$(Q) $(RM) $(LIBGRPC++_OBJS)
	$(Q) $(RM) $(LIBGRPC++_DEPS)
	$(Q) $(RM) libs/libgrpc++.a
	$(Q) $(RM) libs/libgrpc++.so.$(VERSION)


LIBGRPC++_TEST_UTIL_SRC = \
    test/cpp/util/test_ssl_channel.cc \
    gens/test/cpp/util/echo.pb.cc \
    test/cpp/end2end/async_test_server.cc \


LIBGRPC++_TEST_UTIL_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBGRPC++_TEST_UTIL_SRC))))
LIBGRPC++_TEST_UTIL_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBGRPC++_TEST_UTIL_SRC))))

LIBGRPC++_TEST_UTIL_OBJS += $(OPENSSL_DEP)

ifeq ($(NO_SECURE),true)

libs/libgrpc++_test_util.a: openssl_dep_error

else

libs/libgrpc++_test_util.a: $(LIBGRPC++_TEST_UTIL_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libgrpc++_test_util.a $(LIBGRPC++_TEST_UTIL_OBJS)



endif

deps_libgrpc++_test_util: $(LIBGRPC++_TEST_UTIL_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBGRPC++_TEST_UTIL_DEPS)
endif
endif

clean_libgrpc++_test_util:
	$(E) "[CLEAN]   Cleaning libgrpc++_test_util files"
	$(Q) $(RM) $(LIBGRPC++_TEST_UTIL_OBJS)
	$(Q) $(RM) $(LIBGRPC++_TEST_UTIL_DEPS)
	$(Q) $(RM) libs/libgrpc++_test_util.a
	$(Q) $(RM) libs/libgrpc++_test_util.so.$(VERSION)


LIBEND2END_FIXTURE_CHTTP2_FAKE_SECURITY_SRC = \
    test/core/end2end/fixtures/chttp2_fake_security.c \


LIBEND2END_FIXTURE_CHTTP2_FAKE_SECURITY_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_FIXTURE_CHTTP2_FAKE_SECURITY_SRC))))
LIBEND2END_FIXTURE_CHTTP2_FAKE_SECURITY_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_FIXTURE_CHTTP2_FAKE_SECURITY_SRC))))

LIBEND2END_FIXTURE_CHTTP2_FAKE_SECURITY_OBJS += $(OPENSSL_DEP)

ifeq ($(NO_SECURE),true)

libs/libend2end_fixture_chttp2_fake_security.a: openssl_dep_error

else

libs/libend2end_fixture_chttp2_fake_security.a: $(LIBEND2END_FIXTURE_CHTTP2_FAKE_SECURITY_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_fixture_chttp2_fake_security.a $(LIBEND2END_FIXTURE_CHTTP2_FAKE_SECURITY_OBJS)



endif

deps_libend2end_fixture_chttp2_fake_security: $(LIBEND2END_FIXTURE_CHTTP2_FAKE_SECURITY_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_FIXTURE_CHTTP2_FAKE_SECURITY_DEPS)
endif
endif

clean_libend2end_fixture_chttp2_fake_security:
	$(E) "[CLEAN]   Cleaning libend2end_fixture_chttp2_fake_security files"
	$(Q) $(RM) $(LIBEND2END_FIXTURE_CHTTP2_FAKE_SECURITY_OBJS)
	$(Q) $(RM) $(LIBEND2END_FIXTURE_CHTTP2_FAKE_SECURITY_DEPS)
	$(Q) $(RM) libs/libend2end_fixture_chttp2_fake_security.a
	$(Q) $(RM) libs/libend2end_fixture_chttp2_fake_security.so.$(VERSION)


LIBEND2END_FIXTURE_CHTTP2_FULLSTACK_SRC = \
    test/core/end2end/fixtures/chttp2_fullstack.c \


LIBEND2END_FIXTURE_CHTTP2_FULLSTACK_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_FIXTURE_CHTTP2_FULLSTACK_SRC))))
LIBEND2END_FIXTURE_CHTTP2_FULLSTACK_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_FIXTURE_CHTTP2_FULLSTACK_SRC))))

LIBEND2END_FIXTURE_CHTTP2_FULLSTACK_OBJS += $(OPENSSL_DEP)

ifeq ($(NO_SECURE),true)

libs/libend2end_fixture_chttp2_fullstack.a: openssl_dep_error

else

libs/libend2end_fixture_chttp2_fullstack.a: $(LIBEND2END_FIXTURE_CHTTP2_FULLSTACK_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_fixture_chttp2_fullstack.a $(LIBEND2END_FIXTURE_CHTTP2_FULLSTACK_OBJS)



endif

deps_libend2end_fixture_chttp2_fullstack: $(LIBEND2END_FIXTURE_CHTTP2_FULLSTACK_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_FIXTURE_CHTTP2_FULLSTACK_DEPS)
endif
endif

clean_libend2end_fixture_chttp2_fullstack:
	$(E) "[CLEAN]   Cleaning libend2end_fixture_chttp2_fullstack files"
	$(Q) $(RM) $(LIBEND2END_FIXTURE_CHTTP2_FULLSTACK_OBJS)
	$(Q) $(RM) $(LIBEND2END_FIXTURE_CHTTP2_FULLSTACK_DEPS)
	$(Q) $(RM) libs/libend2end_fixture_chttp2_fullstack.a
	$(Q) $(RM) libs/libend2end_fixture_chttp2_fullstack.so.$(VERSION)


LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_FULLSTACK_SRC = \
    test/core/end2end/fixtures/chttp2_simple_ssl_fullstack.c \


LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_FULLSTACK_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_FULLSTACK_SRC))))
LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_FULLSTACK_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_FULLSTACK_SRC))))

LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_FULLSTACK_OBJS += $(OPENSSL_DEP)

ifeq ($(NO_SECURE),true)

libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a: openssl_dep_error

else

libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a: $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_FULLSTACK_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_FULLSTACK_OBJS)



endif

deps_libend2end_fixture_chttp2_simple_ssl_fullstack: $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_FULLSTACK_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_FULLSTACK_DEPS)
endif
endif

clean_libend2end_fixture_chttp2_simple_ssl_fullstack:
	$(E) "[CLEAN]   Cleaning libend2end_fixture_chttp2_simple_ssl_fullstack files"
	$(Q) $(RM) $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_FULLSTACK_OBJS)
	$(Q) $(RM) $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_FULLSTACK_DEPS)
	$(Q) $(RM) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a
	$(Q) $(RM) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.so.$(VERSION)


LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SRC = \
    test/core/end2end/fixtures/chttp2_simple_ssl_with_oauth2_fullstack.c \


LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SRC))))
LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SRC))))

LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_OBJS += $(OPENSSL_DEP)

ifeq ($(NO_SECURE),true)

libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a: openssl_dep_error

else

libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a: $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_OBJS)



endif

deps_libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack: $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_DEPS)
endif
endif

clean_libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack:
	$(E) "[CLEAN]   Cleaning libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack files"
	$(Q) $(RM) $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_OBJS)
	$(Q) $(RM) $(LIBEND2END_FIXTURE_CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_DEPS)
	$(Q) $(RM) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a
	$(Q) $(RM) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.so.$(VERSION)


LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_SRC = \
    test/core/end2end/fixtures/chttp2_socket_pair.c \


LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_SRC))))
LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_SRC))))

LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_OBJS += $(OPENSSL_DEP)

ifeq ($(NO_SECURE),true)

libs/libend2end_fixture_chttp2_socket_pair.a: openssl_dep_error

else

libs/libend2end_fixture_chttp2_socket_pair.a: $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_fixture_chttp2_socket_pair.a $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_OBJS)



endif

deps_libend2end_fixture_chttp2_socket_pair: $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_DEPS)
endif
endif

clean_libend2end_fixture_chttp2_socket_pair:
	$(E) "[CLEAN]   Cleaning libend2end_fixture_chttp2_socket_pair files"
	$(Q) $(RM) $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_OBJS)
	$(Q) $(RM) $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_DEPS)
	$(Q) $(RM) libs/libend2end_fixture_chttp2_socket_pair.a
	$(Q) $(RM) libs/libend2end_fixture_chttp2_socket_pair.so.$(VERSION)


LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SRC = \
    test/core/end2end/fixtures/chttp2_socket_pair_one_byte_at_a_time.c \


LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SRC))))
LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SRC))))

LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_OBJS += $(OPENSSL_DEP)

ifeq ($(NO_SECURE),true)

libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a: openssl_dep_error

else

libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a: $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_OBJS)



endif

deps_libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time: $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_DEPS)
endif
endif

clean_libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time:
	$(E) "[CLEAN]   Cleaning libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time files"
	$(Q) $(RM) $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_OBJS)
	$(Q) $(RM) $(LIBEND2END_FIXTURE_CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_DEPS)
	$(Q) $(RM) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a
	$(Q) $(RM) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.so.$(VERSION)


LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_SRC = \
    test/core/end2end/tests/cancel_after_accept.c \


LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_SRC))))
LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_SRC))))

libs/libend2end_test_cancel_after_accept.a: $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_cancel_after_accept.a $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_OBJS)



deps_libend2end_test_cancel_after_accept: $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_DEPS)
endif

clean_libend2end_test_cancel_after_accept:
	$(E) "[CLEAN]   Cleaning libend2end_test_cancel_after_accept files"
	$(Q) $(RM) $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_DEPS)
	$(Q) $(RM) libs/libend2end_test_cancel_after_accept.a
	$(Q) $(RM) libs/libend2end_test_cancel_after_accept.so.$(VERSION)


LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_SRC = \
    test/core/end2end/tests/cancel_after_accept_and_writes_closed.c \


LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_SRC))))
LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_SRC))))

libs/libend2end_test_cancel_after_accept_and_writes_closed.a: $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_cancel_after_accept_and_writes_closed.a $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_OBJS)



deps_libend2end_test_cancel_after_accept_and_writes_closed: $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_DEPS)
endif

clean_libend2end_test_cancel_after_accept_and_writes_closed:
	$(E) "[CLEAN]   Cleaning libend2end_test_cancel_after_accept_and_writes_closed files"
	$(Q) $(RM) $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_DEPS)
	$(Q) $(RM) libs/libend2end_test_cancel_after_accept_and_writes_closed.a
	$(Q) $(RM) libs/libend2end_test_cancel_after_accept_and_writes_closed.so.$(VERSION)


LIBEND2END_TEST_CANCEL_AFTER_INVOKE_SRC = \
    test/core/end2end/tests/cancel_after_invoke.c \


LIBEND2END_TEST_CANCEL_AFTER_INVOKE_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_CANCEL_AFTER_INVOKE_SRC))))
LIBEND2END_TEST_CANCEL_AFTER_INVOKE_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_CANCEL_AFTER_INVOKE_SRC))))

libs/libend2end_test_cancel_after_invoke.a: $(LIBEND2END_TEST_CANCEL_AFTER_INVOKE_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_cancel_after_invoke.a $(LIBEND2END_TEST_CANCEL_AFTER_INVOKE_OBJS)



deps_libend2end_test_cancel_after_invoke: $(LIBEND2END_TEST_CANCEL_AFTER_INVOKE_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_CANCEL_AFTER_INVOKE_DEPS)
endif

clean_libend2end_test_cancel_after_invoke:
	$(E) "[CLEAN]   Cleaning libend2end_test_cancel_after_invoke files"
	$(Q) $(RM) $(LIBEND2END_TEST_CANCEL_AFTER_INVOKE_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_CANCEL_AFTER_INVOKE_DEPS)
	$(Q) $(RM) libs/libend2end_test_cancel_after_invoke.a
	$(Q) $(RM) libs/libend2end_test_cancel_after_invoke.so.$(VERSION)


LIBEND2END_TEST_CANCEL_BEFORE_INVOKE_SRC = \
    test/core/end2end/tests/cancel_before_invoke.c \


LIBEND2END_TEST_CANCEL_BEFORE_INVOKE_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_CANCEL_BEFORE_INVOKE_SRC))))
LIBEND2END_TEST_CANCEL_BEFORE_INVOKE_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_CANCEL_BEFORE_INVOKE_SRC))))

libs/libend2end_test_cancel_before_invoke.a: $(LIBEND2END_TEST_CANCEL_BEFORE_INVOKE_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_cancel_before_invoke.a $(LIBEND2END_TEST_CANCEL_BEFORE_INVOKE_OBJS)



deps_libend2end_test_cancel_before_invoke: $(LIBEND2END_TEST_CANCEL_BEFORE_INVOKE_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_CANCEL_BEFORE_INVOKE_DEPS)
endif

clean_libend2end_test_cancel_before_invoke:
	$(E) "[CLEAN]   Cleaning libend2end_test_cancel_before_invoke files"
	$(Q) $(RM) $(LIBEND2END_TEST_CANCEL_BEFORE_INVOKE_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_CANCEL_BEFORE_INVOKE_DEPS)
	$(Q) $(RM) libs/libend2end_test_cancel_before_invoke.a
	$(Q) $(RM) libs/libend2end_test_cancel_before_invoke.so.$(VERSION)


LIBEND2END_TEST_CANCEL_IN_A_VACUUM_SRC = \
    test/core/end2end/tests/cancel_in_a_vacuum.c \


LIBEND2END_TEST_CANCEL_IN_A_VACUUM_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_CANCEL_IN_A_VACUUM_SRC))))
LIBEND2END_TEST_CANCEL_IN_A_VACUUM_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_CANCEL_IN_A_VACUUM_SRC))))

libs/libend2end_test_cancel_in_a_vacuum.a: $(LIBEND2END_TEST_CANCEL_IN_A_VACUUM_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_cancel_in_a_vacuum.a $(LIBEND2END_TEST_CANCEL_IN_A_VACUUM_OBJS)



deps_libend2end_test_cancel_in_a_vacuum: $(LIBEND2END_TEST_CANCEL_IN_A_VACUUM_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_CANCEL_IN_A_VACUUM_DEPS)
endif

clean_libend2end_test_cancel_in_a_vacuum:
	$(E) "[CLEAN]   Cleaning libend2end_test_cancel_in_a_vacuum files"
	$(Q) $(RM) $(LIBEND2END_TEST_CANCEL_IN_A_VACUUM_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_CANCEL_IN_A_VACUUM_DEPS)
	$(Q) $(RM) libs/libend2end_test_cancel_in_a_vacuum.a
	$(Q) $(RM) libs/libend2end_test_cancel_in_a_vacuum.so.$(VERSION)


LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_SRC = \
    test/core/end2end/tests/early_server_shutdown_finishes_inflight_calls.c \


LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_SRC))))
LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_SRC))))

libs/libend2end_test_early_server_shutdown_finishes_inflight_calls.a: $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_early_server_shutdown_finishes_inflight_calls.a $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_OBJS)



deps_libend2end_test_early_server_shutdown_finishes_inflight_calls: $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_DEPS)
endif

clean_libend2end_test_early_server_shutdown_finishes_inflight_calls:
	$(E) "[CLEAN]   Cleaning libend2end_test_early_server_shutdown_finishes_inflight_calls files"
	$(Q) $(RM) $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_DEPS)
	$(Q) $(RM) libs/libend2end_test_early_server_shutdown_finishes_inflight_calls.a
	$(Q) $(RM) libs/libend2end_test_early_server_shutdown_finishes_inflight_calls.so.$(VERSION)


LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_SRC = \
    test/core/end2end/tests/early_server_shutdown_finishes_tags.c \


LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_SRC))))
LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_SRC))))

libs/libend2end_test_early_server_shutdown_finishes_tags.a: $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_early_server_shutdown_finishes_tags.a $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_OBJS)



deps_libend2end_test_early_server_shutdown_finishes_tags: $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_DEPS)
endif

clean_libend2end_test_early_server_shutdown_finishes_tags:
	$(E) "[CLEAN]   Cleaning libend2end_test_early_server_shutdown_finishes_tags files"
	$(Q) $(RM) $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_DEPS)
	$(Q) $(RM) libs/libend2end_test_early_server_shutdown_finishes_tags.a
	$(Q) $(RM) libs/libend2end_test_early_server_shutdown_finishes_tags.so.$(VERSION)


LIBEND2END_TEST_INVOKE_LARGE_REQUEST_SRC = \
    test/core/end2end/tests/invoke_large_request.c \


LIBEND2END_TEST_INVOKE_LARGE_REQUEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_INVOKE_LARGE_REQUEST_SRC))))
LIBEND2END_TEST_INVOKE_LARGE_REQUEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_INVOKE_LARGE_REQUEST_SRC))))

libs/libend2end_test_invoke_large_request.a: $(LIBEND2END_TEST_INVOKE_LARGE_REQUEST_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_invoke_large_request.a $(LIBEND2END_TEST_INVOKE_LARGE_REQUEST_OBJS)



deps_libend2end_test_invoke_large_request: $(LIBEND2END_TEST_INVOKE_LARGE_REQUEST_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_INVOKE_LARGE_REQUEST_DEPS)
endif

clean_libend2end_test_invoke_large_request:
	$(E) "[CLEAN]   Cleaning libend2end_test_invoke_large_request files"
	$(Q) $(RM) $(LIBEND2END_TEST_INVOKE_LARGE_REQUEST_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_INVOKE_LARGE_REQUEST_DEPS)
	$(Q) $(RM) libs/libend2end_test_invoke_large_request.a
	$(Q) $(RM) libs/libend2end_test_invoke_large_request.so.$(VERSION)


LIBEND2END_TEST_MAX_CONCURRENT_STREAMS_SRC = \
    test/core/end2end/tests/max_concurrent_streams.c \


LIBEND2END_TEST_MAX_CONCURRENT_STREAMS_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_MAX_CONCURRENT_STREAMS_SRC))))
LIBEND2END_TEST_MAX_CONCURRENT_STREAMS_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_MAX_CONCURRENT_STREAMS_SRC))))

libs/libend2end_test_max_concurrent_streams.a: $(LIBEND2END_TEST_MAX_CONCURRENT_STREAMS_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_max_concurrent_streams.a $(LIBEND2END_TEST_MAX_CONCURRENT_STREAMS_OBJS)



deps_libend2end_test_max_concurrent_streams: $(LIBEND2END_TEST_MAX_CONCURRENT_STREAMS_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_MAX_CONCURRENT_STREAMS_DEPS)
endif

clean_libend2end_test_max_concurrent_streams:
	$(E) "[CLEAN]   Cleaning libend2end_test_max_concurrent_streams files"
	$(Q) $(RM) $(LIBEND2END_TEST_MAX_CONCURRENT_STREAMS_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_MAX_CONCURRENT_STREAMS_DEPS)
	$(Q) $(RM) libs/libend2end_test_max_concurrent_streams.a
	$(Q) $(RM) libs/libend2end_test_max_concurrent_streams.so.$(VERSION)


LIBEND2END_TEST_NO_OP_SRC = \
    test/core/end2end/tests/no_op.c \


LIBEND2END_TEST_NO_OP_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_NO_OP_SRC))))
LIBEND2END_TEST_NO_OP_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_NO_OP_SRC))))

libs/libend2end_test_no_op.a: $(LIBEND2END_TEST_NO_OP_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_no_op.a $(LIBEND2END_TEST_NO_OP_OBJS)



deps_libend2end_test_no_op: $(LIBEND2END_TEST_NO_OP_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_NO_OP_DEPS)
endif

clean_libend2end_test_no_op:
	$(E) "[CLEAN]   Cleaning libend2end_test_no_op files"
	$(Q) $(RM) $(LIBEND2END_TEST_NO_OP_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_NO_OP_DEPS)
	$(Q) $(RM) libs/libend2end_test_no_op.a
	$(Q) $(RM) libs/libend2end_test_no_op.so.$(VERSION)


LIBEND2END_TEST_PING_PONG_STREAMING_SRC = \
    test/core/end2end/tests/ping_pong_streaming.c \


LIBEND2END_TEST_PING_PONG_STREAMING_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_PING_PONG_STREAMING_SRC))))
LIBEND2END_TEST_PING_PONG_STREAMING_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_PING_PONG_STREAMING_SRC))))

libs/libend2end_test_ping_pong_streaming.a: $(LIBEND2END_TEST_PING_PONG_STREAMING_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_ping_pong_streaming.a $(LIBEND2END_TEST_PING_PONG_STREAMING_OBJS)



deps_libend2end_test_ping_pong_streaming: $(LIBEND2END_TEST_PING_PONG_STREAMING_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_PING_PONG_STREAMING_DEPS)
endif

clean_libend2end_test_ping_pong_streaming:
	$(E) "[CLEAN]   Cleaning libend2end_test_ping_pong_streaming files"
	$(Q) $(RM) $(LIBEND2END_TEST_PING_PONG_STREAMING_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_PING_PONG_STREAMING_DEPS)
	$(Q) $(RM) libs/libend2end_test_ping_pong_streaming.a
	$(Q) $(RM) libs/libend2end_test_ping_pong_streaming.so.$(VERSION)


LIBEND2END_TEST_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_SRC = \
    test/core/end2end/tests/request_response_with_binary_metadata_and_payload.c \


LIBEND2END_TEST_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_SRC))))
LIBEND2END_TEST_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_SRC))))

libs/libend2end_test_request_response_with_binary_metadata_and_payload.a: $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_request_response_with_binary_metadata_and_payload.a $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_OBJS)



deps_libend2end_test_request_response_with_binary_metadata_and_payload: $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_DEPS)
endif

clean_libend2end_test_request_response_with_binary_metadata_and_payload:
	$(E) "[CLEAN]   Cleaning libend2end_test_request_response_with_binary_metadata_and_payload files"
	$(Q) $(RM) $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_DEPS)
	$(Q) $(RM) libs/libend2end_test_request_response_with_binary_metadata_and_payload.a
	$(Q) $(RM) libs/libend2end_test_request_response_with_binary_metadata_and_payload.so.$(VERSION)


LIBEND2END_TEST_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_SRC = \
    test/core/end2end/tests/request_response_with_metadata_and_payload.c \


LIBEND2END_TEST_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_SRC))))
LIBEND2END_TEST_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_SRC))))

libs/libend2end_test_request_response_with_metadata_and_payload.a: $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_request_response_with_metadata_and_payload.a $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_OBJS)



deps_libend2end_test_request_response_with_metadata_and_payload: $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_DEPS)
endif

clean_libend2end_test_request_response_with_metadata_and_payload:
	$(E) "[CLEAN]   Cleaning libend2end_test_request_response_with_metadata_and_payload files"
	$(Q) $(RM) $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_DEPS)
	$(Q) $(RM) libs/libend2end_test_request_response_with_metadata_and_payload.a
	$(Q) $(RM) libs/libend2end_test_request_response_with_metadata_and_payload.so.$(VERSION)


LIBEND2END_TEST_REQUEST_RESPONSE_WITH_PAYLOAD_SRC = \
    test/core/end2end/tests/request_response_with_payload.c \


LIBEND2END_TEST_REQUEST_RESPONSE_WITH_PAYLOAD_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_PAYLOAD_SRC))))
LIBEND2END_TEST_REQUEST_RESPONSE_WITH_PAYLOAD_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_PAYLOAD_SRC))))

libs/libend2end_test_request_response_with_payload.a: $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_PAYLOAD_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_request_response_with_payload.a $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_PAYLOAD_OBJS)



deps_libend2end_test_request_response_with_payload: $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_PAYLOAD_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_PAYLOAD_DEPS)
endif

clean_libend2end_test_request_response_with_payload:
	$(E) "[CLEAN]   Cleaning libend2end_test_request_response_with_payload files"
	$(Q) $(RM) $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_PAYLOAD_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_REQUEST_RESPONSE_WITH_PAYLOAD_DEPS)
	$(Q) $(RM) libs/libend2end_test_request_response_with_payload.a
	$(Q) $(RM) libs/libend2end_test_request_response_with_payload.so.$(VERSION)


LIBEND2END_TEST_SIMPLE_DELAYED_REQUEST_SRC = \
    test/core/end2end/tests/simple_delayed_request.c \


LIBEND2END_TEST_SIMPLE_DELAYED_REQUEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_SIMPLE_DELAYED_REQUEST_SRC))))
LIBEND2END_TEST_SIMPLE_DELAYED_REQUEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_SIMPLE_DELAYED_REQUEST_SRC))))

libs/libend2end_test_simple_delayed_request.a: $(LIBEND2END_TEST_SIMPLE_DELAYED_REQUEST_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_simple_delayed_request.a $(LIBEND2END_TEST_SIMPLE_DELAYED_REQUEST_OBJS)



deps_libend2end_test_simple_delayed_request: $(LIBEND2END_TEST_SIMPLE_DELAYED_REQUEST_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_SIMPLE_DELAYED_REQUEST_DEPS)
endif

clean_libend2end_test_simple_delayed_request:
	$(E) "[CLEAN]   Cleaning libend2end_test_simple_delayed_request files"
	$(Q) $(RM) $(LIBEND2END_TEST_SIMPLE_DELAYED_REQUEST_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_SIMPLE_DELAYED_REQUEST_DEPS)
	$(Q) $(RM) libs/libend2end_test_simple_delayed_request.a
	$(Q) $(RM) libs/libend2end_test_simple_delayed_request.so.$(VERSION)


LIBEND2END_TEST_SIMPLE_REQUEST_SRC = \
    test/core/end2end/tests/simple_request.c \


LIBEND2END_TEST_SIMPLE_REQUEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_SIMPLE_REQUEST_SRC))))
LIBEND2END_TEST_SIMPLE_REQUEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_SIMPLE_REQUEST_SRC))))

libs/libend2end_test_simple_request.a: $(LIBEND2END_TEST_SIMPLE_REQUEST_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_simple_request.a $(LIBEND2END_TEST_SIMPLE_REQUEST_OBJS)



deps_libend2end_test_simple_request: $(LIBEND2END_TEST_SIMPLE_REQUEST_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_SIMPLE_REQUEST_DEPS)
endif

clean_libend2end_test_simple_request:
	$(E) "[CLEAN]   Cleaning libend2end_test_simple_request files"
	$(Q) $(RM) $(LIBEND2END_TEST_SIMPLE_REQUEST_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_SIMPLE_REQUEST_DEPS)
	$(Q) $(RM) libs/libend2end_test_simple_request.a
	$(Q) $(RM) libs/libend2end_test_simple_request.so.$(VERSION)


LIBEND2END_TEST_THREAD_STRESS_SRC = \
    test/core/end2end/tests/thread_stress.c \


LIBEND2END_TEST_THREAD_STRESS_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_THREAD_STRESS_SRC))))
LIBEND2END_TEST_THREAD_STRESS_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_THREAD_STRESS_SRC))))

libs/libend2end_test_thread_stress.a: $(LIBEND2END_TEST_THREAD_STRESS_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_thread_stress.a $(LIBEND2END_TEST_THREAD_STRESS_OBJS)



deps_libend2end_test_thread_stress: $(LIBEND2END_TEST_THREAD_STRESS_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_THREAD_STRESS_DEPS)
endif

clean_libend2end_test_thread_stress:
	$(E) "[CLEAN]   Cleaning libend2end_test_thread_stress files"
	$(Q) $(RM) $(LIBEND2END_TEST_THREAD_STRESS_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_THREAD_STRESS_DEPS)
	$(Q) $(RM) libs/libend2end_test_thread_stress.a
	$(Q) $(RM) libs/libend2end_test_thread_stress.so.$(VERSION)


LIBEND2END_TEST_WRITES_DONE_HANGS_WITH_PENDING_READ_SRC = \
    test/core/end2end/tests/writes_done_hangs_with_pending_read.c \


LIBEND2END_TEST_WRITES_DONE_HANGS_WITH_PENDING_READ_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_TEST_WRITES_DONE_HANGS_WITH_PENDING_READ_SRC))))
LIBEND2END_TEST_WRITES_DONE_HANGS_WITH_PENDING_READ_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_TEST_WRITES_DONE_HANGS_WITH_PENDING_READ_SRC))))

libs/libend2end_test_writes_done_hangs_with_pending_read.a: $(LIBEND2END_TEST_WRITES_DONE_HANGS_WITH_PENDING_READ_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_test_writes_done_hangs_with_pending_read.a $(LIBEND2END_TEST_WRITES_DONE_HANGS_WITH_PENDING_READ_OBJS)



deps_libend2end_test_writes_done_hangs_with_pending_read: $(LIBEND2END_TEST_WRITES_DONE_HANGS_WITH_PENDING_READ_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_TEST_WRITES_DONE_HANGS_WITH_PENDING_READ_DEPS)
endif

clean_libend2end_test_writes_done_hangs_with_pending_read:
	$(E) "[CLEAN]   Cleaning libend2end_test_writes_done_hangs_with_pending_read files"
	$(Q) $(RM) $(LIBEND2END_TEST_WRITES_DONE_HANGS_WITH_PENDING_READ_OBJS)
	$(Q) $(RM) $(LIBEND2END_TEST_WRITES_DONE_HANGS_WITH_PENDING_READ_DEPS)
	$(Q) $(RM) libs/libend2end_test_writes_done_hangs_with_pending_read.a
	$(Q) $(RM) libs/libend2end_test_writes_done_hangs_with_pending_read.so.$(VERSION)


LIBEND2END_CERTS_SRC = \
    test/core/end2end/data/ca_cert.c \
    test/core/end2end/data/server1_cert.c \
    test/core/end2end/data/server1_key.c \


LIBEND2END_CERTS_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBEND2END_CERTS_SRC))))
LIBEND2END_CERTS_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBEND2END_CERTS_SRC))))

LIBEND2END_CERTS_OBJS += $(OPENSSL_DEP)

ifeq ($(NO_SECURE),true)

libs/libend2end_certs.a: openssl_dep_error

else

libs/libend2end_certs.a: $(LIBEND2END_CERTS_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libend2end_certs.a $(LIBEND2END_CERTS_OBJS)



endif

deps_libend2end_certs: $(LIBEND2END_CERTS_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LIBEND2END_CERTS_DEPS)
endif
endif

clean_libend2end_certs:
	$(E) "[CLEAN]   Cleaning libend2end_certs files"
	$(Q) $(RM) $(LIBEND2END_CERTS_OBJS)
	$(Q) $(RM) $(LIBEND2END_CERTS_DEPS)
	$(Q) $(RM) libs/libend2end_certs.a
	$(Q) $(RM) libs/libend2end_certs.so.$(VERSION)


LIBGRPC_UNSECURE_SRC = \
    src/core/channel/call_op_string.c \
    src/core/channel/census_filter.c \
    src/core/channel/channel_args.c \
    src/core/channel/channel_stack.c \
    src/core/channel/child_channel.c \
    src/core/channel/client_channel.c \
    src/core/channel/client_setup.c \
    src/core/channel/connected_channel.c \
    src/core/channel/http_client_filter.c \
    src/core/channel/http_filter.c \
    src/core/channel/http_server_filter.c \
    src/core/channel/metadata_buffer.c \
    src/core/channel/noop_filter.c \
    src/core/compression/algorithm.c \
    src/core/compression/message_compress.c \
    src/core/endpoint/endpoint.c \
    src/core/endpoint/secure_endpoint.c \
    src/core/httpcli/format_request.c \
    src/core/httpcli/httpcli.c \
    src/core/httpcli/httpcli_security_context.c \
    src/core/httpcli/parser.c \
    src/core/iomgr/endpoint_pair_posix.c \
    src/core/iomgr/iomgr_libevent.c \
    src/core/iomgr/iomgr_libevent_use_threads.c \
    src/core/iomgr/resolve_address_posix.c \
    src/core/iomgr/sockaddr_utils.c \
    src/core/iomgr/socket_utils_common_posix.c \
    src/core/iomgr/socket_utils_linux.c \
    src/core/iomgr/socket_utils_posix.c \
    src/core/iomgr/tcp_client_posix.c \
    src/core/iomgr/tcp_posix.c \
    src/core/iomgr/tcp_server_posix.c \
    src/core/statistics/census_init.c \
    src/core/statistics/census_rpc_stats.c \
    src/core/statistics/census_tracing.c \
    src/core/statistics/hash_table.c \
    src/core/statistics/census_log.c \
    src/core/statistics/window_stats.c \
    src/core/surface/byte_buffer.c \
    src/core/surface/byte_buffer_reader.c \
    src/core/surface/call.c \
    src/core/surface/channel.c \
    src/core/surface/channel_create.c \
    src/core/surface/client.c \
    src/core/surface/completion_queue.c \
    src/core/surface/event_string.c \
    src/core/surface/init.c \
    src/core/surface/lame_client.c \
    src/core/surface/secure_channel_create.c \
    src/core/surface/secure_server_create.c \
    src/core/surface/server.c \
    src/core/surface/server_chttp2.c \
    src/core/surface/server_create.c \
    src/core/transport/chttp2/alpn.c \
    src/core/transport/chttp2/bin_encoder.c \
    src/core/transport/chttp2/frame_data.c \
    src/core/transport/chttp2/frame_goaway.c \
    src/core/transport/chttp2/frame_ping.c \
    src/core/transport/chttp2/frame_rst_stream.c \
    src/core/transport/chttp2/frame_settings.c \
    src/core/transport/chttp2/frame_window_update.c \
    src/core/transport/chttp2/hpack_parser.c \
    src/core/transport/chttp2/hpack_table.c \
    src/core/transport/chttp2/huffsyms.c \
    src/core/transport/chttp2/status_conversion.c \
    src/core/transport/chttp2/stream_encoder.c \
    src/core/transport/chttp2/stream_map.c \
    src/core/transport/chttp2/timeout_encoding.c \
    src/core/transport/chttp2_transport.c \
    src/core/transport/chttp2/varint.c \
    src/core/transport/metadata.c \
    src/core/transport/stream_op.c \
    src/core/transport/transport.c \
    third_party/cJSON/cJSON.c \

PUBLIC_HEADERS_C += \
    include/grpc/byte_buffer.h \
    include/grpc/byte_buffer_reader.h \
    include/grpc/grpc.h \
    include/grpc/grpc_security.h \
    include/grpc/status.h \

LIBGRPC_UNSECURE_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LIBGRPC_UNSECURE_SRC))))
LIBGRPC_UNSECURE_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LIBGRPC_UNSECURE_SRC))))

libs/libgrpc_unsecure.a: $(LIBGRPC_UNSECURE_OBJS)
	$(E) "[AR]      Creating $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(AR) rcs libs/libgrpc_unsecure.a $(LIBGRPC_UNSECURE_OBJS)

libs/libgrpc_unsecure.so.$(VERSION): $(LIBGRPC_UNSECURE_OBJS)
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) -shared -Wl,-soname,libgrpc_unsecure.so.0 -o libs/libgrpc_unsecure.so.$(VERSION) $(LIBGRPC_UNSECURE_OBJS) $(LDLIBS)

deps_libgrpc_unsecure: $(LIBGRPC_UNSECURE_DEPS)

ifneq ($(NO_DEPS),true)
-include $(LIBGRPC_UNSECURE_DEPS)
endif

clean_libgrpc_unsecure:
	$(E) "[CLEAN]   Cleaning libgrpc_unsecure files"
	$(Q) $(RM) $(LIBGRPC_UNSECURE_OBJS)
	$(Q) $(RM) $(LIBGRPC_UNSECURE_DEPS)
	$(Q) $(RM) libs/libgrpc_unsecure.a
	$(Q) $(RM) libs/libgrpc_unsecure.so.$(VERSION)



# All of the test targets, and protoc plugins


GEN_HPACK_TABLES_SRC = \
    src/core/transport/chttp2/gen_hpack_tables.c \

GEN_HPACK_TABLES_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GEN_HPACK_TABLES_SRC))))
GEN_HPACK_TABLES_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GEN_HPACK_TABLES_SRC))))

ifeq ($(NO_SECURE),true)

bins/gen_hpack_tables: openssl_dep_error

else

bins/gen_hpack_tables: $(GEN_HPACK_TABLES_OBJS) libs/libgrpc_test_util.a libs/libgpr.a libs/libgrpc.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GEN_HPACK_TABLES_OBJS) -Llibs -lgrpc_test_util -lgpr -lgrpc $(LDLIBS) $(LDLIBS_SECURE) -o bins/gen_hpack_tables

endif

deps_gen_hpack_tables: $(GEN_HPACK_TABLES_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GEN_HPACK_TABLES_DEPS)
endif
endif

clean_gen_hpack_tables:
	$(E) "[CLEAN]   Cleaning gen_hpack_tables files"
	$(Q) $(RM) $(GEN_HPACK_TABLES_OBJS)
	$(Q) $(RM) $(GEN_HPACK_TABLES_DEPS)
	$(Q) $(RM) bins/gen_hpack_tables


CPP_PLUGIN_SRC = \
    src/compiler/cpp_plugin.cpp \
    src/compiler/cpp_generator.cpp \

CPP_PLUGIN_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CPP_PLUGIN_SRC))))
CPP_PLUGIN_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CPP_PLUGIN_SRC))))

bins/cpp_plugin: $(CPP_PLUGIN_OBJS)
	$(E) "[HOSTLD]  Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(HOST_LDXX) $(HOST_LDFLAGS) $(CPP_PLUGIN_OBJS) -Llibs $(HOST_LDLIBSXX) $(HOST_LDLIBS) $(HOST_LDLIBS_PROTOC) -o bins/cpp_plugin

deps_cpp_plugin: $(CPP_PLUGIN_DEPS)

ifneq ($(NO_DEPS),true)
-include $(CPP_PLUGIN_DEPS)
endif

clean_cpp_plugin:
	$(E) "[CLEAN]   Cleaning cpp_plugin files"
	$(Q) $(RM) $(CPP_PLUGIN_OBJS)
	$(Q) $(RM) $(CPP_PLUGIN_DEPS)
	$(Q) $(RM) bins/cpp_plugin


RUBY_PLUGIN_SRC = \
    src/compiler/ruby_plugin.cpp \
    src/compiler/ruby_generator.cpp \

RUBY_PLUGIN_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(RUBY_PLUGIN_SRC))))
RUBY_PLUGIN_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(RUBY_PLUGIN_SRC))))

bins/ruby_plugin: $(RUBY_PLUGIN_OBJS)
	$(E) "[HOSTLD]  Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(HOST_LDXX) $(HOST_LDFLAGS) $(RUBY_PLUGIN_OBJS) -Llibs $(HOST_LDLIBSXX) $(HOST_LDLIBS) $(HOST_LDLIBS_PROTOC) -o bins/ruby_plugin

deps_ruby_plugin: $(RUBY_PLUGIN_DEPS)

ifneq ($(NO_DEPS),true)
-include $(RUBY_PLUGIN_DEPS)
endif

clean_ruby_plugin:
	$(E) "[CLEAN]   Cleaning ruby_plugin files"
	$(Q) $(RM) $(RUBY_PLUGIN_OBJS)
	$(Q) $(RM) $(RUBY_PLUGIN_DEPS)
	$(Q) $(RM) bins/ruby_plugin


GRPC_BYTE_BUFFER_READER_TEST_SRC = \
    test/core/surface/byte_buffer_reader_test.c \

GRPC_BYTE_BUFFER_READER_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GRPC_BYTE_BUFFER_READER_TEST_SRC))))
GRPC_BYTE_BUFFER_READER_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GRPC_BYTE_BUFFER_READER_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/grpc_byte_buffer_reader_test: openssl_dep_error

else

bins/grpc_byte_buffer_reader_test: $(GRPC_BYTE_BUFFER_READER_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_BYTE_BUFFER_READER_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/grpc_byte_buffer_reader_test

endif

deps_grpc_byte_buffer_reader_test: $(GRPC_BYTE_BUFFER_READER_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_BYTE_BUFFER_READER_TEST_DEPS)
endif
endif

clean_grpc_byte_buffer_reader_test:
	$(E) "[CLEAN]   Cleaning grpc_byte_buffer_reader_test files"
	$(Q) $(RM) $(GRPC_BYTE_BUFFER_READER_TEST_OBJS)
	$(Q) $(RM) $(GRPC_BYTE_BUFFER_READER_TEST_DEPS)
	$(Q) $(RM) bins/grpc_byte_buffer_reader_test


GPR_CANCELLABLE_TEST_SRC = \
    test/core/support/cancellable_test.c \

GPR_CANCELLABLE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GPR_CANCELLABLE_TEST_SRC))))
GPR_CANCELLABLE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GPR_CANCELLABLE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/gpr_cancellable_test: openssl_dep_error

else

bins/gpr_cancellable_test: $(GPR_CANCELLABLE_TEST_OBJS) libs/libgrpc_test_util.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_CANCELLABLE_TEST_OBJS) -Llibs -lgrpc_test_util -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/gpr_cancellable_test

endif

deps_gpr_cancellable_test: $(GPR_CANCELLABLE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_CANCELLABLE_TEST_DEPS)
endif
endif

clean_gpr_cancellable_test:
	$(E) "[CLEAN]   Cleaning gpr_cancellable_test files"
	$(Q) $(RM) $(GPR_CANCELLABLE_TEST_OBJS)
	$(Q) $(RM) $(GPR_CANCELLABLE_TEST_DEPS)
	$(Q) $(RM) bins/gpr_cancellable_test


GPR_LOG_TEST_SRC = \
    test/core/support/log_test.c \

GPR_LOG_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GPR_LOG_TEST_SRC))))
GPR_LOG_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GPR_LOG_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/gpr_log_test: openssl_dep_error

else

bins/gpr_log_test: $(GPR_LOG_TEST_OBJS) libs/libgrpc_test_util.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_LOG_TEST_OBJS) -Llibs -lgrpc_test_util -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/gpr_log_test

endif

deps_gpr_log_test: $(GPR_LOG_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_LOG_TEST_DEPS)
endif
endif

clean_gpr_log_test:
	$(E) "[CLEAN]   Cleaning gpr_log_test files"
	$(Q) $(RM) $(GPR_LOG_TEST_OBJS)
	$(Q) $(RM) $(GPR_LOG_TEST_DEPS)
	$(Q) $(RM) bins/gpr_log_test


GPR_USEFUL_TEST_SRC = \
    test/core/support/useful_test.c \

GPR_USEFUL_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GPR_USEFUL_TEST_SRC))))
GPR_USEFUL_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GPR_USEFUL_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/gpr_useful_test: openssl_dep_error

else

bins/gpr_useful_test: $(GPR_USEFUL_TEST_OBJS) libs/libgrpc_test_util.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_USEFUL_TEST_OBJS) -Llibs -lgrpc_test_util -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/gpr_useful_test

endif

deps_gpr_useful_test: $(GPR_USEFUL_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_USEFUL_TEST_DEPS)
endif
endif

clean_gpr_useful_test:
	$(E) "[CLEAN]   Cleaning gpr_useful_test files"
	$(Q) $(RM) $(GPR_USEFUL_TEST_OBJS)
	$(Q) $(RM) $(GPR_USEFUL_TEST_DEPS)
	$(Q) $(RM) bins/gpr_useful_test


GPR_CMDLINE_TEST_SRC = \
    test/core/support/cmdline_test.c \

GPR_CMDLINE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GPR_CMDLINE_TEST_SRC))))
GPR_CMDLINE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GPR_CMDLINE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/gpr_cmdline_test: openssl_dep_error

else

bins/gpr_cmdline_test: $(GPR_CMDLINE_TEST_OBJS) libs/libgrpc_test_util.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_CMDLINE_TEST_OBJS) -Llibs -lgrpc_test_util -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/gpr_cmdline_test

endif

deps_gpr_cmdline_test: $(GPR_CMDLINE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_CMDLINE_TEST_DEPS)
endif
endif

clean_gpr_cmdline_test:
	$(E) "[CLEAN]   Cleaning gpr_cmdline_test files"
	$(Q) $(RM) $(GPR_CMDLINE_TEST_OBJS)
	$(Q) $(RM) $(GPR_CMDLINE_TEST_DEPS)
	$(Q) $(RM) bins/gpr_cmdline_test


GPR_HISTOGRAM_TEST_SRC = \
    test/core/support/histogram_test.c \

GPR_HISTOGRAM_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GPR_HISTOGRAM_TEST_SRC))))
GPR_HISTOGRAM_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GPR_HISTOGRAM_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/gpr_histogram_test: openssl_dep_error

else

bins/gpr_histogram_test: $(GPR_HISTOGRAM_TEST_OBJS) libs/libgrpc_test_util.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_HISTOGRAM_TEST_OBJS) -Llibs -lgrpc_test_util -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/gpr_histogram_test

endif

deps_gpr_histogram_test: $(GPR_HISTOGRAM_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_HISTOGRAM_TEST_DEPS)
endif
endif

clean_gpr_histogram_test:
	$(E) "[CLEAN]   Cleaning gpr_histogram_test files"
	$(Q) $(RM) $(GPR_HISTOGRAM_TEST_OBJS)
	$(Q) $(RM) $(GPR_HISTOGRAM_TEST_DEPS)
	$(Q) $(RM) bins/gpr_histogram_test


GPR_HOST_PORT_TEST_SRC = \
    test/core/support/host_port_test.c \

GPR_HOST_PORT_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GPR_HOST_PORT_TEST_SRC))))
GPR_HOST_PORT_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GPR_HOST_PORT_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/gpr_host_port_test: openssl_dep_error

else

bins/gpr_host_port_test: $(GPR_HOST_PORT_TEST_OBJS) libs/libgrpc_test_util.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_HOST_PORT_TEST_OBJS) -Llibs -lgrpc_test_util -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/gpr_host_port_test

endif

deps_gpr_host_port_test: $(GPR_HOST_PORT_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_HOST_PORT_TEST_DEPS)
endif
endif

clean_gpr_host_port_test:
	$(E) "[CLEAN]   Cleaning gpr_host_port_test files"
	$(Q) $(RM) $(GPR_HOST_PORT_TEST_OBJS)
	$(Q) $(RM) $(GPR_HOST_PORT_TEST_DEPS)
	$(Q) $(RM) bins/gpr_host_port_test


GPR_SLICE_BUFFER_TEST_SRC = \
    test/core/support/slice_buffer_test.c \

GPR_SLICE_BUFFER_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GPR_SLICE_BUFFER_TEST_SRC))))
GPR_SLICE_BUFFER_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GPR_SLICE_BUFFER_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/gpr_slice_buffer_test: openssl_dep_error

else

bins/gpr_slice_buffer_test: $(GPR_SLICE_BUFFER_TEST_OBJS) libs/libgrpc_test_util.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_SLICE_BUFFER_TEST_OBJS) -Llibs -lgrpc_test_util -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/gpr_slice_buffer_test

endif

deps_gpr_slice_buffer_test: $(GPR_SLICE_BUFFER_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_SLICE_BUFFER_TEST_DEPS)
endif
endif

clean_gpr_slice_buffer_test:
	$(E) "[CLEAN]   Cleaning gpr_slice_buffer_test files"
	$(Q) $(RM) $(GPR_SLICE_BUFFER_TEST_OBJS)
	$(Q) $(RM) $(GPR_SLICE_BUFFER_TEST_DEPS)
	$(Q) $(RM) bins/gpr_slice_buffer_test


GPR_SLICE_TEST_SRC = \
    test/core/support/slice_test.c \

GPR_SLICE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GPR_SLICE_TEST_SRC))))
GPR_SLICE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GPR_SLICE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/gpr_slice_test: openssl_dep_error

else

bins/gpr_slice_test: $(GPR_SLICE_TEST_OBJS) libs/libgrpc_test_util.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_SLICE_TEST_OBJS) -Llibs -lgrpc_test_util -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/gpr_slice_test

endif

deps_gpr_slice_test: $(GPR_SLICE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_SLICE_TEST_DEPS)
endif
endif

clean_gpr_slice_test:
	$(E) "[CLEAN]   Cleaning gpr_slice_test files"
	$(Q) $(RM) $(GPR_SLICE_TEST_OBJS)
	$(Q) $(RM) $(GPR_SLICE_TEST_DEPS)
	$(Q) $(RM) bins/gpr_slice_test


GPR_STRING_TEST_SRC = \
    test/core/support/string_test.c \

GPR_STRING_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GPR_STRING_TEST_SRC))))
GPR_STRING_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GPR_STRING_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/gpr_string_test: openssl_dep_error

else

bins/gpr_string_test: $(GPR_STRING_TEST_OBJS) libs/libgrpc_test_util.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_STRING_TEST_OBJS) -Llibs -lgrpc_test_util -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/gpr_string_test

endif

deps_gpr_string_test: $(GPR_STRING_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_STRING_TEST_DEPS)
endif
endif

clean_gpr_string_test:
	$(E) "[CLEAN]   Cleaning gpr_string_test files"
	$(Q) $(RM) $(GPR_STRING_TEST_OBJS)
	$(Q) $(RM) $(GPR_STRING_TEST_DEPS)
	$(Q) $(RM) bins/gpr_string_test


GPR_SYNC_TEST_SRC = \
    test/core/support/sync_test.c \

GPR_SYNC_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GPR_SYNC_TEST_SRC))))
GPR_SYNC_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GPR_SYNC_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/gpr_sync_test: openssl_dep_error

else

bins/gpr_sync_test: $(GPR_SYNC_TEST_OBJS) libs/libgrpc_test_util.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_SYNC_TEST_OBJS) -Llibs -lgrpc_test_util -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/gpr_sync_test

endif

deps_gpr_sync_test: $(GPR_SYNC_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_SYNC_TEST_DEPS)
endif
endif

clean_gpr_sync_test:
	$(E) "[CLEAN]   Cleaning gpr_sync_test files"
	$(Q) $(RM) $(GPR_SYNC_TEST_OBJS)
	$(Q) $(RM) $(GPR_SYNC_TEST_DEPS)
	$(Q) $(RM) bins/gpr_sync_test


GPR_THD_TEST_SRC = \
    test/core/support/thd_test.c \

GPR_THD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GPR_THD_TEST_SRC))))
GPR_THD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GPR_THD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/gpr_thd_test: openssl_dep_error

else

bins/gpr_thd_test: $(GPR_THD_TEST_OBJS) libs/libgrpc_test_util.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_THD_TEST_OBJS) -Llibs -lgrpc_test_util -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/gpr_thd_test

endif

deps_gpr_thd_test: $(GPR_THD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_THD_TEST_DEPS)
endif
endif

clean_gpr_thd_test:
	$(E) "[CLEAN]   Cleaning gpr_thd_test files"
	$(Q) $(RM) $(GPR_THD_TEST_OBJS)
	$(Q) $(RM) $(GPR_THD_TEST_DEPS)
	$(Q) $(RM) bins/gpr_thd_test


GPR_TIME_TEST_SRC = \
    test/core/support/time_test.c \

GPR_TIME_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GPR_TIME_TEST_SRC))))
GPR_TIME_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GPR_TIME_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/gpr_time_test: openssl_dep_error

else

bins/gpr_time_test: $(GPR_TIME_TEST_OBJS) libs/libgrpc_test_util.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GPR_TIME_TEST_OBJS) -Llibs -lgrpc_test_util -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/gpr_time_test

endif

deps_gpr_time_test: $(GPR_TIME_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GPR_TIME_TEST_DEPS)
endif
endif

clean_gpr_time_test:
	$(E) "[CLEAN]   Cleaning gpr_time_test files"
	$(Q) $(RM) $(GPR_TIME_TEST_OBJS)
	$(Q) $(RM) $(GPR_TIME_TEST_DEPS)
	$(Q) $(RM) bins/gpr_time_test


MURMUR_HASH_TEST_SRC = \
    test/core/support/murmur_hash_test.c \

MURMUR_HASH_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(MURMUR_HASH_TEST_SRC))))
MURMUR_HASH_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(MURMUR_HASH_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/murmur_hash_test: openssl_dep_error

else

bins/murmur_hash_test: $(MURMUR_HASH_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(MURMUR_HASH_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/murmur_hash_test

endif

deps_murmur_hash_test: $(MURMUR_HASH_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(MURMUR_HASH_TEST_DEPS)
endif
endif

clean_murmur_hash_test:
	$(E) "[CLEAN]   Cleaning murmur_hash_test files"
	$(Q) $(RM) $(MURMUR_HASH_TEST_OBJS)
	$(Q) $(RM) $(MURMUR_HASH_TEST_DEPS)
	$(Q) $(RM) bins/murmur_hash_test


GRPC_STREAM_OP_TEST_SRC = \
    test/core/transport/stream_op_test.c \

GRPC_STREAM_OP_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GRPC_STREAM_OP_TEST_SRC))))
GRPC_STREAM_OP_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GRPC_STREAM_OP_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/grpc_stream_op_test: openssl_dep_error

else

bins/grpc_stream_op_test: $(GRPC_STREAM_OP_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_STREAM_OP_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/grpc_stream_op_test

endif

deps_grpc_stream_op_test: $(GRPC_STREAM_OP_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_STREAM_OP_TEST_DEPS)
endif
endif

clean_grpc_stream_op_test:
	$(E) "[CLEAN]   Cleaning grpc_stream_op_test files"
	$(Q) $(RM) $(GRPC_STREAM_OP_TEST_OBJS)
	$(Q) $(RM) $(GRPC_STREAM_OP_TEST_DEPS)
	$(Q) $(RM) bins/grpc_stream_op_test


ALPN_TEST_SRC = \
    test/core/transport/chttp2/alpn_test.c \

ALPN_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(ALPN_TEST_SRC))))
ALPN_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(ALPN_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/alpn_test: openssl_dep_error

else

bins/alpn_test: $(ALPN_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(ALPN_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/alpn_test

endif

deps_alpn_test: $(ALPN_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ALPN_TEST_DEPS)
endif
endif

clean_alpn_test:
	$(E) "[CLEAN]   Cleaning alpn_test files"
	$(Q) $(RM) $(ALPN_TEST_OBJS)
	$(Q) $(RM) $(ALPN_TEST_DEPS)
	$(Q) $(RM) bins/alpn_test


CHTTP2_STREAM_ENCODER_TEST_SRC = \
    test/core/transport/chttp2/stream_encoder_test.c \

CHTTP2_STREAM_ENCODER_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_STREAM_ENCODER_TEST_SRC))))
CHTTP2_STREAM_ENCODER_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_STREAM_ENCODER_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_stream_encoder_test: openssl_dep_error

else

bins/chttp2_stream_encoder_test: $(CHTTP2_STREAM_ENCODER_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_STREAM_ENCODER_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_stream_encoder_test

endif

deps_chttp2_stream_encoder_test: $(CHTTP2_STREAM_ENCODER_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_STREAM_ENCODER_TEST_DEPS)
endif
endif

clean_chttp2_stream_encoder_test:
	$(E) "[CLEAN]   Cleaning chttp2_stream_encoder_test files"
	$(Q) $(RM) $(CHTTP2_STREAM_ENCODER_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_STREAM_ENCODER_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_stream_encoder_test


HPACK_TABLE_TEST_SRC = \
    test/core/transport/chttp2/hpack_table_test.c \

HPACK_TABLE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(HPACK_TABLE_TEST_SRC))))
HPACK_TABLE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(HPACK_TABLE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/hpack_table_test: openssl_dep_error

else

bins/hpack_table_test: $(HPACK_TABLE_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HPACK_TABLE_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/hpack_table_test

endif

deps_hpack_table_test: $(HPACK_TABLE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HPACK_TABLE_TEST_DEPS)
endif
endif

clean_hpack_table_test:
	$(E) "[CLEAN]   Cleaning hpack_table_test files"
	$(Q) $(RM) $(HPACK_TABLE_TEST_OBJS)
	$(Q) $(RM) $(HPACK_TABLE_TEST_DEPS)
	$(Q) $(RM) bins/hpack_table_test


CHTTP2_STREAM_MAP_TEST_SRC = \
    test/core/transport/chttp2/stream_map_test.c \

CHTTP2_STREAM_MAP_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_STREAM_MAP_TEST_SRC))))
CHTTP2_STREAM_MAP_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_STREAM_MAP_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_stream_map_test: openssl_dep_error

else

bins/chttp2_stream_map_test: $(CHTTP2_STREAM_MAP_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_STREAM_MAP_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_stream_map_test

endif

deps_chttp2_stream_map_test: $(CHTTP2_STREAM_MAP_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_STREAM_MAP_TEST_DEPS)
endif
endif

clean_chttp2_stream_map_test:
	$(E) "[CLEAN]   Cleaning chttp2_stream_map_test files"
	$(Q) $(RM) $(CHTTP2_STREAM_MAP_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_STREAM_MAP_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_stream_map_test


HPACK_PARSER_TEST_SRC = \
    test/core/transport/chttp2/hpack_parser_test.c \

HPACK_PARSER_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(HPACK_PARSER_TEST_SRC))))
HPACK_PARSER_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(HPACK_PARSER_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/hpack_parser_test: openssl_dep_error

else

bins/hpack_parser_test: $(HPACK_PARSER_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HPACK_PARSER_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/hpack_parser_test

endif

deps_hpack_parser_test: $(HPACK_PARSER_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HPACK_PARSER_TEST_DEPS)
endif
endif

clean_hpack_parser_test:
	$(E) "[CLEAN]   Cleaning hpack_parser_test files"
	$(Q) $(RM) $(HPACK_PARSER_TEST_OBJS)
	$(Q) $(RM) $(HPACK_PARSER_TEST_DEPS)
	$(Q) $(RM) bins/hpack_parser_test


TRANSPORT_METADATA_TEST_SRC = \
    test/core/transport/metadata_test.c \

TRANSPORT_METADATA_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(TRANSPORT_METADATA_TEST_SRC))))
TRANSPORT_METADATA_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(TRANSPORT_METADATA_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/transport_metadata_test: openssl_dep_error

else

bins/transport_metadata_test: $(TRANSPORT_METADATA_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TRANSPORT_METADATA_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/transport_metadata_test

endif

deps_transport_metadata_test: $(TRANSPORT_METADATA_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TRANSPORT_METADATA_TEST_DEPS)
endif
endif

clean_transport_metadata_test:
	$(E) "[CLEAN]   Cleaning transport_metadata_test files"
	$(Q) $(RM) $(TRANSPORT_METADATA_TEST_OBJS)
	$(Q) $(RM) $(TRANSPORT_METADATA_TEST_DEPS)
	$(Q) $(RM) bins/transport_metadata_test


CHTTP2_STATUS_CONVERSION_TEST_SRC = \
    test/core/transport/chttp2/status_conversion_test.c \

CHTTP2_STATUS_CONVERSION_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_STATUS_CONVERSION_TEST_SRC))))
CHTTP2_STATUS_CONVERSION_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_STATUS_CONVERSION_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_status_conversion_test: openssl_dep_error

else

bins/chttp2_status_conversion_test: $(CHTTP2_STATUS_CONVERSION_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_STATUS_CONVERSION_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_status_conversion_test

endif

deps_chttp2_status_conversion_test: $(CHTTP2_STATUS_CONVERSION_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_STATUS_CONVERSION_TEST_DEPS)
endif
endif

clean_chttp2_status_conversion_test:
	$(E) "[CLEAN]   Cleaning chttp2_status_conversion_test files"
	$(Q) $(RM) $(CHTTP2_STATUS_CONVERSION_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_STATUS_CONVERSION_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_status_conversion_test


CHTTP2_TRANSPORT_END2END_TEST_SRC = \
    test/core/transport/chttp2_transport_end2end_test.c \

CHTTP2_TRANSPORT_END2END_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_TRANSPORT_END2END_TEST_SRC))))
CHTTP2_TRANSPORT_END2END_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_TRANSPORT_END2END_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_transport_end2end_test: openssl_dep_error

else

bins/chttp2_transport_end2end_test: $(CHTTP2_TRANSPORT_END2END_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_TRANSPORT_END2END_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_transport_end2end_test

endif

deps_chttp2_transport_end2end_test: $(CHTTP2_TRANSPORT_END2END_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_TRANSPORT_END2END_TEST_DEPS)
endif
endif

clean_chttp2_transport_end2end_test:
	$(E) "[CLEAN]   Cleaning chttp2_transport_end2end_test files"
	$(Q) $(RM) $(CHTTP2_TRANSPORT_END2END_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_TRANSPORT_END2END_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_transport_end2end_test


TCP_POSIX_TEST_SRC = \
    test/core/iomgr/tcp_posix_test.c \

TCP_POSIX_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(TCP_POSIX_TEST_SRC))))
TCP_POSIX_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(TCP_POSIX_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/tcp_posix_test: openssl_dep_error

else

bins/tcp_posix_test: $(TCP_POSIX_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TCP_POSIX_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/tcp_posix_test

endif

deps_tcp_posix_test: $(TCP_POSIX_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TCP_POSIX_TEST_DEPS)
endif
endif

clean_tcp_posix_test:
	$(E) "[CLEAN]   Cleaning tcp_posix_test files"
	$(Q) $(RM) $(TCP_POSIX_TEST_OBJS)
	$(Q) $(RM) $(TCP_POSIX_TEST_DEPS)
	$(Q) $(RM) bins/tcp_posix_test


DUALSTACK_SOCKET_TEST_SRC = \
    test/core/end2end/dualstack_socket_test.c \

DUALSTACK_SOCKET_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(DUALSTACK_SOCKET_TEST_SRC))))
DUALSTACK_SOCKET_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(DUALSTACK_SOCKET_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/dualstack_socket_test: openssl_dep_error

else

bins/dualstack_socket_test: $(DUALSTACK_SOCKET_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(DUALSTACK_SOCKET_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/dualstack_socket_test

endif

deps_dualstack_socket_test: $(DUALSTACK_SOCKET_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(DUALSTACK_SOCKET_TEST_DEPS)
endif
endif

clean_dualstack_socket_test:
	$(E) "[CLEAN]   Cleaning dualstack_socket_test files"
	$(Q) $(RM) $(DUALSTACK_SOCKET_TEST_OBJS)
	$(Q) $(RM) $(DUALSTACK_SOCKET_TEST_DEPS)
	$(Q) $(RM) bins/dualstack_socket_test


NO_SERVER_TEST_SRC = \
    test/core/end2end/no_server_test.c \

NO_SERVER_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(NO_SERVER_TEST_SRC))))
NO_SERVER_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(NO_SERVER_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/no_server_test: openssl_dep_error

else

bins/no_server_test: $(NO_SERVER_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(NO_SERVER_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/no_server_test

endif

deps_no_server_test: $(NO_SERVER_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(NO_SERVER_TEST_DEPS)
endif
endif

clean_no_server_test:
	$(E) "[CLEAN]   Cleaning no_server_test files"
	$(Q) $(RM) $(NO_SERVER_TEST_OBJS)
	$(Q) $(RM) $(NO_SERVER_TEST_DEPS)
	$(Q) $(RM) bins/no_server_test


RESOLVE_ADDRESS_TEST_SRC = \
    test/core/iomgr/resolve_address_test.c \

RESOLVE_ADDRESS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(RESOLVE_ADDRESS_TEST_SRC))))
RESOLVE_ADDRESS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(RESOLVE_ADDRESS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/resolve_address_test: openssl_dep_error

else

bins/resolve_address_test: $(RESOLVE_ADDRESS_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(RESOLVE_ADDRESS_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/resolve_address_test

endif

deps_resolve_address_test: $(RESOLVE_ADDRESS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(RESOLVE_ADDRESS_TEST_DEPS)
endif
endif

clean_resolve_address_test:
	$(E) "[CLEAN]   Cleaning resolve_address_test files"
	$(Q) $(RM) $(RESOLVE_ADDRESS_TEST_OBJS)
	$(Q) $(RM) $(RESOLVE_ADDRESS_TEST_DEPS)
	$(Q) $(RM) bins/resolve_address_test


SOCKADDR_UTILS_TEST_SRC = \
    test/core/iomgr/sockaddr_utils_test.c \

SOCKADDR_UTILS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(SOCKADDR_UTILS_TEST_SRC))))
SOCKADDR_UTILS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(SOCKADDR_UTILS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/sockaddr_utils_test: openssl_dep_error

else

bins/sockaddr_utils_test: $(SOCKADDR_UTILS_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SOCKADDR_UTILS_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/sockaddr_utils_test

endif

deps_sockaddr_utils_test: $(SOCKADDR_UTILS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SOCKADDR_UTILS_TEST_DEPS)
endif
endif

clean_sockaddr_utils_test:
	$(E) "[CLEAN]   Cleaning sockaddr_utils_test files"
	$(Q) $(RM) $(SOCKADDR_UTILS_TEST_OBJS)
	$(Q) $(RM) $(SOCKADDR_UTILS_TEST_DEPS)
	$(Q) $(RM) bins/sockaddr_utils_test


TCP_SERVER_POSIX_TEST_SRC = \
    test/core/iomgr/tcp_server_posix_test.c \

TCP_SERVER_POSIX_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(TCP_SERVER_POSIX_TEST_SRC))))
TCP_SERVER_POSIX_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(TCP_SERVER_POSIX_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/tcp_server_posix_test: openssl_dep_error

else

bins/tcp_server_posix_test: $(TCP_SERVER_POSIX_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TCP_SERVER_POSIX_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/tcp_server_posix_test

endif

deps_tcp_server_posix_test: $(TCP_SERVER_POSIX_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TCP_SERVER_POSIX_TEST_DEPS)
endif
endif

clean_tcp_server_posix_test:
	$(E) "[CLEAN]   Cleaning tcp_server_posix_test files"
	$(Q) $(RM) $(TCP_SERVER_POSIX_TEST_OBJS)
	$(Q) $(RM) $(TCP_SERVER_POSIX_TEST_DEPS)
	$(Q) $(RM) bins/tcp_server_posix_test


TCP_CLIENT_POSIX_TEST_SRC = \
    test/core/iomgr/tcp_client_posix_test.c \

TCP_CLIENT_POSIX_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(TCP_CLIENT_POSIX_TEST_SRC))))
TCP_CLIENT_POSIX_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(TCP_CLIENT_POSIX_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/tcp_client_posix_test: openssl_dep_error

else

bins/tcp_client_posix_test: $(TCP_CLIENT_POSIX_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TCP_CLIENT_POSIX_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/tcp_client_posix_test

endif

deps_tcp_client_posix_test: $(TCP_CLIENT_POSIX_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TCP_CLIENT_POSIX_TEST_DEPS)
endif
endif

clean_tcp_client_posix_test:
	$(E) "[CLEAN]   Cleaning tcp_client_posix_test files"
	$(Q) $(RM) $(TCP_CLIENT_POSIX_TEST_OBJS)
	$(Q) $(RM) $(TCP_CLIENT_POSIX_TEST_DEPS)
	$(Q) $(RM) bins/tcp_client_posix_test


GRPC_CHANNEL_STACK_TEST_SRC = \
    test/core/channel/channel_stack_test.c \

GRPC_CHANNEL_STACK_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GRPC_CHANNEL_STACK_TEST_SRC))))
GRPC_CHANNEL_STACK_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GRPC_CHANNEL_STACK_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/grpc_channel_stack_test: openssl_dep_error

else

bins/grpc_channel_stack_test: $(GRPC_CHANNEL_STACK_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_CHANNEL_STACK_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/grpc_channel_stack_test

endif

deps_grpc_channel_stack_test: $(GRPC_CHANNEL_STACK_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_CHANNEL_STACK_TEST_DEPS)
endif
endif

clean_grpc_channel_stack_test:
	$(E) "[CLEAN]   Cleaning grpc_channel_stack_test files"
	$(Q) $(RM) $(GRPC_CHANNEL_STACK_TEST_OBJS)
	$(Q) $(RM) $(GRPC_CHANNEL_STACK_TEST_DEPS)
	$(Q) $(RM) bins/grpc_channel_stack_test


METADATA_BUFFER_TEST_SRC = \
    test/core/channel/metadata_buffer_test.c \

METADATA_BUFFER_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(METADATA_BUFFER_TEST_SRC))))
METADATA_BUFFER_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(METADATA_BUFFER_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/metadata_buffer_test: openssl_dep_error

else

bins/metadata_buffer_test: $(METADATA_BUFFER_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(METADATA_BUFFER_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/metadata_buffer_test

endif

deps_metadata_buffer_test: $(METADATA_BUFFER_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(METADATA_BUFFER_TEST_DEPS)
endif
endif

clean_metadata_buffer_test:
	$(E) "[CLEAN]   Cleaning metadata_buffer_test files"
	$(Q) $(RM) $(METADATA_BUFFER_TEST_OBJS)
	$(Q) $(RM) $(METADATA_BUFFER_TEST_DEPS)
	$(Q) $(RM) bins/metadata_buffer_test


GRPC_COMPLETION_QUEUE_TEST_SRC = \
    test/core/surface/completion_queue_test.c \

GRPC_COMPLETION_QUEUE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GRPC_COMPLETION_QUEUE_TEST_SRC))))
GRPC_COMPLETION_QUEUE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GRPC_COMPLETION_QUEUE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/grpc_completion_queue_test: openssl_dep_error

else

bins/grpc_completion_queue_test: $(GRPC_COMPLETION_QUEUE_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_COMPLETION_QUEUE_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/grpc_completion_queue_test

endif

deps_grpc_completion_queue_test: $(GRPC_COMPLETION_QUEUE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_COMPLETION_QUEUE_TEST_DEPS)
endif
endif

clean_grpc_completion_queue_test:
	$(E) "[CLEAN]   Cleaning grpc_completion_queue_test files"
	$(Q) $(RM) $(GRPC_COMPLETION_QUEUE_TEST_OBJS)
	$(Q) $(RM) $(GRPC_COMPLETION_QUEUE_TEST_DEPS)
	$(Q) $(RM) bins/grpc_completion_queue_test


GRPC_COMPLETION_QUEUE_BENCHMARK_SRC = \
    test/core/surface/completion_queue_benchmark.c \

GRPC_COMPLETION_QUEUE_BENCHMARK_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GRPC_COMPLETION_QUEUE_BENCHMARK_SRC))))
GRPC_COMPLETION_QUEUE_BENCHMARK_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GRPC_COMPLETION_QUEUE_BENCHMARK_SRC))))

ifeq ($(NO_SECURE),true)

bins/grpc_completion_queue_benchmark: openssl_dep_error

else

bins/grpc_completion_queue_benchmark: $(GRPC_COMPLETION_QUEUE_BENCHMARK_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_COMPLETION_QUEUE_BENCHMARK_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/grpc_completion_queue_benchmark

endif

deps_grpc_completion_queue_benchmark: $(GRPC_COMPLETION_QUEUE_BENCHMARK_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_COMPLETION_QUEUE_BENCHMARK_DEPS)
endif
endif

clean_grpc_completion_queue_benchmark:
	$(E) "[CLEAN]   Cleaning grpc_completion_queue_benchmark files"
	$(Q) $(RM) $(GRPC_COMPLETION_QUEUE_BENCHMARK_OBJS)
	$(Q) $(RM) $(GRPC_COMPLETION_QUEUE_BENCHMARK_DEPS)
	$(Q) $(RM) bins/grpc_completion_queue_benchmark


CENSUS_WINDOW_STATS_TEST_SRC = \
    test/core/statistics/window_stats_test.c \

CENSUS_WINDOW_STATS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CENSUS_WINDOW_STATS_TEST_SRC))))
CENSUS_WINDOW_STATS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CENSUS_WINDOW_STATS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/census_window_stats_test: openssl_dep_error

else

bins/census_window_stats_test: $(CENSUS_WINDOW_STATS_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CENSUS_WINDOW_STATS_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/census_window_stats_test

endif

deps_census_window_stats_test: $(CENSUS_WINDOW_STATS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CENSUS_WINDOW_STATS_TEST_DEPS)
endif
endif

clean_census_window_stats_test:
	$(E) "[CLEAN]   Cleaning census_window_stats_test files"
	$(Q) $(RM) $(CENSUS_WINDOW_STATS_TEST_OBJS)
	$(Q) $(RM) $(CENSUS_WINDOW_STATS_TEST_DEPS)
	$(Q) $(RM) bins/census_window_stats_test


CENSUS_STATISTICS_QUICK_TEST_SRC = \
    test/core/statistics/quick_test.c \

CENSUS_STATISTICS_QUICK_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CENSUS_STATISTICS_QUICK_TEST_SRC))))
CENSUS_STATISTICS_QUICK_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CENSUS_STATISTICS_QUICK_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/census_statistics_quick_test: openssl_dep_error

else

bins/census_statistics_quick_test: $(CENSUS_STATISTICS_QUICK_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CENSUS_STATISTICS_QUICK_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/census_statistics_quick_test

endif

deps_census_statistics_quick_test: $(CENSUS_STATISTICS_QUICK_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CENSUS_STATISTICS_QUICK_TEST_DEPS)
endif
endif

clean_census_statistics_quick_test:
	$(E) "[CLEAN]   Cleaning census_statistics_quick_test files"
	$(Q) $(RM) $(CENSUS_STATISTICS_QUICK_TEST_OBJS)
	$(Q) $(RM) $(CENSUS_STATISTICS_QUICK_TEST_DEPS)
	$(Q) $(RM) bins/census_statistics_quick_test


CENSUS_STATISTICS_PERFORMANCE_TEST_SRC = \
    test/core/statistics/performance_test.c \

CENSUS_STATISTICS_PERFORMANCE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CENSUS_STATISTICS_PERFORMANCE_TEST_SRC))))
CENSUS_STATISTICS_PERFORMANCE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CENSUS_STATISTICS_PERFORMANCE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/census_statistics_performance_test: openssl_dep_error

else

bins/census_statistics_performance_test: $(CENSUS_STATISTICS_PERFORMANCE_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CENSUS_STATISTICS_PERFORMANCE_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/census_statistics_performance_test

endif

deps_census_statistics_performance_test: $(CENSUS_STATISTICS_PERFORMANCE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CENSUS_STATISTICS_PERFORMANCE_TEST_DEPS)
endif
endif

clean_census_statistics_performance_test:
	$(E) "[CLEAN]   Cleaning census_statistics_performance_test files"
	$(Q) $(RM) $(CENSUS_STATISTICS_PERFORMANCE_TEST_OBJS)
	$(Q) $(RM) $(CENSUS_STATISTICS_PERFORMANCE_TEST_DEPS)
	$(Q) $(RM) bins/census_statistics_performance_test


CENSUS_STATISTICS_MULTIPLE_WRITERS_TEST_SRC = \
    test/core/statistics/multiple_writers_test.c \

CENSUS_STATISTICS_MULTIPLE_WRITERS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CENSUS_STATISTICS_MULTIPLE_WRITERS_TEST_SRC))))
CENSUS_STATISTICS_MULTIPLE_WRITERS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CENSUS_STATISTICS_MULTIPLE_WRITERS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/census_statistics_multiple_writers_test: openssl_dep_error

else

bins/census_statistics_multiple_writers_test: $(CENSUS_STATISTICS_MULTIPLE_WRITERS_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CENSUS_STATISTICS_MULTIPLE_WRITERS_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/census_statistics_multiple_writers_test

endif

deps_census_statistics_multiple_writers_test: $(CENSUS_STATISTICS_MULTIPLE_WRITERS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CENSUS_STATISTICS_MULTIPLE_WRITERS_TEST_DEPS)
endif
endif

clean_census_statistics_multiple_writers_test:
	$(E) "[CLEAN]   Cleaning census_statistics_multiple_writers_test files"
	$(Q) $(RM) $(CENSUS_STATISTICS_MULTIPLE_WRITERS_TEST_OBJS)
	$(Q) $(RM) $(CENSUS_STATISTICS_MULTIPLE_WRITERS_TEST_DEPS)
	$(Q) $(RM) bins/census_statistics_multiple_writers_test


CENSUS_STATISTICS_MULTIPLE_WRITERS_CIRCULAR_BUFFER_TEST_SRC = \
    test/core/statistics/multiple_writers_circular_buffer_test.c \

CENSUS_STATISTICS_MULTIPLE_WRITERS_CIRCULAR_BUFFER_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CENSUS_STATISTICS_MULTIPLE_WRITERS_CIRCULAR_BUFFER_TEST_SRC))))
CENSUS_STATISTICS_MULTIPLE_WRITERS_CIRCULAR_BUFFER_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CENSUS_STATISTICS_MULTIPLE_WRITERS_CIRCULAR_BUFFER_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/census_statistics_multiple_writers_circular_buffer_test: openssl_dep_error

else

bins/census_statistics_multiple_writers_circular_buffer_test: $(CENSUS_STATISTICS_MULTIPLE_WRITERS_CIRCULAR_BUFFER_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CENSUS_STATISTICS_MULTIPLE_WRITERS_CIRCULAR_BUFFER_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/census_statistics_multiple_writers_circular_buffer_test

endif

deps_census_statistics_multiple_writers_circular_buffer_test: $(CENSUS_STATISTICS_MULTIPLE_WRITERS_CIRCULAR_BUFFER_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CENSUS_STATISTICS_MULTIPLE_WRITERS_CIRCULAR_BUFFER_TEST_DEPS)
endif
endif

clean_census_statistics_multiple_writers_circular_buffer_test:
	$(E) "[CLEAN]   Cleaning census_statistics_multiple_writers_circular_buffer_test files"
	$(Q) $(RM) $(CENSUS_STATISTICS_MULTIPLE_WRITERS_CIRCULAR_BUFFER_TEST_OBJS)
	$(Q) $(RM) $(CENSUS_STATISTICS_MULTIPLE_WRITERS_CIRCULAR_BUFFER_TEST_DEPS)
	$(Q) $(RM) bins/census_statistics_multiple_writers_circular_buffer_test


CENSUS_STUB_TEST_SRC = \
    test/core/statistics/census_stub_test.c \

CENSUS_STUB_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CENSUS_STUB_TEST_SRC))))
CENSUS_STUB_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CENSUS_STUB_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/census_stub_test: openssl_dep_error

else

bins/census_stub_test: $(CENSUS_STUB_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CENSUS_STUB_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/census_stub_test

endif

deps_census_stub_test: $(CENSUS_STUB_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CENSUS_STUB_TEST_DEPS)
endif
endif

clean_census_stub_test:
	$(E) "[CLEAN]   Cleaning census_stub_test files"
	$(Q) $(RM) $(CENSUS_STUB_TEST_OBJS)
	$(Q) $(RM) $(CENSUS_STUB_TEST_DEPS)
	$(Q) $(RM) bins/census_stub_test


CENSUS_HASH_TABLE_TEST_SRC = \
    test/core/statistics/hash_table_test.c \

CENSUS_HASH_TABLE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CENSUS_HASH_TABLE_TEST_SRC))))
CENSUS_HASH_TABLE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CENSUS_HASH_TABLE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/census_hash_table_test: openssl_dep_error

else

bins/census_hash_table_test: $(CENSUS_HASH_TABLE_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CENSUS_HASH_TABLE_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/census_hash_table_test

endif

deps_census_hash_table_test: $(CENSUS_HASH_TABLE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CENSUS_HASH_TABLE_TEST_DEPS)
endif
endif

clean_census_hash_table_test:
	$(E) "[CLEAN]   Cleaning census_hash_table_test files"
	$(Q) $(RM) $(CENSUS_HASH_TABLE_TEST_OBJS)
	$(Q) $(RM) $(CENSUS_HASH_TABLE_TEST_DEPS)
	$(Q) $(RM) bins/census_hash_table_test


FLING_SERVER_SRC = \
    test/core/fling/server.c \

FLING_SERVER_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(FLING_SERVER_SRC))))
FLING_SERVER_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(FLING_SERVER_SRC))))

ifeq ($(NO_SECURE),true)

bins/fling_server: openssl_dep_error

else

bins/fling_server: $(FLING_SERVER_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(FLING_SERVER_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/fling_server

endif

deps_fling_server: $(FLING_SERVER_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FLING_SERVER_DEPS)
endif
endif

clean_fling_server:
	$(E) "[CLEAN]   Cleaning fling_server files"
	$(Q) $(RM) $(FLING_SERVER_OBJS)
	$(Q) $(RM) $(FLING_SERVER_DEPS)
	$(Q) $(RM) bins/fling_server


FLING_CLIENT_SRC = \
    test/core/fling/client.c \

FLING_CLIENT_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(FLING_CLIENT_SRC))))
FLING_CLIENT_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(FLING_CLIENT_SRC))))

ifeq ($(NO_SECURE),true)

bins/fling_client: openssl_dep_error

else

bins/fling_client: $(FLING_CLIENT_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(FLING_CLIENT_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/fling_client

endif

deps_fling_client: $(FLING_CLIENT_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FLING_CLIENT_DEPS)
endif
endif

clean_fling_client:
	$(E) "[CLEAN]   Cleaning fling_client files"
	$(Q) $(RM) $(FLING_CLIENT_OBJS)
	$(Q) $(RM) $(FLING_CLIENT_DEPS)
	$(Q) $(RM) bins/fling_client


FLING_TEST_SRC = \
    test/core/fling/fling_test.c \

FLING_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(FLING_TEST_SRC))))
FLING_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(FLING_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/fling_test: openssl_dep_error

else

bins/fling_test: $(FLING_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(FLING_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/fling_test

endif

deps_fling_test: $(FLING_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FLING_TEST_DEPS)
endif
endif

clean_fling_test:
	$(E) "[CLEAN]   Cleaning fling_test files"
	$(Q) $(RM) $(FLING_TEST_OBJS)
	$(Q) $(RM) $(FLING_TEST_DEPS)
	$(Q) $(RM) bins/fling_test


ECHO_SERVER_SRC = \
    test/core/echo/server.c \

ECHO_SERVER_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(ECHO_SERVER_SRC))))
ECHO_SERVER_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(ECHO_SERVER_SRC))))

ifeq ($(NO_SECURE),true)

bins/echo_server: openssl_dep_error

else

bins/echo_server: $(ECHO_SERVER_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(ECHO_SERVER_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/echo_server

endif

deps_echo_server: $(ECHO_SERVER_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ECHO_SERVER_DEPS)
endif
endif

clean_echo_server:
	$(E) "[CLEAN]   Cleaning echo_server files"
	$(Q) $(RM) $(ECHO_SERVER_OBJS)
	$(Q) $(RM) $(ECHO_SERVER_DEPS)
	$(Q) $(RM) bins/echo_server


ECHO_CLIENT_SRC = \
    test/core/echo/client.c \

ECHO_CLIENT_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(ECHO_CLIENT_SRC))))
ECHO_CLIENT_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(ECHO_CLIENT_SRC))))

ifeq ($(NO_SECURE),true)

bins/echo_client: openssl_dep_error

else

bins/echo_client: $(ECHO_CLIENT_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(ECHO_CLIENT_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/echo_client

endif

deps_echo_client: $(ECHO_CLIENT_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ECHO_CLIENT_DEPS)
endif
endif

clean_echo_client:
	$(E) "[CLEAN]   Cleaning echo_client files"
	$(Q) $(RM) $(ECHO_CLIENT_OBJS)
	$(Q) $(RM) $(ECHO_CLIENT_DEPS)
	$(Q) $(RM) bins/echo_client


ECHO_TEST_SRC = \
    test/core/echo/echo_test.c \

ECHO_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(ECHO_TEST_SRC))))
ECHO_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(ECHO_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/echo_test: openssl_dep_error

else

bins/echo_test: $(ECHO_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(ECHO_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/echo_test

endif

deps_echo_test: $(ECHO_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ECHO_TEST_DEPS)
endif
endif

clean_echo_test:
	$(E) "[CLEAN]   Cleaning echo_test files"
	$(Q) $(RM) $(ECHO_TEST_OBJS)
	$(Q) $(RM) $(ECHO_TEST_DEPS)
	$(Q) $(RM) bins/echo_test


LOW_LEVEL_PING_PONG_BENCHMARK_SRC = \
    test/core/network_benchmarks/low_level_ping_pong.c \

LOW_LEVEL_PING_PONG_BENCHMARK_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LOW_LEVEL_PING_PONG_BENCHMARK_SRC))))
LOW_LEVEL_PING_PONG_BENCHMARK_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LOW_LEVEL_PING_PONG_BENCHMARK_SRC))))

ifeq ($(NO_SECURE),true)

bins/low_level_ping_pong_benchmark: openssl_dep_error

else

bins/low_level_ping_pong_benchmark: $(LOW_LEVEL_PING_PONG_BENCHMARK_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(LOW_LEVEL_PING_PONG_BENCHMARK_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/low_level_ping_pong_benchmark

endif

deps_low_level_ping_pong_benchmark: $(LOW_LEVEL_PING_PONG_BENCHMARK_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LOW_LEVEL_PING_PONG_BENCHMARK_DEPS)
endif
endif

clean_low_level_ping_pong_benchmark:
	$(E) "[CLEAN]   Cleaning low_level_ping_pong_benchmark files"
	$(Q) $(RM) $(LOW_LEVEL_PING_PONG_BENCHMARK_OBJS)
	$(Q) $(RM) $(LOW_LEVEL_PING_PONG_BENCHMARK_DEPS)
	$(Q) $(RM) bins/low_level_ping_pong_benchmark


MESSAGE_COMPRESS_TEST_SRC = \
    test/core/compression/message_compress_test.c \

MESSAGE_COMPRESS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(MESSAGE_COMPRESS_TEST_SRC))))
MESSAGE_COMPRESS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(MESSAGE_COMPRESS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/message_compress_test: openssl_dep_error

else

bins/message_compress_test: $(MESSAGE_COMPRESS_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(MESSAGE_COMPRESS_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/message_compress_test

endif

deps_message_compress_test: $(MESSAGE_COMPRESS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(MESSAGE_COMPRESS_TEST_DEPS)
endif
endif

clean_message_compress_test:
	$(E) "[CLEAN]   Cleaning message_compress_test files"
	$(Q) $(RM) $(MESSAGE_COMPRESS_TEST_OBJS)
	$(Q) $(RM) $(MESSAGE_COMPRESS_TEST_DEPS)
	$(Q) $(RM) bins/message_compress_test


BIN_ENCODER_TEST_SRC = \
    test/core/transport/chttp2/bin_encoder_test.c \

BIN_ENCODER_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(BIN_ENCODER_TEST_SRC))))
BIN_ENCODER_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(BIN_ENCODER_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/bin_encoder_test: openssl_dep_error

else

bins/bin_encoder_test: $(BIN_ENCODER_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(BIN_ENCODER_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/bin_encoder_test

endif

deps_bin_encoder_test: $(BIN_ENCODER_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(BIN_ENCODER_TEST_DEPS)
endif
endif

clean_bin_encoder_test:
	$(E) "[CLEAN]   Cleaning bin_encoder_test files"
	$(Q) $(RM) $(BIN_ENCODER_TEST_OBJS)
	$(Q) $(RM) $(BIN_ENCODER_TEST_DEPS)
	$(Q) $(RM) bins/bin_encoder_test


SECURE_ENDPOINT_TEST_SRC = \
    test/core/endpoint/secure_endpoint_test.c \

SECURE_ENDPOINT_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(SECURE_ENDPOINT_TEST_SRC))))
SECURE_ENDPOINT_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(SECURE_ENDPOINT_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/secure_endpoint_test: openssl_dep_error

else

bins/secure_endpoint_test: $(SECURE_ENDPOINT_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(SECURE_ENDPOINT_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/secure_endpoint_test

endif

deps_secure_endpoint_test: $(SECURE_ENDPOINT_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SECURE_ENDPOINT_TEST_DEPS)
endif
endif

clean_secure_endpoint_test:
	$(E) "[CLEAN]   Cleaning secure_endpoint_test files"
	$(Q) $(RM) $(SECURE_ENDPOINT_TEST_OBJS)
	$(Q) $(RM) $(SECURE_ENDPOINT_TEST_DEPS)
	$(Q) $(RM) bins/secure_endpoint_test


HTTPCLI_FORMAT_REQUEST_TEST_SRC = \
    test/core/httpcli/format_request_test.c \

HTTPCLI_FORMAT_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(HTTPCLI_FORMAT_REQUEST_TEST_SRC))))
HTTPCLI_FORMAT_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(HTTPCLI_FORMAT_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/httpcli_format_request_test: openssl_dep_error

else

bins/httpcli_format_request_test: $(HTTPCLI_FORMAT_REQUEST_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HTTPCLI_FORMAT_REQUEST_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/httpcli_format_request_test

endif

deps_httpcli_format_request_test: $(HTTPCLI_FORMAT_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HTTPCLI_FORMAT_REQUEST_TEST_DEPS)
endif
endif

clean_httpcli_format_request_test:
	$(E) "[CLEAN]   Cleaning httpcli_format_request_test files"
	$(Q) $(RM) $(HTTPCLI_FORMAT_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(HTTPCLI_FORMAT_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/httpcli_format_request_test


HTTPCLI_PARSER_TEST_SRC = \
    test/core/httpcli/parser_test.c \

HTTPCLI_PARSER_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(HTTPCLI_PARSER_TEST_SRC))))
HTTPCLI_PARSER_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(HTTPCLI_PARSER_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/httpcli_parser_test: openssl_dep_error

else

bins/httpcli_parser_test: $(HTTPCLI_PARSER_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HTTPCLI_PARSER_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/httpcli_parser_test

endif

deps_httpcli_parser_test: $(HTTPCLI_PARSER_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HTTPCLI_PARSER_TEST_DEPS)
endif
endif

clean_httpcli_parser_test:
	$(E) "[CLEAN]   Cleaning httpcli_parser_test files"
	$(Q) $(RM) $(HTTPCLI_PARSER_TEST_OBJS)
	$(Q) $(RM) $(HTTPCLI_PARSER_TEST_DEPS)
	$(Q) $(RM) bins/httpcli_parser_test


HTTPCLI_TEST_SRC = \
    test/core/httpcli/httpcli_test.c \

HTTPCLI_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(HTTPCLI_TEST_SRC))))
HTTPCLI_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(HTTPCLI_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/httpcli_test: openssl_dep_error

else

bins/httpcli_test: $(HTTPCLI_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(HTTPCLI_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/httpcli_test

endif

deps_httpcli_test: $(HTTPCLI_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(HTTPCLI_TEST_DEPS)
endif
endif

clean_httpcli_test:
	$(E) "[CLEAN]   Cleaning httpcli_test files"
	$(Q) $(RM) $(HTTPCLI_TEST_OBJS)
	$(Q) $(RM) $(HTTPCLI_TEST_DEPS)
	$(Q) $(RM) bins/httpcli_test


GRPC_CREDENTIALS_TEST_SRC = \
    test/core/security/credentials_test.c \

GRPC_CREDENTIALS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GRPC_CREDENTIALS_TEST_SRC))))
GRPC_CREDENTIALS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GRPC_CREDENTIALS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/grpc_credentials_test: openssl_dep_error

else

bins/grpc_credentials_test: $(GRPC_CREDENTIALS_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_CREDENTIALS_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/grpc_credentials_test

endif

deps_grpc_credentials_test: $(GRPC_CREDENTIALS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_CREDENTIALS_TEST_DEPS)
endif
endif

clean_grpc_credentials_test:
	$(E) "[CLEAN]   Cleaning grpc_credentials_test files"
	$(Q) $(RM) $(GRPC_CREDENTIALS_TEST_OBJS)
	$(Q) $(RM) $(GRPC_CREDENTIALS_TEST_DEPS)
	$(Q) $(RM) bins/grpc_credentials_test


GRPC_BASE64_TEST_SRC = \
    test/core/security/base64_test.c \

GRPC_BASE64_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GRPC_BASE64_TEST_SRC))))
GRPC_BASE64_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GRPC_BASE64_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/grpc_base64_test: openssl_dep_error

else

bins/grpc_base64_test: $(GRPC_BASE64_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_BASE64_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/grpc_base64_test

endif

deps_grpc_base64_test: $(GRPC_BASE64_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_BASE64_TEST_DEPS)
endif
endif

clean_grpc_base64_test:
	$(E) "[CLEAN]   Cleaning grpc_base64_test files"
	$(Q) $(RM) $(GRPC_BASE64_TEST_OBJS)
	$(Q) $(RM) $(GRPC_BASE64_TEST_DEPS)
	$(Q) $(RM) bins/grpc_base64_test


GRPC_JSON_TOKEN_TEST_SRC = \
    test/core/security/json_token_test.c \

GRPC_JSON_TOKEN_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(GRPC_JSON_TOKEN_TEST_SRC))))
GRPC_JSON_TOKEN_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(GRPC_JSON_TOKEN_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/grpc_json_token_test: openssl_dep_error

else

bins/grpc_json_token_test: $(GRPC_JSON_TOKEN_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(GRPC_JSON_TOKEN_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/grpc_json_token_test

endif

deps_grpc_json_token_test: $(GRPC_JSON_TOKEN_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(GRPC_JSON_TOKEN_TEST_DEPS)
endif
endif

clean_grpc_json_token_test:
	$(E) "[CLEAN]   Cleaning grpc_json_token_test files"
	$(Q) $(RM) $(GRPC_JSON_TOKEN_TEST_OBJS)
	$(Q) $(RM) $(GRPC_JSON_TOKEN_TEST_DEPS)
	$(Q) $(RM) bins/grpc_json_token_test


TIMEOUT_ENCODING_TEST_SRC = \
    test/core/transport/chttp2/timeout_encoding_test.c \

TIMEOUT_ENCODING_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(TIMEOUT_ENCODING_TEST_SRC))))
TIMEOUT_ENCODING_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(TIMEOUT_ENCODING_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/timeout_encoding_test: openssl_dep_error

else

bins/timeout_encoding_test: $(TIMEOUT_ENCODING_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TIMEOUT_ENCODING_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/timeout_encoding_test

endif

deps_timeout_encoding_test: $(TIMEOUT_ENCODING_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TIMEOUT_ENCODING_TEST_DEPS)
endif
endif

clean_timeout_encoding_test:
	$(E) "[CLEAN]   Cleaning timeout_encoding_test files"
	$(Q) $(RM) $(TIMEOUT_ENCODING_TEST_OBJS)
	$(Q) $(RM) $(TIMEOUT_ENCODING_TEST_DEPS)
	$(Q) $(RM) bins/timeout_encoding_test


FD_POSIX_TEST_SRC = \
    test/core/iomgr/fd_posix_test.c \

FD_POSIX_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(FD_POSIX_TEST_SRC))))
FD_POSIX_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(FD_POSIX_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/fd_posix_test: openssl_dep_error

else

bins/fd_posix_test: $(FD_POSIX_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(FD_POSIX_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/fd_posix_test

endif

deps_fd_posix_test: $(FD_POSIX_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FD_POSIX_TEST_DEPS)
endif
endif

clean_fd_posix_test:
	$(E) "[CLEAN]   Cleaning fd_posix_test files"
	$(Q) $(RM) $(FD_POSIX_TEST_OBJS)
	$(Q) $(RM) $(FD_POSIX_TEST_DEPS)
	$(Q) $(RM) bins/fd_posix_test


FLING_STREAM_TEST_SRC = \
    test/core/fling/fling_stream_test.c \

FLING_STREAM_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(FLING_STREAM_TEST_SRC))))
FLING_STREAM_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(FLING_STREAM_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/fling_stream_test: openssl_dep_error

else

bins/fling_stream_test: $(FLING_STREAM_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(FLING_STREAM_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/fling_stream_test

endif

deps_fling_stream_test: $(FLING_STREAM_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(FLING_STREAM_TEST_DEPS)
endif
endif

clean_fling_stream_test:
	$(E) "[CLEAN]   Cleaning fling_stream_test files"
	$(Q) $(RM) $(FLING_STREAM_TEST_OBJS)
	$(Q) $(RM) $(FLING_STREAM_TEST_DEPS)
	$(Q) $(RM) bins/fling_stream_test


LAME_CLIENT_TEST_SRC = \
    test/core/surface/lame_client_test.c \

LAME_CLIENT_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(LAME_CLIENT_TEST_SRC))))
LAME_CLIENT_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(LAME_CLIENT_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/lame_client_test: openssl_dep_error

else

bins/lame_client_test: $(LAME_CLIENT_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(LAME_CLIENT_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/lame_client_test

endif

deps_lame_client_test: $(LAME_CLIENT_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(LAME_CLIENT_TEST_DEPS)
endif
endif

clean_lame_client_test:
	$(E) "[CLEAN]   Cleaning lame_client_test files"
	$(Q) $(RM) $(LAME_CLIENT_TEST_OBJS)
	$(Q) $(RM) $(LAME_CLIENT_TEST_DEPS)
	$(Q) $(RM) bins/lame_client_test


THREAD_POOL_TEST_SRC = \
    test/cpp/server/thread_pool_test.cc \

THREAD_POOL_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(THREAD_POOL_TEST_SRC))))
THREAD_POOL_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(THREAD_POOL_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/thread_pool_test: openssl_dep_error

else

bins/thread_pool_test: $(THREAD_POOL_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc++.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(THREAD_POOL_TEST_OBJS) $(GTEST_LIB) -Llibs -lgrpc_test_util -lgrpc++ -lgrpc -lgpr $(LDLIBSXX) $(LDLIBS) $(LDLIBS_SECURE) -o bins/thread_pool_test

endif

deps_thread_pool_test: $(THREAD_POOL_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(THREAD_POOL_TEST_DEPS)
endif
endif

clean_thread_pool_test:
	$(E) "[CLEAN]   Cleaning thread_pool_test files"
	$(Q) $(RM) $(THREAD_POOL_TEST_OBJS)
	$(Q) $(RM) $(THREAD_POOL_TEST_DEPS)
	$(Q) $(RM) bins/thread_pool_test


STATUS_TEST_SRC = \
    test/cpp/util/status_test.cc \

STATUS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(STATUS_TEST_SRC))))
STATUS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(STATUS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/status_test: openssl_dep_error

else

bins/status_test: $(STATUS_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc++.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(STATUS_TEST_OBJS) $(GTEST_LIB) -Llibs -lgrpc_test_util -lgrpc++ -lgrpc -lgpr $(LDLIBSXX) $(LDLIBS) $(LDLIBS_SECURE) -o bins/status_test

endif

deps_status_test: $(STATUS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(STATUS_TEST_DEPS)
endif
endif

clean_status_test:
	$(E) "[CLEAN]   Cleaning status_test files"
	$(Q) $(RM) $(STATUS_TEST_OBJS)
	$(Q) $(RM) $(STATUS_TEST_DEPS)
	$(Q) $(RM) bins/status_test


SYNC_CLIENT_ASYNC_SERVER_TEST_SRC = \
    test/cpp/end2end/sync_client_async_server_test.cc \

SYNC_CLIENT_ASYNC_SERVER_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(SYNC_CLIENT_ASYNC_SERVER_TEST_SRC))))
SYNC_CLIENT_ASYNC_SERVER_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(SYNC_CLIENT_ASYNC_SERVER_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/sync_client_async_server_test: openssl_dep_error

else

bins/sync_client_async_server_test: $(SYNC_CLIENT_ASYNC_SERVER_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc++.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(SYNC_CLIENT_ASYNC_SERVER_TEST_OBJS) $(GTEST_LIB) -Llibs -lgrpc_test_util -lgrpc++ -lgrpc -lgpr $(LDLIBSXX) $(LDLIBS) $(LDLIBS_SECURE) -o bins/sync_client_async_server_test

endif

deps_sync_client_async_server_test: $(SYNC_CLIENT_ASYNC_SERVER_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(SYNC_CLIENT_ASYNC_SERVER_TEST_DEPS)
endif
endif

clean_sync_client_async_server_test:
	$(E) "[CLEAN]   Cleaning sync_client_async_server_test files"
	$(Q) $(RM) $(SYNC_CLIENT_ASYNC_SERVER_TEST_OBJS)
	$(Q) $(RM) $(SYNC_CLIENT_ASYNC_SERVER_TEST_DEPS)
	$(Q) $(RM) bins/sync_client_async_server_test


QPS_CLIENT_SRC = \
    test/cpp/qps/qps_client.cc \

QPS_CLIENT_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(QPS_CLIENT_SRC))))
QPS_CLIENT_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(QPS_CLIENT_SRC))))

ifeq ($(NO_SECURE),true)

bins/qps_client: openssl_dep_error

else

bins/qps_client: $(QPS_CLIENT_OBJS) libs/libgrpc_test_util.a libs/libgrpc++.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(QPS_CLIENT_OBJS) $(GTEST_LIB) -Llibs -lgrpc_test_util -lgrpc++ -lgrpc -lgpr $(LDLIBSXX) $(LDLIBS) $(LDLIBS_SECURE) -o bins/qps_client

endif

deps_qps_client: $(QPS_CLIENT_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(QPS_CLIENT_DEPS)
endif
endif

clean_qps_client:
	$(E) "[CLEAN]   Cleaning qps_client files"
	$(Q) $(RM) $(QPS_CLIENT_OBJS)
	$(Q) $(RM) $(QPS_CLIENT_DEPS)
	$(Q) $(RM) bins/qps_client


QPS_SERVER_SRC = \
    test/cpp/end2end/qps_server.cc \

QPS_SERVER_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(QPS_SERVER_SRC))))
QPS_SERVER_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(QPS_SERVER_SRC))))

ifeq ($(NO_SECURE),true)

bins/qps_server: openssl_dep_error

else

bins/qps_server: $(QPS_SERVER_OBJS) libs/libgrpc_test_util.a libs/libgrpc++.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(QPS_SERVER_OBJS) $(GTEST_LIB) -Llibs -lgrpc_test_util -lgrpc++ -lgrpc -lgpr $(LDLIBSXX) $(LDLIBS) $(LDLIBS_SECURE) -o bins/qps_server

endif

deps_qps_server: $(QPS_SERVER_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(QPS_SERVER_DEPS)
endif
endif

clean_qps_server:
	$(E) "[CLEAN]   Cleaning qps_server files"
	$(Q) $(RM) $(QPS_SERVER_OBJS)
	$(Q) $(RM) $(QPS_SERVER_DEPS)
	$(Q) $(RM) bins/qps_server


INTEROP_SERVER_SRC = \
    gens/test/cpp/interop/empty.pb.cc \
    gens/test/cpp/interop/messages.pb.cc \
    gens/test/cpp/interop/test.pb.cc \
    test/cpp/interop/server.cc \

INTEROP_SERVER_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(INTEROP_SERVER_SRC))))
INTEROP_SERVER_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(INTEROP_SERVER_SRC))))

ifeq ($(NO_SECURE),true)

bins/interop_server: openssl_dep_error

else

bins/interop_server: $(INTEROP_SERVER_OBJS) libs/libgrpc++_test_util.a libs/libgrpc_test_util.a libs/libgrpc++.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(INTEROP_SERVER_OBJS) $(GTEST_LIB) -Llibs -lgrpc++_test_util -lgrpc_test_util -lgrpc++ -lgrpc -lgpr $(LDLIBSXX) $(LDLIBS) $(LDLIBS_SECURE) -o bins/interop_server

endif

deps_interop_server: $(INTEROP_SERVER_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(INTEROP_SERVER_DEPS)
endif
endif

clean_interop_server:
	$(E) "[CLEAN]   Cleaning interop_server files"
	$(Q) $(RM) $(INTEROP_SERVER_OBJS)
	$(Q) $(RM) $(INTEROP_SERVER_DEPS)
	$(Q) $(RM) bins/interop_server


INTEROP_CLIENT_SRC = \
    gens/test/cpp/interop/empty.pb.cc \
    gens/test/cpp/interop/messages.pb.cc \
    gens/test/cpp/interop/test.pb.cc \
    test/cpp/interop/client.cc \

INTEROP_CLIENT_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(INTEROP_CLIENT_SRC))))
INTEROP_CLIENT_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(INTEROP_CLIENT_SRC))))

ifeq ($(NO_SECURE),true)

bins/interop_client: openssl_dep_error

else

bins/interop_client: $(INTEROP_CLIENT_OBJS) libs/libgrpc++_test_util.a libs/libgrpc_test_util.a libs/libgrpc++.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(INTEROP_CLIENT_OBJS) $(GTEST_LIB) -Llibs -lgrpc++_test_util -lgrpc_test_util -lgrpc++ -lgrpc -lgpr $(LDLIBSXX) $(LDLIBS) $(LDLIBS_SECURE) -o bins/interop_client

endif

deps_interop_client: $(INTEROP_CLIENT_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(INTEROP_CLIENT_DEPS)
endif
endif

clean_interop_client:
	$(E) "[CLEAN]   Cleaning interop_client files"
	$(Q) $(RM) $(INTEROP_CLIENT_OBJS)
	$(Q) $(RM) $(INTEROP_CLIENT_DEPS)
	$(Q) $(RM) bins/interop_client


END2END_TEST_SRC = \
    test/cpp/end2end/end2end_test.cc \

END2END_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(END2END_TEST_SRC))))
END2END_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(END2END_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/end2end_test: openssl_dep_error

else

bins/end2end_test: $(END2END_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc++.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LDXX) $(LDFLAGS) $(END2END_TEST_OBJS) $(GTEST_LIB) -Llibs -lgrpc_test_util -lgrpc++ -lgrpc -lgpr $(LDLIBSXX) $(LDLIBS) $(LDLIBS_SECURE) -o bins/end2end_test

endif

deps_end2end_test: $(END2END_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(END2END_TEST_DEPS)
endif
endif

clean_end2end_test:
	$(E) "[CLEAN]   Cleaning end2end_test files"
	$(Q) $(RM) $(END2END_TEST_OBJS)
	$(Q) $(RM) $(END2END_TEST_DEPS)
	$(Q) $(RM) bins/end2end_test


ALARM_TEST_SRC = \
    test/core/iomgr/alarm_test.c \

ALARM_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(ALARM_TEST_SRC))))
ALARM_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(ALARM_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/alarm_test: openssl_dep_error

else

bins/alarm_test: $(ALARM_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(ALARM_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/alarm_test

endif

deps_alarm_test: $(ALARM_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(ALARM_TEST_DEPS)
endif
endif

clean_alarm_test:
	$(E) "[CLEAN]   Cleaning alarm_test files"
	$(Q) $(RM) $(ALARM_TEST_OBJS)
	$(Q) $(RM) $(ALARM_TEST_DEPS)
	$(Q) $(RM) bins/alarm_test


TIME_TEST_SRC = \
    test/core/support/time_test.c \

TIME_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(TIME_TEST_SRC))))
TIME_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(TIME_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/time_test: openssl_dep_error

else

bins/time_test: $(TIME_TEST_OBJS) libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(TIME_TEST_OBJS) -Llibs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/time_test

endif

deps_time_test: $(TIME_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(TIME_TEST_DEPS)
endif
endif

clean_time_test:
	$(E) "[CLEAN]   Cleaning time_test files"
	$(Q) $(RM) $(TIME_TEST_OBJS)
	$(Q) $(RM) $(TIME_TEST_DEPS)
	$(Q) $(RM) bins/time_test


CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_TEST_SRC = \

CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_TEST_SRC))))
CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_cancel_after_accept_test: openssl_dep_error

else

bins/chttp2_fake_security_cancel_after_accept_test: $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_cancel_after_accept.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_cancel_after_accept -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_cancel_after_accept_test

endif

deps_chttp2_fake_security_cancel_after_accept_test: $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_cancel_after_accept_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_cancel_after_accept_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_cancel_after_accept_test


CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC = \

CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC))))
CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_cancel_after_accept_and_writes_closed_test: openssl_dep_error

else

bins/chttp2_fake_security_cancel_after_accept_and_writes_closed_test: $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_cancel_after_accept_and_writes_closed.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_cancel_after_accept_and_writes_closed -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_cancel_after_accept_and_writes_closed_test

endif

deps_chttp2_fake_security_cancel_after_accept_and_writes_closed_test: $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_cancel_after_accept_and_writes_closed_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_cancel_after_accept_and_writes_closed_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_cancel_after_accept_and_writes_closed_test


CHTTP2_FAKE_SECURITY_CANCEL_AFTER_INVOKE_TEST_SRC = \

CHTTP2_FAKE_SECURITY_CANCEL_AFTER_INVOKE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_INVOKE_TEST_SRC))))
CHTTP2_FAKE_SECURITY_CANCEL_AFTER_INVOKE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_INVOKE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_cancel_after_invoke_test: openssl_dep_error

else

bins/chttp2_fake_security_cancel_after_invoke_test: $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_INVOKE_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_cancel_after_invoke.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_INVOKE_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_cancel_after_invoke -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_cancel_after_invoke_test

endif

deps_chttp2_fake_security_cancel_after_invoke_test: $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_INVOKE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_INVOKE_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_cancel_after_invoke_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_cancel_after_invoke_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_INVOKE_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_CANCEL_AFTER_INVOKE_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_cancel_after_invoke_test


CHTTP2_FAKE_SECURITY_CANCEL_BEFORE_INVOKE_TEST_SRC = \

CHTTP2_FAKE_SECURITY_CANCEL_BEFORE_INVOKE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_CANCEL_BEFORE_INVOKE_TEST_SRC))))
CHTTP2_FAKE_SECURITY_CANCEL_BEFORE_INVOKE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_CANCEL_BEFORE_INVOKE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_cancel_before_invoke_test: openssl_dep_error

else

bins/chttp2_fake_security_cancel_before_invoke_test: $(CHTTP2_FAKE_SECURITY_CANCEL_BEFORE_INVOKE_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_cancel_before_invoke.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_CANCEL_BEFORE_INVOKE_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_cancel_before_invoke -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_cancel_before_invoke_test

endif

deps_chttp2_fake_security_cancel_before_invoke_test: $(CHTTP2_FAKE_SECURITY_CANCEL_BEFORE_INVOKE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_CANCEL_BEFORE_INVOKE_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_cancel_before_invoke_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_cancel_before_invoke_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_CANCEL_BEFORE_INVOKE_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_CANCEL_BEFORE_INVOKE_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_cancel_before_invoke_test


CHTTP2_FAKE_SECURITY_CANCEL_IN_A_VACUUM_TEST_SRC = \

CHTTP2_FAKE_SECURITY_CANCEL_IN_A_VACUUM_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_CANCEL_IN_A_VACUUM_TEST_SRC))))
CHTTP2_FAKE_SECURITY_CANCEL_IN_A_VACUUM_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_CANCEL_IN_A_VACUUM_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_cancel_in_a_vacuum_test: openssl_dep_error

else

bins/chttp2_fake_security_cancel_in_a_vacuum_test: $(CHTTP2_FAKE_SECURITY_CANCEL_IN_A_VACUUM_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_cancel_in_a_vacuum.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_CANCEL_IN_A_VACUUM_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_cancel_in_a_vacuum -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_cancel_in_a_vacuum_test

endif

deps_chttp2_fake_security_cancel_in_a_vacuum_test: $(CHTTP2_FAKE_SECURITY_CANCEL_IN_A_VACUUM_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_CANCEL_IN_A_VACUUM_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_cancel_in_a_vacuum_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_cancel_in_a_vacuum_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_CANCEL_IN_A_VACUUM_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_CANCEL_IN_A_VACUUM_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_cancel_in_a_vacuum_test


CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC = \

CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC))))
CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test: openssl_dep_error

else

bins/chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test: $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_early_server_shutdown_finishes_inflight_calls.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_early_server_shutdown_finishes_inflight_calls -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test

endif

deps_chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test: $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test


CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC = \

CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC))))
CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_early_server_shutdown_finishes_tags_test: openssl_dep_error

else

bins/chttp2_fake_security_early_server_shutdown_finishes_tags_test: $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_early_server_shutdown_finishes_tags.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_early_server_shutdown_finishes_tags -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_early_server_shutdown_finishes_tags_test

endif

deps_chttp2_fake_security_early_server_shutdown_finishes_tags_test: $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_early_server_shutdown_finishes_tags_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_early_server_shutdown_finishes_tags_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_early_server_shutdown_finishes_tags_test


CHTTP2_FAKE_SECURITY_INVOKE_LARGE_REQUEST_TEST_SRC = \

CHTTP2_FAKE_SECURITY_INVOKE_LARGE_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_INVOKE_LARGE_REQUEST_TEST_SRC))))
CHTTP2_FAKE_SECURITY_INVOKE_LARGE_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_INVOKE_LARGE_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_invoke_large_request_test: openssl_dep_error

else

bins/chttp2_fake_security_invoke_large_request_test: $(CHTTP2_FAKE_SECURITY_INVOKE_LARGE_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_invoke_large_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_INVOKE_LARGE_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_invoke_large_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_invoke_large_request_test

endif

deps_chttp2_fake_security_invoke_large_request_test: $(CHTTP2_FAKE_SECURITY_INVOKE_LARGE_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_INVOKE_LARGE_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_invoke_large_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_invoke_large_request_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_INVOKE_LARGE_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_INVOKE_LARGE_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_invoke_large_request_test


CHTTP2_FAKE_SECURITY_MAX_CONCURRENT_STREAMS_TEST_SRC = \

CHTTP2_FAKE_SECURITY_MAX_CONCURRENT_STREAMS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_MAX_CONCURRENT_STREAMS_TEST_SRC))))
CHTTP2_FAKE_SECURITY_MAX_CONCURRENT_STREAMS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_MAX_CONCURRENT_STREAMS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_max_concurrent_streams_test: openssl_dep_error

else

bins/chttp2_fake_security_max_concurrent_streams_test: $(CHTTP2_FAKE_SECURITY_MAX_CONCURRENT_STREAMS_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_max_concurrent_streams.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_MAX_CONCURRENT_STREAMS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_max_concurrent_streams -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_max_concurrent_streams_test

endif

deps_chttp2_fake_security_max_concurrent_streams_test: $(CHTTP2_FAKE_SECURITY_MAX_CONCURRENT_STREAMS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_MAX_CONCURRENT_STREAMS_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_max_concurrent_streams_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_max_concurrent_streams_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_MAX_CONCURRENT_STREAMS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_MAX_CONCURRENT_STREAMS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_max_concurrent_streams_test


CHTTP2_FAKE_SECURITY_NO_OP_TEST_SRC = \

CHTTP2_FAKE_SECURITY_NO_OP_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_NO_OP_TEST_SRC))))
CHTTP2_FAKE_SECURITY_NO_OP_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_NO_OP_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_no_op_test: openssl_dep_error

else

bins/chttp2_fake_security_no_op_test: $(CHTTP2_FAKE_SECURITY_NO_OP_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_no_op.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_NO_OP_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_no_op -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_no_op_test

endif

deps_chttp2_fake_security_no_op_test: $(CHTTP2_FAKE_SECURITY_NO_OP_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_NO_OP_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_no_op_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_no_op_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_NO_OP_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_NO_OP_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_no_op_test


CHTTP2_FAKE_SECURITY_PING_PONG_STREAMING_TEST_SRC = \

CHTTP2_FAKE_SECURITY_PING_PONG_STREAMING_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_PING_PONG_STREAMING_TEST_SRC))))
CHTTP2_FAKE_SECURITY_PING_PONG_STREAMING_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_PING_PONG_STREAMING_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_ping_pong_streaming_test: openssl_dep_error

else

bins/chttp2_fake_security_ping_pong_streaming_test: $(CHTTP2_FAKE_SECURITY_PING_PONG_STREAMING_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_ping_pong_streaming.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_PING_PONG_STREAMING_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_ping_pong_streaming -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_ping_pong_streaming_test

endif

deps_chttp2_fake_security_ping_pong_streaming_test: $(CHTTP2_FAKE_SECURITY_PING_PONG_STREAMING_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_PING_PONG_STREAMING_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_ping_pong_streaming_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_ping_pong_streaming_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_PING_PONG_STREAMING_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_PING_PONG_STREAMING_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_ping_pong_streaming_test


CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC = \

CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC))))
CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_request_response_with_binary_metadata_and_payload_test: openssl_dep_error

else

bins/chttp2_fake_security_request_response_with_binary_metadata_and_payload_test: $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_request_response_with_binary_metadata_and_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_request_response_with_binary_metadata_and_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_request_response_with_binary_metadata_and_payload_test

endif

deps_chttp2_fake_security_request_response_with_binary_metadata_and_payload_test: $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_request_response_with_binary_metadata_and_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_request_response_with_binary_metadata_and_payload_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_request_response_with_binary_metadata_and_payload_test


CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC = \

CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC))))
CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_request_response_with_metadata_and_payload_test: openssl_dep_error

else

bins/chttp2_fake_security_request_response_with_metadata_and_payload_test: $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_request_response_with_metadata_and_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_request_response_with_metadata_and_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_request_response_with_metadata_and_payload_test

endif

deps_chttp2_fake_security_request_response_with_metadata_and_payload_test: $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_request_response_with_metadata_and_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_request_response_with_metadata_and_payload_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_request_response_with_metadata_and_payload_test


CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC = \

CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC))))
CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_request_response_with_payload_test: openssl_dep_error

else

bins/chttp2_fake_security_request_response_with_payload_test: $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_request_response_with_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_request_response_with_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_request_response_with_payload_test

endif

deps_chttp2_fake_security_request_response_with_payload_test: $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_request_response_with_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_request_response_with_payload_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_request_response_with_payload_test


CHTTP2_FAKE_SECURITY_SIMPLE_DELAYED_REQUEST_TEST_SRC = \

CHTTP2_FAKE_SECURITY_SIMPLE_DELAYED_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_SIMPLE_DELAYED_REQUEST_TEST_SRC))))
CHTTP2_FAKE_SECURITY_SIMPLE_DELAYED_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_SIMPLE_DELAYED_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_simple_delayed_request_test: openssl_dep_error

else

bins/chttp2_fake_security_simple_delayed_request_test: $(CHTTP2_FAKE_SECURITY_SIMPLE_DELAYED_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_simple_delayed_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_SIMPLE_DELAYED_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_simple_delayed_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_simple_delayed_request_test

endif

deps_chttp2_fake_security_simple_delayed_request_test: $(CHTTP2_FAKE_SECURITY_SIMPLE_DELAYED_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_SIMPLE_DELAYED_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_simple_delayed_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_simple_delayed_request_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_SIMPLE_DELAYED_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_SIMPLE_DELAYED_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_simple_delayed_request_test


CHTTP2_FAKE_SECURITY_SIMPLE_REQUEST_TEST_SRC = \

CHTTP2_FAKE_SECURITY_SIMPLE_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_SIMPLE_REQUEST_TEST_SRC))))
CHTTP2_FAKE_SECURITY_SIMPLE_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_SIMPLE_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_simple_request_test: openssl_dep_error

else

bins/chttp2_fake_security_simple_request_test: $(CHTTP2_FAKE_SECURITY_SIMPLE_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_simple_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_SIMPLE_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_simple_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_simple_request_test

endif

deps_chttp2_fake_security_simple_request_test: $(CHTTP2_FAKE_SECURITY_SIMPLE_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_SIMPLE_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_simple_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_simple_request_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_SIMPLE_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_SIMPLE_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_simple_request_test


CHTTP2_FAKE_SECURITY_THREAD_STRESS_TEST_SRC = \

CHTTP2_FAKE_SECURITY_THREAD_STRESS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_THREAD_STRESS_TEST_SRC))))
CHTTP2_FAKE_SECURITY_THREAD_STRESS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_THREAD_STRESS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_thread_stress_test: openssl_dep_error

else

bins/chttp2_fake_security_thread_stress_test: $(CHTTP2_FAKE_SECURITY_THREAD_STRESS_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_thread_stress.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_THREAD_STRESS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_thread_stress -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_thread_stress_test

endif

deps_chttp2_fake_security_thread_stress_test: $(CHTTP2_FAKE_SECURITY_THREAD_STRESS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_THREAD_STRESS_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_thread_stress_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_thread_stress_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_THREAD_STRESS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_THREAD_STRESS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_thread_stress_test


CHTTP2_FAKE_SECURITY_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC = \

CHTTP2_FAKE_SECURITY_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FAKE_SECURITY_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC))))
CHTTP2_FAKE_SECURITY_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FAKE_SECURITY_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fake_security_writes_done_hangs_with_pending_read_test: openssl_dep_error

else

bins/chttp2_fake_security_writes_done_hangs_with_pending_read_test: $(CHTTP2_FAKE_SECURITY_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS) libs/libend2end_fixture_chttp2_fake_security.a libs/libend2end_test_writes_done_hangs_with_pending_read.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FAKE_SECURITY_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fake_security -lend2end_test_writes_done_hangs_with_pending_read -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fake_security_writes_done_hangs_with_pending_read_test

endif

deps_chttp2_fake_security_writes_done_hangs_with_pending_read_test: $(CHTTP2_FAKE_SECURITY_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FAKE_SECURITY_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)
endif
endif

clean_chttp2_fake_security_writes_done_hangs_with_pending_read_test:
	$(E) "[CLEAN]   Cleaning chttp2_fake_security_writes_done_hangs_with_pending_read_test files"
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FAKE_SECURITY_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fake_security_writes_done_hangs_with_pending_read_test


CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_SRC = \

CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_SRC))))
CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_cancel_after_accept_test: openssl_dep_error

else

bins/chttp2_fullstack_cancel_after_accept_test: $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_cancel_after_accept.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_cancel_after_accept -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_cancel_after_accept_test

endif

deps_chttp2_fullstack_cancel_after_accept_test: $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_cancel_after_accept_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_cancel_after_accept_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_cancel_after_accept_test


CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC = \

CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC))))
CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_cancel_after_accept_and_writes_closed_test: openssl_dep_error

else

bins/chttp2_fullstack_cancel_after_accept_and_writes_closed_test: $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_cancel_after_accept_and_writes_closed.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_cancel_after_accept_and_writes_closed -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_cancel_after_accept_and_writes_closed_test

endif

deps_chttp2_fullstack_cancel_after_accept_and_writes_closed_test: $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_cancel_after_accept_and_writes_closed_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_cancel_after_accept_and_writes_closed_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_cancel_after_accept_and_writes_closed_test


CHTTP2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_SRC = \

CHTTP2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_SRC))))
CHTTP2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_cancel_after_invoke_test: openssl_dep_error

else

bins/chttp2_fullstack_cancel_after_invoke_test: $(CHTTP2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_cancel_after_invoke.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_cancel_after_invoke -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_cancel_after_invoke_test

endif

deps_chttp2_fullstack_cancel_after_invoke_test: $(CHTTP2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_cancel_after_invoke_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_cancel_after_invoke_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_cancel_after_invoke_test


CHTTP2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_SRC = \

CHTTP2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_SRC))))
CHTTP2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_cancel_before_invoke_test: openssl_dep_error

else

bins/chttp2_fullstack_cancel_before_invoke_test: $(CHTTP2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_cancel_before_invoke.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_cancel_before_invoke -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_cancel_before_invoke_test

endif

deps_chttp2_fullstack_cancel_before_invoke_test: $(CHTTP2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_cancel_before_invoke_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_cancel_before_invoke_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_cancel_before_invoke_test


CHTTP2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_SRC = \

CHTTP2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_SRC))))
CHTTP2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_cancel_in_a_vacuum_test: openssl_dep_error

else

bins/chttp2_fullstack_cancel_in_a_vacuum_test: $(CHTTP2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_cancel_in_a_vacuum.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_cancel_in_a_vacuum -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_cancel_in_a_vacuum_test

endif

deps_chttp2_fullstack_cancel_in_a_vacuum_test: $(CHTTP2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_cancel_in_a_vacuum_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_cancel_in_a_vacuum_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_cancel_in_a_vacuum_test


CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC = \

CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC))))
CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test: openssl_dep_error

else

bins/chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test: $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_early_server_shutdown_finishes_inflight_calls.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_early_server_shutdown_finishes_inflight_calls -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test

endif

deps_chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test: $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test


CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC = \

CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC))))
CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_early_server_shutdown_finishes_tags_test: openssl_dep_error

else

bins/chttp2_fullstack_early_server_shutdown_finishes_tags_test: $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_early_server_shutdown_finishes_tags.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_early_server_shutdown_finishes_tags -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_early_server_shutdown_finishes_tags_test

endif

deps_chttp2_fullstack_early_server_shutdown_finishes_tags_test: $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_early_server_shutdown_finishes_tags_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_early_server_shutdown_finishes_tags_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_early_server_shutdown_finishes_tags_test


CHTTP2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_SRC = \

CHTTP2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_SRC))))
CHTTP2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_invoke_large_request_test: openssl_dep_error

else

bins/chttp2_fullstack_invoke_large_request_test: $(CHTTP2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_invoke_large_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_invoke_large_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_invoke_large_request_test

endif

deps_chttp2_fullstack_invoke_large_request_test: $(CHTTP2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_invoke_large_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_invoke_large_request_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_invoke_large_request_test


CHTTP2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_SRC = \

CHTTP2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_SRC))))
CHTTP2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_max_concurrent_streams_test: openssl_dep_error

else

bins/chttp2_fullstack_max_concurrent_streams_test: $(CHTTP2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_max_concurrent_streams.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_max_concurrent_streams -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_max_concurrent_streams_test

endif

deps_chttp2_fullstack_max_concurrent_streams_test: $(CHTTP2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_max_concurrent_streams_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_max_concurrent_streams_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_max_concurrent_streams_test


CHTTP2_FULLSTACK_NO_OP_TEST_SRC = \

CHTTP2_FULLSTACK_NO_OP_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_NO_OP_TEST_SRC))))
CHTTP2_FULLSTACK_NO_OP_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_NO_OP_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_no_op_test: openssl_dep_error

else

bins/chttp2_fullstack_no_op_test: $(CHTTP2_FULLSTACK_NO_OP_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_no_op.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_NO_OP_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_no_op -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_no_op_test

endif

deps_chttp2_fullstack_no_op_test: $(CHTTP2_FULLSTACK_NO_OP_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_NO_OP_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_no_op_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_no_op_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_NO_OP_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_NO_OP_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_no_op_test


CHTTP2_FULLSTACK_PING_PONG_STREAMING_TEST_SRC = \

CHTTP2_FULLSTACK_PING_PONG_STREAMING_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_PING_PONG_STREAMING_TEST_SRC))))
CHTTP2_FULLSTACK_PING_PONG_STREAMING_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_PING_PONG_STREAMING_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_ping_pong_streaming_test: openssl_dep_error

else

bins/chttp2_fullstack_ping_pong_streaming_test: $(CHTTP2_FULLSTACK_PING_PONG_STREAMING_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_ping_pong_streaming.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_PING_PONG_STREAMING_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_ping_pong_streaming -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_ping_pong_streaming_test

endif

deps_chttp2_fullstack_ping_pong_streaming_test: $(CHTTP2_FULLSTACK_PING_PONG_STREAMING_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_PING_PONG_STREAMING_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_ping_pong_streaming_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_ping_pong_streaming_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_PING_PONG_STREAMING_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_PING_PONG_STREAMING_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_ping_pong_streaming_test


CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC = \

CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC))))
CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_request_response_with_binary_metadata_and_payload_test: openssl_dep_error

else

bins/chttp2_fullstack_request_response_with_binary_metadata_and_payload_test: $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_request_response_with_binary_metadata_and_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_request_response_with_binary_metadata_and_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_request_response_with_binary_metadata_and_payload_test

endif

deps_chttp2_fullstack_request_response_with_binary_metadata_and_payload_test: $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_request_response_with_binary_metadata_and_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_request_response_with_binary_metadata_and_payload_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_request_response_with_binary_metadata_and_payload_test


CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC = \

CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC))))
CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_request_response_with_metadata_and_payload_test: openssl_dep_error

else

bins/chttp2_fullstack_request_response_with_metadata_and_payload_test: $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_request_response_with_metadata_and_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_request_response_with_metadata_and_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_request_response_with_metadata_and_payload_test

endif

deps_chttp2_fullstack_request_response_with_metadata_and_payload_test: $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_request_response_with_metadata_and_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_request_response_with_metadata_and_payload_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_request_response_with_metadata_and_payload_test


CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC = \

CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC))))
CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_request_response_with_payload_test: openssl_dep_error

else

bins/chttp2_fullstack_request_response_with_payload_test: $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_request_response_with_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_request_response_with_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_request_response_with_payload_test

endif

deps_chttp2_fullstack_request_response_with_payload_test: $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_request_response_with_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_request_response_with_payload_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_request_response_with_payload_test


CHTTP2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_SRC = \

CHTTP2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_SRC))))
CHTTP2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_simple_delayed_request_test: openssl_dep_error

else

bins/chttp2_fullstack_simple_delayed_request_test: $(CHTTP2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_simple_delayed_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_simple_delayed_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_simple_delayed_request_test

endif

deps_chttp2_fullstack_simple_delayed_request_test: $(CHTTP2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_simple_delayed_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_simple_delayed_request_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_simple_delayed_request_test


CHTTP2_FULLSTACK_SIMPLE_REQUEST_TEST_SRC = \

CHTTP2_FULLSTACK_SIMPLE_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_SIMPLE_REQUEST_TEST_SRC))))
CHTTP2_FULLSTACK_SIMPLE_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_SIMPLE_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_simple_request_test: openssl_dep_error

else

bins/chttp2_fullstack_simple_request_test: $(CHTTP2_FULLSTACK_SIMPLE_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_simple_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_SIMPLE_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_simple_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_simple_request_test

endif

deps_chttp2_fullstack_simple_request_test: $(CHTTP2_FULLSTACK_SIMPLE_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_SIMPLE_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_simple_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_simple_request_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_SIMPLE_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_SIMPLE_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_simple_request_test


CHTTP2_FULLSTACK_THREAD_STRESS_TEST_SRC = \

CHTTP2_FULLSTACK_THREAD_STRESS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_THREAD_STRESS_TEST_SRC))))
CHTTP2_FULLSTACK_THREAD_STRESS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_THREAD_STRESS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_thread_stress_test: openssl_dep_error

else

bins/chttp2_fullstack_thread_stress_test: $(CHTTP2_FULLSTACK_THREAD_STRESS_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_thread_stress.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_THREAD_STRESS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_thread_stress -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_thread_stress_test

endif

deps_chttp2_fullstack_thread_stress_test: $(CHTTP2_FULLSTACK_THREAD_STRESS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_THREAD_STRESS_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_thread_stress_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_thread_stress_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_THREAD_STRESS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_THREAD_STRESS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_thread_stress_test


CHTTP2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC = \

CHTTP2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC))))
CHTTP2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_fullstack_writes_done_hangs_with_pending_read_test: openssl_dep_error

else

bins/chttp2_fullstack_writes_done_hangs_with_pending_read_test: $(CHTTP2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS) libs/libend2end_fixture_chttp2_fullstack.a libs/libend2end_test_writes_done_hangs_with_pending_read.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_fullstack -lend2end_test_writes_done_hangs_with_pending_read -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_fullstack_writes_done_hangs_with_pending_read_test

endif

deps_chttp2_fullstack_writes_done_hangs_with_pending_read_test: $(CHTTP2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)
endif
endif

clean_chttp2_fullstack_writes_done_hangs_with_pending_read_test:
	$(E) "[CLEAN]   Cleaning chttp2_fullstack_writes_done_hangs_with_pending_read_test files"
	$(Q) $(RM) $(CHTTP2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_fullstack_writes_done_hangs_with_pending_read_test


CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_cancel_after_accept_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_cancel_after_accept_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_cancel_after_accept.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_cancel_after_accept -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_cancel_after_accept_test

endif

deps_chttp2_simple_ssl_fullstack_cancel_after_accept_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_cancel_after_accept_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_cancel_after_accept_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_cancel_after_accept_test


CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_cancel_after_accept_and_writes_closed.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_cancel_after_accept_and_writes_closed -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test

endif

deps_chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test


CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_cancel_after_invoke_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_cancel_after_invoke_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_cancel_after_invoke.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_cancel_after_invoke -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_cancel_after_invoke_test

endif

deps_chttp2_simple_ssl_fullstack_cancel_after_invoke_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_cancel_after_invoke_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_cancel_after_invoke_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_cancel_after_invoke_test


CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_cancel_before_invoke_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_cancel_before_invoke_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_cancel_before_invoke.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_cancel_before_invoke -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_cancel_before_invoke_test

endif

deps_chttp2_simple_ssl_fullstack_cancel_before_invoke_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_cancel_before_invoke_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_cancel_before_invoke_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_cancel_before_invoke_test


CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_cancel_in_a_vacuum.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_cancel_in_a_vacuum -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test

endif

deps_chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test


CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_early_server_shutdown_finishes_inflight_calls.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_early_server_shutdown_finishes_inflight_calls -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test

endif

deps_chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test


CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_early_server_shutdown_finishes_tags.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_early_server_shutdown_finishes_tags -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test

endif

deps_chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test


CHTTP2_SIMPLE_SSL_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_invoke_large_request_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_invoke_large_request_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_invoke_large_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_invoke_large_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_invoke_large_request_test

endif

deps_chttp2_simple_ssl_fullstack_invoke_large_request_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_invoke_large_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_invoke_large_request_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_invoke_large_request_test


CHTTP2_SIMPLE_SSL_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_max_concurrent_streams_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_max_concurrent_streams_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_max_concurrent_streams.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_max_concurrent_streams -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_max_concurrent_streams_test

endif

deps_chttp2_simple_ssl_fullstack_max_concurrent_streams_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_max_concurrent_streams_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_max_concurrent_streams_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_max_concurrent_streams_test


CHTTP2_SIMPLE_SSL_FULLSTACK_NO_OP_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_NO_OP_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_NO_OP_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_NO_OP_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_NO_OP_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_no_op_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_no_op_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_NO_OP_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_no_op.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_NO_OP_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_no_op -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_no_op_test

endif

deps_chttp2_simple_ssl_fullstack_no_op_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_NO_OP_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_NO_OP_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_no_op_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_no_op_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_NO_OP_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_NO_OP_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_no_op_test


CHTTP2_SIMPLE_SSL_FULLSTACK_PING_PONG_STREAMING_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_PING_PONG_STREAMING_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_PING_PONG_STREAMING_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_PING_PONG_STREAMING_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_PING_PONG_STREAMING_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_ping_pong_streaming_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_ping_pong_streaming_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_PING_PONG_STREAMING_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_ping_pong_streaming.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_PING_PONG_STREAMING_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_ping_pong_streaming -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_ping_pong_streaming_test

endif

deps_chttp2_simple_ssl_fullstack_ping_pong_streaming_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_PING_PONG_STREAMING_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_PING_PONG_STREAMING_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_ping_pong_streaming_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_ping_pong_streaming_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_PING_PONG_STREAMING_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_PING_PONG_STREAMING_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_ping_pong_streaming_test


CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_request_response_with_binary_metadata_and_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_request_response_with_binary_metadata_and_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test

endif

deps_chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test


CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_request_response_with_metadata_and_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_request_response_with_metadata_and_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test

endif

deps_chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test


CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_request_response_with_payload_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_request_response_with_payload_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_request_response_with_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_request_response_with_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_request_response_with_payload_test

endif

deps_chttp2_simple_ssl_fullstack_request_response_with_payload_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_request_response_with_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_request_response_with_payload_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_request_response_with_payload_test


CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_simple_delayed_request_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_simple_delayed_request_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_simple_delayed_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_simple_delayed_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_simple_delayed_request_test

endif

deps_chttp2_simple_ssl_fullstack_simple_delayed_request_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_simple_delayed_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_simple_delayed_request_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_simple_delayed_request_test


CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_REQUEST_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_REQUEST_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_simple_request_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_simple_request_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_simple_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_simple_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_simple_request_test

endif

deps_chttp2_simple_ssl_fullstack_simple_request_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_simple_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_simple_request_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_SIMPLE_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_simple_request_test


CHTTP2_SIMPLE_SSL_FULLSTACK_THREAD_STRESS_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_THREAD_STRESS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_THREAD_STRESS_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_THREAD_STRESS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_THREAD_STRESS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_thread_stress_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_thread_stress_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_THREAD_STRESS_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_thread_stress.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_THREAD_STRESS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_thread_stress -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_thread_stress_test

endif

deps_chttp2_simple_ssl_fullstack_thread_stress_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_THREAD_STRESS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_THREAD_STRESS_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_thread_stress_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_thread_stress_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_THREAD_STRESS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_THREAD_STRESS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_thread_stress_test


CHTTP2_SIMPLE_SSL_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC = \

CHTTP2_SIMPLE_SSL_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC))))
CHTTP2_SIMPLE_SSL_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test: openssl_dep_error

else

bins/chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_fullstack.a libs/libend2end_test_writes_done_hangs_with_pending_read.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_fullstack -lend2end_test_writes_done_hangs_with_pending_read -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test

endif

deps_chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test: $(CHTTP2_SIMPLE_SSL_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_cancel_after_accept.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_cancel_after_accept -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_cancel_after_accept_and_writes_closed.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_cancel_after_accept_and_writes_closed -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_cancel_after_invoke.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_cancel_after_invoke -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_AFTER_INVOKE_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_cancel_before_invoke.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_cancel_before_invoke -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_BEFORE_INVOKE_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_cancel_in_a_vacuum.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_cancel_in_a_vacuum -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_CANCEL_IN_A_VACUUM_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_early_server_shutdown_finishes_inflight_calls.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_early_server_shutdown_finishes_inflight_calls -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_early_server_shutdown_finishes_tags.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_early_server_shutdown_finishes_tags -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_invoke_large_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_invoke_large_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_INVOKE_LARGE_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_max_concurrent_streams.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_max_concurrent_streams -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_MAX_CONCURRENT_STREAMS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_NO_OP_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_NO_OP_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_NO_OP_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_NO_OP_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_NO_OP_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_no_op_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_no_op_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_NO_OP_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_no_op.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_NO_OP_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_no_op -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_no_op_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_no_op_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_NO_OP_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_NO_OP_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_no_op_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_no_op_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_NO_OP_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_NO_OP_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_no_op_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_PING_PONG_STREAMING_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_PING_PONG_STREAMING_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_PING_PONG_STREAMING_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_PING_PONG_STREAMING_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_PING_PONG_STREAMING_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_PING_PONG_STREAMING_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_ping_pong_streaming.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_PING_PONG_STREAMING_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_ping_pong_streaming -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_PING_PONG_STREAMING_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_PING_PONG_STREAMING_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_PING_PONG_STREAMING_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_PING_PONG_STREAMING_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_request_response_with_binary_metadata_and_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_request_response_with_binary_metadata_and_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_request_response_with_metadata_and_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_request_response_with_metadata_and_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_request_response_with_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_request_response_with_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_simple_delayed_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_simple_delayed_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_DELAYED_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_REQUEST_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_REQUEST_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_simple_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_simple_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_SIMPLE_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_THREAD_STRESS_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_THREAD_STRESS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_THREAD_STRESS_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_THREAD_STRESS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_THREAD_STRESS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_THREAD_STRESS_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_thread_stress.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_THREAD_STRESS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_thread_stress -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_THREAD_STRESS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_THREAD_STRESS_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_THREAD_STRESS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_THREAD_STRESS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test


CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC = \

CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC))))
CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test: openssl_dep_error

else

bins/chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS) libs/libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack.a libs/libend2end_test_writes_done_hangs_with_pending_read.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack -lend2end_test_writes_done_hangs_with_pending_read -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test

endif

deps_chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test: $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)
endif
endif

clean_chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test:
	$(E) "[CLEAN]   Cleaning chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test files"
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test


CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_TEST_SRC = \

CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_TEST_SRC))))
CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_cancel_after_accept_test: openssl_dep_error

else

bins/chttp2_socket_pair_cancel_after_accept_test: $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_cancel_after_accept.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_cancel_after_accept -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_cancel_after_accept_test

endif

deps_chttp2_socket_pair_cancel_after_accept_test: $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_cancel_after_accept_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_cancel_after_accept_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_cancel_after_accept_test


CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC = \

CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC))))
CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_cancel_after_accept_and_writes_closed_test: openssl_dep_error

else

bins/chttp2_socket_pair_cancel_after_accept_and_writes_closed_test: $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_cancel_after_accept_and_writes_closed.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_cancel_after_accept_and_writes_closed -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_cancel_after_accept_and_writes_closed_test

endif

deps_chttp2_socket_pair_cancel_after_accept_and_writes_closed_test: $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_cancel_after_accept_and_writes_closed_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_cancel_after_accept_and_writes_closed_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_cancel_after_accept_and_writes_closed_test


CHTTP2_SOCKET_PAIR_CANCEL_AFTER_INVOKE_TEST_SRC = \

CHTTP2_SOCKET_PAIR_CANCEL_AFTER_INVOKE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_INVOKE_TEST_SRC))))
CHTTP2_SOCKET_PAIR_CANCEL_AFTER_INVOKE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_INVOKE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_cancel_after_invoke_test: openssl_dep_error

else

bins/chttp2_socket_pair_cancel_after_invoke_test: $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_INVOKE_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_cancel_after_invoke.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_INVOKE_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_cancel_after_invoke -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_cancel_after_invoke_test

endif

deps_chttp2_socket_pair_cancel_after_invoke_test: $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_INVOKE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_INVOKE_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_cancel_after_invoke_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_cancel_after_invoke_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_INVOKE_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_CANCEL_AFTER_INVOKE_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_cancel_after_invoke_test


CHTTP2_SOCKET_PAIR_CANCEL_BEFORE_INVOKE_TEST_SRC = \

CHTTP2_SOCKET_PAIR_CANCEL_BEFORE_INVOKE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_CANCEL_BEFORE_INVOKE_TEST_SRC))))
CHTTP2_SOCKET_PAIR_CANCEL_BEFORE_INVOKE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_CANCEL_BEFORE_INVOKE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_cancel_before_invoke_test: openssl_dep_error

else

bins/chttp2_socket_pair_cancel_before_invoke_test: $(CHTTP2_SOCKET_PAIR_CANCEL_BEFORE_INVOKE_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_cancel_before_invoke.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_CANCEL_BEFORE_INVOKE_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_cancel_before_invoke -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_cancel_before_invoke_test

endif

deps_chttp2_socket_pair_cancel_before_invoke_test: $(CHTTP2_SOCKET_PAIR_CANCEL_BEFORE_INVOKE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_CANCEL_BEFORE_INVOKE_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_cancel_before_invoke_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_cancel_before_invoke_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_CANCEL_BEFORE_INVOKE_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_CANCEL_BEFORE_INVOKE_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_cancel_before_invoke_test


CHTTP2_SOCKET_PAIR_CANCEL_IN_A_VACUUM_TEST_SRC = \

CHTTP2_SOCKET_PAIR_CANCEL_IN_A_VACUUM_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_CANCEL_IN_A_VACUUM_TEST_SRC))))
CHTTP2_SOCKET_PAIR_CANCEL_IN_A_VACUUM_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_CANCEL_IN_A_VACUUM_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_cancel_in_a_vacuum_test: openssl_dep_error

else

bins/chttp2_socket_pair_cancel_in_a_vacuum_test: $(CHTTP2_SOCKET_PAIR_CANCEL_IN_A_VACUUM_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_cancel_in_a_vacuum.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_CANCEL_IN_A_VACUUM_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_cancel_in_a_vacuum -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_cancel_in_a_vacuum_test

endif

deps_chttp2_socket_pair_cancel_in_a_vacuum_test: $(CHTTP2_SOCKET_PAIR_CANCEL_IN_A_VACUUM_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_CANCEL_IN_A_VACUUM_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_cancel_in_a_vacuum_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_cancel_in_a_vacuum_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_CANCEL_IN_A_VACUUM_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_CANCEL_IN_A_VACUUM_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_cancel_in_a_vacuum_test


CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC = \

CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC))))
CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test: openssl_dep_error

else

bins/chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test: $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_early_server_shutdown_finishes_inflight_calls.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_early_server_shutdown_finishes_inflight_calls -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test

endif

deps_chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test: $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test


CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC = \

CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC))))
CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_early_server_shutdown_finishes_tags_test: openssl_dep_error

else

bins/chttp2_socket_pair_early_server_shutdown_finishes_tags_test: $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_early_server_shutdown_finishes_tags.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_early_server_shutdown_finishes_tags -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_early_server_shutdown_finishes_tags_test

endif

deps_chttp2_socket_pair_early_server_shutdown_finishes_tags_test: $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_early_server_shutdown_finishes_tags_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_early_server_shutdown_finishes_tags_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_early_server_shutdown_finishes_tags_test


CHTTP2_SOCKET_PAIR_INVOKE_LARGE_REQUEST_TEST_SRC = \

CHTTP2_SOCKET_PAIR_INVOKE_LARGE_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_INVOKE_LARGE_REQUEST_TEST_SRC))))
CHTTP2_SOCKET_PAIR_INVOKE_LARGE_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_INVOKE_LARGE_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_invoke_large_request_test: openssl_dep_error

else

bins/chttp2_socket_pair_invoke_large_request_test: $(CHTTP2_SOCKET_PAIR_INVOKE_LARGE_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_invoke_large_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_INVOKE_LARGE_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_invoke_large_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_invoke_large_request_test

endif

deps_chttp2_socket_pair_invoke_large_request_test: $(CHTTP2_SOCKET_PAIR_INVOKE_LARGE_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_INVOKE_LARGE_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_invoke_large_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_invoke_large_request_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_INVOKE_LARGE_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_INVOKE_LARGE_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_invoke_large_request_test


CHTTP2_SOCKET_PAIR_MAX_CONCURRENT_STREAMS_TEST_SRC = \

CHTTP2_SOCKET_PAIR_MAX_CONCURRENT_STREAMS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_MAX_CONCURRENT_STREAMS_TEST_SRC))))
CHTTP2_SOCKET_PAIR_MAX_CONCURRENT_STREAMS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_MAX_CONCURRENT_STREAMS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_max_concurrent_streams_test: openssl_dep_error

else

bins/chttp2_socket_pair_max_concurrent_streams_test: $(CHTTP2_SOCKET_PAIR_MAX_CONCURRENT_STREAMS_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_max_concurrent_streams.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_MAX_CONCURRENT_STREAMS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_max_concurrent_streams -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_max_concurrent_streams_test

endif

deps_chttp2_socket_pair_max_concurrent_streams_test: $(CHTTP2_SOCKET_PAIR_MAX_CONCURRENT_STREAMS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_MAX_CONCURRENT_STREAMS_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_max_concurrent_streams_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_max_concurrent_streams_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_MAX_CONCURRENT_STREAMS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_MAX_CONCURRENT_STREAMS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_max_concurrent_streams_test


CHTTP2_SOCKET_PAIR_NO_OP_TEST_SRC = \

CHTTP2_SOCKET_PAIR_NO_OP_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_NO_OP_TEST_SRC))))
CHTTP2_SOCKET_PAIR_NO_OP_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_NO_OP_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_no_op_test: openssl_dep_error

else

bins/chttp2_socket_pair_no_op_test: $(CHTTP2_SOCKET_PAIR_NO_OP_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_no_op.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_NO_OP_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_no_op -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_no_op_test

endif

deps_chttp2_socket_pair_no_op_test: $(CHTTP2_SOCKET_PAIR_NO_OP_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_NO_OP_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_no_op_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_no_op_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_NO_OP_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_NO_OP_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_no_op_test


CHTTP2_SOCKET_PAIR_PING_PONG_STREAMING_TEST_SRC = \

CHTTP2_SOCKET_PAIR_PING_PONG_STREAMING_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_PING_PONG_STREAMING_TEST_SRC))))
CHTTP2_SOCKET_PAIR_PING_PONG_STREAMING_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_PING_PONG_STREAMING_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_ping_pong_streaming_test: openssl_dep_error

else

bins/chttp2_socket_pair_ping_pong_streaming_test: $(CHTTP2_SOCKET_PAIR_PING_PONG_STREAMING_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_ping_pong_streaming.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_PING_PONG_STREAMING_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_ping_pong_streaming -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_ping_pong_streaming_test

endif

deps_chttp2_socket_pair_ping_pong_streaming_test: $(CHTTP2_SOCKET_PAIR_PING_PONG_STREAMING_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_PING_PONG_STREAMING_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_ping_pong_streaming_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_ping_pong_streaming_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_PING_PONG_STREAMING_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_PING_PONG_STREAMING_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_ping_pong_streaming_test


CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC = \

CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC))))
CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test: openssl_dep_error

else

bins/chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test: $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_request_response_with_binary_metadata_and_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_request_response_with_binary_metadata_and_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test

endif

deps_chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test: $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test


CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC = \

CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC))))
CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_request_response_with_metadata_and_payload_test: openssl_dep_error

else

bins/chttp2_socket_pair_request_response_with_metadata_and_payload_test: $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_request_response_with_metadata_and_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_request_response_with_metadata_and_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_request_response_with_metadata_and_payload_test

endif

deps_chttp2_socket_pair_request_response_with_metadata_and_payload_test: $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_request_response_with_metadata_and_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_request_response_with_metadata_and_payload_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_request_response_with_metadata_and_payload_test


CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC = \

CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC))))
CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_request_response_with_payload_test: openssl_dep_error

else

bins/chttp2_socket_pair_request_response_with_payload_test: $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_request_response_with_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_request_response_with_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_request_response_with_payload_test

endif

deps_chttp2_socket_pair_request_response_with_payload_test: $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_request_response_with_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_request_response_with_payload_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_request_response_with_payload_test


CHTTP2_SOCKET_PAIR_SIMPLE_DELAYED_REQUEST_TEST_SRC = \

CHTTP2_SOCKET_PAIR_SIMPLE_DELAYED_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_SIMPLE_DELAYED_REQUEST_TEST_SRC))))
CHTTP2_SOCKET_PAIR_SIMPLE_DELAYED_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_SIMPLE_DELAYED_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_simple_delayed_request_test: openssl_dep_error

else

bins/chttp2_socket_pair_simple_delayed_request_test: $(CHTTP2_SOCKET_PAIR_SIMPLE_DELAYED_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_simple_delayed_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_SIMPLE_DELAYED_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_simple_delayed_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_simple_delayed_request_test

endif

deps_chttp2_socket_pair_simple_delayed_request_test: $(CHTTP2_SOCKET_PAIR_SIMPLE_DELAYED_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_SIMPLE_DELAYED_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_simple_delayed_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_simple_delayed_request_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_SIMPLE_DELAYED_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_SIMPLE_DELAYED_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_simple_delayed_request_test


CHTTP2_SOCKET_PAIR_SIMPLE_REQUEST_TEST_SRC = \

CHTTP2_SOCKET_PAIR_SIMPLE_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_SIMPLE_REQUEST_TEST_SRC))))
CHTTP2_SOCKET_PAIR_SIMPLE_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_SIMPLE_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_simple_request_test: openssl_dep_error

else

bins/chttp2_socket_pair_simple_request_test: $(CHTTP2_SOCKET_PAIR_SIMPLE_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_simple_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_SIMPLE_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_simple_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_simple_request_test

endif

deps_chttp2_socket_pair_simple_request_test: $(CHTTP2_SOCKET_PAIR_SIMPLE_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_SIMPLE_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_simple_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_simple_request_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_SIMPLE_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_SIMPLE_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_simple_request_test


CHTTP2_SOCKET_PAIR_THREAD_STRESS_TEST_SRC = \

CHTTP2_SOCKET_PAIR_THREAD_STRESS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_THREAD_STRESS_TEST_SRC))))
CHTTP2_SOCKET_PAIR_THREAD_STRESS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_THREAD_STRESS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_thread_stress_test: openssl_dep_error

else

bins/chttp2_socket_pair_thread_stress_test: $(CHTTP2_SOCKET_PAIR_THREAD_STRESS_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_thread_stress.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_THREAD_STRESS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_thread_stress -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_thread_stress_test

endif

deps_chttp2_socket_pair_thread_stress_test: $(CHTTP2_SOCKET_PAIR_THREAD_STRESS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_THREAD_STRESS_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_thread_stress_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_thread_stress_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_THREAD_STRESS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_THREAD_STRESS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_thread_stress_test


CHTTP2_SOCKET_PAIR_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC = \

CHTTP2_SOCKET_PAIR_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC))))
CHTTP2_SOCKET_PAIR_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_writes_done_hangs_with_pending_read_test: openssl_dep_error

else

bins/chttp2_socket_pair_writes_done_hangs_with_pending_read_test: $(CHTTP2_SOCKET_PAIR_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair.a libs/libend2end_test_writes_done_hangs_with_pending_read.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair -lend2end_test_writes_done_hangs_with_pending_read -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_writes_done_hangs_with_pending_read_test

endif

deps_chttp2_socket_pair_writes_done_hangs_with_pending_read_test: $(CHTTP2_SOCKET_PAIR_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_writes_done_hangs_with_pending_read_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_writes_done_hangs_with_pending_read_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_writes_done_hangs_with_pending_read_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_cancel_after_accept.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_cancel_after_accept -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_cancel_after_accept_and_writes_closed.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_cancel_after_accept_and_writes_closed -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_ACCEPT_AND_WRITES_CLOSED_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_INVOKE_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_INVOKE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_INVOKE_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_INVOKE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_INVOKE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_INVOKE_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_cancel_after_invoke.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_INVOKE_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_cancel_after_invoke -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_INVOKE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_INVOKE_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_INVOKE_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_AFTER_INVOKE_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_BEFORE_INVOKE_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_BEFORE_INVOKE_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_BEFORE_INVOKE_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_BEFORE_INVOKE_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_BEFORE_INVOKE_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_BEFORE_INVOKE_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_cancel_before_invoke.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_BEFORE_INVOKE_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_cancel_before_invoke -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_BEFORE_INVOKE_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_BEFORE_INVOKE_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_BEFORE_INVOKE_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_BEFORE_INVOKE_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_IN_A_VACUUM_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_IN_A_VACUUM_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_IN_A_VACUUM_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_IN_A_VACUUM_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_IN_A_VACUUM_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_IN_A_VACUUM_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_cancel_in_a_vacuum.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_IN_A_VACUUM_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_cancel_in_a_vacuum -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_IN_A_VACUUM_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_IN_A_VACUUM_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_IN_A_VACUUM_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_CANCEL_IN_A_VACUUM_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_early_server_shutdown_finishes_inflight_calls.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_early_server_shutdown_finishes_inflight_calls -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_INFLIGHT_CALLS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_early_server_shutdown_finishes_tags.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_early_server_shutdown_finishes_tags -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_EARLY_SERVER_SHUTDOWN_FINISHES_TAGS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_INVOKE_LARGE_REQUEST_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_INVOKE_LARGE_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_INVOKE_LARGE_REQUEST_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_INVOKE_LARGE_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_INVOKE_LARGE_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_INVOKE_LARGE_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_invoke_large_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_INVOKE_LARGE_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_invoke_large_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_INVOKE_LARGE_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_INVOKE_LARGE_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_INVOKE_LARGE_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_INVOKE_LARGE_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_MAX_CONCURRENT_STREAMS_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_MAX_CONCURRENT_STREAMS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_MAX_CONCURRENT_STREAMS_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_MAX_CONCURRENT_STREAMS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_MAX_CONCURRENT_STREAMS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_MAX_CONCURRENT_STREAMS_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_max_concurrent_streams.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_MAX_CONCURRENT_STREAMS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_max_concurrent_streams -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_MAX_CONCURRENT_STREAMS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_MAX_CONCURRENT_STREAMS_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_MAX_CONCURRENT_STREAMS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_MAX_CONCURRENT_STREAMS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_NO_OP_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_NO_OP_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_NO_OP_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_NO_OP_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_NO_OP_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_no_op_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_no_op_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_NO_OP_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_no_op.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_NO_OP_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_no_op -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_no_op_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_no_op_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_NO_OP_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_NO_OP_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_no_op_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_no_op_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_NO_OP_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_NO_OP_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_no_op_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_PING_PONG_STREAMING_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_PING_PONG_STREAMING_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_PING_PONG_STREAMING_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_PING_PONG_STREAMING_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_PING_PONG_STREAMING_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_PING_PONG_STREAMING_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_ping_pong_streaming.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_PING_PONG_STREAMING_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_ping_pong_streaming -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_PING_PONG_STREAMING_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_PING_PONG_STREAMING_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_PING_PONG_STREAMING_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_PING_PONG_STREAMING_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_request_response_with_binary_metadata_and_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_request_response_with_binary_metadata_and_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_BINARY_METADATA_AND_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_request_response_with_metadata_and_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_request_response_with_metadata_and_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_METADATA_AND_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_request_response_with_payload.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_request_response_with_payload -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_REQUEST_RESPONSE_WITH_PAYLOAD_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_DELAYED_REQUEST_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_DELAYED_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_DELAYED_REQUEST_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_DELAYED_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_DELAYED_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_DELAYED_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_simple_delayed_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_DELAYED_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_simple_delayed_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_DELAYED_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_DELAYED_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_DELAYED_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_DELAYED_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_REQUEST_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_REQUEST_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_REQUEST_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_REQUEST_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_REQUEST_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_simple_request_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_simple_request_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_REQUEST_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_simple_request.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_REQUEST_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_simple_request -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_simple_request_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_simple_request_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_REQUEST_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_REQUEST_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_simple_request_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_simple_request_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_REQUEST_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_SIMPLE_REQUEST_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_simple_request_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_THREAD_STRESS_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_THREAD_STRESS_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_THREAD_STRESS_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_THREAD_STRESS_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_THREAD_STRESS_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_thread_stress_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_thread_stress_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_THREAD_STRESS_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_thread_stress.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_THREAD_STRESS_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_thread_stress -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_thread_stress_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_thread_stress_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_THREAD_STRESS_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_THREAD_STRESS_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_thread_stress_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_thread_stress_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_THREAD_STRESS_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_THREAD_STRESS_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_thread_stress_test


CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC = \

CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS = $(addprefix objs/, $(addsuffix .o, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC))))
CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS = $(addprefix deps/, $(addsuffix .dep, $(basename $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_SRC))))

ifeq ($(NO_SECURE),true)

bins/chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test: openssl_dep_error

else

bins/chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS) libs/libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time.a libs/libend2end_test_writes_done_hangs_with_pending_read.a libs/libend2end_certs.a libs/libgrpc_test_util.a libs/libgrpc.a libs/libgpr.a
	$(E) "[LD]      Linking $@"
	$(Q) mkdir -p `dirname $@`
	$(Q) $(LD) $(LDFLAGS) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS) -Llibs -lend2end_fixture_chttp2_socket_pair_one_byte_at_a_time -lend2end_test_writes_done_hangs_with_pending_read -lend2end_certs -lgrpc_test_util -lgrpc -lgpr $(LDLIBS) $(LDLIBS_SECURE) -o bins/chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test

endif

deps_chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test: $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)

ifneq ($(NO_SECURE),true)
ifneq ($(NO_DEPS),true)
-include $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)
endif
endif

clean_chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test:
	$(E) "[CLEAN]   Cleaning chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test files"
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_OBJS)
	$(Q) $(RM) $(CHTTP2_SOCKET_PAIR_ONE_BYTE_AT_A_TIME_WRITES_DONE_HANGS_WITH_PENDING_READ_TEST_DEPS)
	$(Q) $(RM) bins/chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test






.PHONY: all strip tools dep_error openssl_dep_error openssl_dep_message git_update stop buildtests buildtests_c buildtests_cxx test test_c test_cxx install install_c install_cxx install-headers install-headers_c install-headers_cxx install-shared install-shared_c install-shared_cxx install-static install-static_c install-static_cxx strip strip-shared strip-static strip_c strip-shared_c strip-static_c strip_cxx strip-shared_cxx strip-static_cxx clean deps_libgpr clean_libgpr deps_libgrpc clean_libgrpc deps_libgrpc_test_util clean_libgrpc_test_util deps_libgrpc++ clean_libgrpc++ deps_libgrpc++_test_util clean_libgrpc++_test_util deps_libend2end_fixture_chttp2_fake_security clean_libend2end_fixture_chttp2_fake_security deps_libend2end_fixture_chttp2_fullstack clean_libend2end_fixture_chttp2_fullstack deps_libend2end_fixture_chttp2_simple_ssl_fullstack clean_libend2end_fixture_chttp2_simple_ssl_fullstack deps_libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack clean_libend2end_fixture_chttp2_simple_ssl_with_oauth2_fullstack deps_libend2end_fixture_chttp2_socket_pair clean_libend2end_fixture_chttp2_socket_pair deps_libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time clean_libend2end_fixture_chttp2_socket_pair_one_byte_at_a_time deps_libend2end_test_cancel_after_accept clean_libend2end_test_cancel_after_accept deps_libend2end_test_cancel_after_accept_and_writes_closed clean_libend2end_test_cancel_after_accept_and_writes_closed deps_libend2end_test_cancel_after_invoke clean_libend2end_test_cancel_after_invoke deps_libend2end_test_cancel_before_invoke clean_libend2end_test_cancel_before_invoke deps_libend2end_test_cancel_in_a_vacuum clean_libend2end_test_cancel_in_a_vacuum deps_libend2end_test_early_server_shutdown_finishes_inflight_calls clean_libend2end_test_early_server_shutdown_finishes_inflight_calls deps_libend2end_test_early_server_shutdown_finishes_tags clean_libend2end_test_early_server_shutdown_finishes_tags deps_libend2end_test_invoke_large_request clean_libend2end_test_invoke_large_request deps_libend2end_test_max_concurrent_streams clean_libend2end_test_max_concurrent_streams deps_libend2end_test_no_op clean_libend2end_test_no_op deps_libend2end_test_ping_pong_streaming clean_libend2end_test_ping_pong_streaming deps_libend2end_test_request_response_with_binary_metadata_and_payload clean_libend2end_test_request_response_with_binary_metadata_and_payload deps_libend2end_test_request_response_with_metadata_and_payload clean_libend2end_test_request_response_with_metadata_and_payload deps_libend2end_test_request_response_with_payload clean_libend2end_test_request_response_with_payload deps_libend2end_test_simple_delayed_request clean_libend2end_test_simple_delayed_request deps_libend2end_test_simple_request clean_libend2end_test_simple_request deps_libend2end_test_thread_stress clean_libend2end_test_thread_stress deps_libend2end_test_writes_done_hangs_with_pending_read clean_libend2end_test_writes_done_hangs_with_pending_read deps_libend2end_certs clean_libend2end_certs deps_libgrpc_unsecure clean_libgrpc_unsecure deps_gen_hpack_tables clean_gen_hpack_tables deps_cpp_plugin clean_cpp_plugin deps_ruby_plugin clean_ruby_plugin deps_grpc_byte_buffer_reader_test clean_grpc_byte_buffer_reader_test deps_gpr_cancellable_test clean_gpr_cancellable_test deps_gpr_log_test clean_gpr_log_test deps_gpr_useful_test clean_gpr_useful_test deps_gpr_cmdline_test clean_gpr_cmdline_test deps_gpr_histogram_test clean_gpr_histogram_test deps_gpr_host_port_test clean_gpr_host_port_test deps_gpr_slice_buffer_test clean_gpr_slice_buffer_test deps_gpr_slice_test clean_gpr_slice_test deps_gpr_string_test clean_gpr_string_test deps_gpr_sync_test clean_gpr_sync_test deps_gpr_thd_test clean_gpr_thd_test deps_gpr_time_test clean_gpr_time_test deps_murmur_hash_test clean_murmur_hash_test deps_grpc_stream_op_test clean_grpc_stream_op_test deps_alpn_test clean_alpn_test deps_chttp2_stream_encoder_test clean_chttp2_stream_encoder_test deps_hpack_table_test clean_hpack_table_test deps_chttp2_stream_map_test clean_chttp2_stream_map_test deps_hpack_parser_test clean_hpack_parser_test deps_transport_metadata_test clean_transport_metadata_test deps_chttp2_status_conversion_test clean_chttp2_status_conversion_test deps_chttp2_transport_end2end_test clean_chttp2_transport_end2end_test deps_tcp_posix_test clean_tcp_posix_test deps_dualstack_socket_test clean_dualstack_socket_test deps_no_server_test clean_no_server_test deps_resolve_address_test clean_resolve_address_test deps_sockaddr_utils_test clean_sockaddr_utils_test deps_tcp_server_posix_test clean_tcp_server_posix_test deps_tcp_client_posix_test clean_tcp_client_posix_test deps_grpc_channel_stack_test clean_grpc_channel_stack_test deps_metadata_buffer_test clean_metadata_buffer_test deps_grpc_completion_queue_test clean_grpc_completion_queue_test deps_grpc_completion_queue_benchmark clean_grpc_completion_queue_benchmark deps_census_window_stats_test clean_census_window_stats_test deps_census_statistics_quick_test clean_census_statistics_quick_test deps_census_statistics_performance_test clean_census_statistics_performance_test deps_census_statistics_multiple_writers_test clean_census_statistics_multiple_writers_test deps_census_statistics_multiple_writers_circular_buffer_test clean_census_statistics_multiple_writers_circular_buffer_test deps_census_stub_test clean_census_stub_test deps_census_hash_table_test clean_census_hash_table_test deps_fling_server clean_fling_server deps_fling_client clean_fling_client deps_fling_test clean_fling_test deps_echo_server clean_echo_server deps_echo_client clean_echo_client deps_echo_test clean_echo_test deps_low_level_ping_pong_benchmark clean_low_level_ping_pong_benchmark deps_message_compress_test clean_message_compress_test deps_bin_encoder_test clean_bin_encoder_test deps_secure_endpoint_test clean_secure_endpoint_test deps_httpcli_format_request_test clean_httpcli_format_request_test deps_httpcli_parser_test clean_httpcli_parser_test deps_httpcli_test clean_httpcli_test deps_grpc_credentials_test clean_grpc_credentials_test deps_grpc_base64_test clean_grpc_base64_test deps_grpc_json_token_test clean_grpc_json_token_test deps_timeout_encoding_test clean_timeout_encoding_test deps_fd_posix_test clean_fd_posix_test deps_fling_stream_test clean_fling_stream_test deps_lame_client_test clean_lame_client_test deps_thread_pool_test clean_thread_pool_test deps_status_test clean_status_test deps_sync_client_async_server_test clean_sync_client_async_server_test deps_qps_client clean_qps_client deps_qps_server clean_qps_server deps_interop_server clean_interop_server deps_interop_client clean_interop_client deps_end2end_test clean_end2end_test deps_alarm_test clean_alarm_test deps_time_test clean_time_test deps_chttp2_fake_security_cancel_after_accept_test clean_chttp2_fake_security_cancel_after_accept_test deps_chttp2_fake_security_cancel_after_accept_and_writes_closed_test clean_chttp2_fake_security_cancel_after_accept_and_writes_closed_test deps_chttp2_fake_security_cancel_after_invoke_test clean_chttp2_fake_security_cancel_after_invoke_test deps_chttp2_fake_security_cancel_before_invoke_test clean_chttp2_fake_security_cancel_before_invoke_test deps_chttp2_fake_security_cancel_in_a_vacuum_test clean_chttp2_fake_security_cancel_in_a_vacuum_test deps_chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test clean_chttp2_fake_security_early_server_shutdown_finishes_inflight_calls_test deps_chttp2_fake_security_early_server_shutdown_finishes_tags_test clean_chttp2_fake_security_early_server_shutdown_finishes_tags_test deps_chttp2_fake_security_invoke_large_request_test clean_chttp2_fake_security_invoke_large_request_test deps_chttp2_fake_security_max_concurrent_streams_test clean_chttp2_fake_security_max_concurrent_streams_test deps_chttp2_fake_security_no_op_test clean_chttp2_fake_security_no_op_test deps_chttp2_fake_security_ping_pong_streaming_test clean_chttp2_fake_security_ping_pong_streaming_test deps_chttp2_fake_security_request_response_with_binary_metadata_and_payload_test clean_chttp2_fake_security_request_response_with_binary_metadata_and_payload_test deps_chttp2_fake_security_request_response_with_metadata_and_payload_test clean_chttp2_fake_security_request_response_with_metadata_and_payload_test deps_chttp2_fake_security_request_response_with_payload_test clean_chttp2_fake_security_request_response_with_payload_test deps_chttp2_fake_security_simple_delayed_request_test clean_chttp2_fake_security_simple_delayed_request_test deps_chttp2_fake_security_simple_request_test clean_chttp2_fake_security_simple_request_test deps_chttp2_fake_security_thread_stress_test clean_chttp2_fake_security_thread_stress_test deps_chttp2_fake_security_writes_done_hangs_with_pending_read_test clean_chttp2_fake_security_writes_done_hangs_with_pending_read_test deps_chttp2_fullstack_cancel_after_accept_test clean_chttp2_fullstack_cancel_after_accept_test deps_chttp2_fullstack_cancel_after_accept_and_writes_closed_test clean_chttp2_fullstack_cancel_after_accept_and_writes_closed_test deps_chttp2_fullstack_cancel_after_invoke_test clean_chttp2_fullstack_cancel_after_invoke_test deps_chttp2_fullstack_cancel_before_invoke_test clean_chttp2_fullstack_cancel_before_invoke_test deps_chttp2_fullstack_cancel_in_a_vacuum_test clean_chttp2_fullstack_cancel_in_a_vacuum_test deps_chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test clean_chttp2_fullstack_early_server_shutdown_finishes_inflight_calls_test deps_chttp2_fullstack_early_server_shutdown_finishes_tags_test clean_chttp2_fullstack_early_server_shutdown_finishes_tags_test deps_chttp2_fullstack_invoke_large_request_test clean_chttp2_fullstack_invoke_large_request_test deps_chttp2_fullstack_max_concurrent_streams_test clean_chttp2_fullstack_max_concurrent_streams_test deps_chttp2_fullstack_no_op_test clean_chttp2_fullstack_no_op_test deps_chttp2_fullstack_ping_pong_streaming_test clean_chttp2_fullstack_ping_pong_streaming_test deps_chttp2_fullstack_request_response_with_binary_metadata_and_payload_test clean_chttp2_fullstack_request_response_with_binary_metadata_and_payload_test deps_chttp2_fullstack_request_response_with_metadata_and_payload_test clean_chttp2_fullstack_request_response_with_metadata_and_payload_test deps_chttp2_fullstack_request_response_with_payload_test clean_chttp2_fullstack_request_response_with_payload_test deps_chttp2_fullstack_simple_delayed_request_test clean_chttp2_fullstack_simple_delayed_request_test deps_chttp2_fullstack_simple_request_test clean_chttp2_fullstack_simple_request_test deps_chttp2_fullstack_thread_stress_test clean_chttp2_fullstack_thread_stress_test deps_chttp2_fullstack_writes_done_hangs_with_pending_read_test clean_chttp2_fullstack_writes_done_hangs_with_pending_read_test deps_chttp2_simple_ssl_fullstack_cancel_after_accept_test clean_chttp2_simple_ssl_fullstack_cancel_after_accept_test deps_chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test clean_chttp2_simple_ssl_fullstack_cancel_after_accept_and_writes_closed_test deps_chttp2_simple_ssl_fullstack_cancel_after_invoke_test clean_chttp2_simple_ssl_fullstack_cancel_after_invoke_test deps_chttp2_simple_ssl_fullstack_cancel_before_invoke_test clean_chttp2_simple_ssl_fullstack_cancel_before_invoke_test deps_chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test clean_chttp2_simple_ssl_fullstack_cancel_in_a_vacuum_test deps_chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test clean_chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_inflight_calls_test deps_chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test clean_chttp2_simple_ssl_fullstack_early_server_shutdown_finishes_tags_test deps_chttp2_simple_ssl_fullstack_invoke_large_request_test clean_chttp2_simple_ssl_fullstack_invoke_large_request_test deps_chttp2_simple_ssl_fullstack_max_concurrent_streams_test clean_chttp2_simple_ssl_fullstack_max_concurrent_streams_test deps_chttp2_simple_ssl_fullstack_no_op_test clean_chttp2_simple_ssl_fullstack_no_op_test deps_chttp2_simple_ssl_fullstack_ping_pong_streaming_test clean_chttp2_simple_ssl_fullstack_ping_pong_streaming_test deps_chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test clean_chttp2_simple_ssl_fullstack_request_response_with_binary_metadata_and_payload_test deps_chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test clean_chttp2_simple_ssl_fullstack_request_response_with_metadata_and_payload_test deps_chttp2_simple_ssl_fullstack_request_response_with_payload_test clean_chttp2_simple_ssl_fullstack_request_response_with_payload_test deps_chttp2_simple_ssl_fullstack_simple_delayed_request_test clean_chttp2_simple_ssl_fullstack_simple_delayed_request_test deps_chttp2_simple_ssl_fullstack_simple_request_test clean_chttp2_simple_ssl_fullstack_simple_request_test deps_chttp2_simple_ssl_fullstack_thread_stress_test clean_chttp2_simple_ssl_fullstack_thread_stress_test deps_chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test clean_chttp2_simple_ssl_fullstack_writes_done_hangs_with_pending_read_test deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_test deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_accept_and_writes_closed_test deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_after_invoke_test deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_before_invoke_test deps_chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test clean_chttp2_simple_ssl_with_oauth2_fullstack_cancel_in_a_vacuum_test deps_chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test clean_chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_inflight_calls_test deps_chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test clean_chttp2_simple_ssl_with_oauth2_fullstack_early_server_shutdown_finishes_tags_test deps_chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test clean_chttp2_simple_ssl_with_oauth2_fullstack_invoke_large_request_test deps_chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test clean_chttp2_simple_ssl_with_oauth2_fullstack_max_concurrent_streams_test deps_chttp2_simple_ssl_with_oauth2_fullstack_no_op_test clean_chttp2_simple_ssl_with_oauth2_fullstack_no_op_test deps_chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test clean_chttp2_simple_ssl_with_oauth2_fullstack_ping_pong_streaming_test deps_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test clean_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_binary_metadata_and_payload_test deps_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test clean_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_metadata_and_payload_test deps_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test clean_chttp2_simple_ssl_with_oauth2_fullstack_request_response_with_payload_test deps_chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test clean_chttp2_simple_ssl_with_oauth2_fullstack_simple_delayed_request_test deps_chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test clean_chttp2_simple_ssl_with_oauth2_fullstack_simple_request_test deps_chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test clean_chttp2_simple_ssl_with_oauth2_fullstack_thread_stress_test deps_chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test clean_chttp2_simple_ssl_with_oauth2_fullstack_writes_done_hangs_with_pending_read_test deps_chttp2_socket_pair_cancel_after_accept_test clean_chttp2_socket_pair_cancel_after_accept_test deps_chttp2_socket_pair_cancel_after_accept_and_writes_closed_test clean_chttp2_socket_pair_cancel_after_accept_and_writes_closed_test deps_chttp2_socket_pair_cancel_after_invoke_test clean_chttp2_socket_pair_cancel_after_invoke_test deps_chttp2_socket_pair_cancel_before_invoke_test clean_chttp2_socket_pair_cancel_before_invoke_test deps_chttp2_socket_pair_cancel_in_a_vacuum_test clean_chttp2_socket_pair_cancel_in_a_vacuum_test deps_chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test clean_chttp2_socket_pair_early_server_shutdown_finishes_inflight_calls_test deps_chttp2_socket_pair_early_server_shutdown_finishes_tags_test clean_chttp2_socket_pair_early_server_shutdown_finishes_tags_test deps_chttp2_socket_pair_invoke_large_request_test clean_chttp2_socket_pair_invoke_large_request_test deps_chttp2_socket_pair_max_concurrent_streams_test clean_chttp2_socket_pair_max_concurrent_streams_test deps_chttp2_socket_pair_no_op_test clean_chttp2_socket_pair_no_op_test deps_chttp2_socket_pair_ping_pong_streaming_test clean_chttp2_socket_pair_ping_pong_streaming_test deps_chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test clean_chttp2_socket_pair_request_response_with_binary_metadata_and_payload_test deps_chttp2_socket_pair_request_response_with_metadata_and_payload_test clean_chttp2_socket_pair_request_response_with_metadata_and_payload_test deps_chttp2_socket_pair_request_response_with_payload_test clean_chttp2_socket_pair_request_response_with_payload_test deps_chttp2_socket_pair_simple_delayed_request_test clean_chttp2_socket_pair_simple_delayed_request_test deps_chttp2_socket_pair_simple_request_test clean_chttp2_socket_pair_simple_request_test deps_chttp2_socket_pair_thread_stress_test clean_chttp2_socket_pair_thread_stress_test deps_chttp2_socket_pair_writes_done_hangs_with_pending_read_test clean_chttp2_socket_pair_writes_done_hangs_with_pending_read_test deps_chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test clean_chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_test deps_chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test clean_chttp2_socket_pair_one_byte_at_a_time_cancel_after_accept_and_writes_closed_test deps_chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test clean_chttp2_socket_pair_one_byte_at_a_time_cancel_after_invoke_test deps_chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test clean_chttp2_socket_pair_one_byte_at_a_time_cancel_before_invoke_test deps_chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test clean_chttp2_socket_pair_one_byte_at_a_time_cancel_in_a_vacuum_test deps_chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test clean_chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_inflight_calls_test deps_chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test clean_chttp2_socket_pair_one_byte_at_a_time_early_server_shutdown_finishes_tags_test deps_chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test clean_chttp2_socket_pair_one_byte_at_a_time_invoke_large_request_test deps_chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test clean_chttp2_socket_pair_one_byte_at_a_time_max_concurrent_streams_test deps_chttp2_socket_pair_one_byte_at_a_time_no_op_test clean_chttp2_socket_pair_one_byte_at_a_time_no_op_test deps_chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test clean_chttp2_socket_pair_one_byte_at_a_time_ping_pong_streaming_test deps_chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test clean_chttp2_socket_pair_one_byte_at_a_time_request_response_with_binary_metadata_and_payload_test deps_chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test clean_chttp2_socket_pair_one_byte_at_a_time_request_response_with_metadata_and_payload_test deps_chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test clean_chttp2_socket_pair_one_byte_at_a_time_request_response_with_payload_test deps_chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test clean_chttp2_socket_pair_one_byte_at_a_time_simple_delayed_request_test deps_chttp2_socket_pair_one_byte_at_a_time_simple_request_test clean_chttp2_socket_pair_one_byte_at_a_time_simple_request_test deps_chttp2_socket_pair_one_byte_at_a_time_thread_stress_test clean_chttp2_socket_pair_one_byte_at_a_time_thread_stress_test deps_chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test clean_chttp2_socket_pair_one_byte_at_a_time_writes_done_hangs_with_pending_read_test
