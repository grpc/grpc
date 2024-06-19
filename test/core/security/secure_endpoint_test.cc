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
#include <sys/types.h>

#include <memory>

#include <gtest/gtest.h>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#include "src/core/handshaker/security/event_engine/secure_endpoint.h"
#include "src/core/lib/event_engine/extensions/supports_fd.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint_pair.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/fake_transport_security.h"
#include "src/core/util/useful.h"
#include "test/core/iomgr/endpoint_tests.h"
#include "test/core/test_util/test_config.h"

static gpr_mu* g_mu;
static grpc_pollset* g_pollset;
static bool g_event_engine_secure_endpoint_enabled = false;
static std::shared_ptr<grpc_event_engine::experimental::ThreadPool>
    g_thread_pool = nullptr;

#define TSI_FAKE_FRAME_HEADER_SIZE 4

namespace {

using ::grpc_event_engine::experimental::EndpointPair;
using ::grpc_event_engine::experimental::EndpointSupportsFdExtension;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::grpc_event_engine_endpoint_create;
using ::grpc_event_engine::experimental::
    grpc_take_wrapped_event_engine_endpoint;
using ::grpc_event_engine::experimental::MakeThreadPool;
using ::grpc_event_engine::experimental::QueryExtension;
using ::grpc_event_engine::experimental::Slice;
using ::grpc_event_engine::experimental::SliceBuffer;

}  // namespace

class InterceptEndpoint : public EventEngine::Endpoint,
                          EndpointSupportsFdExtension {
 public:
  InterceptEndpoint(std::unique_ptr<EventEngine::Endpoint> wrapped_ep)
      : wrapped_ep_(std::move(wrapped_ep)) {}
  bool Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const ReadArgs* args) override {
    return wrapped_ep_->Read(std::move(on_read), buffer, args);
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const WriteArgs* args) override {
    int remaining = data->Length();
    while (remaining > 0) {
      // Estimate the frame size of the next frame.
      int next_frame_size = tsi_fake_zero_copy_grpc_protector_next_frame_size(
          data->c_slice_buffer());
      CHECK_GT(next_frame_size, TSI_FAKE_FRAME_HEADER_SIZE);
      // Ensure the protected data size does not exceed the max_frame_size.
      CHECK_LE(next_frame_size - TSI_FAKE_FRAME_HEADER_SIZE,
               args->max_frame_size);
      // Move this frame into a staging buffer and repeat.
      data->MoveFirstNBytesIntoSliceBuffer(next_frame_size, staging_buffer_);
      remaining -= next_frame_size;
    }
    data->Swap(staging_buffer_);
    return wrapped_ep_->Write(std::move(on_writable), data, args);
  }

  ~InterceptEndpoint() override {
    wrapped_ep_.reset();
    staging_buffer_.Clear();
  }

  const EventEngine::ResolvedAddress& GetPeerAddress() const override {
    return wrapped_ep_->GetPeerAddress();
  }

  const EventEngine::ResolvedAddress& GetLocalAddress() const override {
    return wrapped_ep_->GetLocalAddress();
  }

  int GetWrappedFd() override {
    auto* supports_fd =
        ::QueryExtension<EndpointSupportsFdExtension>(wrapped_ep_.get());
    if (supports_fd != nullptr) {
      return supports_fd->GetWrappedFd();
    }
    return -1;
  }

  void Shutdown(absl::AnyInvocable<void(absl::StatusOr<int> release_fd)>
                    on_release_fd) override {
    auto* supports_fd =
        ::QueryExtension<EndpointSupportsFdExtension>(wrapped_ep_.get());
    if (supports_fd != nullptr && on_release_fd != nullptr) {
      supports_fd->Shutdown(std::move(on_release_fd));
    }
  }

 private:
  std::unique_ptr<EventEngine::Endpoint> wrapped_ep_;
  SliceBuffer staging_buffer_;
};

grpc_endpoint* create_secure_endpoint(
    grpc_endpoint* wrapped_ep, tsi_frame_protector* protector,
    tsi_zero_copy_grpc_protector* zero_copy_protector,
    grpc_core::ChannelArgs& args, Slice* leftover, size_t leftover_nslices) {
  if (g_event_engine_secure_endpoint_enabled) {
    return grpc_event_engine_endpoint_create(grpc_core::CreateSecureEndpoint(
        protector, zero_copy_protector,
        grpc_take_wrapped_event_engine_endpoint(wrapped_ep), leftover, args,
        leftover_nslices));
  } else {
    grpc_slice leftover_slice;
    if (leftover != nullptr) {
      leftover_slice = leftover->c_slice();
    }
    return grpc_secure_endpoint_create(
               protector, zero_copy_protector,
               grpc_core::OrphanablePtr<grpc_endpoint>(wrapped_ep),
               (leftover == nullptr ? nullptr : &leftover_slice),
               args.ToC().get(), leftover_nslices)
        .release();
  }
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
  EndpointPair tcp;

  grpc_core::ChannelArgs args;
  args = args.Set(GRPC_ARG_TCP_READ_CHUNK_SIZE, slice_size);
  args = args.Set(GRPC_ARG_RESOURCE_QUOTA, grpc_core::ResourceQuota::Default());
  tcp = grpc_event_engine::experimental::CreateEndpointPair(
      args, g_thread_pool.get());
  grpc_endpoint* client_endpoint;
  grpc_endpoint* server_endpoint;

  // TODO(vigneshbabu): Extend the intercept endpoint logic to cover non-zero
  // copy based frame protectors as well.
  if (use_zero_copy_protector && leftover_nslices == 0) {
    auto client_intercept_ep =
        std::make_unique<InterceptEndpoint>(std::move(tcp.client_ep));
    client_endpoint =
        grpc_event_engine_endpoint_create(std::move(client_intercept_ep));
    auto server_intercept_ep =
        std::make_unique<InterceptEndpoint>(std::move(tcp.server_ep));
    server_endpoint =
        grpc_event_engine_endpoint_create(std::move(server_intercept_ep));
  } else {
    client_endpoint =
        grpc_event_engine_endpoint_create(std::move(tcp.client_ep));
    server_endpoint =
        grpc_event_engine_endpoint_create(std::move(tcp.server_ep));
  }

  grpc_endpoint_add_to_pollset(client_endpoint, g_pollset);
  grpc_endpoint_add_to_pollset(server_endpoint, g_pollset);

  if (leftover_nslices == 0) {
    f.client_ep =
        create_secure_endpoint(client_endpoint, fake_read_protector,
                               fake_read_zero_copy_protector, args, nullptr, 0);
  } else {
    unsigned i;
    tsi_result result;
    size_t still_pending_size;
    size_t total_buffer_size = 8192;
    size_t buffer_size = total_buffer_size;
    uint8_t* encrypted_buffer = static_cast<uint8_t*>(gpr_malloc(buffer_size));
    uint8_t* cur = encrypted_buffer;
    Slice encrypted_leftover;
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
    encrypted_leftover =
        Slice::FromCopiedBuffer(reinterpret_cast<const char*>(encrypted_buffer),
                                total_buffer_size - buffer_size);
    f.client_ep = create_secure_endpoint(client_endpoint, fake_read_protector,
                                         fake_read_zero_copy_protector, args,
                                         &encrypted_leftover, 1);

    gpr_free(encrypted_buffer);
  }

  f.server_ep =
      create_secure_endpoint(server_endpoint, fake_write_protector,
                             fake_write_zero_copy_protector, args, nullptr, 0);
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
  ASSERT_TRUE(grpc_slice_eq(s, incoming.slices[0]));

  grpc_endpoint_destroy(f.client_ep);
  grpc_endpoint_destroy(f.server_ep);

  grpc_slice_unref(s);
  grpc_slice_buffer_destroy(&incoming);

  clean_up();
}

static void destroy_pollset(void* p, grpc_error_handle /*error*/) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(p));
}

TEST(SecureEndpointTest, IomgrEndpointTest) {
  grpc_closure destroyed;
  grpc_init();

  {
    grpc_core::ExecCtx exec_ctx;
    g_thread_pool = MakeThreadPool(8);
    g_pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(g_pollset, &g_mu);

    // Run tests with iomgr-based secure endpoint.
    grpc_endpoint_tests(configs[0], g_pollset, g_mu);
    grpc_endpoint_tests(configs[1], g_pollset, g_mu);
    test_leftover(configs[2], 1);
    test_leftover(configs[3], 1);

    GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, g_pollset,
                      grpc_schedule_on_exec_ctx);
    grpc_pollset_shutdown(g_pollset, &destroyed);
    g_thread_pool->Quiesce();
  }

  grpc_shutdown();

  gpr_free(g_pollset);
}

TEST(SecureEndpointTest, EventEngineEndpointTest) {
  g_event_engine_secure_endpoint_enabled = true;
  grpc_closure destroyed;
  grpc_init();

  {
    grpc_core::ExecCtx exec_ctx;
    g_thread_pool = MakeThreadPool(8);
    g_pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(g_pollset, &g_mu);

    // Run tests with EventEngine-based secure endpoint.
    grpc_endpoint_tests(configs[0], g_pollset, g_mu);
    grpc_endpoint_tests(configs[1], g_pollset, g_mu);
    test_leftover(configs[2], 1);
    test_leftover(configs[3], 1);

    GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, g_pollset,
                      grpc_schedule_on_exec_ctx);
    grpc_pollset_shutdown(g_pollset, &destroyed);
    g_thread_pool->Quiesce();
  }

  grpc_shutdown();

  gpr_free(g_pollset);
  g_event_engine_secure_endpoint_enabled = false;
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
