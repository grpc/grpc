//
//
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
//
//

//
//
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
//
//

#include "src/core/handshaker/security/event_engine/secure_endpoint.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

#include "include/grpc/event_engine/event_engine.h"
#include "include/grpc/event_engine/slice.h"
#include "include/grpc/event_engine/slice_buffer.h"
#include "include/grpc/slice.h"
#include "include/grpc/support/alloc.h"
#include "include/grpc/support/log.h"
#include <grpc/support/port_platform.h>

#include "src/core/lib/debug/trace_impl.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/extensions/supports_fd.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/tsi/transport_security_grpc.h"
#include "src/core/tsi/transport_security_interface.h"

#define STAGING_BUFFER_SIZE 8192

namespace {
using ::grpc_event_engine::experimental::EndpointSupportsFdExtension;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::ExtendedType;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::MutableSlice;
using ::grpc_event_engine::experimental::QueryExtension;
using ::grpc_event_engine::experimental::Slice;
using ::grpc_event_engine::experimental::SliceBuffer;
using ::grpc_event_engine::experimental::internal::SliceCast;

class SecureEndpointImpl : public grpc_core::RefCounted<SecureEndpointImpl> {
 public:
  SecureEndpointImpl(tsi_frame_protector* protector,
                     tsi_zero_copy_grpc_protector* zero_copy_protector,
                     std::unique_ptr<EventEngine::Endpoint> to_wrap,
                     Slice* leftover_slices,
                     const grpc_core::ChannelArgs& channel_args,
                     size_t leftover_nslices)
      : wrapped_ep_(std::move(to_wrap)),
        protector_(protector),
        zero_copy_protector_(zero_copy_protector) {
    for (size_t i = 0; i < leftover_nslices; i++) {
      leftover_bytes_.Append(leftover_slices[i].Ref());
    }
    memory_owner_ =
        grpc_core::ResourceQuotaFromChannelArgs(channel_args.ToC().get())
            ->memory_quota()
            ->CreateMemoryOwner();
    self_reservation_ = memory_owner_.MakeReservation(sizeof(*this));
    if (!zero_copy_protector_) {
      read_staging_buffer_ = SliceCast<MutableSlice>(memory_owner_.MakeSlice(
          grpc_core::MemoryRequest(STAGING_BUFFER_SIZE)));
      write_staging_buffer_ = SliceCast<MutableSlice>(memory_owner_.MakeSlice(
          grpc_core::MemoryRequest(STAGING_BUFFER_SIZE)));
    }
    has_posted_reclaimer_.store(false, std::memory_order_relaxed);
    min_progress_size_ = 1;
  }

  ~SecureEndpointImpl() override {
    memory_owner_.Reset();
    wrapped_ep_.reset();
    tsi_frame_protector_destroy(protector_);
    tsi_zero_copy_grpc_protector_destroy(zero_copy_protector_);
  }

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const EventEngine::Endpoint::ReadArgs* args) {
    read_cb_ = std::move(on_read);
    read_buffer_ = buffer;
    read_buffer_->Clear();

    // Take ref for the read callback.
    Ref().release();

    // Read any leftover bytes from handshake.
    if (leftover_bytes_.Count() > 0) {
      leftover_bytes_.Swap(source_buffer_);
      CHECK_EQ(leftover_bytes_.Count(), 0u);
      absl::Status status = ProcessRead(absl::OkStatus());
      if (status.ok()) {
        MaybeLogBuffer(read_buffer_, /*is_read=*/true);
        // Corresponds to the Ref held by SecureEndpointImpl::Read.
        Unref();
        return true;
      } else {
        // Processed bytes unsuccessfully. Call `on_read` asynchronously with
        // error.
        CallReadCb(status);
        return false;
      }
    }

    if (wrapped_ep_->Read(
            absl::bind_front(&SecureEndpointImpl::ProcessReadAsync, this),
            &source_buffer_, args)) {
      absl::Status status = ProcessRead(absl::OkStatus());
      if (status.ok()) {
        // Read and processed bytes successfully.
        // Corresponds to the Ref held by SecureEndpointImpl::Read.
        Unref();
        return true;
      } else {
        // Processed bytes unsuccessfully. Call `on_read` asynchronously with
        // error.
        CallReadCb(status);
        return false;
      }
    } else {
      // `process_read_async` will be called by wrapped_ep_ once read has
      // completed.
      return false;
    }
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const EventEngine::Endpoint::WriteArgs* args) {
    unsigned i;
    tsi_result result = TSI_OK;

    {
      grpc_core::MutexLock l(&write_mu_);
      uint8_t* cur = write_staging_buffer_.begin();
      uint8_t* end = write_staging_buffer_.end();

      output_buffer_.Clear();
      MaybeLogBuffer(data, /*is_read=*/false);

      if (zero_copy_protector_ != nullptr) {
        // Use zero-copy grpc protector to protect.
        result = TSI_OK;
        // Break the input slices into chunks of size = max_frame_size and
        // call tsi_zero_copy_grpc_protector_protect on each chunk. This
        // ensures that the protector cannot create frames larger than the
        // specified max_frame_size.
        while (data->Length() > static_cast<size_t>(args->max_frame_size) &&
               result == TSI_OK) {
          data->MoveFirstNBytesIntoSliceBuffer(
              static_cast<size_t>(args->max_frame_size),
              protector_staging_buffer_);
          result = tsi_zero_copy_grpc_protector_protect(
              zero_copy_protector_, protector_staging_buffer_.c_slice_buffer(),
              output_buffer_.c_slice_buffer());
        }
        if (result == TSI_OK && data->Length() > 0) {
          result = tsi_zero_copy_grpc_protector_protect(
              zero_copy_protector_, data->c_slice_buffer(),
              output_buffer_.c_slice_buffer());
        }
        protector_staging_buffer_.Clear();
      } else {
        // Use frame protector to protect.
        for (i = 0; i < data->Count(); i++) {
          grpc_slice plain = (*data)[i].c_slice();
          uint8_t* message_bytes = GRPC_SLICE_START_PTR(plain);
          size_t message_size = GRPC_SLICE_LENGTH(plain);
          while (message_size > 0) {
            size_t protected_buffer_size_to_send =
                static_cast<size_t>(end - cur);
            size_t processed_message_size = message_size;
            protector_mu_.Lock();
            result = tsi_frame_protector_protect(
                protector_, message_bytes, &processed_message_size, cur,
                &protected_buffer_size_to_send);
            protector_mu_.Unlock();
            if (result != TSI_OK) {
              gpr_log(GPR_ERROR, "Encryption error: %s",
                      tsi_result_to_string(result));
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
            size_t protected_buffer_size_to_send =
                static_cast<size_t>(end - cur);
            protector_mu_.Lock();
            result = tsi_frame_protector_protect_flush(
                protector_, cur, &protected_buffer_size_to_send,
                &still_pending_size);
            protector_mu_.Unlock();
            if (result != TSI_OK) break;
            cur += protected_buffer_size_to_send;
            if (cur == end) {
              FlushWriteStagingBuffer(&cur, &end);
            }
          } while (still_pending_size > 0);
          if (cur != write_staging_buffer_.begin()) {
            output_buffer_.Append(
                SliceCast<Slice>(write_staging_buffer_.TakeFirst(
                    static_cast<size_t>(cur - write_staging_buffer_.begin()))));
          }
        }
      }
    }

    // Take ref for the write callback.
    Ref().release();

    if (result != TSI_OK) {
      // TODO(yangg) do different things according to the error type?
      output_buffer_.Clear();
      // Call on_writable with error.
      GetDefaultEventEngine()->Run(
          [this, on_writable = std::move(on_writable), result]() mutable {
            grpc_core::ExecCtx exec_ctx;
            on_writable(GRPC_ERROR_CREATE(absl::StrCat(
                "Wrap failed (", tsi_result_to_string(result), ")")));
            // Corresponds to the Ref held by SecureEndpointImpl::Write.
            Unref();
          });
      return false;
    }

    write_cb_ = std::move(on_writable);
    if (wrapped_ep_->Write(
            absl::bind_front(&SecureEndpointImpl::CallWriteCb, this),
            &output_buffer_, args)) {
      // Bytes have been successfully written, no need to call `on_writable`.
      write_cb_ = nullptr;
      // Corresponds to the Ref held by SecureEndpointImpl::Write.
      Unref();
      return true;
    } else {
      // `on_write` will be called by wrapped_ep_ once write has completed.
      return false;
    }
  }

  const EventEngine::ResolvedAddress& GetPeerAddress() const {
    return wrapped_ep_->GetPeerAddress();
  }

  const EventEngine::ResolvedAddress& GetLocalAddress() const {
    return wrapped_ep_->GetLocalAddress();
  }

  int GetWrappedFd() {
    auto* supports_fd =
        QueryExtension<EndpointSupportsFdExtension>(wrapped_ep_.get());
    if (supports_fd != nullptr) {
      return supports_fd->GetWrappedFd();
    }
    return -1;
  }

  void Shutdown(
      absl::AnyInvocable<void(absl::StatusOr<int> release_fd)> on_release_fd) {
    auto* supports_fd =
        QueryExtension<EndpointSupportsFdExtension>(wrapped_ep_.get());
    if (supports_fd != nullptr && on_release_fd != nullptr) {
      supports_fd->Shutdown(std::move(on_release_fd));
    }
  }

 private:
  void MaybeLogBuffer(SliceBuffer* data, bool is_read) {
    // TODO(alishananda): add new trace flag for event engine secure endpoint
    if (GRPC_TRACE_FLAG_ENABLED(secure_endpoint)) {
      size_t i = 0;
      for (i = 0; i < data->Count(); i++) {
        char* data_str = grpc_dump_slice((*data)[i].c_slice(),
                                         GPR_DUMP_HEX | GPR_DUMP_ASCII);
        VLOG(2) << (is_read ? "READ " : "WRITE ") << this << ": " << data_str;
        gpr_free(data_str);
      }
    }
  }

  void CallWriteCb(absl::Status error) {
    GetDefaultEventEngine()->Run(
        [this, write_cb = std::exchange(write_cb_, nullptr), error]() mutable {
          grpc_core::ExecCtx exec_ctx;
          write_cb(error);
          // Corresponds to the Ref held by SecureEndpointImpl::Write.
          Unref();
        });
  }

  // Returns OK status if reading and processing of bytes was successful.
  // Returns error status if reading or processing of bytes failed.
  absl::Status ProcessRead(absl::Status error) {
    unsigned i;
    uint8_t keep_looping = 0;
    tsi_result result = TSI_OK;

    {
      grpc_core::MutexLock l(&read_mu_);
      uint8_t* cur = read_staging_buffer_.begin();
      uint8_t* end = read_staging_buffer_.end();

      if (!error.ok()) {
        read_buffer_->Clear();
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
            zero_copy_protector_, source_buffer_.c_slice_buffer(),
            read_buffer_->c_slice_buffer(), &min_progress_size);
        min_progress_size = std::max(1, min_progress_size);
        min_progress_size_ = result != TSI_OK ? 1 : min_progress_size;
      } else {
        // Use frame protector to unprotect.
        // TODO(yangg): check error, maybe bail out early
        for (i = 0; i < source_buffer_.Count(); i++) {
          const Slice& encrypted = source_buffer_[i];
          const uint8_t* message_bytes = encrypted.begin();
          size_t message_size = encrypted.size();

          while (message_size > 0 || keep_looping) {
            size_t unprotected_buffer_size_written =
                static_cast<size_t>(end - cur);
            size_t processed_message_size = message_size;
            protector_mu_.Lock();
            result = tsi_frame_protector_unprotect(
                protector_, message_bytes, &processed_message_size, cur,
                &unprotected_buffer_size_written);
            protector_mu_.Unlock();
            if (result != TSI_OK) {
              gpr_log(GPR_ERROR, "Decryption error: %s",
                      tsi_result_to_string(result));
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
              keep_looping = 1;
            } else if (unprotected_buffer_size_written > 0) {
              keep_looping = 1;
            } else {
              keep_looping = 0;
            }
          }
          if (result != TSI_OK) break;
        }

        if (cur != read_staging_buffer_.begin()) {
          read_buffer_->Append(SliceCast<Slice>(read_staging_buffer_.TakeFirst(
              static_cast<size_t>(cur - read_staging_buffer_.begin()))));
        }
      }
    }

    if (!error.ok()) {
      return GRPC_ERROR_CREATE_REFERENCING("Secure read failed", &error, 1);
    }

    // TODO(yangg) experiment with moving this block after read_cb to see if it
    // helps latency
    source_buffer_.Clear();

    if (result != TSI_OK) {
      read_buffer_->Clear();
      return GRPC_ERROR_CREATE(
          absl::StrCat("Unwrap failed (", tsi_result_to_string(result), ")"));
    }

    return absl::OkStatus();
  }

  // For use by wrapped endpoint's Read function.
  void ProcessReadAsync(absl::Status error) { CallReadCb(ProcessRead(error)); }

  void CallReadCb(absl::Status error) {
    MaybeLogBuffer(read_buffer_, /*is_read=*/true);
    read_buffer_ = nullptr;
    GetDefaultEventEngine()->Run(
        [this, read_cb = std::exchange(read_cb_, nullptr), error]() mutable {
          grpc_core::ExecCtx exec_ctx;
          read_cb(error);
          // Corresponds to the Ref held by SecureEndpointImpl::Read.
          Unref();
        });
  }

  void FlushReadStagingBuffer(uint8_t** cur, uint8_t** end)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_) {
    read_buffer_->AppendIndexed(
        SliceCast<Slice>(std::move(read_staging_buffer_)));
    read_staging_buffer_ = SliceCast<MutableSlice>(
        memory_owner_.MakeSlice(grpc_core::MemoryRequest(STAGING_BUFFER_SIZE)));
    *cur = read_staging_buffer_.begin();
    *end = read_staging_buffer_.end();
  }

  void FlushWriteStagingBuffer(uint8_t** cur, uint8_t** end)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(write_mu_) {
    output_buffer_.AppendIndexed(
        SliceCast<Slice>(std::move(write_staging_buffer_)));
    write_staging_buffer_ = SliceCast<MutableSlice>(
        memory_owner_.MakeSlice(grpc_core::MemoryRequest(STAGING_BUFFER_SIZE)));
    *cur = write_staging_buffer_.begin();
    *end = write_staging_buffer_.end();
    MaybePostReclaimer();
  }

  void MaybePostReclaimer() {
    if (!has_posted_reclaimer_) {
      // Take ref for the reclaimer.
      Ref().release();
      has_posted_reclaimer_.exchange(true, std::memory_order_relaxed);
      memory_owner_.PostReclaimer(
          grpc_core::ReclamationPass::kBenign,
          [this](absl::optional<grpc_core::ReclamationSweep> sweep) {
            if (sweep.has_value()) {
              GRPC_TRACE_LOG(resource_quota, INFO)
                  << "secure endpoint: benign reclamation to free memory";
              MutableSlice temp_read_slice;
              MutableSlice temp_write_slice;

              read_mu_.Lock();
              temp_read_slice = std::move(read_staging_buffer_);
              read_mu_.Unlock();

              write_mu_.Lock();
              temp_write_slice = std::move(write_staging_buffer_);
              write_mu_.Unlock();

              has_posted_reclaimer_.exchange(false, std::memory_order_relaxed);
            }
            // Corresponds to the Ref held by
            // SecureEndpointImpl::MaybePostReclaimer.
            Unref();
          });
    }
  }

  // Secure endpoint should be the only owner of the wrapped endpoint.
  std::unique_ptr<EventEngine::Endpoint> wrapped_ep_;
  struct tsi_frame_protector* protector_;
  struct tsi_zero_copy_grpc_protector* zero_copy_protector_;
  grpc_core::Mutex protector_mu_;
  grpc_core::Mutex read_mu_;
  grpc_core::Mutex write_mu_;
  // saved upper level callbacks and user_data.
  absl::AnyInvocable<void(absl::Status)> read_cb_;
  absl::AnyInvocable<void(absl::Status)> write_cb_;
  SliceBuffer* read_buffer_ = nullptr;
  SliceBuffer source_buffer_;
  // saved handshaker leftover data to unprotect.
  SliceBuffer leftover_bytes_;
  // buffers for read and write
  MutableSlice read_staging_buffer_ ABSL_GUARDED_BY(read_mu_);
  MutableSlice write_staging_buffer_ ABSL_GUARDED_BY(write_mu_);
  SliceBuffer output_buffer_;
  grpc_core::MemoryOwner memory_owner_;
  grpc_core::MemoryAllocator::Reservation self_reservation_;
  std::atomic<bool> has_posted_reclaimer_;
  int min_progress_size_;
  SliceBuffer protector_staging_buffer_;
};

class SecureEndpoint
    : public ExtendedType<EventEngine::Endpoint, EndpointSupportsFdExtension> {
 public:
  SecureEndpoint(tsi_frame_protector* protector,
                 tsi_zero_copy_grpc_protector* zero_copy_protector,
                 std::unique_ptr<EventEngine::Endpoint> to_wrap,
                 Slice* leftover_slices,
                 const grpc_core::ChannelArgs& channel_args,
                 size_t leftover_nslices)
      : impl_(new SecureEndpointImpl(protector, zero_copy_protector,
                                     std::move(to_wrap), leftover_slices,
                                     channel_args, leftover_nslices)) {}

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const ReadArgs* args) override {
    return impl_->Read(std::move(on_read), buffer, args);
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const WriteArgs* args) override {
    return impl_->Write(std::move(on_writable), data, args);
  }

  const EventEngine::ResolvedAddress& GetPeerAddress() const override {
    return impl_->GetPeerAddress();
  }

  const EventEngine::ResolvedAddress& GetLocalAddress() const override {
    return impl_->GetLocalAddress();
  }

  int GetWrappedFd() override { return impl_->GetWrappedFd(); }

  void Shutdown(absl::AnyInvocable<void(absl::StatusOr<int> release_fd)>
                    on_release_fd) override {
    impl_->Shutdown(std::move(on_release_fd));
  }

 private:
  grpc_core::RefCountedPtr<SecureEndpointImpl> impl_;
};

}  // namespace

namespace grpc_core {
std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
CreateSecureEndpoint(
    tsi_frame_protector* protector,
    tsi_zero_copy_grpc_protector* zero_copy_protector,
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        to_wrap,
    grpc_event_engine::experimental::Slice* leftover_slices,
    const grpc_core::ChannelArgs& channel_args, size_t leftover_nslices) {
  return std::make_unique<SecureEndpoint>(protector, zero_copy_protector,
                                          std::move(to_wrap), leftover_slices,
                                          channel_args, leftover_nslices);
}
}  // namespace grpc_core
