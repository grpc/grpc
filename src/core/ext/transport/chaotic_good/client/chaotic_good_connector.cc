// Copyright 2024 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/client/chaotic_good_connector.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include "src/core/client_channel/client_channel_factory.h"
#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/ext/transport/chaotic_good/client_transport.h"
#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chaotic_good/settings_metadata.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/event_engine/extensions/chaotic_good_extension.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/promise/wait_for_callback.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_create.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {
using grpc_event_engine::experimental::EventEngine;
namespace {
const int32_t kDataAlignmentBytes = 64;
const int32_t kTimeoutSecs = 120;
}  // namespace

ChaoticGoodConnector::ChaoticGoodConnector(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : event_engine_(std::move(event_engine)),
      handshake_mgr_(MakeRefCounted<HandshakeManager>()) {
  arena_->SetContext<grpc_event_engine::experimental::EventEngine>(
      event_engine_.get());
}

ChaoticGoodConnector::~ChaoticGoodConnector() {
  CHECK_EQ(notify_, nullptr);
  if (connect_activity_ != nullptr) {
    connect_activity_.reset();
  }
}

auto ChaoticGoodConnector::DataEndpointReadSettingsFrame(
    RefCountedPtr<ChaoticGoodConnector> self) {
  return TrySeq(
      self->data_endpoint_.ReadSlice(FrameHeader::kFrameHeaderSize),
      [self](Slice slice) mutable {
        // Read setting frame;
        // Parse frame header
        auto frame_header_ =
            FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
                GRPC_SLICE_START_PTR(slice.c_slice())));
        return If(
            frame_header_.ok(),
            [frame_header_ = *frame_header_, self]() {
              auto frame_header_length = frame_header_.GetFrameLength();
              return TrySeq(self->data_endpoint_.Read(frame_header_length),
                            []() { return absl::OkStatus(); });
            },
            [status = frame_header_.status()]() { return status; });
      });
}

auto ChaoticGoodConnector::DataEndpointWriteSettingsFrame(
    RefCountedPtr<ChaoticGoodConnector> self) {
  // Serialize setting frame.
  SettingsFrame frame;
  // frame.header set connectiion_type: control
  frame.headers = SettingsMetadata{SettingsMetadata::ConnectionType::kData,
                                   self->connection_id_, kDataAlignmentBytes}
                      .ToMetadataBatch();
  bool saw_encoding_errors = false;
  auto write_buffer =
      frame.Serialize(&self->hpack_compressor_, saw_encoding_errors);
  // ignore encoding errors: they will be logged separately already
  return self->data_endpoint_.Write(std::move(write_buffer.control));
}

auto ChaoticGoodConnector::WaitForDataEndpointSetup(
    RefCountedPtr<ChaoticGoodConnector> self) {
  // Data endpoint on_connect callback.
  grpc_event_engine::experimental::EventEngine::OnConnectCallback
      on_data_endpoint_connect =
          [self](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>>
                     endpoint) mutable {
            ExecCtx exec_ctx;
            if (!endpoint.ok() || self->handshake_mgr_ == nullptr) {
              ExecCtx::Run(DEBUG_LOCATION,
                           std::exchange(self->notify_, nullptr),
                           GRPC_ERROR_CREATE("connect endpoint failed"));
              return;
            }
            auto* chaotic_good_ext =
                grpc_event_engine::experimental::QueryExtension<
                    grpc_event_engine::experimental::ChaoticGoodExtension>(
                    endpoint.value().get());
            if (chaotic_good_ext != nullptr) {
              chaotic_good_ext->EnableStatsCollection(
                  /*is_control_channel=*/false);
            }
            self->data_endpoint_ =
                PromiseEndpoint(std::move(endpoint.value()), SliceBuffer());
            self->data_endpoint_ready_.Set();
          };
  self->event_engine_->Connect(
      std::move(on_data_endpoint_connect), *self->resolved_addr_,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(
          self->args_.channel_args),
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
          "data_endpoint_connection"),
      std::chrono::seconds(kTimeoutSecs));

  return TrySeq(Race(
      TrySeq(self->data_endpoint_ready_.Wait(),
             [self]() mutable {
               return TrySeq(DataEndpointWriteSettingsFrame(self),
                             DataEndpointReadSettingsFrame(self),
                             []() -> absl::Status { return absl::OkStatus(); });
             }),
      TrySeq(Sleep(Timestamp::Now() + Duration::Seconds(kTimeoutSecs)),
             []() -> absl::Status {
               return absl::DeadlineExceededError(
                   "Data endpoint connect deadline exceeded.");
             })));
}

auto ChaoticGoodConnector::ControlEndpointReadSettingsFrame(
    RefCountedPtr<ChaoticGoodConnector> self) {
  return TrySeq(
      self->control_endpoint_.ReadSlice(FrameHeader::kFrameHeaderSize),
      [self](Slice slice) {
        // Parse frame header
        auto frame_header = FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
            GRPC_SLICE_START_PTR(slice.c_slice())));
        return If(
            frame_header.ok(),
            TrySeq(
                self->control_endpoint_.Read(frame_header->GetFrameLength()),
                [frame_header = *frame_header, self](SliceBuffer buffer) {
                  // Deserialize setting frame.
                  SettingsFrame frame;
                  BufferPair buffer_pair{std::move(buffer), SliceBuffer()};
                  auto status = frame.Deserialize(
                      &self->hpack_parser_, frame_header,
                      absl::BitGenRef(self->bitgen_), GetContext<Arena>(),
                      std::move(buffer_pair), FrameLimits{});
                  if (!status.ok()) return status;
                  if (frame.headers == nullptr) {
                    return absl::UnavailableError("no settings headers");
                  }
                  auto settings_metadata =
                      SettingsMetadata::FromMetadataBatch(*frame.headers);
                  if (!settings_metadata.ok()) {
                    return settings_metadata.status();
                  }
                  if (!settings_metadata->connection_id.has_value()) {
                    return absl::UnavailableError(
                        "no connection id in settings frame");
                  }
                  self->connection_id_ = *settings_metadata->connection_id;
                  return absl::OkStatus();
                },
                WaitForDataEndpointSetup(self)),
            [status = frame_header.status()]() { return status; });
      });
}

auto ChaoticGoodConnector::ControlEndpointWriteSettingsFrame(
    RefCountedPtr<ChaoticGoodConnector> self) {
  // Serialize setting frame.
  SettingsFrame frame;
  // frame.header set connectiion_type: control
  frame.headers = SettingsMetadata{SettingsMetadata::ConnectionType::kControl,
                                   absl::nullopt, absl::nullopt}
                      .ToMetadataBatch();
  bool saw_encoding_errors = false;
  auto write_buffer =
      frame.Serialize(&self->hpack_compressor_, saw_encoding_errors);
  // ignore encoding errors: they will be logged separately already
  return self->control_endpoint_.Write(std::move(write_buffer.control));
}

void ChaoticGoodConnector::Connect(const Args& args, Result* result,
                                   grpc_closure* notify) {
  {
    MutexLock lock(&mu_);
    result_ = result;
    if (is_shutdown_) {
      CHECK_EQ(notify_, nullptr);
      ExecCtx::Run(DEBUG_LOCATION, notify,
                   GRPC_ERROR_CREATE("connector shutdown"));
      return;
    }
  }
  args_ = args;
  notify_ = notify;
  resolved_addr_ = EventEngine::ResolvedAddress(
      reinterpret_cast<const sockaddr*>(args_.address->addr),
      args_.address->len);
  CHECK_NE(resolved_addr_.value().address(), nullptr);
  grpc_event_engine::experimental::EventEngine::OnConnectCallback on_connect =
      [self = RefAsSubclass<ChaoticGoodConnector>()](
          absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>>
              endpoint) mutable {
        ExecCtx exec_ctx;
        if (!endpoint.ok() || self->handshake_mgr_ == nullptr) {
          auto endpoint_status = endpoint.status();
          auto error = GRPC_ERROR_CREATE_REFERENCING("connect endpoint failed",
                                                     &endpoint_status, 1);
          ExecCtx::Run(DEBUG_LOCATION, std::exchange(self->notify_, nullptr),
                       error);
          return;
        }
        auto* chaotic_good_ext =
            grpc_event_engine::experimental::QueryExtension<
                grpc_event_engine::experimental::ChaoticGoodExtension>(
                endpoint->get());
        if (chaotic_good_ext != nullptr) {
          chaotic_good_ext->EnableStatsCollection(/*is_control_channel=*/true);
          chaotic_good_ext->UseMemoryQuota(
              ResourceQuota::Default()->memory_quota());
        }
        auto* p = self.get();
        p->handshake_mgr_->DoHandshake(
            OrphanablePtr<grpc_endpoint>(
                grpc_event_engine_endpoint_create(std::move(*endpoint))),
            p->args_.channel_args, p->args_.deadline, nullptr /* acceptor */,
            [self = std::move(self)](absl::StatusOr<HandshakerArgs*> result) {
              self->OnHandshakeDone(std::move(result));
            });
      };
  event_engine_->Connect(
      std::move(on_connect), *resolved_addr_,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(
          args_.channel_args),
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
          "data_endpoint_connection"),
      std::chrono::seconds(kTimeoutSecs));
}

void ChaoticGoodConnector::OnHandshakeDone(
    absl::StatusOr<HandshakerArgs*> result) {
  // Start receiving setting frames;
  {
    MutexLock lock(&mu_);
    if (!result.ok() || is_shutdown_) {
      absl::Status error = result.status();
      if (result.ok()) {
        error = GRPC_ERROR_CREATE("connector shutdown");
      }
      result_->Reset();
      ExecCtx::Run(DEBUG_LOCATION, std::exchange(notify_, nullptr), error);
      return;
    }
  }
  if ((*result)->endpoint != nullptr) {
    CHECK(grpc_event_engine::experimental::grpc_is_event_engine_endpoint(
        (*result)->endpoint.get()));
    control_endpoint_ =
        PromiseEndpoint(grpc_event_engine::experimental::
                            grpc_take_wrapped_event_engine_endpoint(
                                (*result)->endpoint.release()),
                        SliceBuffer());
    auto activity = MakeActivity(
        [self = RefAsSubclass<ChaoticGoodConnector>()] {
          return TrySeq(ControlEndpointWriteSettingsFrame(self),
                        ControlEndpointReadSettingsFrame(self),
                        []() { return absl::OkStatus(); });
        },
        EventEngineWakeupScheduler(event_engine_),
        [self = RefAsSubclass<ChaoticGoodConnector>()](absl::Status status) {
          if (GRPC_TRACE_FLAG_ENABLED(chaotic_good)) {
            LOG(INFO) << "ChaoticGoodConnector::OnHandshakeDone: " << status;
          }
          if (status.ok()) {
            MutexLock lock(&self->mu_);
            self->result_->transport = new ChaoticGoodClientTransport(
                std::move(self->control_endpoint_),
                std::move(self->data_endpoint_), self->args_.channel_args,
                self->event_engine_, std::move(self->hpack_parser_),
                std::move(self->hpack_compressor_));
            self->result_->channel_args = self->args_.channel_args;
            ExecCtx::Run(DEBUG_LOCATION, std::exchange(self->notify_, nullptr),
                         status);
          } else if (self->notify_ != nullptr) {
            ExecCtx::Run(DEBUG_LOCATION, std::exchange(self->notify_, nullptr),
                         status);
          }
        },
        arena_);
    MutexLock lock(&mu_);
    if (!is_shutdown_) {
      connect_activity_ = std::move(activity);
    }
  } else {
    // Handshaking succeeded but there is no endpoint.
    MutexLock lock(&mu_);
    result_->Reset();
    auto error = GRPC_ERROR_CREATE("handshake complete with empty endpoint.");
    ExecCtx::Run(
        DEBUG_LOCATION, std::exchange(notify_, nullptr),
        absl::InternalError("handshake complete with empty endpoint."));
  }
}

namespace {

class ChaoticGoodChannelFactory final : public ClientChannelFactory {
 public:
  RefCountedPtr<Subchannel> CreateSubchannel(
      const grpc_resolved_address& address, const ChannelArgs& args) override {
    return Subchannel::Create(
        MakeOrphanable<ChaoticGoodConnector>(
            args.GetObjectRef<grpc_event_engine::experimental::EventEngine>()),
        address, args);
  }
};

}  // namespace
}  // namespace chaotic_good
}  // namespace grpc_core

grpc_channel* grpc_chaotic_good_channel_create(const char* target,
                                               const grpc_channel_args* args) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_chaotic_good_channel_create(target=" << target
      << ",  args=" << (void*)args << ")";
  grpc_channel* channel = nullptr;
  grpc_error_handle error;
  // Create channel.
  auto r = grpc_core::ChannelCreate(
      target,
      grpc_core::CoreConfiguration::Get()
          .channel_args_preconditioning()
          .PreconditionChannelArgs(args)
          .SetObject(grpc_core::NoDestructSingleton<
                     grpc_core::chaotic_good::ChaoticGoodChannelFactory>::Get())
          .Set(GRPC_ARG_USE_V3_STACK, true),
      GRPC_CLIENT_CHANNEL, nullptr);
  if (r.ok()) {
    return r->release()->c_ptr();
  }
  LOG(ERROR) << "Failed to create chaotic good client channel: " << r.status();
  error = absl_status_to_grpc_error(r.status());
  intptr_t integer;
  grpc_status_code status = GRPC_STATUS_INTERNAL;
  if (grpc_error_get_int(error, grpc_core::StatusIntProperty::kRpcStatus,
                         &integer)) {
    status = static_cast<grpc_status_code>(integer);
  }
  channel = grpc_lame_client_channel_create(
      target, status, "Failed to create chaotic good client channel");
  return channel;
}
