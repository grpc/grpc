//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/handshaker/security/secure_endpoint.h"

#include <fcntl.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <sys/types.h>

#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/fake_transport_security.h"
#include "src/core/util/crash.h"
#include "src/core/util/useful.h"
#include "test/core/iomgr/endpoint_tests.h"
#include "test/core/test_util/test_config.h"

static gpr_mu* g_mu;
static grpc_pollset* g_pollset;

#define TSI_FAKE_FRAME_HEADER_SIZE 4

typedef struct intercept_endpoint {
  grpc_endpoint base;
  grpc_endpoint* wrapped_ep;
  grpc_slice_buffer staging_buffer;
} intercept_endpoint;

static void me_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                    grpc_closure* cb, bool urgent, int min_progress_size) {
  intercept_endpoint* m = reinterpret_cast<intercept_endpoint*>(ep);
  grpc_endpoint_read(m->wrapped_ep, slices, cb, urgent, min_progress_size);
}

static void me_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                     grpc_closure* cb, void* arg, int max_frame_size) {
  intercept_endpoint* m = reinterpret_cast<intercept_endpoint*>(ep);
  int remaining = slices->length;
  while (remaining > 0) {
    // Estimate the frame size of the next frame.
    int next_frame_size =
        tsi_fake_zero_copy_grpc_protector_next_frame_size(slices);
    ASSERT_GT(next_frame_size, TSI_FAKE_FRAME_HEADER_SIZE);
    // Ensure the protected data size does not exceed the max_frame_size.
    ASSERT_LE(next_frame_size - TSI_FAKE_FRAME_HEADER_SIZE, max_frame_size);
    // Move this frame into a staging buffer and repeat.
    grpc_slice_buffer_move_first(slices, next_frame_size, &m->staging_buffer);
    remaining -= next_frame_size;
  }
  grpc_slice_buffer_swap(&m->staging_buffer, slices);
  grpc_endpoint_write(m->wrapped_ep, slices, cb, arg, max_frame_size);
}

static void me_add_to_pollset(grpc_endpoint* /*ep*/,
                              grpc_pollset* /*pollset*/) {}

static void me_add_to_pollset_set(grpc_endpoint* /*ep*/,
                                  grpc_pollset_set* /*pollset*/) {}

static void me_delete_from_pollset_set(grpc_endpoint* /*ep*/,
                                       grpc_pollset_set* /*pollset*/) {}

static void me_destroy(grpc_endpoint* ep) {
  intercept_endpoint* m = reinterpret_cast<intercept_endpoint*>(ep);
  grpc_endpoint_destroy(m->wrapped_ep);
  grpc_slice_buffer_destroy(&m->staging_buffer);
  gpr_free(m);
}

static absl::string_view me_get_peer(grpc_endpoint* /*ep*/) {
  return "fake:intercept-endpoint";
}

static absl::string_view me_get_local_address(grpc_endpoint* /*ep*/) {
  return "fake:intercept-endpoint";
}

static int me_get_fd(grpc_endpoint* /*ep*/) { return -1; }

static bool me_can_track_err(grpc_endpoint* /*ep*/) { return false; }

static const grpc_endpoint_vtable vtable = {me_read,
                                            me_write,
                                            me_add_to_pollset,
                                            me_add_to_pollset_set,
                                            me_delete_from_pollset_set,
                                            me_destroy,
                                            me_get_peer,
                                            me_get_local_address,
                                            me_get_fd,
                                            me_can_track_err};

grpc_endpoint* wrap_with_intercept_endpoint(grpc_endpoint* wrapped_ep) {
  intercept_endpoint* m =
      static_cast<intercept_endpoint*>(gpr_malloc(sizeof(*m)));
  m->base.vtable = &vtable;
  m->wrapped_ep = wrapped_ep;
  grpc_slice_buffer_init(&m->staging_buffer);
  return &m->base;
}

static grpc_endpoint_test_fixture secure_endpoint_create_fixture_tcp_socketpair(
    size_t slice_size, grpc_slice* leftover_slices, size_t leftover_nslices,
    bool use_zero_copy_protector) {
  grpc_core::ExecCtx exec_ctx;
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

  grpc_arg a[2];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER;
  a[0].value.integer = static_cast<int>(slice_size);
  a[1].key = const_cast<char*>(GRPC_ARG_RESOURCE_QUOTA);
  a[1].type = GRPC_ARG_POINTER;
  a[1].value.pointer.p = grpc_resource_quota_create("test");
  a[1].value.pointer.vtable = grpc_resource_quota_arg_vtable();
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  tcp = grpc_iomgr_create_endpoint_pair("fixture", &args);
  grpc_endpoint_add_to_pollset(tcp.client, g_pollset);
  grpc_endpoint_add_to_pollset(tcp.server, g_pollset);

  // TODO(vigneshbabu): Extend the intercept endpoint logic to cover non-zero
  // copy based frame protectors as well.
  if (use_zero_copy_protector && leftover_nslices == 0) {
    tcp.client = wrap_with_intercept_endpoint(tcp.client);
    tcp.server = wrap_with_intercept_endpoint(tcp.server);
  }

  if (leftover_nslices == 0) {
    f.client_ep = grpc_secure_endpoint_create(
                      fake_read_protector, fake_read_zero_copy_protector,
                      grpc_core::OrphanablePtr<grpc_endpoint>(tcp.client),
                      nullptr, 0, grpc_core::ChannelArgs::FromC(args))
                      .release();
  } else {
    unsigned i;
    tsi_result result;
    size_t still_pending_size;
    size_t total_buffer_size = 8192;
    size_t buffer_size = total_buffer_size;
    uint8_t* encrypted_buffer = static_cast<uint8_t*>(gpr_malloc(buffer_size));
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
        EXPECT_EQ(result, TSI_OK);
        message_bytes += processed_message_size;
        message_size -= processed_message_size;
        cur += protected_buffer_size_to_send;
        EXPECT_GE(buffer_size, protected_buffer_size_to_send);
        buffer_size -= protected_buffer_size_to_send;
      }
      grpc_slice_unref(plain);
    }
    do {
      size_t protected_buffer_size_to_send = buffer_size;
      result = tsi_frame_protector_protect_flush(fake_write_protector, cur,
                                                 &protected_buffer_size_to_send,
                                                 &still_pending_size);
      EXPECT_EQ(result, TSI_OK);
      cur += protected_buffer_size_to_send;
      EXPECT_GE(buffer_size, protected_buffer_size_to_send);
      buffer_size -= protected_buffer_size_to_send;
    } while (still_pending_size > 0);
    encrypted_leftover = grpc_slice_from_copied_buffer(
        reinterpret_cast<const char*>(encrypted_buffer),
        total_buffer_size - buffer_size);
    f.client_ep =
        grpc_secure_endpoint_create(
            fake_read_protector, fake_read_zero_copy_protector,
            grpc_core::OrphanablePtr<grpc_endpoint>(tcp.client),
            &encrypted_leftover, 1, grpc_core::ChannelArgs::FromC(args))
            .release();
    grpc_slice_unref(encrypted_leftover);
    gpr_free(encrypted_buffer);
  }

  f.server_ep = grpc_secure_endpoint_create(
                    fake_write_protector, fake_write_zero_copy_protector,
                    grpc_core::OrphanablePtr<grpc_endpoint>(tcp.server),
                    nullptr, 0, grpc_core::ChannelArgs::FromC(args))
                    .release();
  grpc_resource_quota_unref(
      static_cast<grpc_resource_quota*>(a[1].value.pointer.p));
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

static void inc_call_ctr(void* arg, grpc_error_handle /*error*/) {
  ++*static_cast<int*>(arg);
}

static void test_leftover(grpc_endpoint_test_config config, size_t slice_size) {
  grpc_endpoint_test_fixture f = config.create_fixture(slice_size);
  grpc_slice_buffer incoming;
  grpc_slice s =
      grpc_slice_from_copied_string("hello world 12345678900987654321");
  grpc_core::ExecCtx exec_ctx;
  int n = 0;
  grpc_closure done_closure;
  LOG(INFO) << "Start test left over";

  grpc_slice_buffer_init(&incoming);
  GRPC_CLOSURE_INIT(&done_closure, inc_call_ctr, &n, grpc_schedule_on_exec_ctx);
  grpc_endpoint_read(f.client_ep, &incoming, &done_closure, /*urgent=*/false,
                     /*min_progress_size=*/1);

  grpc_core::ExecCtx::Get()->Flush();
  ASSERT_EQ(n, 1);
  ASSERT_EQ(incoming.count, 1);
  ASSERT_EQ(grpc_core::StringViewFromSlice(s),
            grpc_core::StringViewFromSlice(incoming.slices[0]));

  grpc_endpoint_destroy(f.client_ep);
  grpc_endpoint_destroy(f.server_ep);

  grpc_slice_unref(s);
  grpc_slice_buffer_destroy(&incoming);

  clean_up();
}

static void destroy_pollset(void* p, grpc_error_handle /*error*/) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(p));
}

TEST(SecureEndpointTest, MainTest) {
  grpc_closure destroyed;
  grpc_init();

  {
    grpc_core::ExecCtx exec_ctx;
    g_pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(g_pollset, &g_mu);
    grpc_endpoint_tests(configs[0], g_pollset, g_mu);
    grpc_endpoint_tests(configs[1], g_pollset, g_mu);
    test_leftover(configs[2], 1);
    test_leftover(configs[3], 1);
    GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, g_pollset,
                      grpc_schedule_on_exec_ctx);
    grpc_pollset_shutdown(g_pollset, &destroyed);
  }

  grpc_shutdown();

  gpr_free(g_pollset);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
