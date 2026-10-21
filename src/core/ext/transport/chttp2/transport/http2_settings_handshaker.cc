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
#include <grpc/support/port_platform.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/frame_settings.h"
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
#include "src/core/util/status_helper.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {

namespace {

using http2::Http2ErrorCode;

class Http2SettingsHandshaker final : public Handshaker {
 public:
  explicit Http2SettingsHandshaker(bool is_client)
      : is_client_(is_client),
        read_state_(is_client ? ReadState::kReadingSettingsFrameHeader
                              : ReadState::kReadingClientPreface) {}

  absl::string_view name() const override { return "http2_settings"; }

  void DoHandshake(
      HandshakerArgs* args,
      absl::AnyInvocable<void(absl::Status)> on_handshake_done) override;

  void Shutdown(absl::Status error) override;

 private:
  enum class ReadState {
    kReadingClientPreface,
    kReadingSettingsFrameHeader,
    kReadingSettingsFramePayload,
    kDone,
  };

  static void OnWriteDoneScheduler(void* arg, grpc_error_handle error);
  static void OnReadDoneScheduler(void* arg, grpc_error_handle error);

  void OnWriteDone(absl::Status error);
  void OnReadDone(absl::Status error);

  void StartReadLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  absl::Status ProcessReadBufferLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void RecordErrorLocked(absl::Status error) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // Returns true if the handshake is completely finished and the caller should
  // invoke on_handshake_done outside the lock.
  bool MaybeFinishLocked(
      absl::AnyInvocable<void(absl::Status)>* on_handshake_done,
      absl::Status* status) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  const bool is_client_;

  Mutex mu_;
  HandshakerArgs* args_ = nullptr;
  absl::AnyInvocable<void(absl::Status)> on_handshake_done_
      ABSL_GUARDED_BY(mu_);

  Http2SettingsManager settings_ ABSL_GUARDED_BY(mu_);
  SliceBuffer write_buffer_ ABSL_GUARDED_BY(mu_);
  SliceBuffer read_bytes_for_http1_check_ ABSL_GUARDED_BY(mu_);

  grpc_closure on_write_done_scheduler_ ABSL_GUARDED_BY(mu_);
  grpc_closure on_read_done_scheduler_ ABSL_GUARDED_BY(mu_);

  bool write_in_progress_ ABSL_GUARDED_BY(mu_) = false;
  bool read_in_progress_ ABSL_GUARDED_BY(mu_) = false;
  bool write_done_ ABSL_GUARDED_BY(mu_) = false;
  bool read_done_ ABSL_GUARDED_BY(mu_) = false;
  bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;
  bool sent_goaway_ ABSL_GUARDED_BY(mu_) = false;
  absl::Status overall_status_ ABSL_GUARDED_BY(mu_);

  ReadState read_state_ ABSL_GUARDED_BY(mu_);
  size_t prefix_bytes_read_ ABSL_GUARDED_BY(mu_) = 0;
  uint8_t frame_header_buf_[9] ABSL_GUARDED_BY(mu_);
  size_t frame_header_bytes_read_ ABSL_GUARDED_BY(mu_) = 0;
  uint32_t frame_length_ ABSL_GUARDED_BY(mu_) = 0;
  uint8_t frame_type_ ABSL_GUARDED_BY(mu_) = 0;
  uint8_t frame_flags_ ABSL_GUARDED_BY(mu_) = 0;
  uint32_t frame_stream_id_ ABSL_GUARDED_BY(mu_) = 0;
  uint32_t payload_bytes_remaining_ ABSL_GUARDED_BY(mu_) = 0;
  grpc_chttp2_settings_parser settings_parser_ ABSL_GUARDED_BY(mu_);
};

void Http2SettingsHandshaker::DoHandshake(
    HandshakerArgs* args,
    absl::AnyInvocable<void(absl::Status)> on_handshake_done) {
  absl::AnyInvocable<void(absl::Status)> done_cb;
  absl::Status done_status;
  bool finished = false;
  {
    MutexLock lock(mu_);
    if (args->exit_early || args->endpoint == nullptr) {
      done_cb = std::move(on_handshake_done);
      done_status = absl::OkStatus();
      finished = true;
    } else if (is_shutdown_) {
      done_cb = std::move(on_handshake_done);
      done_status = overall_status_;
      finished = true;
    } else {
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

      // Start asynchronous write.
      write_in_progress_ = true;
      Ref().release();  // Ref held by OnWriteDoneScheduler
      grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs
          write_args;
      write_args.set_max_frame_size(INT_MAX);
      grpc_endpoint_write(
          args_->endpoint.get(), write_buffer_.c_slice_buffer(),
          GRPC_CLOSURE_INIT(&on_write_done_scheduler_,
                            &Http2SettingsHandshaker::OnWriteDoneScheduler,
                            this, grpc_schedule_on_exec_ctx),
          std::move(write_args));

      // Process any leftover bytes already in args_->read_buffer before
      // initiating an endpoint read.
      if (is_client_) {
        for (size_t i = 0; i < args_->read_buffer.Count(); ++i) {
          read_bytes_for_http1_check_.Append(args_->read_buffer.RefSlice(i));
        }
      }
      absl::Status read_status = ProcessReadBufferLocked();
      if (!read_status.ok()) {
        RecordErrorLocked(std::move(read_status));
        read_done_ = true;
        finished = MaybeFinishLocked(&done_cb, &done_status);
      } else if (read_state_ == ReadState::kDone) {
        read_bytes_for_http1_check_.Clear();
        read_done_ = true;
        finished = MaybeFinishLocked(&done_cb, &done_status);
      } else {
        StartReadLocked();
      }
    }
  }
  if (finished) {
    InvokeOnHandshakeDone(args, std::move(done_cb), std::move(done_status));
  }
}

void Http2SettingsHandshaker::Shutdown(absl::Status error) {
  MutexLock lock(mu_);
  if (!is_shutdown_) {
    is_shutdown_ = true;
    RecordErrorLocked(error.ok() ? GRPC_ERROR_CREATE("Handshaker shutdown")
                                 : std::move(error));
    if (on_handshake_done_ != nullptr && args_ != nullptr) {
      args_->endpoint.reset();
    }
  }
}

void Http2SettingsHandshaker::OnWriteDoneScheduler(void* arg,
                                                   grpc_error_handle error) {
  auto* self = static_cast<Http2SettingsHandshaker*>(arg);
  self->args_->event_engine->Run([self, error = std::move(error)]() mutable {
    ExecCtx exec_ctx;
    self->OnWriteDone(std::move(error));
  });
}

void Http2SettingsHandshaker::OnWriteDone(absl::Status error) {
  HandshakerArgs* args = nullptr;
  absl::AnyInvocable<void(absl::Status)> done_cb;
  absl::Status done_status;
  bool finished = false;
  {
    MutexLock lock(mu_);
    args = args_;
    write_in_progress_ = false;
    write_done_ = true;
    write_buffer_.Clear();
    if (!error.ok() || args_->endpoint == nullptr) {
      if (!sent_goaway_) {
        RecordErrorLocked(error.ok() ? GRPC_ERROR_CREATE("Handshaker shutdown")
                                     : std::move(error));
      }
      if (read_in_progress_ && args_->endpoint != nullptr) {
        args_->endpoint.reset();
      }
    }
    finished = MaybeFinishLocked(&done_cb, &done_status);
  }
  Unref();
  if (finished) {
    InvokeOnHandshakeDone(args, std::move(done_cb), std::move(done_status));
  }
}

void Http2SettingsHandshaker::OnReadDoneScheduler(void* arg,
                                                  grpc_error_handle error) {
  auto* self = static_cast<Http2SettingsHandshaker*>(arg);
  self->args_->event_engine->Run([self, error = std::move(error)]() mutable {
    ExecCtx exec_ctx;
    self->OnReadDone(std::move(error));
  });
}

void Http2SettingsHandshaker::OnReadDone(absl::Status error) {
  HandshakerArgs* args = nullptr;
  absl::AnyInvocable<void(absl::Status)> done_cb;
  absl::Status done_status;
  bool finished = false;
  {
    MutexLock lock(mu_);
    args = args_;
    read_in_progress_ = false;
    if (!error.ok() || args_->endpoint == nullptr) {
      RecordErrorLocked(error.ok() ? GRPC_ERROR_CREATE("Handshaker shutdown")
                                   : std::move(error));
      read_done_ = true;
      if (write_in_progress_ && args_->endpoint != nullptr) {
        args_->endpoint.reset();
      }
      finished = MaybeFinishLocked(&done_cb, &done_status);
    } else {
      if (is_client_) {
        for (size_t i = 0; i < args_->read_buffer.Count(); ++i) {
          read_bytes_for_http1_check_.Append(args_->read_buffer.RefSlice(i));
        }
      }
      absl::Status parse_status = ProcessReadBufferLocked();
      if (!parse_status.ok()) {
        RecordErrorLocked(std::move(parse_status));
        read_done_ = true;
        intptr_t unused_http2_error;
        if (write_in_progress_ && args_->endpoint != nullptr &&
            !grpc_error_get_int(overall_status_, StatusIntProperty::kHttp2Error,
                                &unused_http2_error)) {
          args_->endpoint.reset();
        }
        finished = MaybeFinishLocked(&done_cb, &done_status);
      } else if (read_state_ == ReadState::kDone) {
        read_bytes_for_http1_check_.Clear();
        read_done_ = true;
        finished = MaybeFinishLocked(&done_cb, &done_status);
      } else {
        StartReadLocked();
      }
    }
  }
  Unref();
  if (finished) {
    InvokeOnHandshakeDone(args, std::move(done_cb), std::move(done_status));
  }
}

void Http2SettingsHandshaker::StartReadLocked() {
  GRPC_CHECK(!read_in_progress_);
  args_->read_buffer.Clear();
  read_in_progress_ = true;
  Ref().release();  // Ref held by OnReadDoneScheduler
  grpc_endpoint_read(
      args_->endpoint.get(), args_->read_buffer.c_slice_buffer(),
      GRPC_CLOSURE_INIT(&on_read_done_scheduler_,
                        &Http2SettingsHandshaker::OnReadDoneScheduler, this,
                        grpc_schedule_on_exec_ctx),
      /*urgent=*/true, /*min_progress_size=*/1);
}

absl::Status Http2SettingsHandshaker::ProcessReadBufferLocked() {
  while (args_->read_buffer.Count() > 0 && read_state_ != ReadState::kDone) {
    Slice slice = args_->read_buffer.TakeFirst();
    if (slice.empty()) continue;
    const uint8_t* cur = slice.begin();
    const uint8_t* end = slice.end();

    while (cur < end && read_state_ != ReadState::kDone) {
      switch (read_state_) {
        case ReadState::kReadingClientPreface: {
          while (cur < end &&
                 prefix_bytes_read_ < GRPC_CHTTP2_CLIENT_CONNECT_STRLEN) {
            if (*cur != static_cast<uint8_t>(
                            GRPC_CHTTP2_CLIENT_CONNECT_STRING[prefix_bytes_read_])) {
              return GRPC_ERROR_CREATE(absl::StrFormat(
                  "Connect string mismatch: expected '%c' (%d) got '%c' (%d) "
                  "at byte %d",
                  GRPC_CHTTP2_CLIENT_CONNECT_STRING[prefix_bytes_read_],
                  static_cast<int>( static_cast<uint8_t>(
                      GRPC_CHTTP2_CLIENT_CONNECT_STRING[prefix_bytes_read_])),
                  *cur, static_cast<int>(*cur),
                  static_cast<int>(prefix_bytes_read_)));
            }
            ++cur;
            ++prefix_bytes_read_;
          }
          if (prefix_bytes_read_ == GRPC_CHTTP2_CLIENT_CONNECT_STRLEN) {
            read_state_ = ReadState::kReadingSettingsFrameHeader;
          }
          break;
        }
        case ReadState::kReadingSettingsFrameHeader: {
          while (cur < end && frame_header_bytes_read_ < 9) {
            frame_header_buf_[frame_header_bytes_read_++] = *cur++;
          }
          if (frame_header_bytes_read_ == 9) {
            frame_length_ =
                (static_cast<uint32_t>(frame_header_buf_[0]) << 16) |
                (static_cast<uint32_t>(frame_header_buf_[1]) << 8) |
                static_cast<uint32_t>(frame_header_buf_[2]);
            frame_type_ = frame_header_buf_[3];
            frame_flags_ = frame_header_buf_[4];
            frame_stream_id_ =
                ((static_cast<uint32_t>(frame_header_buf_[5]) & 0x7f) << 24) |
                (static_cast<uint32_t>(frame_header_buf_[6]) << 16) |
                (static_cast<uint32_t>(frame_header_buf_[7]) << 8) |
                static_cast<uint32_t>(frame_header_buf_[8]);
            if (frame_type_ != GRPC_CHTTP2_FRAME_SETTINGS ||
                frame_flags_ != 0) {
              return grpc_error_set_int(
                  GRPC_ERROR_CREATE(absl::StrFormat(
                      "Expected SETTINGS frame as the first frame, got frame "
                      "type %d, flags %d",
                      frame_type_, frame_flags_)),
                  StatusIntProperty::kHttp2Error,
                  static_cast<intptr_t>(Http2ErrorCode::kProtocolError));
            }
            if (frame_stream_id_ != 0) {
              return grpc_error_set_int(
                  GRPC_ERROR_CREATE(
                      "Settings frame received for non-zero stream"),
                  StatusIntProperty::kHttp2Error,
                  static_cast<intptr_t>(Http2ErrorCode::kProtocolError));
            }
            if (frame_length_ > settings_.acked().max_frame_size()) {
              return grpc_error_set_int(
                  GRPC_ERROR_CREATE(
                      absl::StrFormat("Frame size %d larger than max frame "
                                      "size %d",
                                      frame_length_,
                                      settings_.acked().max_frame_size())),
                  StatusIntProperty::kHttp2Error,
                  static_cast<intptr_t>(Http2ErrorCode::kFrameSizeError));
            }
            grpc_error_handle err = grpc_chttp2_settings_parser_begin_frame(
                &settings_parser_, frame_length_, frame_flags_,
                settings_.mutable_peer());
            if (!err.ok()) {
              return grpc_error_set_int(
                  err, StatusIntProperty::kHttp2Error,
                  static_cast<intptr_t>(Http2ErrorCode::kFrameSizeError));
            }
            payload_bytes_remaining_ = frame_length_;
            if (payload_bytes_remaining_ == 0) {
              err = grpc_chttp2_settings_parser_parse(
                  &settings_parser_, /*t=*/nullptr, /*s=*/nullptr,
                  grpc_empty_slice(), /*is_last=*/1);
              if (!err.ok()) return err;
              read_state_ = ReadState::kDone;
            } else {
              read_state_ = ReadState::kReadingSettingsFramePayload;
            }
          }
          break;
        }
        case ReadState::kReadingSettingsFramePayload: {
          const size_t avail = static_cast<size_t>(end - cur);
          const size_t take =
              std::min(avail, static_cast<size_t>(payload_bytes_remaining_));
          const size_t offset = static_cast<size_t>(cur - slice.begin());
          Slice payload_slice = slice.RefSubSlice(offset, take);
          cur += take;
          payload_bytes_remaining_ -= static_cast<uint32_t>(take);
          const int is_last = (payload_bytes_remaining_ == 0) ? 1 : 0;
          grpc_error_handle err = grpc_chttp2_settings_parser_parse(
              &settings_parser_, /*t=*/nullptr, /*s=*/nullptr,
              payload_slice.c_slice(), is_last);
          if (!err.ok()) return err;
          if (is_last) {
            read_state_ = ReadState::kDone;
          }
          break;
        }
        case ReadState::kDone:
          break;
      }
    }

    // If we finished reading the SETTINGS frame and there are leftover bytes in
    // this slice, put them back at the front of args_->read_buffer.
    if (cur < end) {
      const size_t offset = static_cast<size_t>(cur - slice.begin());
      const size_t remaining = static_cast<size_t>(end - cur);
      SliceBuffer remaining_buffer;
      remaining_buffer.Append(slice.RefSubSlice(offset, remaining));
      remaining_buffer.TakeAndAppend(args_->read_buffer);
      remaining_buffer.Swap(&args_->read_buffer);
    }
  }
  return absl::OkStatus();
}

void Http2SettingsHandshaker::RecordErrorLocked(absl::Status error) {
  if (!overall_status_.ok()) return;
  if (is_client_ && read_bytes_for_http1_check_.Count() > 0) {
    grpc_error_handle http1_error = grpc_chttp2_try_http_parsing(
        *read_bytes_for_http1_check_.c_slice_buffer());
    if (!http1_error.ok()) {
      grpc_error_handle errors[2] = {error, http1_error};
      error = GRPC_ERROR_CREATE_REFERENCING("Failed parsing HTTP/2", errors, 2);
    }
    read_bytes_for_http1_check_.Clear();
  }
  overall_status_ = std::move(error);
}

bool Http2SettingsHandshaker::MaybeFinishLocked(
    absl::AnyInvocable<void(absl::Status)>* on_handshake_done,
    absl::Status* status) {
  if (!write_done_ || !read_done_) {
    return false;
  }
  if (!overall_status_.ok() && !is_shutdown_ && !sent_goaway_ &&
      args_->endpoint != nullptr) {
    intptr_t http2_error = 0;
    if (grpc_error_get_int(overall_status_, StatusIntProperty::kHttp2Error,
                           &http2_error)) {
      sent_goaway_ = true;
      write_done_ = false;
      write_in_progress_ = true;
      Http2Frame goaway_frame(Http2GoawayFrame{
          0, static_cast<uint32_t>(http2_error),
          Slice::FromCopiedString(overall_status_.message())});
      Serialize(absl::Span<Http2Frame>(&goaway_frame, 1), write_buffer_);
      Ref().release();  // Ref held by OnWriteDoneScheduler
      grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs
          write_args;
      write_args.set_max_frame_size(INT_MAX);
      grpc_endpoint_write(
          args_->endpoint.get(), write_buffer_.c_slice_buffer(),
          GRPC_CLOSURE_INIT(&on_write_done_scheduler_,
                            &Http2SettingsHandshaker::OnWriteDoneScheduler,
                            this, grpc_schedule_on_exec_ctx),
          std::move(write_args));
      return false;
    }
  }
  if (overall_status_.ok()) {
    args_->args = args_->args.SetObject(
        MakeRefCounted<Http2SettingsHandshakeResult>(settings_.local(),
                                                     settings_.peer()));
  } else {
    args_->read_buffer.Clear();
  }
  *on_handshake_done = std::move(on_handshake_done_);
  *status = overall_status_;
  return true;
}

}  // namespace

RefCountedPtr<Handshaker> MakeHttp2SettingsHandshaker(bool is_client) {
  return MakeRefCounted<Http2SettingsHandshaker>(is_client);
}

}  // namespace grpc_core
