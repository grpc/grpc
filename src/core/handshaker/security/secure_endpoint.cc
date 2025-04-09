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

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/memory_request.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <inttypes.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/tsi/transport_security_grpc.h"
#include "src/core/tsi/transport_security_interface.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"

#define STAGING_BUFFER_SIZE 8192

static void on_read(void* user_data, grpc_error_handle error);
static void on_write(void* user_data, grpc_error_handle error);

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
    if (leftover_nslices > 0) {
      leftover_bytes_ = std::make_unique<SliceBuffer>();
      for (size_t i = 0; i < leftover_nslices; i++) {
        leftover_bytes_->Append(Slice(leftover_slices[i]));
      }
    }
    if (zero_copy_protector_ != nullptr) {
      read_staging_buffer_ = grpc_empty_slice();
      write_staging_buffer_ = grpc_empty_slice();
    } else {
      read_staging_buffer_ = memory_owner_.MakeSlice(
          grpc_core::MemoryRequest(STAGING_BUFFER_SIZE));
      write_staging_buffer_ = memory_owner_.MakeSlice(
          grpc_core::MemoryRequest(STAGING_BUFFER_SIZE));
    }
  }

  ~FrameProtector() {
    tsi_frame_protector_destroy(protector_);
    tsi_zero_copy_grpc_protector_destroy(zero_copy_protector_);
    grpc_core::CSliceUnref(read_staging_buffer_);
    grpc_core::CSliceUnref(write_staging_buffer_);
  }

  void MaybePostReclaimer() {
    if (!has_posted_reclaimer_.exchange(true, std::memory_order_relaxed)) {
      memory_owner_.PostReclaimer(
          grpc_core::ReclamationPass::kBenign,
          [self = Ref()](std::optional<grpc_core::ReclamationSweep> sweep) {
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

              grpc_core::CSliceUnref(temp_read_slice);
              grpc_core::CSliceUnref(temp_write_slice);
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
        memory_owner_.MakeSlice(grpc_core::MemoryRequest(STAGING_BUFFER_SIZE));
    *cur = GRPC_SLICE_START_PTR(read_staging_buffer_);
    *end = GRPC_SLICE_END_PTR(read_staging_buffer_);
  }

  void FinishRead(bool ok) {
    if (GRPC_TRACE_FLAG_ENABLED(secure_endpoint) && ABSL_VLOG_IS_ON(2)) {
      size_t i;
      for (i = 0; i < read_buffer_->count; i++) {
        char* data = grpc_dump_slice(read_buffer_->slices[i],
                                     GPR_DUMP_HEX | GPR_DUMP_ASCII);
        VLOG(2) << "READ " << this << ": " << data;
        gpr_free(data);
      }
    }
    // TODO(yangg) experiment with moving this block after read_cb to see if it
    // helps latency
    source_buffer_.Clear();
    if (!ok) grpc_slice_buffer_reset_and_unref(read_buffer_);
    read_buffer_ = nullptr;
  }

  absl::Status Unprotect(absl::Status read_status) {
    bool keep_looping = false;
    tsi_result result = TSI_OK;

    grpc_core::MutexLock l(&read_mu_);

#if 0
    // If we were shut down after this callback was scheduled with OK
    // status but before it was invoked, we need to treat that as an error.
    if (ep->wrapped_ep == nullptr && error.ok()) {
      error = absl::CancelledError("secure endpoint shutdown");
    }
#endif

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
        grpc_slice encrypted = source_buffer_.c_slice_at(i);
        uint8_t* message_bytes = GRPC_SLICE_START_PTR(encrypted);
        size_t message_size = GRPC_SLICE_LENGTH(encrypted);

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

    return read_status;
  }

  void BeginRead(grpc_slice_buffer* slices) {
    read_buffer_ = slices;
    grpc_slice_buffer_reset_and_unref(read_buffer_);
  }

  bool MaybeCompleteReadImmediately() {
    if (leftover_bytes_ != nullptr) {
      grpc_slice_buffer_swap(leftover_bytes_->c_slice_buffer(), read_buffer_);
      leftover_bytes_.reset();
      return true;
    }
    return false;
  }

  grpc_slice_buffer* source_buffer() { return source_buffer_.c_slice_buffer(); }

  int min_progress_size() const { return min_progress_size_; }

 private:
  struct tsi_frame_protector* const protector_;
  struct tsi_zero_copy_grpc_protector* const zero_copy_protector_;
  Mutex mu_;
  Mutex read_mu_;
  Mutex write_mu_;
  Mutex protector_mu_;
  grpc_slice_buffer* read_buffer_ = nullptr;
  SliceBuffer source_buffer_;
  // saved handshaker leftover data to unprotect.
  std::unique_ptr<SliceBuffer> leftover_bytes_;
  // buffers for read and write
  grpc_slice read_staging_buffer_ ABSL_GUARDED_BY(read_mu_);
  grpc_slice write_staging_buffer_ ABSL_GUARDED_BY(write_mu_);
  SliceBuffer output_buffer_;
  grpc_core::MemoryOwner memory_owner_;
  grpc_core::MemoryAllocator::Reservation self_reservation_;
  std::atomic<bool> has_posted_reclaimer_{false};
  int min_progress_size_ = 1;
  SliceBuffer protector_staging_buffer_;
};
}  // namespace
}  // namespace grpc_core

namespace {
struct secure_endpoint : public grpc_endpoint {
  secure_endpoint(const grpc_endpoint_vtable* vtbl,
                  tsi_frame_protector* protector,
                  tsi_zero_copy_grpc_protector* zero_copy_protector,
                  grpc_core::OrphanablePtr<grpc_endpoint> endpoint,
                  grpc_slice* leftover_slices, size_t leftover_nslices,
                  const grpc_core::ChannelArgs& args)
      : wrapped_ep(std::move(endpoint)),
        frame_protector(protector, zero_copy_protector, leftover_slices,
                        leftover_nslices, args) {
    this->vtable = vtbl;
    GRPC_CLOSURE_INIT(&on_read, ::on_read, this, grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&on_write, ::on_write, this, grpc_schedule_on_exec_ctx);
    gpr_ref_init(&ref, 1);
  }

  ~secure_endpoint() {}

  grpc_core::OrphanablePtr<grpc_endpoint> wrapped_ep;
  grpc_core::FrameProtector frame_protector;
  // saved upper level callbacks and user_data.
  grpc_closure* read_cb = nullptr;
  grpc_closure* write_cb = nullptr;
  grpc_closure on_read;
  grpc_closure on_write;
  gpr_refcount ref;
};
}  // namespace

static void destroy(secure_endpoint* ep) { delete ep; }

#ifndef NDEBUG
#define SECURE_ENDPOINT_UNREF(ep, reason) \
  secure_endpoint_unref((ep), (reason), __FILE__, __LINE__)
#define SECURE_ENDPOINT_REF(ep, reason) \
  secure_endpoint_ref((ep), (reason), __FILE__, __LINE__)
static void secure_endpoint_unref(secure_endpoint* ep, const char* reason,
                                  const char* file, int line) {
  if (GRPC_TRACE_FLAG_ENABLED(secure_endpoint)) {
    gpr_atm val = gpr_atm_no_barrier_load(&ep->ref.count);
    VLOG(2).AtLocation(file, line) << "SECENDP unref " << ep << " : " << reason
                                   << " " << val << " -> " << val - 1;
  }
  if (gpr_unref(&ep->ref)) {
    destroy(ep);
  }
}

static void secure_endpoint_ref(secure_endpoint* ep, const char* reason,
                                const char* file, int line) {
  if (GRPC_TRACE_FLAG_ENABLED(secure_endpoint)) {
    gpr_atm val = gpr_atm_no_barrier_load(&ep->ref.count);
    VLOG(2).AtLocation(file, line) << "SECENDP   ref " << ep << " : " << reason
                                   << " " << val << " -> " << val + 1;
  }
  gpr_ref(&ep->ref);
}
#else
#define SECURE_ENDPOINT_UNREF(ep, reason) secure_endpoint_unref((ep))
#define SECURE_ENDPOINT_REF(ep, reason) secure_endpoint_ref((ep))
static void secure_endpoint_unref(secure_endpoint* ep) {
  if (gpr_unref(&ep->ref)) {
    destroy(ep);
  }
}

static void secure_endpoint_ref(secure_endpoint* ep) { gpr_ref(&ep->ref); }
#endif

static void call_read_cb(secure_endpoint* ep, grpc_error_handle error) {
  ep->frame_protector.FinishRead(error.ok());
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, ep->read_cb, error);
  SECURE_ENDPOINT_UNREF(ep, "read");
}

static void on_read(void* user_data, grpc_error_handle error) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(user_data);

  error = ep->frame_protector.Unprotect(std::move(error));

  if (!error.ok()) {
    call_read_cb(
        ep, GRPC_ERROR_CREATE_REFERENCING("Secure read failed", &error, 1));
    return;
  }

  call_read_cb(ep, absl::OkStatus());
}

static void endpoint_read(grpc_endpoint* secure_ep, grpc_slice_buffer* slices,
                          grpc_closure* cb, bool urgent,
                          int /*min_progress_size*/) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  ep->read_cb = cb;
  ep->frame_protector.BeginRead(slices);

  SECURE_ENDPOINT_REF(ep, "read");
  if (ep->frame_protector.MaybeCompleteReadImmediately()) {
    on_read(ep, absl::OkStatus());
    return;
  }

  grpc_endpoint_read(ep->wrapped_ep.get(), ep->frame_protector.source_buffer(),
                     &ep->on_read, urgent,
                     ep->frame_protector.min_progress_size());
}

static void flush_write_staging_buffer(secure_endpoint* ep, uint8_t** cur,
                                       uint8_t** end)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(ep->write_mu) {
  grpc_slice_buffer_add_indexed(&ep->output_buffer, ep->write_staging_buffer);
  ep->write_staging_buffer =
      ep->memory_owner.MakeSlice(grpc_core::MemoryRequest(STAGING_BUFFER_SIZE));
  *cur = GRPC_SLICE_START_PTR(ep->write_staging_buffer);
  *end = GRPC_SLICE_END_PTR(ep->write_staging_buffer);
  maybe_post_reclaimer(ep);
}

static void on_write(void* user_data, grpc_error_handle error) {
  secure_endpoint* ep = static_cast<secure_endpoint*>(user_data);
  grpc_closure* cb = ep->write_cb;
  ep->write_cb = nullptr;
  SECURE_ENDPOINT_UNREF(ep, "write");
  grpc_core::EnsureRunInExecCtx([cb, error = std::move(error)]() {
    grpc_core::Closure::Run(DEBUG_LOCATION, cb, error);
  });
}

static void endpoint_write(grpc_endpoint* secure_ep, grpc_slice_buffer* slices,
                           grpc_closure* cb, void* arg, int max_frame_size) {
  GRPC_LATENT_SEE_INNER_SCOPE("secure_endpoint write");
  unsigned i;
  tsi_result result = TSI_OK;
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);

  {
    grpc_core::MutexLock l(&ep->write_mu);
    uint8_t* cur = GRPC_SLICE_START_PTR(ep->write_staging_buffer);
    uint8_t* end = GRPC_SLICE_END_PTR(ep->write_staging_buffer);

    grpc_slice_buffer_reset_and_unref(&ep->output_buffer);

    if (GRPC_TRACE_FLAG_ENABLED(secure_endpoint) && ABSL_VLOG_IS_ON(2)) {
      for (i = 0; i < slices->count; i++) {
        char* data =
            grpc_dump_slice(slices->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
        VLOG(2) << "WRITE " << ep << ": " << data;
        gpr_free(data);
      }
    }

    if (ep->zero_copy_protector != nullptr) {
      // Use zero-copy grpc protector to protect.
      result = TSI_OK;
      // Break the input slices into chunks of size = max_frame_size and call
      // tsi_zero_copy_grpc_protector_protect on each chunk. This ensures that
      // the protector cannot create frames larger than the specified
      // max_frame_size.
      while (slices->length > static_cast<size_t>(max_frame_size) &&
             result == TSI_OK) {
        grpc_slice_buffer_move_first(slices,
                                     static_cast<size_t>(max_frame_size),
                                     &ep->protector_staging_buffer);
        result = tsi_zero_copy_grpc_protector_protect(
            ep->zero_copy_protector, &ep->protector_staging_buffer,
            &ep->output_buffer);
      }
      if (result == TSI_OK && slices->length > 0) {
        result = tsi_zero_copy_grpc_protector_protect(
            ep->zero_copy_protector, slices, &ep->output_buffer);
      }
      grpc_slice_buffer_reset_and_unref(&ep->protector_staging_buffer);
    } else {
      // Use frame protector to protect.
      for (i = 0; i < slices->count; i++) {
        grpc_slice plain = slices->slices[i];
        uint8_t* message_bytes = GRPC_SLICE_START_PTR(plain);
        size_t message_size = GRPC_SLICE_LENGTH(plain);
        while (message_size > 0) {
          size_t protected_buffer_size_to_send = static_cast<size_t>(end - cur);
          size_t processed_message_size = message_size;
          gpr_mu_lock(&ep->protector_mu);
          result = tsi_frame_protector_protect(ep->protector, message_bytes,
                                               &processed_message_size, cur,
                                               &protected_buffer_size_to_send);
          gpr_mu_unlock(&ep->protector_mu);
          if (result != TSI_OK) {
            LOG(ERROR) << "Encryption error: " << tsi_result_to_string(result);
            break;
          }
          message_bytes += processed_message_size;
          message_size -= processed_message_size;
          cur += protected_buffer_size_to_send;

          if (cur == end) {
            flush_write_staging_buffer(ep, &cur, &end);
          }
        }
        if (result != TSI_OK) break;
      }
      if (result == TSI_OK) {
        size_t still_pending_size;
        do {
          size_t protected_buffer_size_to_send = static_cast<size_t>(end - cur);
          gpr_mu_lock(&ep->protector_mu);
          result = tsi_frame_protector_protect_flush(
              ep->protector, cur, &protected_buffer_size_to_send,
              &still_pending_size);
          gpr_mu_unlock(&ep->protector_mu);
          if (result != TSI_OK) break;
          cur += protected_buffer_size_to_send;
          if (cur == end) {
            flush_write_staging_buffer(ep, &cur, &end);
          }
        } while (still_pending_size > 0);
        if (cur != GRPC_SLICE_START_PTR(ep->write_staging_buffer)) {
          grpc_slice_buffer_add(
              &ep->output_buffer,
              grpc_slice_split_head(
                  &ep->write_staging_buffer,
                  static_cast<size_t>(
                      cur - GRPC_SLICE_START_PTR(ep->write_staging_buffer))));
        }
      }
    }
  }

  if (result != TSI_OK) {
    // TODO(yangg) do different things according to the error type?
    grpc_slice_buffer_reset_and_unref(&ep->output_buffer);
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, cb,
        GRPC_ERROR_CREATE(
            absl::StrCat("Wrap failed (", tsi_result_to_string(result), ")")));
    return;
  }

  // Need to hold a ref here, because the wrapped endpoint may access
  // output_buffer at any time until the write completes.
  SECURE_ENDPOINT_REF(ep, "write");
  ep->write_cb = cb;
  grpc_endpoint_write(ep->wrapped_ep.get(), &ep->output_buffer, &ep->on_write,
                      arg, max_frame_size);
}

static void endpoint_destroy(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  ep->read_mu.Lock();
  ep->wrapped_ep.reset();
  ep->memory_owner.Reset();
  ep->read_mu.Unlock();
  SECURE_ENDPOINT_UNREF(ep, "destroy");
}

static void endpoint_add_to_pollset(grpc_endpoint* secure_ep,
                                    grpc_pollset* pollset) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  grpc_endpoint_add_to_pollset(ep->wrapped_ep.get(), pollset);
}

static void endpoint_add_to_pollset_set(grpc_endpoint* secure_ep,
                                        grpc_pollset_set* pollset_set) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  grpc_endpoint_add_to_pollset_set(ep->wrapped_ep.get(), pollset_set);
}

static void endpoint_delete_from_pollset_set(grpc_endpoint* secure_ep,
                                             grpc_pollset_set* pollset_set) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  grpc_endpoint_delete_from_pollset_set(ep->wrapped_ep.get(), pollset_set);
}

static absl::string_view endpoint_get_peer(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  return grpc_endpoint_get_peer(ep->wrapped_ep.get());
}

static absl::string_view endpoint_get_local_address(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  return grpc_endpoint_get_local_address(ep->wrapped_ep.get());
}

static int endpoint_get_fd(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  return grpc_endpoint_get_fd(ep->wrapped_ep.get());
}

static bool endpoint_can_track_err(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  return grpc_endpoint_can_track_err(ep->wrapped_ep.get());
}

static const grpc_endpoint_vtable vtable = {endpoint_read,
                                            endpoint_write,
                                            endpoint_add_to_pollset,
                                            endpoint_add_to_pollset_set,
                                            endpoint_delete_from_pollset_set,
                                            endpoint_destroy,
                                            endpoint_get_peer,
                                            endpoint_get_local_address,
                                            endpoint_get_fd,
                                            endpoint_can_track_err};

namespace grpc_event_engine::experimental {
namespace {

class SecureEndpoint final : public EventEngine::Endpoint {
 public:
  bool Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const ReadArgs* in_args) override {
    return impl_->Read(std::move(on_read, buffer, in_args));
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const WriteArgs* args) override;

  const ResolvedAddress& GetPeerAddress() const {
    return wrapped_ep_->GetPeerAddress();
  }

  const ResolvedAddress& GetLocalAddress() const {
    return wrapped_ep_->GetLocalAddress();
  }

 private:
  class Impl : public grpc_core::RefCounted<Impl> {
   public:
    bool Read() {
      on_read_ = std::move(on_read);
      read_requested_bytes_ = in_args == nullptr ? 1 : in_args->read_hint_bytes;
      ReadArgs args;
      args.read_hint_bytes = min_progress_size_;
      if (wrapped_ep_->Read(
              [impl = Ref()](absl::Status status) {
                if (!status.ok()) {
                  std::move(impl->on_read_)(std::move(status));
                  return;
                }
                UnprotectAndContinueRead();
              },
              &source_buffer_, &args)) {
        UnprotectAndContinueRead();
      }
    }

   private:
    absl::AnyInvocable<void(absl::Status)> on_read_;
    absl::AnyInvocable<void(absl::Status)> on_write_;
    std::unique_ptr<EventEngine::Endpoint> wrapped_ep_;
    SliceBuffer source_buffer_;
    SliceBuffer leftover_bytes_;
    SliceBuffer read_buffer_;
    std::shared_ptr<EventEngine> event_engine_;
    int64_t min_progress_size_ = 1;
    int64_t read_requested_bytes_;
    struct tsi_zero_copy_grpc_protector* zero_copy_protector_;
  };

  grpc_core::RefCountedPtr<Impl> impl_;
};

}  // namespace
}  // namespace grpc_event_engine::experimental

grpc_core::OrphanablePtr<grpc_endpoint> grpc_secure_endpoint_create(
    struct tsi_frame_protector* protector,
    struct tsi_zero_copy_grpc_protector* zero_copy_protector,
    grpc_core::OrphanablePtr<grpc_endpoint> to_wrap,
    grpc_slice* leftover_slices, const grpc_channel_args* channel_args,
    size_t leftover_nslices) {
  if (grpc_core::IsEventEngineSecureEndpointEnabled()) {
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        event_engine_endpoint = grpc_event_engine::experimental::
            grpc_take_wrapped_event_engine_endpoint(to_wrap.get());
    if (event_engine_endpoint != nullptr) {
      return grpc_event_engine::experimental::grpc_event_engine_endpoint_create(
          std::make_unique<SecureEndpoint>(
              std::move(event_engine_endpoint), protector, zero_copy_protector,
              leftover_slices, leftover_nslices, channel_args));
    }
  }
  return grpc_core::MakeOrphanable<secure_endpoint>(
      &vtable, protector, zero_copy_protector, std::move(to_wrap),
      leftover_slices, channel_args, leftover_nslices);
}
