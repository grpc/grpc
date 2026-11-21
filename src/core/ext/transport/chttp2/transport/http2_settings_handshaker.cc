//
//
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
//
//

#include "src/core/ext/transport/chttp2/transport/http2_settings_handshaker.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/legacy_frame.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {

namespace {

class Http2SettingsHandshaker final : public Handshaker {
 public:
  explicit Http2SettingsHandshaker(bool is_client)
      : is_client_(is_client), prefix_read_(is_client) {}

  absl::string_view name() const override { return "http2_settings"; }

  void DoHandshake(
      HandshakerArgs* args,
      absl::AnyInvocable<void(absl::Status)> on_handshake_done) override;

  void Shutdown(absl::Status error) override;

 private:
  static void OnWriteDoneScheduler(void* arg, grpc_error_handle error);
  static void OnReadDoneScheduler(void* arg, grpc_error_handle error);

  void OnWriteDoneLocked(absl::Status error) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void OnReadDoneLocked(absl::Status error) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void StartReadLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void HandshakeFailedLocked(absl::Status error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void MaybeFinishLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  const bool is_client_;

  Mutex mu_;
  HandshakerArgs* args_ = nullptr;
  absl::AnyInvocable<void(absl::Status)> on_handshake_done_
      ABSL_GUARDED_BY(mu_);

  Http2SettingsManager settings_ ABSL_GUARDED_BY(mu_);
  SliceBuffer write_buffer_ ABSL_GUARDED_BY(mu_);
  SliceBuffer read_buffer_ ABSL_GUARDED_BY(mu_);
  bool prefix_read_ ABSL_GUARDED_BY(mu_);
  std::optional<Http2FrameHeader> frame_header_ ABSL_GUARDED_BY(mu_);

  grpc_closure on_write_done_scheduler_ ABSL_GUARDED_BY(mu_);
  grpc_closure on_read_done_scheduler_ ABSL_GUARDED_BY(mu_);

  bool write_done_ ABSL_GUARDED_BY(mu_) = false;
  bool read_done_ ABSL_GUARDED_BY(mu_) = false;
  absl::Status status_ ABSL_GUARDED_BY(mu_);
};

void Http2SettingsHandshaker::DoHandshake(
    HandshakerArgs* args,
    absl::AnyInvocable<void(absl::Status)> on_handshake_done) {
  if (args->exit_early || args->endpoint == nullptr) {
    InvokeOnHandshakeDone(args, std::move(on_handshake_done), absl::OkStatus());
    return;
  }
  MutexLock lock(mu_);
  args_ = args;
  on_handshake_done_ = std::move(on_handshake_done);
  // Initialize local settings from channel args.
  InitLocalSettings(settings_.mutable_local(), is_client_);
  ReadSettingsFromChannelArgs(args_->args, settings_.mutable_local(),
                              is_client_);
  // Prepare outgoing connection preface (client only) and initial SETTINGS
  // frame.
  if (is_client_) {
    write_buffer_.Append(
        Slice::FromStaticString(GRPC_CHTTP2_CLIENT_CONNECT_STRING));
  }
  std::optional<Http2SettingsFrame> settings_frame =
      settings_.MaybeSendUpdate();
  GRPC_CHECK(settings_frame.has_value());
  Http2Frame frame(std::move(*settings_frame));
  Serialize(absl::Span<Http2Frame>(&frame, 1), write_buffer_);
  // Start write.
  grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs write_args;
  write_args.set_max_frame_size(INT_MAX);
  grpc_endpoint_write(
      args_->endpoint.get(), write_buffer_.c_slice_buffer(),
      GRPC_CLOSURE_INIT(&on_write_done_scheduler_, &OnWriteDoneScheduler,
                        Ref().release(),  // released by callback
                        grpc_schedule_on_exec_ctx),
      std::move(write_args));
  // Process any bytes already in args_->read_buffer and start reading if
  // needed.
  OnReadDoneLocked(absl::OkStatus());
}

void Http2SettingsHandshaker::Shutdown(absl::Status /*error*/) {
  MutexLock lock(mu_);
  if (on_handshake_done_ != nullptr) args_->endpoint.reset();
}

void Http2SettingsHandshaker::OnWriteDoneScheduler(void* arg,
                                                   grpc_error_handle error) {
  auto* self = static_cast<Http2SettingsHandshaker*>(arg);
  self->args_->event_engine->Run(
      [self = RefCountedPtr<Http2SettingsHandshaker>(self),
       error = std::move(error)]() mutable {
        ExecCtx exec_ctx;
        {
          MutexLock lock(self->mu_);
          self->OnWriteDoneLocked(std::move(error));
        }
        self.reset();  // while ExecCtx in scope
      });
}

void Http2SettingsHandshaker::OnWriteDoneLocked(absl::Status error) {
  write_done_ = true;
  write_buffer_.Clear();
  if (!error.ok() || args_->endpoint == nullptr) {
    HandshakeFailedLocked(std::move(error));
  } else {
    MaybeFinishLocked();
  }
}

void Http2SettingsHandshaker::OnReadDoneScheduler(void* arg,
                                                  grpc_error_handle error) {
  auto* self = static_cast<Http2SettingsHandshaker*>(arg);
  self->args_->event_engine->Run(
      [self = RefCountedPtr<Http2SettingsHandshaker>(self),
       error = std::move(error)]() mutable {
        ExecCtx exec_ctx;
        {
          MutexLock lock(self->mu_);
          self->OnReadDoneLocked(std::move(error));
        }
        self.reset();  // while ExecCtx in scope
      });
}

void Http2SettingsHandshaker::OnReadDoneLocked(absl::Status error) {
  if (!error.ok() || args_->endpoint == nullptr) {
    read_done_ = true;
    HandshakeFailedLocked(std::move(error));
    return;
  }
  // Append any newly read slices into read_buffer_.
  read_buffer_.TakeAndAppend(args_->read_buffer);
  // On the server, validate and consume the 24-byte client connection preface.
  if (!prefix_read_) {
    const size_t len =
        std::min(read_buffer_.Length(), GRPC_CHTTP2_CLIENT_CONNECT_STRLEN);
    uint8_t buf[GRPC_CHTTP2_CLIENT_CONNECT_STRLEN];
    read_buffer_.CopyFirstNBytesIntoBuffer(len, buf);
    if (memcmp(buf, GRPC_CHTTP2_CLIENT_CONNECT_STRING, len) != 0) {
      read_done_ = true;
      HandshakeFailedLocked(
          GRPC_ERROR_CREATE("Connect string mismatch in HTTP/2 preface"));
      return;
    }
    if (len < GRPC_CHTTP2_CLIENT_CONNECT_STRLEN) {
      StartReadLocked();
      return;
    }
    read_buffer_.MoveFirstNBytesIntoBuffer(GRPC_CHTTP2_CLIENT_CONNECT_STRLEN,
                                           buf);
    prefix_read_ = true;
  }
  // Read and validate the 9-byte SETTINGS frame header.
  if (!frame_header_.has_value()) {
    if (read_buffer_.Length() < kFrameHeaderSize) {
      StartReadLocked();
      return;
    }
    uint8_t buf[kFrameHeaderSize];
    read_buffer_.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, buf);
    Http2FrameHeader hdr = Http2FrameHeader::Parse(buf);
    if (hdr.type != GRPC_CHTTP2_FRAME_SETTINGS || hdr.flags != 0) {
      read_done_ = true;
      HandshakeFailedLocked(GRPC_ERROR_CREATE(absl::StrFormat(
          "Expected SETTINGS frame as the first frame, got frame type %d, "
          "flags %d",
          hdr.type, hdr.flags)));
      return;
    }
    if (hdr.stream_id != 0) {
      read_done_ = true;
      HandshakeFailedLocked(
          GRPC_ERROR_CREATE("Settings frame received for non-zero stream"));
      return;
    }
    if (hdr.length % 6 != 0) {
      read_done_ = true;
      HandshakeFailedLocked(
          GRPC_ERROR_CREATE("settings frames must be a multiple of six bytes"));
      return;
    }
    if (hdr.length > settings_.acked().max_frame_size()) {
      read_done_ = true;
      HandshakeFailedLocked(GRPC_ERROR_CREATE(
          absl::StrFormat("Frame size %d larger than max frame size %d",
                          hdr.length, settings_.acked().max_frame_size())));
      return;
    }
    read_buffer_.MoveFirstNBytesIntoBuffer(kFrameHeaderSize, buf);
    frame_header_ = hdr;
  }
  // Wait for the full SETTINGS frame payload, then parse and apply it.
  if (read_buffer_.Length() < frame_header_->length) {
    StartReadLocked();
    return;
  }
  SliceBuffer payload;
  read_buffer_.MoveFirstNBytesIntoSliceBuffer(frame_header_->length, payload);
  http2::ValueOrHttp2Status<Http2Frame> frame =
      ParseFramePayload(*frame_header_, std::move(payload));
  if (!frame.IsOk()) {
    read_done_ = true;
    HandshakeFailedLocked(GRPC_ERROR_CREATE(frame.DebugString()));
    return;
  }
  auto& settings_frame = std::get<Http2SettingsFrame>(frame.value());
  http2::Http2ErrorCode apply_err =
      settings_.ApplyIncomingSettings(settings_frame.settings);
  if (apply_err != http2::Http2ErrorCode::kNoError) {
    read_done_ = true;
    HandshakeFailedLocked(GRPC_ERROR_CREATE(
        absl::StrFormat("invalid settings frame: %s",
                        http2::Http2Status::DebugGetCode(apply_err))));
    return;
  }
  read_done_ = true;
  MaybeFinishLocked();
}

void Http2SettingsHandshaker::StartReadLocked() {
  grpc_endpoint_read(
      args_->endpoint.get(), args_->read_buffer.c_slice_buffer(),
      GRPC_CLOSURE_INIT(&on_read_done_scheduler_, &OnReadDoneScheduler,
                        Ref().release(),  // released by callback
                        grpc_schedule_on_exec_ctx),
      /*urgent=*/true, /*min_progress_size=*/1);
}

void Http2SettingsHandshaker::HandshakeFailedLocked(absl::Status error) {
  if (error.ok()) {
    // If we were shut down after an endpoint operation succeeded but before
    // the endpoint callback was invoked, generate our own error.
    error = GRPC_ERROR_CREATE("Handshaker shutdown");
  } else if (is_client_ && read_buffer_.Length() > 0) {
    grpc_error_handle http1_error =
        grpc_chttp2_try_http1_parsing(*read_buffer_.c_slice_buffer());
    if (!http1_error.ok()) {
      grpc_error_handle errors[2] = {error, http1_error};
      error = GRPC_ERROR_CREATE_REFERENCING("Failed parsing HTTP/2", errors, 2);
    }
  }
  if (status_.ok()) status_ = std::move(error);
  args_->endpoint.reset();
  MaybeFinishLocked();
}

void Http2SettingsHandshaker::MaybeFinishLocked() {
  if (!write_done_ || !read_done_) return;
  if (status_.ok()) {
    read_buffer_.Swap(&args_->read_buffer);
    args_->args =
        args_->args.SetObject(MakeRefCounted<Http2SettingsHandshakeResult>(
            settings_.local(), settings_.peer()));
  }
  InvokeOnHandshakeDone(args_, std::move(on_handshake_done_),
                        std::move(status_));
}

}  // namespace

RefCountedPtr<Handshaker> MakeHttp2SettingsHandshaker(bool is_client) {
  return MakeRefCounted<Http2SettingsHandshaker>(is_client);
}

}  // namespace grpc_core
