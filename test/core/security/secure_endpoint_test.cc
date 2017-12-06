/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "test/core/iomgr/endpoint_tests.h"

#include <fcntl.h>
#include <sys/types.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/security/transport/secure_endpoint.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/fake_transport_security.h"
#include "test/core/util/test_config.h"

static gpr_mu* g_mu;
static grpc_pollset* g_pollset;

static grpc_endpoint_test_fixture secure_endpoint_create_fixture_tcp_socketpair(
    size_t slice_size, grpc_slice* leftover_slices, size_t leftover_nslices,
    bool use_zero_copy_protector) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  tsi_frame_protector* fake_read_protector =
      tsi_create_fake_frame_protector(nullptr);
  tsi_frame_protector* fake_write_protector =
      tsi_create_fake_frame_protector(nullptr);
  tsi_zero_copy_grpc_protector* fake_read_zero_copy_protector =
      use_zero_copy_protector
          ? tsi_create_fake_zero_copy_grpc_protector(nullptr)
          : nullptr;
  tsi_zero_copy_grpc_protector* fake_write_zero_copy_protector =
      use_zero_copy_protector
          ? tsi_create_fake_zero_copy_grpc_protector(nullptr)
          : nullptr;
  grpc_endpoint_test_fixture f;
  grpc_endpoint_pair tcp;

  grpc_arg a[1];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER;
  a[0].value.integer = (int)slice_size;
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  tcp = grpc_iomgr_create_endpoint_pair("fixture", &args);
  grpc_endpoint_add_to_pollset(&exec_ctx, tcp.client, g_pollset);
  grpc_endpoint_add_to_pollset(&exec_ctx, tcp.server, g_pollset);

  if (leftover_nslices == 0) {
    f.client_ep = grpc_secure_endpoint_create(fake_read_protector,
                                              fake_read_zero_copy_protector,
                                              tcp.client, nullptr, 0);
  } else {
    unsigned i;
    tsi_result result;
    size_t still_pending_size;
    size_t total_buffer_size = 8192;
    size_t buffer_size = total_buffer_size;
    uint8_t* encrypted_buffer = (uint8_t*)gpr_malloc(buffer_size);
    uint8_t* cur = encrypted_buffer;
    grpc_slice encrypted_leftover;
    for (i = 0; i < leftover_nslices; i++) {
      grpc_slice plain = leftover_slices[i];
      uint8_t* message_bytes = GRPC_SLICE_START_PTR(plain);
      size_t message_size = GRPC_SLICE_LENGTH(plain);
      while (message_size > 0) {
        size_t protected_buffer_size_to_send = buffer_size;
        size_t processed_message_size = message_size;
        result = tsi_frame_protector_protect(
            fake_write_protector, message_bytes, &processed_message_size, cur,
            &protected_buffer_size_to_send);
        GPR_ASSERT(result == TSI_OK);
        message_bytes += processed_message_size;
        message_size -= processed_message_size;
        cur += protected_buffer_size_to_send;
        GPR_ASSERT(buffer_size >= protected_buffer_size_to_send);
        buffer_size -= protected_buffer_size_to_send;
      }
      grpc_slice_unref(plain);
    }
    do {
      size_t protected_buffer_size_to_send = buffer_size;
      result = tsi_frame_protector_protect_flush(fake_write_protector, cur,
                                                 &protected_buffer_size_to_send,
                                                 &still_pending_size);
      GPR_ASSERT(result == TSI_OK);
      cur += protected_buffer_size_to_send;
      GPR_ASSERT(buffer_size >= protected_buffer_size_to_send);
      buffer_size -= protected_buffer_size_to_send;
    } while (still_pending_size > 0);
    encrypted_leftover = grpc_slice_from_copied_buffer(
        (const char*)encrypted_buffer, total_buffer_size - buffer_size);
    f.client_ep = grpc_secure_endpoint_create(
        fake_read_protector, fake_read_zero_copy_protector, tcp.client,
        &encrypted_leftover, 1);
    grpc_slice_unref(encrypted_leftover);
    gpr_free(encrypted_buffer);
  }

  f.server_ep = grpc_secure_endpoint_create(fake_write_protector,
                                            fake_write_zero_copy_protector,
                                            tcp.server, nullptr, 0);
  grpc_exec_ctx_finish(&exec_ctx);
  return f;
}

static grpc_endpoint_test_fixture
secure_endpoint_create_fixture_tcp_socketpair_noleftover(size_t slice_size) {
  return secure_endpoint_create_fixture_tcp_socketpair(slice_size, nullptr, 0,
                                                       false);
}

static grpc_endpoint_test_fixture
secure_endpoint_create_fixture_tcp_socketpair_noleftover_zero_copy(
    size_t slice_size) {
  return secure_endpoint_create_fixture_tcp_socketpair(slice_size, nullptr, 0,
                                                       true);
}

static grpc_endpoint_test_fixture
secure_endpoint_create_fixture_tcp_socketpair_leftover(size_t slice_size) {
  grpc_slice s =
      grpc_slice_from_copied_string("hello world 12345678900987654321");
  return secure_endpoint_create_fixture_tcp_socketpair(slice_size, &s, 1,
                                                       false);
}

static grpc_endpoint_test_fixture
secure_endpoint_create_fixture_tcp_socketpair_leftover_zero_copy(
    size_t slice_size) {
  grpc_slice s =
      grpc_slice_from_copied_string("hello world 12345678900987654321");
  return secure_endpoint_create_fixture_tcp_socketpair(slice_size, &s, 1, true);
}

static void clean_up(void) {}

static grpc_endpoint_test_config configs[] = {
    {"secure_ep/tcp_socketpair",
     secure_endpoint_create_fixture_tcp_socketpair_noleftover, clean_up},
    {"secure_ep/tcp_socketpair_zero_copy",
     secure_endpoint_create_fixture_tcp_socketpair_noleftover_zero_copy,
     clean_up},
    {"secure_ep/tcp_socketpair_leftover",
     secure_endpoint_create_fixture_tcp_socketpair_leftover, clean_up},
    {"secure_ep/tcp_socketpair_leftover_zero_copy",
     secure_endpoint_create_fixture_tcp_socketpair_leftover_zero_copy,
     clean_up},
};

static void inc_call_ctr(grpc_exec_ctx* exec_ctx, void* arg,
                         grpc_error* error) {
  ++*(int*)arg;
}

static void test_leftover(grpc_endpoint_test_config config, size_t slice_size) {
  grpc_endpoint_test_fixture f = config.create_fixture(slice_size);
  grpc_slice_buffer incoming;
  grpc_slice s =
      grpc_slice_from_copied_string("hello world 12345678900987654321");
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  int n = 0;
  grpc_closure done_closure;
  gpr_log(GPR_INFO, "Start test left over");

  grpc_slice_buffer_init(&incoming);
  GRPC_CLOSURE_INIT(&done_closure, inc_call_ctr, &n, grpc_schedule_on_exec_ctx);
  grpc_endpoint_read(&exec_ctx, f.client_ep, &incoming, &done_closure);
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(n == 1);
  GPR_ASSERT(incoming.count == 1);
  GPR_ASSERT(grpc_slice_eq(s, incoming.slices[0]));

  grpc_endpoint_shutdown(
      &exec_ctx, f.client_ep,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("test_leftover end"));
  grpc_endpoint_shutdown(
      &exec_ctx, f.server_ep,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("test_leftover end"));
  grpc_endpoint_destroy(&exec_ctx, f.client_ep);
  grpc_endpoint_destroy(&exec_ctx, f.server_ep);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_slice_unref_internal(&exec_ctx, s);
  grpc_slice_buffer_destroy_internal(&exec_ctx, &incoming);

  clean_up();
}

static void destroy_pollset(grpc_exec_ctx* exec_ctx, void* p,
                            grpc_error* error) {
  grpc_pollset_destroy(exec_ctx, (grpc_pollset*)p);
}

int main(int argc, char** argv) {
  grpc_closure destroyed;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_test_init(argc, argv);

  grpc_init();
  g_pollset = (grpc_pollset*)gpr_zalloc(grpc_pollset_size());
  grpc_pollset_init(g_pollset, &g_mu);
  grpc_endpoint_tests(configs[0], g_pollset, g_mu);
  grpc_endpoint_tests(configs[1], g_pollset, g_mu);
  test_leftover(configs[2], 1);
  test_leftover(configs[3], 1);
  GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, g_pollset,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(&exec_ctx, g_pollset, &destroyed);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_shutdown();

  gpr_free(g_pollset);

  return 0;
}
