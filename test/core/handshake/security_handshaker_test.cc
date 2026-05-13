// Copyright 2026 gRPC authors.
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

#include "src/core/handshaker/security/security_handshaker.h"
#include "src/core/handshaker/security/security_telemetry.h"
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <stdlib.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/tsi/fake_transport_security.h"
#include "test/core/test_util/mock_endpoint.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/transport/auth_context.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include <grpc/event_engine/event_engine.h>
#include "absl/synchronization/notification.h"

namespace grpc_core {
namespace {

using grpc_event_engine::experimental::MockEndpointController;
using grpc_event_engine::experimental::GetDefaultEventEngine;
using grpc_event_engine::experimental::MockEndpoint;
using grpc_event_engine::experimental::grpc_event_engine_endpoint_create;

class MockSecurityConnector : public grpc_security_connector {
 public:
  MockSecurityConnector() : grpc_security_connector("https") {}
  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("mock");
    return kFactory.Create();
  }
  void check_peer(tsi_peer peer, grpc_endpoint* ep, const ChannelArgs& args,
                  RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    abort();
  }
  void cancel_check_peer(grpc_closure* on_peer_checked, grpc_error_handle error) override {}
  int cmp(const grpc_security_connector* other) const override { return 0; }
  
  bool is_client() const override { return true; }
};

class MockSuccessSecurityConnector : public grpc_security_connector {
 public:
  MockSuccessSecurityConnector() : grpc_security_connector("https") {}
  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("mock");
    return kFactory.Create();
  }
  void check_peer(tsi_peer peer, grpc_endpoint* ep, const ChannelArgs& args,
                  RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    *auth_context = MakeRefCounted<grpc_auth_context>(nullptr);
    grpc_auth_context_add_property(auth_context->get(),
                                   GRPC_SSL_SESSION_REUSED_PROPERTY, "true", 4);
    tsi_peer_destruct(&peer);
    ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, absl::OkStatus());
  }
  void cancel_check_peer(grpc_closure* on_peer_checked, grpc_error_handle error) override {}
  int cmp(const grpc_security_connector* other) const override { return 0; }
  
  bool is_client() const override { return true; }
};

class LoopbackMockEndpointController : public grpc_event_engine::experimental::BaseMockEndpointController {
 public:
  explicit LoopbackMockEndpointController(std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine)
      : engine_(std::move(engine)) {}

  ~LoopbackMockEndpointController() override {
    MutexLock lock(&mu_);
    if (on_read_) {
      engine_->Run([cb = std::move(on_read_)]() mutable {
        cb(absl::InternalError("Endpoint Shutdown"));
      });
      on_read_ = nullptr;
    }
  }

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
            grpc_event_engine::experimental::SliceBuffer* buffer,
            grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs args) override {
    MutexLock lock(&mu_);
    if (read_buffer_.Count() > 0) {
      read_buffer_.Swap(*buffer);
      engine_->Run([cb = std::move(on_read)]() mutable { cb(absl::OkStatus()); });
    } else {
      on_read_ = std::move(on_read);
      on_read_slice_buffer_ = buffer;
    }
    return false;
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             grpc_event_engine::experimental::SliceBuffer* data,
             grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs args) override {
    if (data->Count() > 0) {
      std::string written_data;
      for (size_t i = 0; i < data->Count(); ++i) {
        auto slice = data->RefSlice(i);
        written_data.append(reinterpret_cast<const char*>(slice.data()), slice.size());
      }
      data->Clear();

      if (written_data.find("CLIENT_INIT") != std::string::npos) {
        unsigned char frame[15];
        frame[0] = 15; frame[1] = 0; frame[2] = 0; frame[3] = 0;
        memcpy(frame + 4, "SERVER_INIT", 11);
        
        MutexLock lock(&mu_);
        if (on_read_) {
          on_read_slice_buffer_->Append(grpc_event_engine::experimental::Slice::FromCopiedBuffer(reinterpret_cast<const char*>(frame), 15));
          engine_->Run([cb = std::move(on_read_)]() mutable { cb(absl::OkStatus()); });
          on_read_ = nullptr;
          on_read_slice_buffer_ = nullptr;
        } else {
          read_buffer_.Append(grpc_event_engine::experimental::Slice::FromCopiedBuffer(reinterpret_cast<const char*>(frame), 15));
        }
      } else if (written_data.find("CLIENT_FINISHED") != std::string::npos) {
        unsigned char frame[19];
        frame[0] = 19; frame[1] = 0; frame[2] = 0; frame[3] = 0;
        memcpy(frame + 4, "SERVER_FINISHED", 15);
        
        MutexLock lock(&mu_);
        if (on_read_) {
          on_read_slice_buffer_->Append(grpc_event_engine::experimental::Slice::FromCopiedBuffer(reinterpret_cast<const char*>(frame), 19));
          engine_->Run([cb = std::move(on_read_)]() mutable { cb(absl::OkStatus()); });
          on_read_ = nullptr;
          on_read_slice_buffer_ = nullptr;
        } else {
          read_buffer_.Append(grpc_event_engine::experimental::Slice::FromCopiedBuffer(reinterpret_cast<const char*>(frame), 19));
        }
      }
    }

    engine_->Run([cb = std::move(on_writable)]() mutable { cb(absl::OkStatus()); });
    return false;
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine_;
  Mutex mu_;
  grpc_event_engine::experimental::SliceBuffer read_buffer_ ABSL_GUARDED_BY(mu_);
  absl::AnyInvocable<void(absl::Status)> on_read_ ABSL_GUARDED_BY(mu_);
  grpc_event_engine::experimental::SliceBuffer* on_read_slice_buffer_ ABSL_GUARDED_BY(mu_) = nullptr;
};

TEST(SecurityHandshakerTest, MetricEmissionOnFailure) {
  ExecCtx exec_ctx; // Needed for gRPC core operations
  
  // Register hook capturing shared_ptr by value to avoid dangling references
  auto hook_called = std::make_shared<bool>(false);
  RegisterHistogramCollectionHook([hook_called](const InstrumentMetadata::Description* instrument,
                                                 absl::Span<const std::string> labels, int64_t value) {
    if (instrument->name == "grpc.security.client.handshaker.duration") {
      if (!*hook_called) {
        *hook_called = true;
        // We expect 4 labels: status, target, protocol, resumed
        ASSERT_EQ(labels.size(), 4);
        // Status should be non-OK
        EXPECT_NE(labels[0], "OK");
        // Target should be "unknown"
        EXPECT_EQ(labels[1], "unknown");
        // Protocol should be "mock" (from connector_->type().name())
        EXPECT_EQ(labels[2], "mock");
        // Resumed should be "false"
        EXPECT_EQ(labels[3], "false");
      }
    }
  });

  // Create mock endpoint to avoid segfaults on read/write
  auto engine = GetDefaultEventEngine();
  auto mock_ctrl = MockEndpointController::Create(engine);
  grpc_endpoint* mock_ep = mock_ctrl->TakeCEndpoint();

  // Create handshaker using fake TSI handshaker
  auto connector = MakeRefCounted<MockSecurityConnector>();
  auto handshaker = SecurityHandshakerCreate(tsi_create_fake_handshaker(1 /* is_client */), connector.get(), ChannelArgs());

  // Force read failure to trigger handshake failure and metric emission
  mock_ctrl->NoMoreReads();

  // Drive handshake with heap-allocated args
  auto args = std::make_shared<HandshakerArgs>();
  args->args = ChannelArgs();
  args->endpoint = OrphanablePtr<grpc_endpoint>(mock_ep);
  args->event_engine = engine.get();
  
  absl::Notification done;
  handshaker->DoHandshake(args.get(), [&done, args, connector, handshaker](absl::Status status) {
    done.Notify();
  });

  // Wait for it to complete. It should fail due to NoMoreReads.
  EXPECT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(5)));
  EXPECT_TRUE(*hook_called);
}

TEST(SecurityHandshakerTest, MetricEmissionOnSuccess) {
  ExecCtx exec_ctx; // Needed for gRPC core operations
  
  // Register hook capturing shared_ptr by value to avoid dangling references
  auto hook_called = std::make_shared<bool>(false);
  RegisterHistogramCollectionHook([hook_called](const InstrumentMetadata::Description* instrument,
                                                 absl::Span<const std::string> labels, int64_t value) {
    if (instrument->name == "grpc.security.client.handshaker.duration") {
      if (!*hook_called) {
        *hook_called = true;
        // We expect 4 labels: status, target, protocol, resumed
        ASSERT_EQ(labels.size(), 4);
        // Status should be OK
        EXPECT_EQ(labels[0], "OK");
        // Target should be "dns:///localhost:50051" (from args_->args.GetString(GRPC_ARG_SERVER_URI))
        EXPECT_EQ(labels[1], "dns:///localhost:50051");
        // Protocol should be "mock"
        EXPECT_EQ(labels[2], "mock");
        // Resumed should be "true" (because we set it to true in check_peer)
        EXPECT_EQ(labels[3], "true");
      }
    }
  });

  auto engine = GetDefaultEventEngine();
  auto loopback_ctrl = std::make_shared<LoopbackMockEndpointController>(engine);
  grpc_endpoint* mock_ep = grpc_event_engine_endpoint_create(
      std::make_unique<MockEndpoint>(loopback_ctrl));

  auto connector = MakeRefCounted<MockSuccessSecurityConnector>();
  auto handshaker = SecurityHandshakerCreate(tsi_create_fake_handshaker(1 /* is_client */), connector.get(), ChannelArgs());

  // Drive handshake with heap-allocated args
  auto args = std::make_shared<HandshakerArgs>();
  args->args = ChannelArgs()
                   .Set(GRPC_ARG_SERVER_URI, "dns:///localhost:50051")
                   .SetObject<ResourceQuota>(ResourceQuota::Default());
  args->endpoint = OrphanablePtr<grpc_endpoint>(mock_ep);
  args->event_engine = engine.get();
  
  absl::Notification done;
  handshaker->DoHandshake(args.get(), [&done, loopback_ctrl, connector, handshaker, args](absl::Status status) {
    EXPECT_TRUE(status.ok());
    done.Notify();
  });

  // Wait for it to complete. It should succeed.
  EXPECT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(5)));
  EXPECT_TRUE(*hook_called);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  // Create a dummy root scope to register labels of interest so they are not filtered in the test!
  auto dummy_scope = grpc_core::CreateRootCollectionScope({
      "grpc.security.handshaker.status",
      "grpc.target",
      "grpc.security.handshaker.protocol",
      "grpc.security.handshaker.resumed"
  });
  grpc_init(); // Needed to initialize gRPC types and systems
  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
