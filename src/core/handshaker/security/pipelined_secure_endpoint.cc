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

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/memory_request.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "src/core/handshaker/security/pipelining_heuristic_selector.h"
#include "src/core/handshaker/security/secure_endpoint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/tsi/transport_security_grpc.h"
#include "src/core/tsi/transport_security_interface.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/latent_see.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

#define STAGING_BUFFER_SIZE 8192

namespace grpc_core {
namespace {
class FrameProtector : public RefCounted<FrameProtector> {
 public:
  FrameProtector(tsi_frame_protector* protector,
                 tsi_zero_copy_grpc_protector* zero_copy_protector,
                 grpc_slice* leftover_slices, size_t leftover_nslices,
                 const ChannelArgs& args)
      : protector_(protector),
        zero_copy_protector_(zero_copy_protector),
        memory_owner_(args.GetObject<ResourceQuota>()
                          ->memory_quota()
                          ->CreateMemoryOwner()),
        self_reservation_(memory_owner_.MakeReservation(sizeof(*this))) {
    GRPC_TRACE_LOG(secure_endpoint, INFO)
        << "FrameProtector: " << this << " protector: " << protector_
        << " zero_copy_protector: " << zero_copy_protector_
        << " leftover_nslices: " << leftover_nslices;
    if (leftover_nslices > 0) {
      leftover_bytes_ = std::make_unique<SliceBuffer>();
      for (size_t i = 0; i < leftover_nslices; i++) {
        leftover_bytes_->Append(Slice(CSliceRef(leftover_slices[i])));
      }
    }
    if (zero_copy_protector_ != nullptr) {
      read_staging_buffer_ = grpc_empty_slice();
      write_staging_buffer_ = grpc_empty_slice();
    } else {
      read_staging_buffer_ =
          memory_owner_.MakeSlice(MemoryRequest(STAGING_BUFFER_SIZE));
      write_staging_buffer_ =
          memory_owner_.MakeSlice(MemoryRequest(STAGING_BUFFER_SIZE));
    }
    is_zero_copy_protector_ = zero_copy_protector_ != nullptr;
  }

  ~FrameProtector() override {
    tsi_frame_protector_destroy(protector_);
    tsi_zero_copy_grpc_protector_destroy(zero_copy_protector_);
    CSliceUnref(read_staging_buffer_);
    CSliceUnref(write_staging_buffer_);
  }

  Mutex* read_mu() ABSL_LOCK_RETURNED(read_mu_) { return &read_mu_; }
  Mutex* write_mu() ABSL_LOCK_RETURNED(write_mu_) { return &write_mu_; }

  void TraceOp(absl::string_view op, grpc_slice_buffer* slices) {
    if (GRPC_TRACE_FLAG_ENABLED(secure_endpoint)) {
      size_t i;
      if (slices->length < 64) {
        for (i = 0; i < slices->count; i++) {
          char* data =
              grpc_dump_slice(slices->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
          LOG(INFO) << op << " " << this << ": " << data;
          gpr_free(data);
        }
      } else {
        grpc_slice first = GRPC_SLICE_MALLOC(64);
        grpc_slice_buffer_copy_first_into_buffer(slices, 64,
                                                 GRPC_SLICE_START_PTR(first));
        char* data = grpc_dump_slice(first, GPR_DUMP_HEX | GPR_DUMP_ASCII);
        LOG(INFO) << op << " first:" << this << ": " << data;
        gpr_free(data);
        CSliceUnref(first);
      }
    }
  }

  void MaybePostReclaimer() {
    if (!has_posted_reclaimer_.exchange(true, std::memory_order_relaxed)) {
      memory_owner_.PostReclaimer(
          ReclamationPass::kBenign,
          [self = Ref()](std::optional<ReclamationSweep> sweep) {
            if (sweep.has_value()) {
              GRPC_TRACE_LOG(resource_quota, INFO)
                  << "secure endpoint: benign reclamation to free memory";
              grpc_slice temp_read_slice;
              grpc_slice temp_write_slice;

              self->read_mu_.Lock();
              temp_read_slice =
                  std::exchange(self->read_staging_buffer_, grpc_empty_slice());
              self->read_mu_.Unlock();

              self->write_mu_.Lock();
              temp_write_slice = std::exchange(self->write_staging_buffer_,
                                               grpc_empty_slice());
              self->write_mu_.Unlock();

              CSliceUnref(temp_read_slice);
              CSliceUnref(temp_write_slice);
              self->has_posted_reclaimer_.store(false,
                                                std::memory_order_relaxed);
            }
          });
    }
  }

  void FlushReadStagingBuffer(uint8_t** cur, uint8_t** end)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_) {
    grpc_slice_buffer_add_indexed(read_buffer_, read_staging_buffer_);
    read_staging_buffer_ =
        memory_owner_.MakeSlice(MemoryRequest(STAGING_BUFFER_SIZE));
    *cur = GRPC_SLICE_START_PTR(read_staging_buffer_);
    *end = GRPC_SLICE_END_PTR(read_staging_buffer_);
  }

  void FinishRead(bool ok) {
    TraceOp("FinishRead", read_buffer_);
    // TODO(yangg) experiment with moving this block after read_cb to see if it
    // helps latency
    source_buffer_.Clear();
    if (!ok) grpc_slice_buffer_reset_and_unref(read_buffer_);
    read_buffer_ = nullptr;
  }

  bool IsZeroCopyProtector() const { return is_zero_copy_protector_; }

  absl::Status Unprotect(absl::Status read_status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_) {
    if (shutdown_) {
      return absl::CancelledError("secure endpoint shutdown");
    }

    GRPC_LATENT_SEE_SCOPE("unprotect");
    bool keep_looping = false;
    tsi_result result = TSI_OK;

    uint8_t* cur = GRPC_SLICE_START_PTR(read_staging_buffer_);
    uint8_t* end = GRPC_SLICE_END_PTR(read_staging_buffer_);

    if (!read_status.ok()) {
      grpc_slice_buffer_reset_and_unref(read_buffer_);
    } else if (zero_copy_protector_ != nullptr) {
      // Use zero-copy grpc protector to unprotect.
      int min_progress_size = 1;
      // Get the size of the last frame which is not yet fully decrypted.
      // This estimated frame size is stored in ep->min_progress_size which is
      // passed to the TCP layer to indicate the minimum number of
      // bytes that need to be read to make meaningful progress. This would
      // avoid reading of small slices from the network.
      // TODO(vigneshbabu): Set min_progress_size in the regular
      // (non-zero-copy) frame protector code path as well.
      result = tsi_zero_copy_grpc_protector_unprotect(
          zero_copy_protector_, source_buffer_.c_slice_buffer(), read_buffer_,
          &min_progress_size);
      min_progress_size = std::max(1, min_progress_size);
      min_progress_size_ = result != TSI_OK ? 1 : min_progress_size;
    } else {
      // Use frame protector to unprotect.
      // TODO(yangg) check error, maybe bail out early
      for (size_t i = 0; i < source_buffer_.Count(); i++) {
        grpc_slice encrypted = source_buffer_.c_slice_buffer()->slices[i];
        uint8_t* message_bytes = GRPC_SLICE_START_PTR(encrypted);
        size_t message_size = GRPC_SLICE_LENGTH(encrypted);

        while (message_size > 0 || keep_looping) {
          size_t unprotected_buffer_size_written =
              static_cast<size_t>(end - cur);
          size_t processed_message_size = message_size;
          if (IsTsiFrameProtectorWithoutLocksEnabled()) {
            result = tsi_frame_protector_unprotect(
                protector_, message_bytes, &processed_message_size, cur,
                &unprotected_buffer_size_written);
          } else {
            protector_mu_.Lock();
            result = tsi_frame_protector_unprotect(
                protector_, message_bytes, &processed_message_size, cur,
                &unprotected_buffer_size_written);
            protector_mu_.Unlock();
          }
          if (result != TSI_OK) {
            LOG(ERROR) << "Decryption error: " << tsi_result_to_string(result);
            break;
          }
          message_bytes += processed_message_size;
          message_size -= processed_message_size;
          cur += unprotected_buffer_size_written;

          if (cur == end) {
            FlushReadStagingBuffer(&cur, &end);
            // Force to enter the loop again to extract buffered bytes in
            // protector. The bytes could be buffered because of running out
            // of staging_buffer. If this happens at the end of all slices,
            // doing another unprotect avoids leaving data in the protector.
            keep_looping = true;
          } else if (unprotected_buffer_size_written > 0) {
            keep_looping = true;
          } else {
            keep_looping = false;
          }
        }
        if (result != TSI_OK) break;
      }

      if (cur != GRPC_SLICE_START_PTR(read_staging_buffer_)) {
        grpc_slice_buffer_add(
            read_buffer_,
            grpc_slice_split_head(
                &read_staging_buffer_,
                static_cast<size_t>(
                    cur - GRPC_SLICE_START_PTR(read_staging_buffer_))));
      }
    }

    if (read_status.ok() && result != TSI_OK) {
      read_status = GRPC_ERROR_CREATE(
          absl::StrCat("Unwrap failed (", tsi_result_to_string(result), ")"));
    }

    GRPC_TRACE_LOG(secure_endpoint, INFO)
        << "Unprotect: " << this << " read_status: " << read_status;

    return read_status;
  }

  void BeginRead(grpc_slice_buffer* slices) {
    read_buffer_ = slices;
    grpc_slice_buffer_reset_and_unref(read_buffer_);
  }

  bool MaybeReadLeftoverBytes(
      grpc_event_engine::experimental::SliceBuffer* dest) {
    GRPC_TRACE_LOG(secure_endpoint, INFO)
        << "ReadLeftoverBytes: " << this
        << " leftover_bytes_: " << leftover_bytes_.get();
    if (leftover_bytes_ == nullptr) {
      return false;
    }
    grpc_slice_buffer_swap(leftover_bytes_->c_slice_buffer(),
                           dest->c_slice_buffer());
    leftover_bytes_.reset();
    return true;
  }

  void SetSourceBuffer(
      std::unique_ptr<grpc_event_engine::experimental::SliceBuffer>
          source_buffer) {
    source_buffer_ = std::move(*source_buffer);
    source_buffer.reset();
  }

  int min_progress_size() const { return min_progress_size_; }

  void FlushWriteStagingBuffer(uint8_t** cur, uint8_t** end)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(write_mu_) {
    output_buffer_.AppendIndexed(
        grpc_event_engine::experimental::Slice(write_staging_buffer_));
    write_staging_buffer_ =
        memory_owner_.MakeSlice(MemoryRequest(STAGING_BUFFER_SIZE));
    *cur = GRPC_SLICE_START_PTR(write_staging_buffer_);
    *end = GRPC_SLICE_END_PTR(write_staging_buffer_);
    MaybePostReclaimer();
  }

  tsi_result Protect(grpc_slice_buffer* slices, int max_frame_size)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(write_mu_) {
    if (shutdown_) return TSI_FAILED_PRECONDITION;

    GRPC_LATENT_SEE_SCOPE("protect");
    uint8_t* cur = GRPC_SLICE_START_PTR(write_staging_buffer_);
    uint8_t* end = GRPC_SLICE_END_PTR(write_staging_buffer_);

    output_buffer_.Clear();

    TraceOp("Protect", slices);

    tsi_result result = TSI_OK;
    if (zero_copy_protector_ != nullptr) {
      // Use zero-copy grpc protector to protect.
      // Break the input slices into chunks of size = max_frame_size and call
      // tsi_zero_copy_grpc_protector_protect on each chunk. This ensures that
      // the protector cannot create frames larger than the specified
      // max_frame_size.
      while (slices->length > static_cast<size_t>(max_frame_size) &&
             result == TSI_OK) {
        grpc_slice_buffer_move_first(
            slices, static_cast<size_t>(max_frame_size),
            protector_staging_buffer_.c_slice_buffer());
        result = tsi_zero_copy_grpc_protector_protect(
            zero_copy_protector_, protector_staging_buffer_.c_slice_buffer(),
            output_buffer_.c_slice_buffer());
      }
      if (result == TSI_OK && slices->length > 0) {
        result = tsi_zero_copy_grpc_protector_protect(
            zero_copy_protector_, slices, output_buffer_.c_slice_buffer());
      }
      protector_staging_buffer_.Clear();
    } else {
      // Use frame protector to protect.
      for (size_t i = 0; i < slices->count; i++) {
        grpc_slice plain = slices->slices[i];
        uint8_t* message_bytes = GRPC_SLICE_START_PTR(plain);
        size_t message_size = GRPC_SLICE_LENGTH(plain);
        while (message_size > 0) {
          size_t protected_buffer_size_to_send = static_cast<size_t>(end - cur);
          size_t processed_message_size = message_size;
          if (IsTsiFrameProtectorWithoutLocksEnabled()) {
            result = tsi_frame_protector_protect(
                protector_, message_bytes, &processed_message_size, cur,
                &protected_buffer_size_to_send);
          } else {
            protector_mu_.Lock();
            result = tsi_frame_protector_protect(
                protector_, message_bytes, &processed_message_size, cur,
                &protected_buffer_size_to_send);
            protector_mu_.Unlock();
          }
          if (result != TSI_OK) {
            LOG(ERROR) << "Encryption error: " << tsi_result_to_string(result);
            break;
          }
          message_bytes += processed_message_size;
          message_size -= processed_message_size;
          cur += protected_buffer_size_to_send;

          if (cur == end) {
            FlushWriteStagingBuffer(&cur, &end);
          }
        }
        if (result != TSI_OK) break;
      }
      if (result == TSI_OK) {
        size_t still_pending_size;
        do {
          size_t protected_buffer_size_to_send = static_cast<size_t>(end - cur);
          if (IsTsiFrameProtectorWithoutLocksEnabled()) {
            result = tsi_frame_protector_protect_flush(
                protector_, cur, &protected_buffer_size_to_send,
                &still_pending_size);
          } else {
            protector_mu_.Lock();
            result = tsi_frame_protector_protect_flush(
                protector_, cur, &protected_buffer_size_to_send,
                &still_pending_size);
            protector_mu_.Unlock();
          }
          if (result != TSI_OK) break;
          cur += protected_buffer_size_to_send;
          if (cur == end) {
            FlushWriteStagingBuffer(&cur, &end);
          }
        } while (still_pending_size > 0);
        if (cur != GRPC_SLICE_START_PTR(write_staging_buffer_)) {
          output_buffer_.Append(
              grpc_event_engine::experimental::Slice(grpc_slice_split_head(
                  &write_staging_buffer_,
                  static_cast<size_t>(
                      cur - GRPC_SLICE_START_PTR(write_staging_buffer_)))));
        }
      }
    }
    // TODO(yangg) do different things according to the error type?
    if (result != TSI_OK) output_buffer_.Clear();
    return result;
  }

  grpc_event_engine::experimental::SliceBuffer* output_buffer() {
    return &output_buffer_;
  }

  void Shutdown() {
    shutdown_ = true;
    memory_owner_.Reset();
  }

 private:
  struct tsi_frame_protector* const protector_;
  struct tsi_zero_copy_grpc_protector* const zero_copy_protector_;
  Mutex mu_;
  Mutex write_mu_;
  // The read mutex must be acquired after the write mutex for shutdown
  // purposes.
  Mutex read_mu_ ABSL_ACQUIRED_AFTER(write_mu_);
  Mutex protector_mu_;
  grpc_slice_buffer* read_buffer_ = nullptr;
  grpc_event_engine::experimental::SliceBuffer source_buffer_;
  // saved handshaker leftover data to unprotect.
  std::unique_ptr<SliceBuffer> leftover_bytes_;
  // buffers for read and write
  grpc_slice read_staging_buffer_ ABSL_GUARDED_BY(read_mu_);
  grpc_slice write_staging_buffer_ ABSL_GUARDED_BY(write_mu_);
  grpc_event_engine::experimental::SliceBuffer output_buffer_;
  MemoryOwner memory_owner_;
  MemoryAllocator::Reservation self_reservation_;
  std::atomic<bool> has_posted_reclaimer_{false};
  int min_progress_size_ = 1;
  SliceBuffer protector_staging_buffer_;
  bool shutdown_ = false;
  bool is_zero_copy_protector_ = false;
};
}  // namespace
}  // namespace grpc_core

namespace grpc_event_engine::experimental {
namespace {

class PipelinedSecureEndpoint final : public EventEngine::Endpoint {
 public:
  PipelinedSecureEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          wrapped_ep,
      struct tsi_frame_protector* protector,
      struct tsi_zero_copy_grpc_protector* zero_copy_protector,
      grpc_slice* leftover_slices, size_t leftover_nslices,
      const grpc_core::ChannelArgs& channel_args)
      : impl_(grpc_core::MakeRefCounted<Impl>(
            std::move(wrapped_ep), protector, zero_copy_protector,
            leftover_slices, leftover_nslices, channel_args)) {}

  ~PipelinedSecureEndpoint() override { impl_->Shutdown(); }

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            ReadArgs in_args) override {
    return impl_->Read(std::move(on_read), buffer, std::move(in_args));
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, WriteArgs args) override {
    return impl_->Write(std::move(on_writable), data, std::move(args));
  }

  const EventEngine::ResolvedAddress& GetPeerAddress() const override {
    return impl_->GetPeerAddress();
  }

  const EventEngine::ResolvedAddress& GetLocalAddress() const override {
    return impl_->GetLocalAddress();
  }

  void* QueryExtension(absl::string_view id) override {
    return impl_->QueryExtension(id);
  }

  std::shared_ptr<TelemetryInfo> GetTelemetryInfo() const override {
    return std::make_shared<Impl::TelemetryInfo>(impl_->GetTelemetryInfo());
  }

 private:
  class Impl : public grpc_core::RefCounted<Impl> {
   public:
    class TelemetryInfo : public EventEngine::Endpoint::TelemetryInfo {
     public:
      explicit TelemetryInfo(
          std::shared_ptr<EventEngine::Endpoint::TelemetryInfo>
              wrapped_telemetry_info)
          : wrapped_telemetry_info_(std::move(wrapped_telemetry_info)) {}

      std::vector<size_t> AllWriteMetrics() const override {
        return wrapped_telemetry_info_
                   ? wrapped_telemetry_info_->AllWriteMetrics()
                   : std::vector<size_t>{};
      }

      std::optional<absl::string_view> GetMetricName(
          size_t key) const override {
        return wrapped_telemetry_info_
                   ? wrapped_telemetry_info_->GetMetricName(key)
                   : std::nullopt;
      }

      std::optional<size_t> GetMetricKey(
          absl::string_view name) const override {
        return wrapped_telemetry_info_
                   ? wrapped_telemetry_info_->GetMetricKey(name)
                   : std::nullopt;
      }

      std::shared_ptr<EventEngine::Endpoint::MetricsSet> GetMetricsSet(
          absl::Span<const size_t> keys) const override {
        return wrapped_telemetry_info_
                   ? wrapped_telemetry_info_->GetMetricsSet(keys)
                   : nullptr;
      }

      std::shared_ptr<EventEngine::Endpoint::MetricsSet> GetFullMetricsSet()
          const override {
        return wrapped_telemetry_info_
                   ? wrapped_telemetry_info_->GetFullMetricsSet()
                   : nullptr;
      }

     private:
      std::shared_ptr<EventEngine::Endpoint::TelemetryInfo>
          wrapped_telemetry_info_;
    };

    Impl(std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
             wrapped_ep,
         struct tsi_frame_protector* protector,
         struct tsi_zero_copy_grpc_protector* zero_copy_protector,
         grpc_slice* leftover_slices, size_t leftover_nslices,
         const grpc_core::ChannelArgs& channel_args)
        : staging_protected_data_buffer_(std::make_unique<SliceBuffer>()),
          frame_protector_(protector, zero_copy_protector, leftover_slices,
                           leftover_nslices, channel_args),
          wrapped_ep_(std::move(wrapped_ep)),
          event_engine_(channel_args.GetObjectRef<
                        grpc_event_engine::experimental::EventEngine>()) {
      if (event_engine_ == nullptr) {
        event_engine_ = GetDefaultEventEngine();
      }
      // Kick off the first endpoint read ahead of the first transport read.
      StartFirstRead();
    }

    bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
              SliceBuffer* buffer, ReadArgs args) {
      GRPC_LATENT_SEE_SCOPE("secure_endpoint read");

      grpc_core::ReleasableMutexLock lock(&read_queue_mu_);
      // If there's been an error observed asynchronously, then fail out with
      // that error.
      if (!unprotecting_.ok()) {
        event_engine_->Run(
            [on_read = std::move(on_read),
             status = unprotecting_.status()]() mutable { on_read(status); });
        return false;
      }

      // If we already have some unprotected bytes available, we can just return
      // those here.
      if (waiting_for_transport_read_) {
        waiting_for_transport_read_ = false;
        if (unprotected_data_buffer_ != nullptr) {
          *buffer = std::move(*unprotected_data_buffer_);
          unprotected_data_buffer_.reset();
        }
        // We might have been waiting for this transport read before continuing
        // to unprotect. If we were (the last read completed and there are now
        // bytes to unprotect), then calling ContinueUnprotect will start
        // unprotecting these bytes. If we weren't (the last read has not
        // completed yet), then ContinueUnprotect will just return early and be
        // called again once that read completes.
        if (unprotecting_.value()) {
          if (protected_data_buffer_ != nullptr) {
            // We have some protected bytes waiting to be unprotected. We need
            // to kick off unprotecting those bytes.
            lock.Release();
            event_engine_->Run([impl = Ref()]() mutable {
              grpc_core::ExecCtx exec_ctx;
              ContinueUnprotect(std::move(impl));
            });
          } else {
            // The last read has not completed yet, so we need to wait for it to
            // complete before we can unprotect anything.
            unprotecting_ = false;
          }
        }
        return true;
      }

      // If we're already unprotecting on another thread, we need to store the
      // buffer until we have some unprotected bytes to give it.
      GRPC_CHECK(on_read_ == nullptr);
      pending_output_buffer_ = buffer;
      on_read_ = std::move(on_read);
      last_read_args_ = std::move(args);

      if (!heuristic_selector_.IsPipeliningEnabled() &&
          !unprotecting_.value()) {
        unprotecting_ = true;
        lock.Release();
        // Unprotect inline since pipelining is disabled.
        return UnprotectInline();
      }
      return false;
    }

    bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
               SliceBuffer* data, WriteArgs args) {
      GRPC_LATENT_SEE_SCOPE("secure_endpoint write");
      tsi_result result;
      frame_protector_.TraceOp("Write", data->c_slice_buffer());
      {
        grpc_core::MutexLock lock(frame_protector_.write_mu());
        result = frame_protector_.Protect(data->c_slice_buffer(),
                                          args.max_frame_size());
      }
      if (result != TSI_OK) {
        event_engine_->Run(
            [on_writable = std::move(on_writable), result]() mutable {
              on_writable(GRPC_ERROR_CREATE(absl::StrCat(
                  "Wrap failed (", tsi_result_to_string(result), ")")));
            });
        return false;
      }
      on_write_ = std::move(on_writable);
      frame_protector_.TraceOp(
          "Write", frame_protector_.output_buffer()->c_slice_buffer());
      return wrapped_ep_->Write(
          [impl = Ref()](absl::Status status) mutable {
            auto on_write = std::move(impl->on_write_);
            impl.reset();
            on_write(status);
          },
          frame_protector_.output_buffer(), std::move(args));
    }

    const EventEngine::ResolvedAddress& GetPeerAddress() const {
      return wrapped_ep_->GetPeerAddress();
    }

    const EventEngine::ResolvedAddress& GetLocalAddress() const {
      return wrapped_ep_->GetLocalAddress();
    }

    void* QueryExtension(absl::string_view id) {
      return wrapped_ep_->QueryExtension(id);
    }

    void Shutdown() {
      std::unique_ptr<EventEngine::Endpoint> wrapped_ep;
      grpc_core::MutexLock write_lock(frame_protector_.write_mu());
      grpc_core::MutexLock read_lock(frame_protector_.read_mu());
      grpc_core::MutexLock shutdown_read_lock(&shutdown_read_mu_);
      wrapped_ep = std::move(wrapped_ep_);
      frame_protector_.Shutdown();
    }

    std::shared_ptr<TelemetryInfo> GetTelemetryInfo() const {
      return std::make_shared<Impl::TelemetryInfo>(
          wrapped_ep_->GetTelemetryInfo());
    }

   private:
    void StartFirstRead() ABSL_LOCKS_EXCLUDED(read_queue_mu_) {
      grpc_core::ReleasableMutexLock lock(&read_queue_mu_);
      unprotecting_ = true;
      GRPC_CHECK(protected_data_buffer_ == nullptr);
      GRPC_CHECK(unprotected_data_buffer_ == nullptr);
      // First, check if there are any leftover bytes to unprotect. If there
      // are, we can immediately start unprotecting those bytes.
      if (frame_protector_.MaybeReadLeftoverBytes(
              staging_protected_data_buffer_.get())) {
        MoveStagingIntoProtectedBuffer();
        lock.Release();
        // Unprotect inline since we expect a small number of leftover bytes.
        ContinueUnprotect(Ref(), /*unprotect_inline=*/true);
        return;
      }

      // Kick off the first read in another thread.
      ReadArgs args;
      args.set_read_hint_bytes(frame_protector_.min_progress_size());
      lock.Release();
      event_engine_->Run([impl = Ref(), args = std::move(args)]() mutable {
        grpc_core::ExecCtx exec_ctx;
        // If the endpoint closed whilst waiting for this callback, then
        // fail out the read and we're done.
        grpc_core::ReleasableMutexLock lock(&impl->shutdown_read_mu_);
        if (impl->wrapped_ep_ == nullptr) {
          lock.Release();
          FailReads(std::move(impl),
                    absl::CancelledError("secure endpoint shutdown"));
          return;
        }
        const bool read_finished_immediately = impl->wrapped_ep_->Read(
            [impl = impl->Ref()](absl::Status status) mutable {
              grpc_core::ExecCtx exec_ctx;
              FinishInlineRead(std::move(impl), status);
            },
            impl->staging_protected_data_buffer_.get(), std::move(args));
        if (read_finished_immediately) {
          lock.Release();
          {
            grpc_core::MutexLock lock(&impl->read_queue_mu_);
            impl->frame_protector_.TraceOp(
                "ReadImm",
                impl->staging_protected_data_buffer_->c_slice_buffer());
            impl->MoveStagingIntoProtectedBuffer();
          }
          ContinueUnprotect(std::move(impl));
        }
      });
    }

    static void FinishInlineRead(grpc_core::RefCountedPtr<Impl> impl,
                                 absl::Status status)
        ABSL_LOCKS_EXCLUDED(impl->read_queue_mu_) {
      if (status.ok()) {
        {
          grpc_core::MutexLock lock(&impl->read_queue_mu_);
          impl->frame_protector_.TraceOp(
              "Read", impl->staging_protected_data_buffer_->c_slice_buffer());
          impl->MoveStagingIntoProtectedBuffer();
        }
        ContinueUnprotect(std::move(impl));
        return;
      }
      FailReads(std::move(impl), status);
    }

    void MoveStagingIntoProtectedBuffer()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_queue_mu_) {
      protected_data_buffer_ = std::move(staging_protected_data_buffer_);
      staging_protected_data_buffer_ = std::make_unique<SliceBuffer>();
    }

    bool ShouldContinueUnprotect()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_queue_mu_) {
      if (unprotecting_.ok() && !unprotecting_.value()) {
        unprotecting_ = true;
        return true;
      }
      return false;
    }

    static void FailReads(grpc_core::RefCountedPtr<Impl> impl,
                          absl::Status status)
        ABSL_LOCKS_EXCLUDED(read_queue_mu_) {
      impl->read_queue_mu_.Lock();
      impl->unprotecting_ = status;
      auto on_read = std::move(impl->on_read_);
      impl->read_queue_mu_.Unlock();
      impl.reset();
      if (on_read != nullptr) on_read(status);
    };

    bool UnprotectInline() ABSL_LOCKS_EXCLUDED(read_queue_mu_) {
      ReadArgs args;
      args.set_read_hint_bytes(frame_protector_.min_progress_size());

      // If the endpoint closed whilst waiting for this callback, then
      // fail out the read and we're done.
      grpc_core::ReleasableMutexLock shutdown_read_lock(&shutdown_read_mu_);
      if (wrapped_ep_ == nullptr) {
        shutdown_read_lock.Release();
        FailReads(Ref(), absl::CancelledError("secure endpoint shutdown"));
        return false;
      }
      const bool read_finished_immediately = wrapped_ep_->Read(
          [impl = Ref()](absl::Status status) mutable {
            grpc_core::ExecCtx exec_ctx;
            FinishInlineRead(std::move(impl), status);
          },
          staging_protected_data_buffer_.get(), std::move(args));
      if (read_finished_immediately) {
        shutdown_read_lock.Release();
        {
          grpc_core::MutexLock read_queue_lock(&read_queue_mu_);
          frame_protector_.TraceOp(
              "ReadImm", staging_protected_data_buffer_->c_slice_buffer());
          MoveStagingIntoProtectedBuffer();
        }
        // Unprotected bytes will be stored directly into buffer and returned
        // inline.
        ContinueUnprotect(Ref(), /*unprotect_inline=*/true);
        {
          // If there was an error, the read will fail asynchronously;
          // otherwise, we can return the bytes inline.
          grpc_core::MutexLock read_queue_lock(&read_queue_mu_);
          return unprotecting_.ok();
        }
      }
      return false;
    }

    static void ContinueUnprotect(grpc_core::RefCountedPtr<Impl> impl,
                                  bool unprotect_inline = false) {
      GRPC_LATENT_SEE_SCOPE("secure endpoint continue unprotect");
      ReadArgs args;
      std::unique_ptr<SliceBuffer> source_buffer;
      std::unique_ptr<SliceBuffer> read_buffer;
      absl::Status unprotect_status;
      absl::AnyInvocable<void(absl::Status)> on_read;
      bool enable_pipelining = false;
      bool exit_loop = false;

      /*
      Data passes through the buffers as follows:
      (1) Data is read into staging buffer.
      (2) Once ready to be unprotected, data is moved from staging buffer to
      protected data buffer.
      (3) Data is passed to frame protector's source buffer and unprotected into
      read buffer.
      (4) Unprotected data is passed from read buffer to unprotected data
      buffer.
      (5) Lastly, data is optionally passed to pending output buffer if there is
      currently a pending read.
      */
      while (true) {
        {
          grpc_core::ReleasableMutexLock lock(&impl->read_queue_mu_);
          if (!impl->unprotecting_.ok()) {
            // Something failed or we're shutting down, so fail reads.
            auto status = impl->unprotecting_.status();
            lock.Release();
            FailReads(std::move(impl), status);
            return;
          }

          if (impl->protected_data_buffer_ == nullptr) {
            // No pending data to unprotect - we're done.
            impl->unprotecting_ = false;
            lock.Release();
            return;
          }

          // There's more data to unprotect - grab it under the queue lock.
          source_buffer = std::move(impl->protected_data_buffer_);
          impl->frame_protector_.TraceOp("data",
                                         source_buffer->c_slice_buffer());
          args = std::move(impl->last_read_args_);
          if (impl->frame_protector_.IsZeroCopyProtector()) {
            // We currently only track min progress size for zero copy
            // protectors. Once we add this for non-zero copy protectors, we
            // should remove the if condition.
            // Since we start unprotecting after the first read, the min
            // progress size does not account for the bytes we have already read
            // in the previous read, so we need to subtract them here.
            args.set_read_hint_bytes(std::max<int64_t>(
                1, impl->frame_protector_.min_progress_size() -
                       source_buffer->Length()));
          } else if (args.read_hint_bytes() > 0) {
            args.set_read_hint_bytes(std::max<int64_t>(
                1, args.read_hint_bytes() - source_buffer->Length()));
          }

          impl->heuristic_selector_.RecordRead(source_buffer->Length());
          enable_pipelining = impl->heuristic_selector_.IsPipeliningEnabled();
        }

        // Kick off the next read in another thread while we unprotect in this
        // thread if the frame is large enough or we don't know the frame size
        // yet.
        if (enable_pipelining) {
          impl->event_engine_->Run(
              [impl = impl->Ref(), args = std::move(args)]() mutable {
                grpc_core::ExecCtx exec_ctx;
                StartAsyncRead(std::move(impl), std::move(args));
              });
        }

        {
          grpc_core::ReleasableMutexLock lock(impl->frame_protector_.read_mu());
          // Unprotect the bytes.
          GRPC_CHECK(read_buffer == nullptr);
          impl->frame_protector_.SetSourceBuffer(std::move(source_buffer));
          read_buffer = std::make_unique<SliceBuffer>();
          impl->frame_protector_.BeginRead(read_buffer->c_slice_buffer());
          unprotect_status = impl->frame_protector_.Unprotect(absl::OkStatus());
          impl->frame_protector_.FinishRead(unprotect_status.ok());
          if (!unprotect_status.ok()) {
            lock.Release();
            FailReads(std::move(impl), unprotect_status);
            return;
          }
        }

        impl->read_queue_mu_.Lock();
        impl->unprotected_data_buffer_ = std::move(read_buffer);
        // We have a read waiting on this unprotected data - call the
        // callback and continue in loop.
        if (impl->on_read_ != nullptr) {
          *impl->pending_output_buffer_ =
              std::move(*impl->unprotected_data_buffer_);
          impl->unprotected_data_buffer_.reset();
          on_read = std::move(impl->on_read_);
          if (!impl->heuristic_selector_.IsPipeliningEnabled() &&
              impl->protected_data_buffer_ == nullptr) {
            // Since pipelining is disabled and there is no more data to
            // unprotect, we can exit the loop once done.
            impl->unprotecting_ = false;
            exit_loop = true;
          }
          impl->read_queue_mu_.Unlock();
          if (!unprotect_inline) {
            impl->event_engine_->Run([on_read = std::move(on_read)]() mutable {
              on_read(absl::OkStatus());
            });
          }
        } else {
          // We're waiting on the next read to read this data. We're done
          // for now.
          impl->waiting_for_transport_read_ = true;
          impl->read_queue_mu_.Unlock();
          exit_loop = true;
        }

        if (exit_loop) {
          break;
        }
      }
    }

    static void StartAsyncRead(grpc_core::RefCountedPtr<Impl> impl,
                               ReadArgs args)
        ABSL_LOCKS_EXCLUDED(impl->shutdown_read_mu_, impl->read_queue_mu_) {
      grpc_core::ReleasableMutexLock lock(&impl->shutdown_read_mu_);
      if (impl->wrapped_ep_ == nullptr) {
        lock.Release();
        FailReads(std::move(impl),
                  absl::CancelledError("secure endpoint shutdown"));
        return;
      }
      const bool read_finished_immediately = impl->wrapped_ep_->Read(
          [impl = impl->Ref()](absl::Status status) mutable {
            grpc_core::ExecCtx exec_ctx;
            FinishAsyncRead(std::move(impl), status);
          },
          impl->staging_protected_data_buffer_.get(), std::move(args));
      if (read_finished_immediately) {
        lock.Release();
        bool should_unprotect = false;
        {
          grpc_core::MutexLock lock(&impl->read_queue_mu_);
          impl->frame_protector_.TraceOp(
              "ReadImm",
              impl->staging_protected_data_buffer_->c_slice_buffer());
          impl->MoveStagingIntoProtectedBuffer();
          should_unprotect = impl->ShouldContinueUnprotect();
        }
        // We now have data to unprotect so we should continue unprotecting
        // if not currently doing so.
        if (should_unprotect) {
          ContinueUnprotect(std::move(impl));
        }
      }
    }

    static void FinishAsyncRead(grpc_core::RefCountedPtr<Impl> impl,
                                absl::Status status)
        ABSL_LOCKS_EXCLUDED(impl->read_queue_mu_) {
      bool should_unprotect = false;
      {
        grpc_core::MutexLock lock(&impl->read_queue_mu_);
        should_unprotect = impl->ShouldContinueUnprotect();
        if (!status.ok()) {
          // We rely on ContinueUnprotect to fail the read if the
          // status is not ok, since we might currently have an
          // unprotect in progress and we want to return that data
          // before we fail future reads.
          impl->unprotecting_ = status;
        } else {
          impl->frame_protector_.TraceOp(
              "Read", impl->staging_protected_data_buffer_->c_slice_buffer());
          impl->MoveStagingIntoProtectedBuffer();
        }
      }
      // We now have data to unprotect so we should continue unprotecting
      // if not currently doing so.
      if (should_unprotect) {
        ContinueUnprotect(std::move(impl));
      }
    }

    grpc_core::Mutex read_queue_mu_;
    // We have a separate read shutdown lock in order to avoid sharing a lock
    // between read and unprotect. This way, we can perform reads and
    // unprotects in parallel. We also must acquire this after both frame
    // protector mutexes for shutdown purposes.
    grpc_core::Mutex shutdown_read_mu_
        ABSL_ACQUIRED_AFTER(frame_protector_.read_mu());
    absl::StatusOr<bool> unprotecting_ ABSL_GUARDED_BY(read_queue_mu_) = false;
    std::unique_ptr<SliceBuffer> unprotected_data_buffer_
        ABSL_GUARDED_BY(read_queue_mu_);
    std::unique_ptr<SliceBuffer> staging_protected_data_buffer_;
    SliceBuffer* pending_output_buffer_ ABSL_GUARDED_BY(read_queue_mu_);
    std::unique_ptr<SliceBuffer> protected_data_buffer_
        ABSL_GUARDED_BY(read_queue_mu_);
    ReadArgs last_read_args_ ABSL_GUARDED_BY(read_queue_mu_);
    absl::AnyInvocable<void(absl::Status)> on_read_
        ABSL_GUARDED_BY(read_queue_mu_) = nullptr;
    // Since writes are currently not pipelined, we don't need to protect this
    // callback with a mutex.
    absl::AnyInvocable<void(absl::Status)> on_write_ = nullptr;
    // Operations to the frame protector are protected by mutexes, so the frame
    // protector itself does not need to be protected.
    grpc_core::FrameProtector frame_protector_;
    // Resetting wrapped endpoint requires holding three different locks, but
    // other operations using it require holding only one of those locks, so we
    // don't want any guard annotations on the wrapped_ep_ field.
    std::unique_ptr<EventEngine::Endpoint> wrapped_ep_;
    std::shared_ptr<EventEngine> event_engine_;
    bool waiting_for_transport_read_ ABSL_GUARDED_BY(read_queue_mu_) = false;
    grpc_core::PipeliningHeuristicSelector heuristic_selector_
        ABSL_GUARDED_BY(read_queue_mu_);
  };

  grpc_core::RefCountedPtr<Impl> impl_;
};

}  // namespace
}  // namespace grpc_event_engine::experimental

grpc_core::OrphanablePtr<grpc_endpoint> grpc_pipelined_secure_endpoint_create(
    struct tsi_frame_protector* protector,
    struct tsi_zero_copy_grpc_protector* zero_copy_protector,
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        to_wrap,
    grpc_slice* leftover_slices, const grpc_core::ChannelArgs& channel_args,
    size_t leftover_nslices) {
  return grpc_core::OrphanablePtr<grpc_endpoint>(
      grpc_event_engine::experimental::grpc_event_engine_endpoint_create(
          std::make_unique<
              grpc_event_engine::experimental::PipelinedSecureEndpoint>(
              std::move(to_wrap), protector, zero_copy_protector,
              leftover_slices, leftover_nslices, channel_args)));
}
